/**
 * Graphite Context Stub Implementation
 *
 * This stub provides a createGraphiteContext() that returns nullptr
 * when the Graphite GPU backend is not available (e.g., no Vulkan SDK on Windows).
 *
 * The build system should:
 * - Compile this stub when Vulkan SDK is NOT available
 * - Compile graphite_context_vulkan.cpp when Vulkan SDK IS available
 *
 * The preprocessor guard ensures only one implementation is active.
 */

// Only compile this stub when Vulkan/Graphite is NOT available
// The build script defines GRAPHITE_VULKAN_AVAILABLE when Vulkan SDK is present
#ifndef GRAPHITE_VULKAN_AVAILABLE

#include "graphite_context.h"
#include <cstdio>

namespace svgplayer {

/**
 * Stub factory function when Graphite backend is unavailable.
 * Returns nullptr so the player falls back to software rendering.
 */
std::unique_ptr<GraphiteContext> createGraphiteContext(SDL_Window* /*window*/) {
    // Graphite GPU backend not available - return nullptr
    // Player will detect this and use software rasterization instead
    fprintf(stderr, "[Graphite] GPU backend not available (no Vulkan SDK)\n");
    return nullptr;
}

} // namespace svgplayer

#endif // GRAPHITE_VULKAN_AVAILABLE
