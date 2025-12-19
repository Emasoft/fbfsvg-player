// SVGTypes.h - Shared type definitions for SVG Player SDK
// This header provides unified data structures used across all platforms
// (macOS, Linux, iOS)

#ifndef SVGPLAYER_TYPES_H
#define SVGPLAYER_TYPES_H

#include <stddef.h>  // For size_t

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// Playback State
//==============================================================================

typedef enum {
    SVGPlaybackState_Stopped = 0,
    SVGPlaybackState_Playing,
    SVGPlaybackState_Paused
} SVGPlaybackState;

//==============================================================================
// Repeat Mode
//==============================================================================

typedef enum {
    SVGRepeatMode_None = 0,     // Play once and stop
    SVGRepeatMode_Loop,         // Loop indefinitely from start
    SVGRepeatMode_Reverse,      // Play forward then backward (ping-pong)
    SVGRepeatMode_Count         // Loop a specific number of times
} SVGRepeatMode;

//==============================================================================
// Render Statistics
//==============================================================================

// Unified render statistics structure - all platforms should use these fields
typedef struct {
    double renderTimeMs;       // Time to render last frame in milliseconds
    double updateTimeMs;       // Time to update animation state in milliseconds
    double animationTimeMs;    // Current animation time in milliseconds
    int currentFrame;          // Current frame index (0-based)
    int totalFrames;           // Total frames in animation
    double fps;                // Current frames per second
    size_t peakMemoryBytes;    // Peak memory usage in bytes (0 if unavailable)
    int elementsRendered;      // Number of SVG elements rendered (0 if unavailable)
    int frameSkips;            // Number of frames skipped due to slow rendering
} SVGRenderStats;

//==============================================================================
// Size Information
//==============================================================================

typedef struct {
    int width;                 // Width in pixels
    int height;                // Height in pixels
} SVGSize;

//==============================================================================
// Animation Information
//==============================================================================

typedef struct {
    const char* attributeName; // Attribute being animated (e.g., "xlink:href")
    const char* targetElement; // Target element ID
    double beginTime;          // Animation start time in seconds
    double duration;           // Animation duration in seconds
    int keyframeCount;         // Number of keyframes
    int isDiscrete;            // 1 if discrete animation, 0 if continuous
} SVGAnimationInfo;

#ifdef __cplusplus
}
#endif

#endif // SVGPLAYER_TYPES_H
