/**
 * Metal Context for Skia GPU Rendering
 *
 * This file encapsulates Metal device, command queue, and Skia GPU context
 * initialization. It provides a C++ interface for the main player to use
 * Metal-accelerated SVG rendering.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#include <SDL.h>
#include <SDL_metal.h>

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlTypes.h"
#include "include/gpu/ganesh/mtl/SkSurfaceMetal.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkCanvas.h"
#include <cstdlib>  // for std::getenv

#include "metal_context.h"

namespace svgplayer {

// Internal implementation struct holding Obj-C objects
struct MetalContextImpl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    SDL_MetalView metalView = nullptr;
    CAMetalLayer* metalLayer = nil;
    sk_sp<GrDirectContext> skiaContext;
    bool initialized = false;
};

MetalContext::MetalContext() : impl_(new MetalContextImpl()) {}

MetalContext::~MetalContext() {
    destroy();
    delete impl_;
}

bool MetalContext::initialize(SDL_Window* window) {
    if (!window) {
        fprintf(stderr, "[Metal] Error: NULL window\n");
        return false;
    }

    // Create Metal device (system default GPU)
    impl_->device = MTLCreateSystemDefaultDevice();
    if (!impl_->device) {
        fprintf(stderr, "[Metal] Error: Failed to create Metal device\n");
        return false;
    }
    printf("[Metal] Using device: %s\n", [[impl_->device name] UTF8String]);

    // Create command queue
    impl_->queue = [impl_->device newCommandQueue];
    if (!impl_->queue) {
        fprintf(stderr, "[Metal] Error: Failed to create command queue\n");
        destroy();  // Clean up device before returning
        return false;
    }

    // Create SDL Metal view (this creates a CAMetalLayer-backed view)
    impl_->metalView = SDL_Metal_CreateView(window);
    if (!impl_->metalView) {
        fprintf(stderr, "[Metal] Error: Failed to create SDL Metal view: %s\n", SDL_GetError());
        destroy();  // Clean up device and queue before returning
        return false;
    }

    // Get the CAMetalLayer from the view
    void* layerPtr = SDL_Metal_GetLayer(impl_->metalView);
    if (!layerPtr) {
        fprintf(stderr, "[Metal] Error: Failed to get Metal layer\n");
        destroy();  // Clean up all allocated resources before returning
        return false;
    }
    impl_->metalLayer = (__bridge CAMetalLayer*)layerPtr;

    // Configure the layer
    impl_->metalLayer.device = impl_->device;
    impl_->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    impl_->metalLayer.framebufferOnly = NO;  // Skia needs to read from the texture

    // Handle potential nil NSScreen (headless environments or early startup)
    NSScreen* mainScreen = [NSScreen mainScreen];
    CGFloat contentsScale = mainScreen ? [mainScreen backingScaleFactor] : 2.0;  // Default to Retina
    impl_->metalLayer.contentsScale = contentsScale;

    // Allow nextDrawable to timeout (default 1 second) instead of blocking indefinitely
    // This prevents the main loop from freezing if all drawables are in use
    if (@available(macOS 10.15.4, *)) {
        impl_->metalLayer.allowsNextDrawableTimeout = YES;
    }

    // Configure triple buffering for smoother frame pacing
    // 3 drawables reduces frame latency compared to default 2 (double buffering)
    impl_->metalLayer.maximumDrawableCount = 3;

    // Use async presentation (faster, but may cause tearing if VSync is off)
    // presentsWithTransaction = NO means drawable is presented immediately after commit
    impl_->metalLayer.presentsWithTransaction = NO;

    // Enable VSync by default for smooth playback
    if (@available(macOS 10.13, *)) {
        impl_->metalLayer.displaySyncEnabled = YES;
    }

    // Set initial drawable size based on window size
    int drawableW, drawableH;
    SDL_Metal_GetDrawableSize(window, &drawableW, &drawableH);
    if (drawableW > 0 && drawableH > 0) {
        impl_->metalLayer.drawableSize = CGSizeMake(drawableW, drawableH);
        printf("[Metal] Initial drawable size: %dx%d\n", drawableW, drawableH);
    } else {
        // Fallback to window size (reuse contentsScale already computed above)
        int windowW, windowH;
        SDL_GetWindowSize(window, &windowW, &windowH);
        impl_->metalLayer.drawableSize = CGSizeMake(windowW * contentsScale, windowH * contentsScale);
        printf("[Metal] Using window size * scale: %dx%d\n", (int)(windowW * contentsScale), (int)(windowH * contentsScale));
    }

    // Create Skia GPU context with Metal backend
    GrMtlBackendContext backendContext;
    // Use retain semantics for the sk_cfp wrapper
    backendContext.fDevice.retain((__bridge void*)impl_->device);
    backendContext.fQueue.retain((__bridge void*)impl_->queue);

    impl_->skiaContext = GrDirectContexts::MakeMetal(backendContext);
    if (!impl_->skiaContext) {
        fprintf(stderr, "[Metal] Error: Failed to create Skia GPU context\n");
        destroy();  // Clean up all Metal resources before returning
        return false;
    }

    impl_->initialized = true;
    printf("[Metal] Successfully initialized Metal backend for Skia\n");
    return true;
}

void MetalContext::destroy() {
    if (!impl_->initialized) return;

    impl_->skiaContext.reset();

    if (impl_->metalView) {
        SDL_Metal_DestroyView(impl_->metalView);
        impl_->metalView = nullptr;
    }

    impl_->metalLayer = nil;
    impl_->queue = nil;
    impl_->device = nil;
    impl_->initialized = false;

    printf("[Metal] Destroyed Metal context\n");
}

bool MetalContext::isInitialized() const {
    return impl_->initialized;
}

void MetalContext::updateDrawableSize(int width, int height) {
    if (!impl_->initialized || !impl_->metalLayer) return;

    // Validate dimensions to prevent Metal validation errors
    if (width <= 0 || height <= 0) return;

    impl_->metalLayer.drawableSize = CGSizeMake(width, height);
}

GrDirectContext* MetalContext::getGrContext() const {
    return impl_->skiaContext.get();
}

sk_sp<SkSurface> MetalContext::createSurface(int width, int height, GrMTLHandle* outDrawable) {
    if (!impl_->initialized) {
        fprintf(stderr, "[Metal] createSurface: context not initialized\n");
        return nullptr;
    }

    // Validate dimensions
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "[Metal] createSurface: invalid dimensions %dx%d\n", width, height);
        return nullptr;
    }

    // Update drawable size to match requested dimensions
    impl_->metalLayer.drawableSize = CGSizeMake(width, height);

    // Verify drawable size was set correctly
    CGSize actualSize = impl_->metalLayer.drawableSize;
    if (actualSize.width <= 0 || actualSize.height <= 0) {
        fprintf(stderr, "[Metal] createSurface: layer drawable size is zero after setting to %dx%d\n", width, height);
        return nullptr;
    }

    // Create surface wrapping the CAMetalLayer
    // Note: Skia uses a LAZY proxy - the drawable is only acquired when the
    // surface is actually instantiated (when drawing happens, not here)
    GrMTLHandle tempDrawable = nullptr;
    sk_sp<SkSurface> surface = SkSurfaces::WrapCAMetalLayer(
        impl_->skiaContext.get(),
        (__bridge GrMTLHandle)impl_->metalLayer,
        kTopLeft_GrSurfaceOrigin,
        1,  // sample count (no MSAA)
        kBGRA_8888_SkColorType,
        nullptr,  // colorSpace (sRGB default)
        nullptr,  // surfaceProps
        &tempDrawable);

    if (surface) {
        // Force lazy proxy instantiation by accessing the canvas and flushing
        // This triggers Skia's lazy callback which acquires the drawable
        SkCanvas* canvas = surface->getCanvas();
        if (canvas) {
            // A minimal operation to force instantiation
            canvas->clear(SK_ColorTRANSPARENT);
            impl_->skiaContext->flushAndSubmit();
        }

        // After instantiation, tempDrawable should now be set
        if (std::getenv("RENDER_DEBUG")) {
            fprintf(stderr, "[Metal] createSurface: surface=%p, tempDrawable=%p (after flush)\n",
                    surface.get(), tempDrawable);
        }
    }

    // Copy drawable to output parameter
    if (outDrawable) {
        *outDrawable = tempDrawable;
    }

    if (!surface) {
        // Log diagnostic info without acquiring a drawable (which would exhaust the pool)
        fprintf(stderr, "[Metal] createSurface: Skia WrapCAMetalLayer failed (layer size: %.0fx%.0f)\n",
                actualSize.width, actualSize.height);
    }

    return surface;
}

void MetalContext::presentDrawable(GrMTLHandle drawable) {
    if (!impl_->initialized || !drawable) return;

    @try {
        // Flush Skia GPU work
        impl_->skiaContext->flushAndSubmit();

        // Present the drawable
        id<MTLCommandBuffer> commandBuffer = [impl_->queue commandBuffer];
        if (!commandBuffer) {
            fprintf(stderr, "[Metal] Error: Failed to create command buffer for presentation\n");
            return;
        }
        [commandBuffer presentDrawable:(__bridge id<CAMetalDrawable>)drawable];
        [commandBuffer commit];
    } @catch (NSException *exception) {
        fprintf(stderr, "[Metal] Exception during present: %s - %s\n",
                [[exception name] UTF8String],
                [[exception reason] UTF8String]);
    }
}

void MetalContext::flush() {
    if (impl_->skiaContext) {
        impl_->skiaContext->flushAndSubmit();
    }
}

void MetalContext::setVSyncEnabled(bool enabled) {
    if (!impl_->initialized || !impl_->metalLayer) return;

    // displaySyncEnabled requires macOS 10.13+
    // When enabled (default), presentation syncs to display refresh rate
    // When disabled, frames present immediately (may cause tearing but lower latency)
    if (@available(macOS 10.13, *)) {
        impl_->metalLayer.displaySyncEnabled = enabled;
        printf("[Metal] VSync %s\n", enabled ? "enabled" : "disabled");
    } else {
        printf("[Metal] VSync control not available on this macOS version\n");
    }
}

bool MetalContext::isVSyncEnabled() const {
    if (!impl_->initialized || !impl_->metalLayer) return true;  // Default is enabled

    if (@available(macOS 10.13, *)) {
        return impl_->metalLayer.displaySyncEnabled;
    }
    return true;  // Assume VSync is on for older macOS
}

void MetalContext::setMaximumDrawableCount(int count) {
    if (!impl_->initialized || !impl_->metalLayer) return;

    // CAMetalLayer supports 2 (double buffering) or 3 (triple buffering)
    // Triple buffering reduces latency at the cost of one extra frame of memory
    if (count < 2) count = 2;
    if (count > 3) count = 3;

    impl_->metalLayer.maximumDrawableCount = count;
    printf("[Metal] Maximum drawable count set to %d\n", count);
}

int MetalContext::getMaximumDrawableCount() const {
    if (!impl_->initialized || !impl_->metalLayer) return 2;  // Default

    return (int)impl_->metalLayer.maximumDrawableCount;
}

// Factory function
std::unique_ptr<MetalContext> createMetalContext(SDL_Window* window) {
    auto context = std::make_unique<MetalContext>();
    if (!context->initialize(window)) {
        return nullptr;
    }
    return context;
}

} // namespace svgplayer
