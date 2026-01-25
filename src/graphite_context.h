/**
 * Graphite Context for Skia GPU Rendering
 *
 * Provides Skia Graphite GPU-accelerated SVG rendering.
 * Supports Metal (macOS/iOS) and Vulkan (Linux/Windows) backends.
 *
 * This header is C++ compatible - all Objective-C and Vulkan details are hidden
 * in the platform-specific implementation files.
 */

#ifndef GRAPHITE_CONTEXT_H
#define GRAPHITE_CONTEXT_H

#include <memory>
#include <SDL.h>
#include "include/core/SkSurface.h"

// Forward declarations for Skia Graphite types
namespace skgpu::graphite {
    class Context;
    class Recorder;
}

namespace svgplayer {

/**
 * Abstract interface for Graphite GPU contexts.
 *
 * Graphite is Skia's next-generation GPU backend that replaces Ganesh.
 * It provides better performance through modern GPU API usage and
 * improved batching of draw operations.
 *
 * Platform-specific implementations:
 * - Metal Graphite (macOS/iOS): graphite_context_metal.mm
 * - Vulkan Graphite (Linux/Windows): graphite_context_vulkan.cpp
 */
class GraphiteContext {
public:
    virtual ~GraphiteContext() = default;

    // Non-copyable
    GraphiteContext(const GraphiteContext&) = delete;
    GraphiteContext& operator=(const GraphiteContext&) = delete;

    /**
     * Initialize the Graphite context with the given SDL window.
     * Creates GPU device, command queue, and Skia Graphite context.
     *
     * @param window SDL window to render into
     * @return true if initialization succeeded
     */
    virtual bool initialize(SDL_Window* window) = 0;

    /**
     * Destroy the Graphite context and release all resources.
     */
    virtual void destroy() = 0;

    /**
     * Check if the Graphite context is initialized and ready.
     */
    virtual bool isInitialized() const = 0;

    /**
     * Update the drawable/swapchain size when window is resized.
     *
     * @param width New width in pixels
     * @param height New height in pixels
     */
    virtual void updateDrawableSize(int width, int height) = 0;

    /**
     * Create a GPU-backed SkSurface for rendering.
     * The surface is backed by the Graphite Recorder.
     *
     * @param width Surface width in pixels
     * @param height Surface height in pixels
     * @return SkSurface for rendering, or nullptr on failure
     */
    virtual sk_sp<SkSurface> createSurface(int width, int height) = 0;

    /**
     * Submit the current frame's recorded commands for execution.
     * This snaps the Recorder and inserts the recording into the Context.
     *
     * @return true if submission succeeded
     */
    virtual bool submitFrame() = 0;

    /**
     * Present the rendered frame to the screen.
     * Call this after submitFrame() to display the result.
     */
    virtual void present() = 0;

    /**
     * Enable or disable VSync (display sync).
     * When enabled, presentation is synchronized with display refresh rate.
     *
     * @param enabled true to enable VSync, false to disable
     */
    virtual void setVSyncEnabled(bool enabled) = 0;

    /**
     * Get the name of the GPU backend being used.
     * @return "Metal Graphite" or "Vulkan Graphite"
     */
    virtual const char* getBackendName() const = 0;

protected:
    GraphiteContext() = default;
};

/**
 * Factory function to create and initialize a platform-appropriate GraphiteContext.
 *
 * On macOS/iOS: Creates Metal Graphite context
 * On Linux/Windows: Creates Vulkan Graphite context
 *
 * @param window SDL window to render into
 * @return Initialized GraphiteContext, or nullptr if Graphite is unavailable
 */
std::unique_ptr<GraphiteContext> createGraphiteContext(SDL_Window* window);

} // namespace svgplayer

#endif // GRAPHITE_CONTEXT_H
