/**
 * Metal Context for Skia GPU Rendering
 *
 * Provides GPU-accelerated SVG rendering via Skia's Metal backend.
 * This header is C++ compatible - all Objective-C is hidden in the .mm file.
 */

#ifndef METAL_CONTEXT_H
#define METAL_CONTEXT_H

#include <memory>
#include <SDL.h>
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlTypes.h"

namespace svgplayer {

// Forward declaration of implementation
struct MetalContextImpl;

/**
 * MetalContext manages the Metal device, command queue, and Skia GPU context.
 *
 * Usage:
 *   auto metalContext = createMetalContext(window);
 *   if (metalContext) {
 *       GrMTLHandle drawable;
 *       auto surface = metalContext->createSurface(width, height, &drawable);
 *       // ... render to surface->getCanvas() ...
 *       metalContext->presentDrawable(drawable);
 *   }
 */
class MetalContext {
public:
    MetalContext();
    ~MetalContext();

    // Non-copyable
    MetalContext(const MetalContext&) = delete;
    MetalContext& operator=(const MetalContext&) = delete;

    /**
     * Initialize Metal context with the given SDL window.
     * Creates Metal device, command queue, CAMetalLayer, and Skia GPU context.
     * @param window SDL window to render into
     * @return true if initialization succeeded
     */
    bool initialize(SDL_Window* window);

    /**
     * Destroy the Metal context and release all resources.
     */
    void destroy();

    /**
     * Check if the Metal context is initialized.
     */
    bool isInitialized() const;

    /**
     * Update the drawable size when window is resized.
     * @param width New width in pixels
     * @param height New height in pixels
     */
    void updateDrawableSize(int width, int height);

    /**
     * Get the Skia GPU context for advanced usage.
     */
    GrDirectContext* getGrContext() const;

    /**
     * Create a GPU-backed SkSurface for rendering.
     * Acquires the next drawable from the CAMetalLayer.
     *
     * @param width Surface width in pixels
     * @param height Surface height in pixels
     * @param outDrawable Receives the drawable handle (must be presented later)
     * @return SkSurface for rendering, or nullptr on failure
     */
    sk_sp<SkSurface> createSurface(int width, int height, GrMTLHandle* outDrawable);

    /**
     * Present the drawable to the screen.
     * Call this after rendering to the surface is complete.
     *
     * @param drawable The drawable handle from createSurface()
     */
    void presentDrawable(GrMTLHandle drawable);

    /**
     * Flush pending Skia GPU commands without presenting.
     */
    void flush();

    /**
     * Enable or disable VSync (display sync).
     * When enabled, presentation is synchronized with display refresh rate.
     * When disabled, frames are presented as fast as possible (may cause tearing).
     *
     * @param enabled true to enable VSync, false to disable
     */
    void setVSyncEnabled(bool enabled);

    /**
     * Check if VSync is currently enabled.
     * @return true if VSync is enabled
     */
    bool isVSyncEnabled() const;

    /**
     * Set the maximum number of drawable buffers.
     * Default is 2 (double buffering). Set to 3 for triple buffering.
     *
     * @param count Number of drawable buffers (2 or 3)
     */
    void setMaximumDrawableCount(int count);

    /**
     * Get the current maximum drawable count.
     * @return Number of drawable buffers
     */
    int getMaximumDrawableCount() const;

private:
    MetalContextImpl* impl_;
};

/**
 * Factory function to create and initialize a MetalContext.
 *
 * @param window SDL window to render into
 * @return Initialized MetalContext, or nullptr if Metal is unavailable
 */
std::unique_ptr<MetalContext> createMetalContext(SDL_Window* window);

} // namespace svgplayer

#endif // METAL_CONTEXT_H
