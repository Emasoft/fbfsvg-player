// svg_player.h - macOS SVG Player SDK
//
// This header provides the macOS SDK for the SVG animation player.
// It forwards to the unified cross-platform API defined in shared/svg_player_api.h
//
// Usage:
//   1. Create a player: FBFSVGPlayer_Create()
//   2. Load an SVG file: FBFSVGPlayer_LoadSVG() or FBFSVGPlayer_LoadSVGData()
//   3. In your display link callback:
//      - FBFSVGPlayer_Update() to advance animation time
//      - FBFSVGPlayer_Render() to render to a pixel buffer
//   4. Display the pixel buffer in a NSImageView or CALayer
//   5. Cleanup: FBFSVGPlayer_Destroy()
//
// Thread Safety:
//   - Each FBFSVGPlayerRef should only be used from one thread at a time
//   - Multiple FBFSVGPlayerRef instances can be used from different threads
//
// Memory:
//   - The caller is responsible for allocating/freeing the pixel buffer
//   - The pixel buffer must be width * height * 4 bytes (RGBA, 8-bit per channel)
//
// Copyright (c) 2024. MIT License.

#pragma once

// Include the unified cross-platform API
#include "../../shared/fbfsvg_player_api.h"

// ============================================================================
// macOS-Specific Extensions
// ============================================================================
//
// These functions are specific to the macOS SDK and may not be available
// on other platforms.

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

// Metal extensions for macOS are available via the unified API's
// platform extension mechanism when compiled with Metal support.
//
// The unified API automatically uses CoreText for font management
// on macOS/iOS.

#endif // TARGET_OS_OSX
#endif // __APPLE__

