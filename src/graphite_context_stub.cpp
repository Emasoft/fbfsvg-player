/**
 * Stub implementation of createGraphiteContext when Vulkan is not available.
 * Returns nullptr to disable GPU rendering - falls back to CPU raster.
 */

#include "graphite_context.h"

namespace svgplayer {

std::unique_ptr<GraphiteContext> createGraphiteContext(SDL_Window* /*window*/) {
    // Vulkan not available - return nullptr to use CPU raster path
    return nullptr;
}

} // namespace svgplayer
