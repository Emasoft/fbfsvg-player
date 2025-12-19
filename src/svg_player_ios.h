// svg_player_ios.h - iOS SVG Player public API
// This header provides a C-compatible API for integrating SVG rendering
// with iOS UIKit applications.
//
// Usage:
//   1. Create a renderer: SVGPlayer_Create()
//   2. Load an SVG file: SVGPlayer_LoadSVG()
//   3. In your display link callback:
//      - SVGPlayer_Update() to advance animation time
//      - SVGPlayer_Render() to render to a pixel buffer
//   4. Display the pixel buffer in a UIImageView or CALayer
//   5. Cleanup: SVGPlayer_Destroy()

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to SVG player instance
typedef struct SVGPlayer* SVGPlayerHandle;

// Animation playback state
typedef enum {
    SVGPlaybackState_Stopped,
    SVGPlaybackState_Playing,
    SVGPlaybackState_Paused
} SVGPlaybackState;

// Rendering statistics
typedef struct {
    double renderTimeMs;      // Time to render last frame
    double animationTimeMs;   // Current animation time
    int currentFrame;         // Current frame index
    int totalFrames;          // Total frames in animation
    double fps;               // Current frames per second
} SVGRenderStats;

// Create a new SVG player instance
// Returns NULL on failure
SVGPlayerHandle SVGPlayer_Create(void);

// Destroy an SVG player instance and free all resources
void SVGPlayer_Destroy(SVGPlayerHandle player);

// Load an SVG file
// Returns true on success, false on failure
bool SVGPlayer_LoadSVG(SVGPlayerHandle player, const char* filepath);

// Load SVG from memory buffer
// Returns true on success, false on failure
bool SVGPlayer_LoadSVGData(SVGPlayerHandle player, const void* data, size_t length);

// Get the intrinsic size of the loaded SVG
// Returns false if no SVG is loaded
bool SVGPlayer_GetSize(SVGPlayerHandle player, int* width, int* height);

// Set playback state
void SVGPlayer_SetPlaybackState(SVGPlayerHandle player, SVGPlaybackState state);

// Get current playback state
SVGPlaybackState SVGPlayer_GetPlaybackState(SVGPlayerHandle player);

// Update animation time
// Call this from your CADisplayLink callback
// deltaTime is the time since the last update in seconds
void SVGPlayer_Update(SVGPlayerHandle player, double deltaTime);

// Seek to a specific time in the animation (in seconds)
void SVGPlayer_SeekTo(SVGPlayerHandle player, double timeSeconds);

// Render the current frame to a pixel buffer
// The buffer must be pre-allocated with width * height * 4 bytes (RGBA)
// scale: HiDPI scale factor (e.g., 2.0 for Retina, 3.0 for @3x)
// Returns true on success
bool SVGPlayer_Render(SVGPlayerHandle player,
                      void* pixelBuffer,
                      int width,
                      int height,
                      float scale);

// Get rendering statistics
SVGRenderStats SVGPlayer_GetStats(SVGPlayerHandle player);

// Get the animation duration in seconds
double SVGPlayer_GetDuration(SVGPlayerHandle player);

// Check if the animation loops
bool SVGPlayer_IsLooping(SVGPlayerHandle player);

// Set animation loop mode
void SVGPlayer_SetLooping(SVGPlayerHandle player, bool looping);

// Get the last error message (may be empty)
const char* SVGPlayer_GetLastError(SVGPlayerHandle player);

#ifdef __cplusplus
}
#endif
