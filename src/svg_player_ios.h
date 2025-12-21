// svg_player_ios.h - iOS SVG Player SDK
//
// This header provides the iOS SDK for the SVG animation player.
// It forwards to the unified cross-platform API defined in shared/svg_player_api.h
//
// Usage:
//   1. Create a player: SVGPlayer_Create()
//   2. Load an SVG file: SVGPlayer_LoadSVG() or SVGPlayer_LoadSVGData()
//   3. In your display link callback:
//      - SVGPlayer_Update() to advance animation time
//      - SVGPlayer_Render() to render to a pixel buffer
//   4. Display the pixel buffer in a UIImageView or CALayer
//   5. Cleanup: SVGPlayer_Destroy()
//
// Thread Safety:
//   - Each SVGPlayerRef should only be used from one thread at a time
//   - Multiple SVGPlayerRef instances can be used from different threads
//
// Memory:
//   - The caller is responsible for allocating/freeing the pixel buffer
//   - The pixel buffer must be width * height * 4 bytes (RGBA, 8-bit per channel)

#pragma once

// Include the unified cross-platform API
#include "../shared/svg_player_api.h"

// All functionality is provided by the unified API in shared/svg_player_api.h
