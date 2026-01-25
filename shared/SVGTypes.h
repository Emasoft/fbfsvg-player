// SVGTypes.h - Shared type definitions for SVG Player SDK
// This header provides unified data structures used across all platforms
// (macOS, Linux, iOS)

#ifndef FBFSVGPLAYER_TYPES_H
#define FBFSVGPLAYER_TYPES_H

#include <stddef.h>  // For size_t
#include <stdint.h>  // For int32_t, uint64_t, etc.

#ifdef __cplusplus
#include <cstdint>   // C++ version of stdint.h
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

// Extended size information including viewBox details
typedef struct {
    int width;                 // Width in SVG units
    int height;                // Height in SVG units
    float viewBoxX;            // ViewBox origin X
    float viewBoxY;            // ViewBox origin Y
    float viewBoxWidth;        // ViewBox width
    float viewBoxHeight;       // ViewBox height
} SVGSizeInfo;

//==============================================================================
// Coordinate Types
//==============================================================================

// Point in dual coordinate systems (for hit testing)
typedef struct {
    float viewX;               // X in view/screen coordinates
    float viewY;               // Y in view/screen coordinates
    float svgX;                // X in SVG viewBox coordinates
    float svgY;                // Y in SVG viewBox coordinates
} SVGDualPoint;

// Rectangle bounds
typedef struct {
    float x;
    float y;
    float width;
    float height;
} SVGRect;

//==============================================================================
// Debug Overlay Flags
//==============================================================================

typedef enum {
    SVGDebugFlag_None           = 0,
    SVGDebugFlag_ShowFPS        = 1 << 0,
    SVGDebugFlag_ShowFrameInfo  = 1 << 1,
    SVGDebugFlag_ShowTiming     = 1 << 2,
    SVGDebugFlag_ShowMemory     = 1 << 3,
    SVGDebugFlag_ShowBounds     = 1 << 4,
    SVGDebugFlag_ShowAll        = 0xFFFFFFFF
} SVGDebugFlags;

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

#endif // FBFSVGPLAYER_TYPES_H
