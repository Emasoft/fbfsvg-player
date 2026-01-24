/**
 * Metal Graphite Context for Skia GPU Rendering (macOS/iOS)
 *
 * This file provides Skia Graphite GPU-accelerated rendering using Metal.
 * Graphite is Skia's next-generation GPU backend that replaces Ganesh.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#ifdef __APPLE__
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    #import <UIKit/UIKit.h>
    #define GRAPHITE_IOS 1
#else
    #import <AppKit/AppKit.h>
    #define GRAPHITE_MACOS 1
#endif
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include <mutex>

// Skia Graphite headers
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/mtl/MtlBackendContext.h"
#include "include/gpu/graphite/mtl/MtlGraphiteTypes.h"
#include "include/gpu/graphite/mtl/MtlGraphiteTypes_cpp.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"

#include "graphite_context.h"

namespace svgplayer {

/**
 * Metal Graphite context implementation.
 * Uses CAMetalLayer for presentation and Skia Graphite for rendering.
 */
class MetalGraphiteContext : public GraphiteContext {
public:
    MetalGraphiteContext() = default;
    ~MetalGraphiteContext() override { destroy(); }

    bool initialize(SDL_Window* window) override {
        if (!window) {
            fprintf(stderr, "[Graphite Metal] Error: NULL window\n");
            return false;
        }

        // Create Metal device (system default GPU)
        device_ = MTLCreateSystemDefaultDevice();
        if (!device_) {
            fprintf(stderr, "[Graphite Metal] Error: Failed to create Metal device\n");
            return false;
        }
        printf("[Graphite Metal] Using device: %s\n", [[device_ name] UTF8String]);

        // Create command queue
        queue_ = [device_ newCommandQueue];
        if (!queue_) {
            fprintf(stderr, "[Graphite Metal] Error: Failed to create command queue\n");
            destroy();
            return false;
        }

        // Create SDL Metal view
        metalView_ = SDL_Metal_CreateView(window);
        if (!metalView_) {
            fprintf(stderr, "[Graphite Metal] Error: Failed to create SDL Metal view: %s\n", SDL_GetError());
            destroy();
            return false;
        }

        // Get CAMetalLayer from the view
        void* layerPtr = SDL_Metal_GetLayer(metalView_);
        if (!layerPtr) {
            fprintf(stderr, "[Graphite Metal] Error: Failed to get Metal layer\n");
            destroy();
            return false;
        }
        metalLayer_ = (__bridge CAMetalLayer*)layerPtr;

        // Configure the Metal layer
        metalLayer_.device = device_;
        metalLayer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer_.framebufferOnly = NO;  // Skia needs to read from the texture

#if GRAPHITE_MACOS
        NSScreen* mainScreen = [NSScreen mainScreen];
        CGFloat contentsScale = mainScreen ? [mainScreen backingScaleFactor] : 2.0;
#else
        CGFloat contentsScale = [[UIScreen mainScreen] scale];
#endif
        metalLayer_.contentsScale = contentsScale;

        // Ensure the layer content fills the entire bounds (critical for fullscreen)
        // kCAGravityResize stretches the content to fill the layer bounds
        metalLayer_.contentsGravity = kCAGravityResize;

        // Configure drawable management
        if (@available(macOS 10.15.4, iOS 13.0, *)) {
            metalLayer_.allowsNextDrawableTimeout = YES;
        }
        metalLayer_.maximumDrawableCount = 3;  // Triple buffering
        metalLayer_.presentsWithTransaction = NO;

        // Enable VSync by default
#if GRAPHITE_MACOS
        if (@available(macOS 10.13, *)) {
            metalLayer_.displaySyncEnabled = YES;
        }
#endif

        // Set initial drawable size
        int drawableW, drawableH;
        SDL_Metal_GetDrawableSize(window, &drawableW, &drawableH);
        if (drawableW > 0 && drawableH > 0) {
            metalLayer_.drawableSize = CGSizeMake(drawableW, drawableH);
            drawableWidth_ = drawableW;
            drawableHeight_ = drawableH;
        } else {
            int windowW, windowH;
            SDL_GetWindowSize(window, &windowW, &windowH);
            drawableWidth_ = (int)(windowW * contentsScale);
            drawableHeight_ = (int)(windowH * contentsScale);
            metalLayer_.drawableSize = CGSizeMake(drawableWidth_, drawableHeight_);
        }
        printf("[Graphite Metal] Initial drawable size: %dx%d\n", drawableWidth_, drawableHeight_);

        // Create Skia Graphite context with Metal backend
        skgpu::graphite::MtlBackendContext backendContext;
        backendContext.fDevice.retain((__bridge void*)device_);
        backendContext.fQueue.retain((__bridge void*)queue_);

        skgpu::graphite::ContextOptions options;
        // Use default options for now

        context_ = skgpu::graphite::ContextFactory::MakeMetal(backendContext, options);
        if (!context_) {
            fprintf(stderr, "[Graphite Metal] Error: Failed to create Skia Graphite context\n");
            destroy();
            return false;
        }

        // Create a Recorder for recording draw commands
        recorder_ = context_->makeRecorder();
        if (!recorder_) {
            fprintf(stderr, "[Graphite Metal] Error: Failed to create Skia Graphite recorder\n");
            destroy();
            return false;
        }

        initialized_ = true;
        printf("[Graphite Metal] Successfully initialized Metal Graphite backend\n");
        return true;
    }

    void destroy() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (recorder_) {
            recorder_.reset();
        }

        if (context_) {
            // Ensure all pending work is complete before destroying
            context_->submit(skgpu::graphite::SyncToCpu::kYes);
            context_.reset();
        }

        if (currentDrawable_) {
            currentDrawable_ = nil;
        }

        if (metalView_) {
            SDL_Metal_DestroyView(metalView_);
            metalView_ = nullptr;
        }

        metalLayer_ = nil;
        queue_ = nil;
        device_ = nil;
        initialized_ = false;

        printf("[Graphite Metal] Destroyed Metal Graphite context\n");
    }

    bool isInitialized() const override {
        return initialized_;
    }

    void updateDrawableSize(int width, int height) override {
        if (!initialized_ || !metalLayer_) return;
        if (width <= 0 || height <= 0) return;

        std::lock_guard<std::mutex> lock(mutex_);
        metalLayer_.drawableSize = CGSizeMake(width, height);
        drawableWidth_ = width;
        drawableHeight_ = height;
    }

    sk_sp<SkSurface> createSurface(int width, int height) override {
        if (!initialized_) {
            fprintf(stderr, "[Graphite Metal] createSurface: context not initialized\n");
            return nullptr;
        }

        if (width <= 0 || height <= 0) {
            fprintf(stderr, "[Graphite Metal] createSurface: invalid dimensions %dx%d\n", width, height);
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Update drawable size to match requested render dimensions (in pixels)
        // The layer frame/bounds are managed by SDL - we only control the drawable resolution
        metalLayer_.drawableSize = CGSizeMake(width, height);
        drawableWidth_ = width;
        drawableHeight_ = height;

        // Debug: Log drawable and layer info
        if (std::getenv("RENDER_DEBUG")) {
            CGSize actualSize = metalLayer_.drawableSize;
            CGRect frame = metalLayer_.frame;
            fprintf(stderr, "[Graphite Metal] createSurface: drawable=%dx%d, frame=(%.0f,%.0f,%.0f,%.0f), scale=%.1f\n",
                    width, height, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height,
                    metalLayer_.contentsScale);
        }

        // Acquire next drawable
        currentDrawable_ = [metalLayer_ nextDrawable];
        if (!currentDrawable_) {
            fprintf(stderr, "[Graphite Metal] createSurface: Failed to acquire drawable\n");
            return nullptr;
        }

        id<MTLTexture> texture = [currentDrawable_ texture];
        if (!texture) {
            fprintf(stderr, "[Graphite Metal] createSurface: Failed to get drawable texture\n");
            currentDrawable_ = nil;
            return nullptr;
        }

        // Verify texture dimensions match requested size
        NSUInteger texW = [texture width];
        NSUInteger texH = [texture height];
        if (std::getenv("RENDER_DEBUG")) {
            fprintf(stderr, "[Graphite Metal] Texture actual size: %lux%lu, requested: %dx%d\n",
                    (unsigned long)texW, (unsigned long)texH, width, height);
        }
        if (texW != (NSUInteger)width || texH != (NSUInteger)height) {
            fprintf(stderr, "[Graphite Metal] WARNING: Texture size mismatch! texture=%lux%lu, requested=%dx%d\n",
                    (unsigned long)texW, (unsigned long)texH, width, height);
        }

        // Create BackendTexture from Metal texture using Graphite API
        skgpu::graphite::BackendTexture backendTexture =
            skgpu::graphite::BackendTextures::MakeMetal(
                SkISize::Make(width, height),
                (__bridge CFTypeRef)texture
            );

        if (!backendTexture.isValid()) {
            fprintf(stderr, "[Graphite Metal] createSurface: Failed to create BackendTexture\n");
            currentDrawable_ = nil;
            return nullptr;
        }

        // Create SkSurface wrapping the drawable texture
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendTexture(
            recorder_.get(),
            backendTexture,
            kBGRA_8888_SkColorType,
            SkColorSpace::MakeSRGB(),
            nullptr,  // surfaceProps
            nullptr,  // textureReleaseProc
            nullptr   // releaseContext
        );

        if (!surface) {
            fprintf(stderr, "[Graphite Metal] createSurface: Failed to create surface from texture\n");
            currentDrawable_ = nil;
            return nullptr;
        }

        // DIAGNOSTIC: Verify surface dimensions match requested dimensions
        // This catches the 1/4 screen bug where surface is smaller than expected
        int surfaceW = surface->width();
        int surfaceH = surface->height();
        if (surfaceW != width || surfaceH != height) {
            fprintf(stderr, "[Graphite Metal] CRITICAL ERROR: Surface size mismatch!\n");
            fprintf(stderr, "[Graphite Metal]   Requested: %dx%d\n", width, height);
            fprintf(stderr, "[Graphite Metal]   Surface:   %dx%d\n", surfaceW, surfaceH);
            fprintf(stderr, "[Graphite Metal]   Texture:   %lux%lu\n", (unsigned long)texW, (unsigned long)texH);
            fprintf(stderr, "[Graphite Metal]   This causes 1/4 screen rendering bug!\n");
            // Return nullptr to force fallback - do not render with wrong dimensions
            currentDrawable_ = nil;
            return nullptr;
        }

        if (std::getenv("RENDER_DEBUG")) {
            fprintf(stderr, "[Graphite Metal] Surface created: %dx%d (matches requested)\n", surfaceW, surfaceH);
        }

        return surface;
    }

    bool submitFrame() override {
        if (!initialized_ || !recorder_) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Snap the recording from the recorder
        std::unique_ptr<skgpu::graphite::Recording> recording = recorder_->snap();
        if (!recording) {
            fprintf(stderr, "[Graphite Metal] submitFrame: Failed to snap recording\n");
            return false;
        }

        // Insert the recording into the context
        skgpu::graphite::InsertRecordingInfo insertInfo;
        insertInfo.fRecording = recording.get();

        if (!context_->insertRecording(insertInfo)) {
            fprintf(stderr, "[Graphite Metal] submitFrame: Failed to insert recording\n");
            return false;
        }

        // Submit GPU work
        context_->submit(skgpu::graphite::SyncToCpu::kNo);

        return true;
    }

    void present() override {
        if (!initialized_ || !currentDrawable_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        @try {
            // Create command buffer for presentation
            id<MTLCommandBuffer> commandBuffer = [queue_ commandBuffer];
            if (commandBuffer) {
                [commandBuffer presentDrawable:currentDrawable_];
                [commandBuffer commit];
            }
        } @catch (NSException* exception) {
            fprintf(stderr, "[Graphite Metal] Exception during present: %s - %s\n",
                    [[exception name] UTF8String],
                    [[exception reason] UTF8String]);
        }

        currentDrawable_ = nil;
    }

    void setVSyncEnabled(bool enabled) override {
        if (!initialized_ || !metalLayer_) return;

#if GRAPHITE_MACOS
        if (@available(macOS 10.13, *)) {
            metalLayer_.displaySyncEnabled = enabled;
            printf("[Graphite Metal] VSync %s\n", enabled ? "enabled" : "disabled");
        }
#else
        // iOS always uses VSync
        (void)enabled;
#endif
    }

    const char* getBackendName() const override {
        return "Metal Graphite";
    }

private:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    SDL_MetalView metalView_ = nullptr;
    CAMetalLayer* metalLayer_ = nil;
    id<CAMetalDrawable> currentDrawable_ = nil;

    std::unique_ptr<skgpu::graphite::Context> context_;
    std::unique_ptr<skgpu::graphite::Recorder> recorder_;

    int drawableWidth_ = 0;
    int drawableHeight_ = 0;
    bool initialized_ = false;
    std::mutex mutex_;
};

// Factory function for Apple platforms
std::unique_ptr<GraphiteContext> createGraphiteContext(SDL_Window* window) {
    auto context = std::make_unique<MetalGraphiteContext>();
    if (!context->initialize(window)) {
        return nullptr;
    }
    return context;
}

} // namespace svgplayer
