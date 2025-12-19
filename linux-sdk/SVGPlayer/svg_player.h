// svg_player.h - Cross-platform SVG Player C API
//
// This header provides a C-compatible API for integrating SVG rendering
// with any application framework (GTK, Qt, SDL2, raw framebuffer, etc.)
//
// Usage:
//   1. Create a player: SVGPlayer_Create()
//   2. Load an SVG file: SVGPlayer_LoadSVG() or SVGPlayer_LoadSVGData()
//   3. In your render loop:
//      - SVGPlayer_Update() to advance animation time
//      - SVGPlayer_Render() to render to a pixel buffer
//   4. Display the pixel buffer using your GUI toolkit
//   5. Cleanup: SVGPlayer_Destroy()
//
// Thread Safety:
//   - Each SVGPlayerHandle should only be used from one thread at a time
//   - Multiple SVGPlayerHandle instances can be used from different threads
//
// Memory:
//   - The caller is responsible for allocating/freeing the pixel buffer
//   - The pixel buffer must be width * height * 4 bytes (RGBA, 8-bit per channel)

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Shared type definitions (SVGPlaybackState, SVGRepeatMode, SVGRenderStats, SVGSize, etc.)
#include "../../shared/SVGTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Library version
#define SVG_PLAYER_VERSION_MAJOR 1
#define SVG_PLAYER_VERSION_MINOR 0
#define SVG_PLAYER_VERSION_PATCH 0

// Export/import macros for shared library
#ifdef _WIN32
    #ifdef SVG_PLAYER_BUILDING_DLL
        #define SVG_PLAYER_API __declspec(dllexport)
    #else
        #define SVG_PLAYER_API __declspec(dllimport)
    #endif
#else
    #ifdef SVG_PLAYER_BUILDING_DLL
        #define SVG_PLAYER_API __attribute__((visibility("default")))
    #else
        #define SVG_PLAYER_API
    #endif
#endif

// Opaque handle to SVG player instance
typedef struct SVGPlayer* SVGPlayerHandle;

// Extended SVG size information (Linux SDK provides more detail than base SVGSize)
typedef struct {
    int width;                 // Width in SVG units
    int height;                // Height in SVG units
    float viewBoxX;            // ViewBox X origin
    float viewBoxY;            // ViewBox Y origin
    float viewBoxWidth;        // ViewBox width
    float viewBoxHeight;       // ViewBox height
} SVGSizeInfo;

// Point in both coordinate systems (for element touch events)
typedef struct {
    float viewX;               // X in view/screen coordinates
    float viewY;               // Y in view/screen coordinates
    float svgX;                // X in SVG viewbox coordinates
    float svgY;                // Y in SVG viewbox coordinates
} SVGDualPoint;

// ============================================================================
// Lifecycle Functions
// ============================================================================

/// Create a new SVG player instance
/// @return Handle to the player, or NULL on failure
SVG_PLAYER_API SVGPlayerHandle SVGPlayer_Create(void);

/// Destroy an SVG player instance and free all resources
/// @param player Handle to destroy (safe to pass NULL)
SVG_PLAYER_API void SVGPlayer_Destroy(SVGPlayerHandle player);

/// Get the library version string
/// @return Version string (e.g., "1.0.0")
SVG_PLAYER_API const char* SVGPlayer_GetVersion(void);

// ============================================================================
// Loading Functions
// ============================================================================

/// Load an SVG file from disk
/// @param player Handle to the player
/// @param filepath Path to the SVG file
/// @return true on success, false on failure (check SVGPlayer_GetLastError)
SVG_PLAYER_API bool SVGPlayer_LoadSVG(SVGPlayerHandle player, const char* filepath);

/// Load SVG from memory buffer
/// @param player Handle to the player
/// @param data Pointer to SVG data (UTF-8 encoded XML)
/// @param length Length of data in bytes
/// @return true on success, false on failure
SVG_PLAYER_API bool SVGPlayer_LoadSVGData(SVGPlayerHandle player, const void* data, size_t length);

/// Unload the current SVG and free associated resources
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Unload(SVGPlayerHandle player);

/// Check if an SVG is currently loaded
/// @param player Handle to the player
/// @return true if an SVG is loaded
SVG_PLAYER_API bool SVGPlayer_IsLoaded(SVGPlayerHandle player);

// ============================================================================
// Size and Dimension Functions
// ============================================================================

/// Get the intrinsic size of the loaded SVG
/// @param player Handle to the player
/// @param width Output: width in SVG units (can be NULL)
/// @param height Output: height in SVG units (can be NULL)
/// @return true if SVG is loaded, false otherwise
SVG_PLAYER_API bool SVGPlayer_GetSize(SVGPlayerHandle player, int* width, int* height);

/// Get detailed size information including viewBox
/// @param player Handle to the player
/// @param info Output: size information structure
/// @return true if SVG is loaded, false otherwise
SVG_PLAYER_API bool SVGPlayer_GetSizeInfo(SVGPlayerHandle player, SVGSizeInfo* info);

// ============================================================================
// Playback Control Functions
// ============================================================================

/// Start or resume playback
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Play(SVGPlayerHandle player);

/// Pause playback at current position
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Pause(SVGPlayerHandle player);

/// Stop playback and reset to beginning
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Stop(SVGPlayerHandle player);

/// Toggle between play and pause
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_TogglePlayback(SVGPlayerHandle player);

/// Set playback state directly
/// @param player Handle to the player
/// @param state New playback state
SVG_PLAYER_API void SVGPlayer_SetPlaybackState(SVGPlayerHandle player, SVGPlaybackState state);

/// Get current playback state
/// @param player Handle to the player
/// @return Current playback state
SVG_PLAYER_API SVGPlaybackState SVGPlayer_GetPlaybackState(SVGPlayerHandle player);

// ============================================================================
// Repeat Mode Functions
// ============================================================================

/// Set repeat mode
/// @param player Handle to the player
/// @param mode Repeat mode
SVG_PLAYER_API void SVGPlayer_SetRepeatMode(SVGPlayerHandle player, SVGRepeatMode mode);

/// Get current repeat mode
/// @param player Handle to the player
/// @return Current repeat mode
SVG_PLAYER_API SVGRepeatMode SVGPlayer_GetRepeatMode(SVGPlayerHandle player);

/// Set repeat count (used with SVGRepeatMode_Count)
/// @param player Handle to the player
/// @param count Number of times to repeat
SVG_PLAYER_API void SVGPlayer_SetRepeatCount(SVGPlayerHandle player, int count);

/// Get current repeat count
/// @param player Handle to the player
/// @return Current repeat count setting
SVG_PLAYER_API int SVGPlayer_GetRepeatCount(SVGPlayerHandle player);

/// Get number of completed loop iterations
/// @param player Handle to the player
/// @return Number of completed loops
SVG_PLAYER_API int SVGPlayer_GetCompletedLoops(SVGPlayerHandle player);

// Legacy looping API (for compatibility)
SVG_PLAYER_API bool SVGPlayer_IsLooping(SVGPlayerHandle player);
SVG_PLAYER_API void SVGPlayer_SetLooping(SVGPlayerHandle player, bool looping);

// ============================================================================
// Playback Rate Functions
// ============================================================================

/// Set playback rate (speed multiplier)
/// @param player Handle to the player
/// @param rate Speed multiplier (1.0 = normal, 2.0 = 2x speed, 0.5 = half speed)
/// @note Rate is clamped to 0.1 - 10.0 range
SVG_PLAYER_API void SVGPlayer_SetPlaybackRate(SVGPlayerHandle player, float rate);

/// Get current playback rate
/// @param player Handle to the player
/// @return Current playback rate
SVG_PLAYER_API float SVGPlayer_GetPlaybackRate(SVGPlayerHandle player);

// ============================================================================
// Timeline Functions
// ============================================================================

/// Update animation time (call from your render loop)
/// @param player Handle to the player
/// @param deltaTime Time since last update in seconds
SVG_PLAYER_API void SVGPlayer_Update(SVGPlayerHandle player, double deltaTime);

/// Seek to a specific time
/// @param player Handle to the player
/// @param timeSeconds Time in seconds (clamped to valid range)
SVG_PLAYER_API void SVGPlayer_SeekTo(SVGPlayerHandle player, double timeSeconds);

/// Seek to a specific frame
/// @param player Handle to the player
/// @param frame Frame number (0-indexed, clamped to valid range)
SVG_PLAYER_API void SVGPlayer_SeekToFrame(SVGPlayerHandle player, int frame);

/// Seek to a progress position
/// @param player Handle to the player
/// @param progress Value from 0.0 (start) to 1.0 (end)
SVG_PLAYER_API void SVGPlayer_SeekToProgress(SVGPlayerHandle player, float progress);

/// Get animation duration
/// @param player Handle to the player
/// @return Duration in seconds (0 for static SVG)
SVG_PLAYER_API double SVGPlayer_GetDuration(SVGPlayerHandle player);

/// Get current time position
/// @param player Handle to the player
/// @return Current time in seconds
SVG_PLAYER_API double SVGPlayer_GetCurrentTime(SVGPlayerHandle player);

/// Get current progress
/// @param player Handle to the player
/// @return Progress from 0.0 to 1.0
SVG_PLAYER_API float SVGPlayer_GetProgress(SVGPlayerHandle player);

/// Get current frame number
/// @param player Handle to the player
/// @return Current frame (0-indexed)
SVG_PLAYER_API int SVGPlayer_GetCurrentFrame(SVGPlayerHandle player);

/// Get total frame count
/// @param player Handle to the player
/// @return Total number of frames
SVG_PLAYER_API int SVGPlayer_GetTotalFrames(SVGPlayerHandle player);

/// Get frame rate
/// @param player Handle to the player
/// @return Frame rate in FPS
SVG_PLAYER_API float SVGPlayer_GetFrameRate(SVGPlayerHandle player);

// ============================================================================
// Frame Stepping Functions
// ============================================================================

/// Step forward by one frame (pauses playback)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_StepForward(SVGPlayerHandle player);

/// Step backward by one frame (pauses playback)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_StepBackward(SVGPlayerHandle player);

/// Step by specific number of frames
/// @param player Handle to the player
/// @param frames Number of frames (positive = forward, negative = backward)
SVG_PLAYER_API void SVGPlayer_StepByFrames(SVGPlayerHandle player, int frames);

// ============================================================================
// Rendering Functions
// ============================================================================

/// Render the current frame to a pixel buffer
///
/// The buffer must be pre-allocated with width * height * 4 bytes.
/// Output format is RGBA with 8 bits per channel, premultiplied alpha.
///
/// @param player Handle to the player
/// @param pixelBuffer Pointer to pre-allocated RGBA pixel buffer
/// @param width Width of the buffer in pixels
/// @param height Height of the buffer in pixels
/// @param scale HiDPI scale factor (1.0 for standard, 2.0 for HiDPI)
/// @return true on success, false on failure
SVG_PLAYER_API bool SVGPlayer_Render(SVGPlayerHandle player,
                                      void* pixelBuffer,
                                      int width,
                                      int height,
                                      float scale);

/// Render a specific frame at a specific time
/// @param player Handle to the player
/// @param pixelBuffer Pointer to pre-allocated RGBA pixel buffer
/// @param width Width of the buffer in pixels
/// @param height Height of the buffer in pixels
/// @param scale HiDPI scale factor
/// @param timeSeconds Time in seconds for the frame to render
/// @return true on success, false on failure
SVG_PLAYER_API bool SVGPlayer_RenderAtTime(SVGPlayerHandle player,
                                            void* pixelBuffer,
                                            int width,
                                            int height,
                                            float scale,
                                            double timeSeconds);

// ============================================================================
// Statistics and Diagnostics
// ============================================================================

/// Get rendering statistics
/// @param player Handle to the player
/// @return Rendering statistics structure
SVG_PLAYER_API SVGRenderStats SVGPlayer_GetStats(SVGPlayerHandle player);

/// Get the last error message
/// @param player Handle to the player
/// @return Error message string (empty if no error)
SVG_PLAYER_API const char* SVGPlayer_GetLastError(SVGPlayerHandle player);

/// Clear the last error
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_ClearError(SVGPlayerHandle player);

// ============================================================================
// Coordinate Conversion Functions
// ============================================================================

/// Convert view coordinates to SVG coordinates
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @param svgX Output: X in SVG coordinates
/// @param svgY Output: Y in SVG coordinates
/// @return true on success
SVG_PLAYER_API bool SVGPlayer_ViewToSVG(SVGPlayerHandle player,
                                         float viewX, float viewY,
                                         int viewWidth, int viewHeight,
                                         float* svgX, float* svgY);

/// Convert SVG coordinates to view coordinates
/// @param player Handle to the player
/// @param svgX X in SVG coordinates
/// @param svgY Y in SVG coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @param viewX Output: X in view coordinates
/// @param viewY Output: Y in view coordinates
/// @return true on success
SVG_PLAYER_API bool SVGPlayer_SVGToView(SVGPlayerHandle player,
                                         float svgX, float svgY,
                                         int viewWidth, int viewHeight,
                                         float* viewX, float* viewY);

// ============================================================================
// Element Touch/Hit Testing Functions (for interactive SVGs)
// ============================================================================

/// Subscribe to touch events for an SVG element by its ID
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
SVG_PLAYER_API void SVGPlayer_SubscribeToElement(SVGPlayerHandle player, const char* objectID);

/// Unsubscribe from touch events for an element
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
SVG_PLAYER_API void SVGPlayer_UnsubscribeFromElement(SVGPlayerHandle player, const char* objectID);

/// Unsubscribe from all element events
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_UnsubscribeFromAllElements(SVGPlayerHandle player);

/// Hit test to find which subscribed element is at a point
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @return The objectID of the hit element, or NULL if none
/// @note The returned string is valid until the next call to this function
SVG_PLAYER_API const char* SVGPlayer_HitTest(SVGPlayerHandle player,
                                              float viewX, float viewY,
                                              int viewWidth, int viewHeight);

/// Get the bounding rect of an element in SVG coordinates
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
/// @param x Output: X of bounding box
/// @param y Output: Y of bounding box
/// @param width Output: Width of bounding box
/// @param height Output: Height of bounding box
/// @return true if element found, false otherwise
SVG_PLAYER_API bool SVGPlayer_GetElementBounds(SVGPlayerHandle player,
                                                const char* objectID,
                                                float* x, float* y,
                                                float* width, float* height);

#ifdef __cplusplus
}
#endif
