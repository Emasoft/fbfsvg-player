// svg_player.h - Linux FBF SVG Player SDK
//
// This header provides the Linux SDK for the FBF SVG animation player.
// It forwards to the unified cross-platform API defined in shared/fbfsvg_player_api.h
//
// Usage:
//   1. Create a player: FBFSVGPlayer_Create()
//   2. Load an SVG file: FBFSVGPlayer_LoadSVG() or FBFSVGPlayer_LoadSVGData()
//   3. In your render loop:
//      - FBFSVGPlayer_Update() to advance animation time
//      - FBFSVGPlayer_Render() to render to a pixel buffer
//   4. Display the pixel buffer using your GUI toolkit
//   5. Cleanup: FBFSVGPlayer_Destroy()
//
// Thread Safety:
//   - Each FBFSVGPlayerRef should only be used from one thread at a time
//   - Multiple FBFSVGPlayerRef instances can be used from different threads
//
// Memory:
//   - The caller is responsible for allocating/freeing the pixel buffer
//   - The pixel buffer must be width * height * 4 bytes (RGBA, 8-bit per channel)

#pragma once

// Include the unified cross-platform API
#include "../../shared/fbfsvg_player_api.h"

// All functionality is provided by the unified API in shared/fbfsvg_player_api.h
