// SVGPlayerMetalRenderer.mm - Metal-based GPU renderer for SVGPlayerView
//
// This implementation uses MTKView for efficient GPU rendering of SVG content.
// The Skia rendering engine outputs to a pixel buffer, which is then uploaded
// to a Metal texture for display.

#import "SVGPlayerMetalRenderer.h"
#import "SVGPlayerController.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

@interface SVGPlayerMetalRenderer () <MTKViewDelegate>
// Parent view reference (weak to avoid retain cycle)
@property (nonatomic, weak) UIView *parentView;
// SVG controller providing content
@property (nonatomic, strong) SVGPlayerController *controller;
// Metal device
@property (nonatomic, strong) id<MTLDevice> device;
// Metal command queue for rendering commands
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
// Metal view for display
@property (nonatomic, strong) MTKView *internalMetalView;
// Current render size in pixels
@property (nonatomic, assign) CGSize renderSize;
// Current display scale
@property (nonatomic, assign) CGFloat displayScale;
// Pixel buffer for Skia rendering (RGBA format)
@property (nonatomic, assign) void *pixelBuffer;
// Size of allocated pixel buffer
@property (nonatomic, assign) NSInteger pixelBufferSize;
// Metal texture for displaying rendered content
@property (nonatomic, strong) id<MTLTexture> renderTexture;
// Render pipeline for blitting texture to screen
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
// Vertex buffer for fullscreen quad
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@end

// Vertex structure for fullscreen quad
typedef struct {
    vector_float2 position;
    vector_float2 texCoord;
} QuadVertex;

@implementation SVGPlayerMetalRenderer

#pragma mark - Class Methods

+ (BOOL)isMetalAvailable {
    return MTLCreateSystemDefaultDevice() != nil;
}

#pragma mark - Initialization

- (instancetype)initWithView:(UIView *)view controller:(SVGPlayerController *)controller {
    if (self = [super init]) {
        _parentView = view;
        _controller = controller;

        // Get display scale from view's trait collection (iOS 26+ compatible)
        // Falls back to 2.0 if view isn't in a window yet
        CGFloat scale = view.traitCollection.displayScale;
        _displayScale = (scale > 0) ? scale : 2.0;

        // Initialize Metal
        _device = MTLCreateSystemDefaultDevice();
        if (!_device) {
            NSLog(@"SVGPlayerMetalRenderer: Metal is not available on this device");
            return nil;
        }

        _commandQueue = [_device newCommandQueue];
        if (!_commandQueue) {
            NSLog(@"SVGPlayerMetalRenderer: Failed to create command queue");
            return nil;
        }

        // Create MTKView
        [self setupMetalView];

        // Create render pipeline
        [self setupRenderPipeline];

        // Create vertex buffer for fullscreen quad
        [self setupVertexBuffer];
    }
    return self;
}

- (void)dealloc {
    [self cleanup];
}

#pragma mark - Setup

- (void)setupMetalView {
    UIView *parent = self.parentView;
    if (!parent) return;

    _internalMetalView = [[MTKView alloc] initWithFrame:parent.bounds device:self.device];
    _internalMetalView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _internalMetalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    _internalMetalView.clearColor = MTLClearColorMake(1.0, 1.0, 1.0, 1.0);
    _internalMetalView.delegate = self;
    // Manual rendering mode - we trigger draws explicitly
    _internalMetalView.enableSetNeedsDisplay = YES;
    _internalMetalView.paused = YES;

    [parent addSubview:_internalMetalView];
    [parent sendSubviewToBack:_internalMetalView];
}

- (void)setupRenderPipeline {
    // Shader source for simple texture blit
    NSString *shaderSource = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float2 texCoord [[attribute(1)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texCoord;\n"
    "};\n"
    "\n"
    "vertex VertexOut vertexShader(VertexIn in [[stage_in]]) {\n"
    "    VertexOut out;\n"
    "    out.position = float4(in.position, 0.0, 1.0);\n"
    "    out.texCoord = in.texCoord;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 fragmentShader(VertexOut in [[stage_in]],\n"
    "                                texture2d<float> texture [[texture(0)]]) {\n"
    "    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);\n"
    "    // Convert from RGBA (Skia) to BGRA (Metal) - swap R and B\n"
    "    float4 color = texture.sample(textureSampler, in.texCoord);\n"
    "    return float4(color.b, color.g, color.r, color.a);\n"
    "}\n";

    NSError *error = nil;
    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (!library) {
        NSLog(@"SVGPlayerMetalRenderer: Failed to create shader library: %@", error);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"fragmentShader"];

    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = self.internalMetalView.colorPixelFormat;

    // Vertex descriptor
    MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(vector_float2);
    vertexDescriptor.attributes[1].bufferIndex = 0;
    vertexDescriptor.layouts[0].stride = sizeof(QuadVertex);
    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    _pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!_pipelineState) {
        NSLog(@"SVGPlayerMetalRenderer: Failed to create pipeline state: %@", error);
    }
}

- (void)setupVertexBuffer {
    // Fullscreen quad (two triangles) with flipped Y for texture coordinates
    QuadVertex vertices[] = {
        // Triangle 1
        {{-1.0, -1.0}, {0.0, 1.0}},
        {{ 1.0, -1.0}, {1.0, 1.0}},
        {{-1.0,  1.0}, {0.0, 0.0}},
        // Triangle 2
        {{ 1.0, -1.0}, {1.0, 1.0}},
        {{ 1.0,  1.0}, {1.0, 0.0}},
        {{-1.0,  1.0}, {0.0, 0.0}},
    };

    _vertexBuffer = [self.device newBufferWithBytes:vertices
                                              length:sizeof(vertices)
                                             options:MTLResourceStorageModeShared];
}

#pragma mark - SVGPlayerRenderer Protocol

- (void)updateForSize:(CGSize)size scale:(CGFloat)scale {
    self.displayScale = scale;

    // Calculate pixel dimensions
    NSInteger pixelWidth = (NSInteger)(size.width * scale);
    NSInteger pixelHeight = (NSInteger)(size.height * scale);

    if (pixelWidth <= 0 || pixelHeight <= 0) return;

    self.renderSize = CGSizeMake(pixelWidth, pixelHeight);

    // Allocate or reallocate pixel buffer
    NSInteger requiredSize = pixelWidth * pixelHeight * 4;
    if (requiredSize != self.pixelBufferSize) {
        if (self.pixelBuffer) {
            free(self.pixelBuffer);
        }
        self.pixelBuffer = malloc(requiredSize);
        self.pixelBufferSize = requiredSize;

        // Clear the buffer
        if (self.pixelBuffer) {
            memset(self.pixelBuffer, 255, requiredSize); // White background
        }
    }

    // Create texture for this size
    [self createTextureWithWidth:pixelWidth height:pixelHeight];

    // Update MTKView drawable size
    self.internalMetalView.drawableSize = self.renderSize;
}

- (void)createTextureWithWidth:(NSInteger)width height:(NSInteger)height {
    MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.usage = MTLTextureUsageShaderRead;

    self.renderTexture = [self.device newTextureWithDescriptor:descriptor];
}

- (void)render {
    if (!self.pixelBuffer || !self.controller.isLoaded) {
        return;
    }

    NSInteger width = (NSInteger)self.renderSize.width;
    NSInteger height = (NSInteger)self.renderSize.height;

    if (width <= 0 || height <= 0) return;

    // Render SVG to pixel buffer using Skia
    BOOL success = [self.controller renderToBuffer:self.pixelBuffer
                                             width:width
                                            height:height
                                             scale:self.displayScale];

    if (!success) {
        return;
    }

    // Upload pixel buffer to Metal texture
    if (self.renderTexture) {
        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [self.renderTexture replaceRegion:region
                              mipmapLevel:0
                                withBytes:self.pixelBuffer
                              bytesPerRow:width * 4];
    }

    // Trigger MTKView redraw
    [self.internalMetalView setNeedsDisplay];
}

- (UIImage *)captureImage {
    if (!self.pixelBuffer || self.renderSize.width <= 0 || self.renderSize.height <= 0) {
        return nil;
    }

    NSInteger width = (NSInteger)self.renderSize.width;
    NSInteger height = (NSInteger)self.renderSize.height;
    NSInteger bytesPerRow = width * 4;

    // Create CGImage from pixel buffer
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(self.pixelBuffer,
                                                  width, height,
                                                  8, bytesPerRow,
                                                  colorSpace,
                                                  kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);

    if (!context) return nil;

    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    CGContextRelease(context);

    if (!cgImage) return nil;

    UIImage *image = [UIImage imageWithCGImage:cgImage
                                         scale:self.displayScale
                                   orientation:UIImageOrientationUp];
    CGImageRelease(cgImage);

    return image;
}

- (void)cleanup {
    if (self.pixelBuffer) {
        free(self.pixelBuffer);
        self.pixelBuffer = NULL;
        self.pixelBufferSize = 0;
    }

    [self.internalMetalView removeFromSuperview];
    self.internalMetalView = nil;
    self.renderTexture = nil;
    self.pipelineState = nil;
    self.vertexBuffer = nil;
    self.commandQueue = nil;
    self.device = nil;
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // Handle size changes if needed
}

- (void)drawInMTKView:(MTKView *)view {
    if (!self.renderTexture || !self.pipelineState || !self.vertexBuffer) {
        return;
    }

    id<CAMetalDrawable> drawable = view.currentDrawable;
    MTLRenderPassDescriptor *passDescriptor = view.currentRenderPassDescriptor;

    if (!drawable || !passDescriptor) {
        return;
    }

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    if (!commandBuffer) return;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
    if (!encoder) return;

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
    [encoder setFragmentTexture:self.renderTexture atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];

    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Properties

- (UIView *)metalView {
    return self.internalMetalView;
}

@end
