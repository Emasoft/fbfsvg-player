// fbfsvg_player_api.h - Unified Cross-Platform FBF.SVG Player C API
//
// This is the single source of truth for the FBF.SVG Player API.
// All platform SDKs (iOS, macOS, Linux, Windows) use this header.
//
// Design principles:
// - Pure C API for maximum ABI compatibility
// - Opaque handle pattern (FBFSVGPlayerRef) for type safety
// - No exceptions - use return codes and error strings
// - Thread-safe for single-writer access (caller manages synchronization)
//
// Usage:
//   1. Create player: FBFSVGPlayer_Create()
//   2. Load SVG: FBFSVGPlayer_LoadSVG() or FBFSVGPlayer_LoadSVGData()
//   3. In render loop: FBFSVGPlayer_Update() + FBFSVGPlayer_Render()
//   4. Cleanup: FBFSVGPlayer_Destroy()
//
// Copyright (c) 2024. MIT License.

#ifndef FBFSVG_PLAYER_API_H
#define FBFSVG_PLAYER_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Include shared type definitions
#include "SVGTypes.h"

// Include centralized version management
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// API Version and Export Macros
// =============================================================================

// Version comes from shared/version.h (single source of truth)
#define FBFSVG_PLAYER_API_VERSION_MAJOR FBFSVG_PLAYER_VERSION_MAJOR
#define FBFSVG_PLAYER_API_VERSION_MINOR FBFSVG_PLAYER_VERSION_MINOR
#define FBFSVG_PLAYER_API_VERSION_PATCH FBFSVG_PLAYER_VERSION_PATCH

// Export/import macros for shared library builds
#ifdef _WIN32
#ifdef FBFSVG_PLAYER_BUILDING_DLL
#define FBFSVG_PLAYER_API __declspec(dllexport)
#else
#define FBFSVG_PLAYER_API __declspec(dllimport)
#endif
#else
#ifdef FBFSVG_PLAYER_BUILDING_DLL
#define FBFSVG_PLAYER_API __attribute__((visibility("default")))
#else
#define FBFSVG_PLAYER_API
#endif
#endif

// =============================================================================
// Opaque Handle Type
// =============================================================================

/// Opaque handle to FBF.SVG player instance
/// All API functions operate on this handle
typedef struct FBFSVGPlayer* FBFSVGPlayerRef;

// =============================================================================
// Extended Type Definitions
// =============================================================================

// NOTE: SVGSizeInfo, SVGDualPoint, SVGRect, and SVGDebugFlags are defined in SVGTypes.h
// They are included via #include "SVGTypes.h" above

// =============================================================================
// Callback Type Definitions
// =============================================================================

/// Callback when playback state changes
/// @param userData User-provided context pointer
/// @param newState The new playback state
typedef void (*SVGStateChangeCallback)(void* userData, SVGPlaybackState newState);

/// Callback when animation loops (returns to start or reverses)
/// @param userData User-provided context pointer
/// @param loopCount Number of completed loops
typedef void (*SVGLoopCallback)(void* userData, int loopCount);

/// Callback when animation reaches end (non-looping mode)
/// @param userData User-provided context pointer
typedef void (*SVGEndCallback)(void* userData);

/// Callback when an error occurs
/// @param userData User-provided context pointer
/// @param errorCode Error code
/// @param errorMessage Error description (valid only during callback)
typedef void (*SVGErrorCallback)(void* userData, int errorCode, const char* errorMessage);

/// Callback when a subscribed element is touched/clicked
/// @param userData User-provided context pointer
/// @param elementID The ID of the touched element
/// @param point Coordinates in both view and SVG space
typedef void (*SVGElementTouchCallback)(void* userData, const char* elementID, SVGDualPoint point);

// =============================================================================
// Section 1: Lifecycle Functions
// =============================================================================

/// Create a new FBF.SVG player instance
/// @return Handle to the player, or NULL on failure
FBFSVG_PLAYER_API FBFSVGPlayerRef FBFSVGPlayer_Create(void);

/// Destroy an FBF.SVG player instance and free all resources
/// @param player Handle to destroy (safe to pass NULL)
FBFSVG_PLAYER_API void FBFSVGPlayer_Destroy(FBFSVGPlayerRef player);

/// Get the library version as a string
/// @return Version string (e.g., "1.0.0") - static, do not free
FBFSVG_PLAYER_API const char* FBFSVGPlayer_GetVersion(void);

/// Get detailed version numbers
/// @param major Output: major version (can be NULL)
/// @param minor Output: minor version (can be NULL)
/// @param patch Output: patch version (can be NULL)
FBFSVG_PLAYER_API void FBFSVGPlayer_GetVersionNumbers(int* major, int* minor, int* patch);

// =============================================================================
// Section 2: Loading Functions
// =============================================================================

/// Load an SVG file from disk
/// @param player Handle to the player
/// @param filepath Path to the SVG file
/// @return true on success, false on failure (check FBFSVGPlayer_GetLastError)
FBFSVG_PLAYER_API bool FBFSVGPlayer_LoadSVG(FBFSVGPlayerRef player, const char* filepath);

/// Load SVG from memory buffer
/// @param player Handle to the player
/// @param data Pointer to SVG data (UTF-8 encoded XML)
/// @param length Length of data in bytes
/// @return true on success, false on failure
FBFSVG_PLAYER_API bool FBFSVGPlayer_LoadSVGData(FBFSVGPlayerRef player, const void* data, size_t length);

/// Unload the current SVG and free associated resources
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_Unload(FBFSVGPlayerRef player);

/// Check if an SVG is currently loaded
/// @param player Handle to the player
/// @return true if an SVG is loaded and ready for playback
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsLoaded(FBFSVGPlayerRef player);

/// Check if the loaded SVG has animations
/// @param player Handle to the player
/// @return true if SVG contains SMIL animations
FBFSVG_PLAYER_API bool FBFSVGPlayer_HasAnimations(FBFSVGPlayerRef player);

// =============================================================================
// Section 3: Size and Dimension Functions
// =============================================================================

/// Get the intrinsic size of the loaded SVG
/// @param player Handle to the player
/// @param width Output: width in SVG units (can be NULL)
/// @param height Output: height in SVG units (can be NULL)
/// @return true if SVG is loaded, false otherwise
FBFSVG_PLAYER_API bool FBFSVGPlayer_GetSize(FBFSVGPlayerRef player, int* width, int* height);

/// Get detailed size information including viewBox
/// @param player Handle to the player
/// @param info Output: size information structure
/// @return true if SVG is loaded, false otherwise
FBFSVG_PLAYER_API bool FBFSVGPlayer_GetSizeInfo(FBFSVGPlayerRef player, SVGSizeInfo* info);

/// Set the viewport size for rendering
/// @param player Handle to the player
/// @param width Viewport width in pixels
/// @param height Viewport height in pixels
FBFSVG_PLAYER_API void FBFSVGPlayer_SetViewport(FBFSVGPlayerRef player, int width, int height);

// =============================================================================
// Section 4: Playback Control Functions
// =============================================================================

/// Start or resume playback
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_Play(FBFSVGPlayerRef player);

/// Pause playback at current position
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_Pause(FBFSVGPlayerRef player);

/// Stop playback and reset to beginning
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_Stop(FBFSVGPlayerRef player);

/// Toggle between play and pause
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_TogglePlayback(FBFSVGPlayerRef player);

/// Set playback state directly
/// @param player Handle to the player
/// @param state New playback state
FBFSVG_PLAYER_API void FBFSVGPlayer_SetPlaybackState(FBFSVGPlayerRef player, SVGPlaybackState state);

/// Get current playback state
/// @param player Handle to the player
/// @return Current playback state
FBFSVG_PLAYER_API SVGPlaybackState FBFSVGPlayer_GetPlaybackState(FBFSVGPlayerRef player);

/// Check if currently playing
/// @param player Handle to the player
/// @return true if state is Playing
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsPlaying(FBFSVGPlayerRef player);

/// Check if currently paused
/// @param player Handle to the player
/// @return true if state is Paused
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsPaused(FBFSVGPlayerRef player);

/// Check if currently stopped
/// @param player Handle to the player
/// @return true if state is Stopped
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsStopped(FBFSVGPlayerRef player);

// =============================================================================
// Section 5: Repeat Mode Functions
// =============================================================================

/// Set repeat mode
/// @param player Handle to the player
/// @param mode Repeat mode (None, Loop, Reverse, Count)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetRepeatMode(FBFSVGPlayerRef player, SVGRepeatMode mode);

/// Get current repeat mode
/// @param player Handle to the player
/// @return Current repeat mode
FBFSVG_PLAYER_API SVGRepeatMode FBFSVGPlayer_GetRepeatMode(FBFSVGPlayerRef player);

/// Set repeat count (used with SVGRepeatMode_Count)
/// @param player Handle to the player
/// @param count Number of times to repeat (minimum 1)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetRepeatCount(FBFSVGPlayerRef player, int count);

/// Get current repeat count setting
/// @param player Handle to the player
/// @return Current repeat count
FBFSVG_PLAYER_API int FBFSVGPlayer_GetRepeatCount(FBFSVGPlayerRef player);

/// Get number of completed loop iterations
/// @param player Handle to the player
/// @return Number of completed loops
FBFSVG_PLAYER_API int FBFSVGPlayer_GetCompletedLoops(FBFSVGPlayerRef player);

/// Check if currently playing forward (false during reverse phase of ping-pong)
/// @param player Handle to the player
/// @return true if playing forward
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsPlayingForward(FBFSVGPlayerRef player);

// Legacy looping API (for backward compatibility)
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsLooping(FBFSVGPlayerRef player);
FBFSVG_PLAYER_API void FBFSVGPlayer_SetLooping(FBFSVGPlayerRef player, bool looping);

// =============================================================================
// Section 6: Playback Rate Functions
// =============================================================================

/// Set playback rate (speed multiplier)
/// @param player Handle to the player
/// @param rate Speed multiplier (1.0 = normal, 2.0 = 2x, 0.5 = half)
/// @note Rate is clamped to 0.1 - 10.0 range. Negative values play in reverse.
FBFSVG_PLAYER_API void FBFSVGPlayer_SetPlaybackRate(FBFSVGPlayerRef player, float rate);

/// Get current playback rate
/// @param player Handle to the player
/// @return Current playback rate
FBFSVG_PLAYER_API float FBFSVGPlayer_GetPlaybackRate(FBFSVGPlayerRef player);

// =============================================================================
// Section 7: Timeline Functions
// =============================================================================

/// Update animation time (call from render loop)
/// @param player Handle to the player
/// @param deltaTime Time since last update in seconds
/// @return true if animation state changed (needs re-render)
FBFSVG_PLAYER_API bool FBFSVGPlayer_Update(FBFSVGPlayerRef player, double deltaTime);

/// Get animation duration
/// @param player Handle to the player
/// @return Duration in seconds (0 for static SVG)
FBFSVG_PLAYER_API double FBFSVGPlayer_GetDuration(FBFSVGPlayerRef player);

/// Get current time position
/// @param player Handle to the player
/// @return Current time in seconds
FBFSVG_PLAYER_API double FBFSVGPlayer_GetCurrentTime(FBFSVGPlayerRef player);

/// Get current progress (0.0 to 1.0)
/// @param player Handle to the player
/// @return Progress from 0.0 to 1.0
FBFSVG_PLAYER_API float FBFSVGPlayer_GetProgress(FBFSVGPlayerRef player);

/// Get current frame number (0-indexed)
/// @param player Handle to the player
/// @return Current frame number
FBFSVG_PLAYER_API int FBFSVGPlayer_GetCurrentFrame(FBFSVGPlayerRef player);

/// Get total frame count
/// @param player Handle to the player
/// @return Total number of frames
FBFSVG_PLAYER_API int FBFSVGPlayer_GetTotalFrames(FBFSVGPlayerRef player);

/// Get frame rate
/// @param player Handle to the player
/// @return Frame rate in FPS
FBFSVG_PLAYER_API float FBFSVGPlayer_GetFrameRate(FBFSVGPlayerRef player);

// =============================================================================
// Section 8: Seeking Functions
// =============================================================================

/// Seek to a specific time
/// @param player Handle to the player
/// @param timeSeconds Time in seconds (clamped to valid range)
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekTo(FBFSVGPlayerRef player, double timeSeconds);

/// Seek to a specific frame (0-indexed)
/// @param player Handle to the player
/// @param frame Frame number (clamped to valid range)
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekToFrame(FBFSVGPlayerRef player, int frame);

/// Seek to a progress position
/// @param player Handle to the player
/// @param progress Value from 0.0 (start) to 1.0 (end)
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekToProgress(FBFSVGPlayerRef player, float progress);

/// Seek to start (time = 0)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekToStart(FBFSVGPlayerRef player);

/// Seek to end (time = duration)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekToEnd(FBFSVGPlayerRef player);

/// Seek forward by time interval
/// @param player Handle to the player
/// @param seconds Time to skip forward
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekForwardByTime(FBFSVGPlayerRef player, double seconds);

/// Seek backward by time interval
/// @param player Handle to the player
/// @param seconds Time to skip backward
FBFSVG_PLAYER_API void FBFSVGPlayer_SeekBackwardByTime(FBFSVGPlayerRef player, double seconds);

// =============================================================================
// Section 9: Frame Stepping Functions
// =============================================================================

/// Step forward by one frame (pauses playback)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_StepForward(FBFSVGPlayerRef player);

/// Step backward by one frame (pauses playback)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_StepBackward(FBFSVGPlayerRef player);

/// Step by specific number of frames
/// @param player Handle to the player
/// @param frames Number of frames (positive = forward, negative = backward)
FBFSVG_PLAYER_API void FBFSVGPlayer_StepByFrames(FBFSVGPlayerRef player, int frames);

// =============================================================================
// Section 10: Scrubbing Functions
// =============================================================================

/// Begin interactive scrubbing session (saves state, pauses playback)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_BeginScrubbing(FBFSVGPlayerRef player);

/// Update position during scrubbing
/// @param player Handle to the player
/// @param progress Progress value (0.0 to 1.0)
FBFSVG_PLAYER_API void FBFSVGPlayer_ScrubToProgress(FBFSVGPlayerRef player, float progress);

/// End scrubbing session
/// @param player Handle to the player
/// @param resume Whether to resume previous playback state
FBFSVG_PLAYER_API void FBFSVGPlayer_EndScrubbing(FBFSVGPlayerRef player, bool resume);

/// Check if currently in scrubbing mode
/// @param player Handle to the player
/// @return true if scrubbing
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsScrubbing(FBFSVGPlayerRef player);

// =============================================================================
// Section 11: Rendering Functions
// =============================================================================

/// Render the current frame to a pixel buffer
/// Buffer must be pre-allocated: width * height * 4 bytes (RGBA, premultiplied)
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor (1.0 = standard, 2.0 = Retina)
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_Render(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale);

/// Render a specific time to a pixel buffer
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor
/// @param timeSeconds Time in seconds for the frame to render
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_RenderAtTime(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale,
                                           double timeSeconds);

/// Render a specific frame to a pixel buffer
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor
/// @param frame Frame number (0-indexed)
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_RenderFrame(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale,
                                          int frame);

// =============================================================================
// Section 12: Coordinate Conversion Functions
// =============================================================================

/// Convert view coordinates to SVG coordinates
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @param svgX Output: X in SVG coordinates
/// @param svgY Output: Y in SVG coordinates
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_ViewToSVG(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight,
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
FBFSVG_PLAYER_API bool FBFSVGPlayer_SVGToView(FBFSVGPlayerRef player, float svgX, float svgY, int viewWidth, int viewHeight,
                                        float* viewX, float* viewY);

// =============================================================================
// Section 13: Zoom and ViewBox Functions
// =============================================================================
// These functions control the visible area of the SVG (zoom/pan).
// The zoom is implemented by modifying the viewBox sent to the Skia renderer.
// This allows both programmatic zoom and gesture-based zoom (iOS pinch) to
// share the same underlying logic.

/// Get the current viewBox (visible area in SVG coordinates)
/// @param player Handle to the player
/// @param x Output: viewBox origin X
/// @param y Output: viewBox origin Y
/// @param width Output: viewBox width
/// @param height Output: viewBox height
/// @return true if SVG is loaded
FBFSVG_PLAYER_API bool FBFSVGPlayer_GetViewBox(FBFSVGPlayerRef player, float* x, float* y, float* width, float* height);

/// Set the viewBox (visible area in SVG coordinates)
/// This is the core zoom mechanism - a smaller viewBox = zoomed in
/// @param player Handle to the player
/// @param x ViewBox origin X
/// @param y ViewBox origin Y
/// @param width ViewBox width
/// @param height ViewBox height
FBFSVG_PLAYER_API void FBFSVGPlayer_SetViewBox(FBFSVGPlayerRef player, float x, float y, float width, float height);

/// Reset viewBox to the SVG's original viewBox
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_ResetViewBox(FBFSVGPlayerRef player);

/// Get the current zoom level (1.0 = no zoom)
/// @param player Handle to the player
/// @return Current zoom level
FBFSVG_PLAYER_API float FBFSVGPlayer_GetZoom(FBFSVGPlayerRef player);

/// Set zoom level centered on a point
/// @param player Handle to the player
/// @param zoom Zoom level (1.0 = original, 2.0 = 2x zoom, 0.5 = zoom out)
/// @param centerX Center point X in view coordinates (use -1 for view center)
/// @param centerY Center point Y in view coordinates (use -1 for view center)
/// @param viewWidth Current view width (for center calculation)
/// @param viewHeight Current view height (for center calculation)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetZoom(FBFSVGPlayerRef player, float zoom, float centerX, float centerY,
                                       int viewWidth, int viewHeight);

/// Zoom in by a factor, centered on view center
/// @param player Handle to the player
/// @param factor Zoom factor (e.g., 1.5 = zoom in 50%)
/// @param viewWidth Current view width
/// @param viewHeight Current view height
FBFSVG_PLAYER_API void FBFSVGPlayer_ZoomIn(FBFSVGPlayerRef player, float factor, int viewWidth, int viewHeight);

/// Zoom out by a factor, centered on view center
/// @param player Handle to the player
/// @param factor Zoom factor (e.g., 0.75 = zoom out 25%)
/// @param viewWidth Current view width
/// @param viewHeight Current view height
FBFSVG_PLAYER_API void FBFSVGPlayer_ZoomOut(FBFSVGPlayerRef player, float factor, int viewWidth, int viewHeight);

/// Zoom to fit a specific rectangle in view
/// @param player Handle to the player
/// @param svgX Rectangle X in SVG coordinates
/// @param svgY Rectangle Y in SVG coordinates
/// @param svgWidth Rectangle width in SVG coordinates
/// @param svgHeight Rectangle height in SVG coordinates
FBFSVG_PLAYER_API void FBFSVGPlayer_ZoomToRect(FBFSVGPlayerRef player, float svgX, float svgY, float svgWidth, float svgHeight);

/// Zoom to fit an element by its ID
/// @param player Handle to the player
/// @param elementID The id attribute of the SVG element
/// @param padding Padding around element (in SVG units, 0 for tight fit)
/// @return true if element found and zoom applied
FBFSVG_PLAYER_API bool FBFSVGPlayer_ZoomToElement(FBFSVGPlayerRef player, const char* elementID, float padding);

/// Pan the viewBox by a delta (for drag gestures)
/// @param player Handle to the player
/// @param deltaX Pan delta X in view coordinates
/// @param deltaY Pan delta Y in view coordinates
/// @param viewWidth Current view width (for coordinate conversion)
/// @param viewHeight Current view height (for coordinate conversion)
FBFSVG_PLAYER_API void FBFSVGPlayer_Pan(FBFSVGPlayerRef player, float deltaX, float deltaY, int viewWidth, int viewHeight);

/// Get minimum allowed zoom level
/// @param player Handle to the player
/// @return Minimum zoom (default 0.1)
FBFSVG_PLAYER_API float FBFSVGPlayer_GetMinZoom(FBFSVGPlayerRef player);

/// Set minimum allowed zoom level
/// @param player Handle to the player
/// @param minZoom Minimum zoom level (default 0.1)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetMinZoom(FBFSVGPlayerRef player, float minZoom);

/// Get maximum allowed zoom level
/// @param player Handle to the player
/// @return Maximum zoom (default 10.0)
FBFSVG_PLAYER_API float FBFSVGPlayer_GetMaxZoom(FBFSVGPlayerRef player);

/// Set maximum allowed zoom level
/// @param player Handle to the player
/// @param maxZoom Maximum zoom level (default 10.0)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetMaxZoom(FBFSVGPlayerRef player, float maxZoom);

// =============================================================================
// Section 14: Hit Testing Functions
// =============================================================================

/// Subscribe to touch events for an SVG element by its ID
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
FBFSVG_PLAYER_API void FBFSVGPlayer_SubscribeToElement(FBFSVGPlayerRef player, const char* objectID);

/// Unsubscribe from touch events for an element
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
FBFSVG_PLAYER_API void FBFSVGPlayer_UnsubscribeFromElement(FBFSVGPlayerRef player, const char* objectID);

/// Unsubscribe from all element events
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_UnsubscribeFromAllElements(FBFSVGPlayerRef player);

/// Hit test to find which subscribed element is at a point
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @return The objectID of the hit element, or NULL if none
/// @note The returned string is valid until the next call to this function
FBFSVG_PLAYER_API const char* FBFSVGPlayer_HitTest(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth,
                                             int viewHeight);

/// Get the bounding rect of an element in SVG coordinates
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
/// @param bounds Output: bounding rectangle
/// @return true if element found, false otherwise
FBFSVG_PLAYER_API bool FBFSVGPlayer_GetElementBounds(FBFSVGPlayerRef player, const char* objectID, SVGRect* bounds);

/// Get all elements at a point (for layered SVGs)
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @param outElements Output: array of element IDs (caller allocates)
/// @param maxElements Maximum number of elements to return
/// @return Number of elements found
FBFSVG_PLAYER_API int FBFSVGPlayer_GetElementsAtPoint(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth,
                                                int viewHeight, const char** outElements, int maxElements);

// =============================================================================
// Section 14: Element Information Functions
// =============================================================================

/// Get an element by its ID
/// @param player Handle to the player
/// @param elementID The id attribute of the element
/// @return true if element exists
FBFSVG_PLAYER_API bool FBFSVGPlayer_ElementExists(FBFSVGPlayerRef player, const char* elementID);

/// Get a property value from an element
/// @param player Handle to the player
/// @param elementID The id attribute of the element
/// @param propertyName The property name (e.g., "fill", "stroke")
/// @param outValue Output: buffer for property value
/// @param maxLength Maximum length of outValue buffer
/// @return true if property was found
FBFSVG_PLAYER_API bool FBFSVGPlayer_GetElementProperty(FBFSVGPlayerRef player, const char* elementID, const char* propertyName,
                                                 char* outValue, int maxLength);

// =============================================================================
// Section 15: Callback Functions
// =============================================================================

/// Set callback for playback state changes
/// @param player Handle to the player
/// @param callback Function to call on state change (NULL to remove)
/// @param userData Context pointer passed to callback
FBFSVG_PLAYER_API void FBFSVGPlayer_SetStateChangeCallback(FBFSVGPlayerRef player, SVGStateChangeCallback callback,
                                                     void* userData);

/// Set callback for loop events
/// @param player Handle to the player
/// @param callback Function to call when animation loops (NULL to remove)
/// @param userData Context pointer passed to callback
FBFSVG_PLAYER_API void FBFSVGPlayer_SetLoopCallback(FBFSVGPlayerRef player, SVGLoopCallback callback, void* userData);

/// Set callback for end events (non-looping mode)
/// @param player Handle to the player
/// @param callback Function to call when animation ends (NULL to remove)
/// @param userData Context pointer passed to callback
FBFSVG_PLAYER_API void FBFSVGPlayer_SetEndCallback(FBFSVGPlayerRef player, SVGEndCallback callback, void* userData);

/// Set callback for error events
/// @param player Handle to the player
/// @param callback Function to call on errors (NULL to remove)
/// @param userData Context pointer passed to callback
FBFSVG_PLAYER_API void FBFSVGPlayer_SetErrorCallback(FBFSVGPlayerRef player, SVGErrorCallback callback, void* userData);

/// Set callback for element touch events
/// @param player Handle to the player
/// @param callback Function to call when subscribed element is touched (NULL to remove)
/// @param userData Context pointer passed to callback
FBFSVG_PLAYER_API void FBFSVGPlayer_SetElementTouchCallback(FBFSVGPlayerRef player, SVGElementTouchCallback callback,
                                                      void* userData);

// =============================================================================
// Section 16: Statistics and Diagnostics
// =============================================================================

/// Get rendering statistics
/// @param player Handle to the player
/// @return Rendering statistics structure
FBFSVG_PLAYER_API SVGRenderStats FBFSVGPlayer_GetStats(FBFSVGPlayerRef player);

/// Reset statistics counters
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_ResetStats(FBFSVGPlayerRef player);

/// Get the last error message
/// @param player Handle to the player
/// @return Error message string (empty if no error)
FBFSVG_PLAYER_API const char* FBFSVGPlayer_GetLastError(FBFSVGPlayerRef player);

/// Clear the last error
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_ClearError(FBFSVGPlayerRef player);

// =============================================================================
// Section 17: Pre-buffering Functions (for performance)
// =============================================================================

/// Enable or disable frame pre-buffering
/// @param player Handle to the player
/// @param enable true to enable pre-buffering
FBFSVG_PLAYER_API void FBFSVGPlayer_EnablePreBuffer(FBFSVGPlayerRef player, bool enable);

/// Check if pre-buffering is enabled
/// @param player Handle to the player
/// @return true if pre-buffering is enabled
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsPreBufferEnabled(FBFSVGPlayerRef player);

/// Set number of frames to pre-buffer ahead
/// @param player Handle to the player
/// @param frameCount Number of frames to buffer (default: 3)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetPreBufferFrames(FBFSVGPlayerRef player, int frameCount);

/// Get number of frames currently buffered
/// @param player Handle to the player
/// @return Number of buffered frames ready for display
FBFSVG_PLAYER_API int FBFSVGPlayer_GetBufferedFrames(FBFSVGPlayerRef player);

/// Clear the pre-buffer (e.g., after seeking)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_ClearPreBuffer(FBFSVGPlayerRef player);

// =============================================================================
// Section 18: Debug Overlay Functions
// =============================================================================

/// Enable or disable debug overlay
/// @param player Handle to the player
/// @param enable true to show debug overlay
FBFSVG_PLAYER_API void FBFSVGPlayer_EnableDebugOverlay(FBFSVGPlayerRef player, bool enable);

/// Check if debug overlay is enabled
/// @param player Handle to the player
/// @return true if debug overlay is enabled
FBFSVG_PLAYER_API bool FBFSVGPlayer_IsDebugOverlayEnabled(FBFSVGPlayerRef player);

/// Set debug overlay flags (what to display)
/// @param player Handle to the player
/// @param flags Bitwise OR of SVGDebugFlags
FBFSVG_PLAYER_API void FBFSVGPlayer_SetDebugFlags(FBFSVGPlayerRef player, uint32_t flags);

/// Get current debug flags
/// @param player Handle to the player
/// @return Current debug flags
FBFSVG_PLAYER_API uint32_t FBFSVGPlayer_GetDebugFlags(FBFSVGPlayerRef player);

// =============================================================================
// Section 19: Utility Functions
// =============================================================================

/// Format time as string (MM:SS.mmm)
/// @param timeSeconds Time in seconds
/// @param outBuffer Output buffer for formatted string
/// @param bufferSize Size of output buffer
/// @return Pointer to outBuffer
FBFSVG_PLAYER_API const char* FBFSVGPlayer_FormatTime(double timeSeconds, char* outBuffer, int bufferSize);

/// Convert time to frame number
/// @param player Handle to the player
/// @param timeSeconds Time in seconds
/// @return Frame number (0-indexed)
FBFSVG_PLAYER_API int FBFSVGPlayer_TimeToFrame(FBFSVGPlayerRef player, double timeSeconds);

/// Convert frame number to time
/// @param player Handle to the player
/// @param frame Frame number (0-indexed)
/// @return Time in seconds
FBFSVG_PLAYER_API double FBFSVGPlayer_FrameToTime(FBFSVGPlayerRef player, int frame);

// =============================================================================
// Section 20: Multi-SVG Compositing Functions
// =============================================================================
// These functions allow compositing multiple SVGs into a single scene.
// Each SVG becomes a "layer" with its own position, opacity, z-order, and transform.
// Layers are rendered in z-order (lowest first) when using RenderComposite.
// The "primary" SVG (loaded via LoadSVG) is always layer 0 with z-order 0.

/// Opaque handle to an SVG layer within a scene
typedef struct FBFSVGLayer* FBFSVGLayerRef;

/// Layer blend mode for compositing
typedef enum {
    /// Normal alpha blending (default)
    FBFSVGLayerBlend_Normal = 0,
    /// Multiply blend mode
    FBFSVGLayerBlend_Multiply,
    /// Screen blend mode
    FBFSVGLayerBlend_Screen,
    /// Overlay blend mode
    FBFSVGLayerBlend_Overlay,
    /// Darken blend mode
    FBFSVGLayerBlend_Darken,
    /// Lighten blend mode
    FBFSVGLayerBlend_Lighten
} FBFSVGLayerBlendMode;

/// Create a new layer by loading an SVG file
/// @param player Handle to the player
/// @param filepath Path to the SVG file
/// @return Handle to the new layer, or NULL on failure
FBFSVG_PLAYER_API FBFSVGLayerRef FBFSVGPlayer_CreateLayer(FBFSVGPlayerRef player, const char* filepath);

/// Create a new layer from SVG data in memory
/// @param player Handle to the player
/// @param data Pointer to SVG data (UTF-8 encoded XML)
/// @param length Length of data in bytes
/// @return Handle to the new layer, or NULL on failure
FBFSVG_PLAYER_API FBFSVGLayerRef FBFSVGPlayer_CreateLayerFromData(FBFSVGPlayerRef player, const void* data, size_t length);

/// Destroy a layer and free its resources
/// @param player Handle to the player
/// @param layer Handle to the layer to destroy
FBFSVG_PLAYER_API void FBFSVGPlayer_DestroyLayer(FBFSVGPlayerRef player, FBFSVGLayerRef layer);

/// Get the number of layers (including primary SVG as layer 0)
/// @param player Handle to the player
/// @return Number of layers
FBFSVG_PLAYER_API int FBFSVGPlayer_GetLayerCount(FBFSVGPlayerRef player);

/// Get a layer by index (0 = primary SVG)
/// @param player Handle to the player
/// @param index Layer index (0-based)
/// @return Layer handle, or NULL if index out of range
FBFSVG_PLAYER_API FBFSVGLayerRef FBFSVGPlayer_GetLayerAtIndex(FBFSVGPlayerRef player, int index);

/// Set layer position (offset from origin)
/// @param layer Handle to the layer
/// @param x X offset in pixels
/// @param y Y offset in pixels
FBFSVG_PLAYER_API void FBFSVGLayer_SetPosition(FBFSVGLayerRef layer, float x, float y);

/// Get layer position
/// @param layer Handle to the layer
/// @param x Output: X offset (can be NULL)
/// @param y Output: Y offset (can be NULL)
FBFSVG_PLAYER_API void FBFSVGLayer_GetPosition(FBFSVGLayerRef layer, float* x, float* y);

/// Set layer opacity
/// @param layer Handle to the layer
/// @param opacity Opacity value (0.0 = transparent, 1.0 = opaque)
FBFSVG_PLAYER_API void FBFSVGLayer_SetOpacity(FBFSVGLayerRef layer, float opacity);

/// Get layer opacity
/// @param layer Handle to the layer
/// @return Current opacity (0.0 to 1.0)
FBFSVG_PLAYER_API float FBFSVGLayer_GetOpacity(FBFSVGLayerRef layer);

/// Set layer z-order (render order)
/// @param layer Handle to the layer
/// @param zOrder Z-order value (higher = rendered on top)
FBFSVG_PLAYER_API void FBFSVGLayer_SetZOrder(FBFSVGLayerRef layer, int zOrder);

/// Get layer z-order
/// @param layer Handle to the layer
/// @return Current z-order value
FBFSVG_PLAYER_API int FBFSVGLayer_GetZOrder(FBFSVGLayerRef layer);

/// Set layer visibility
/// @param layer Handle to the layer
/// @param visible true to show, false to hide
FBFSVG_PLAYER_API void FBFSVGLayer_SetVisible(FBFSVGLayerRef layer, bool visible);

/// Check if layer is visible
/// @param layer Handle to the layer
/// @return true if visible
FBFSVG_PLAYER_API bool FBFSVGLayer_IsVisible(FBFSVGLayerRef layer);

/// Set layer scale
/// @param layer Handle to the layer
/// @param scaleX Horizontal scale (1.0 = original size)
/// @param scaleY Vertical scale (1.0 = original size)
FBFSVG_PLAYER_API void FBFSVGLayer_SetScale(FBFSVGLayerRef layer, float scaleX, float scaleY);

/// Get layer scale
/// @param layer Handle to the layer
/// @param scaleX Output: horizontal scale (can be NULL)
/// @param scaleY Output: vertical scale (can be NULL)
FBFSVG_PLAYER_API void FBFSVGLayer_GetScale(FBFSVGLayerRef layer, float* scaleX, float* scaleY);

/// Set layer rotation around its center
/// @param layer Handle to the layer
/// @param angleDegrees Rotation angle in degrees (clockwise)
FBFSVG_PLAYER_API void FBFSVGLayer_SetRotation(FBFSVGLayerRef layer, float angleDegrees);

/// Get layer rotation
/// @param layer Handle to the layer
/// @return Current rotation in degrees
FBFSVG_PLAYER_API float FBFSVGLayer_GetRotation(FBFSVGLayerRef layer);

/// Set layer blend mode
/// @param layer Handle to the layer
/// @param blendMode Blend mode for compositing
FBFSVG_PLAYER_API void FBFSVGLayer_SetBlendMode(FBFSVGLayerRef layer, FBFSVGLayerBlendMode blendMode);

/// Get layer blend mode
/// @param layer Handle to the layer
/// @return Current blend mode
FBFSVG_PLAYER_API FBFSVGLayerBlendMode FBFSVGLayer_GetBlendMode(FBFSVGLayerRef layer);

/// Get the intrinsic size of a layer's SVG
/// @param layer Handle to the layer
/// @param width Output: width in SVG units (can be NULL)
/// @param height Output: height in SVG units (can be NULL)
/// @return true if layer is valid
FBFSVG_PLAYER_API bool FBFSVGLayer_GetSize(FBFSVGLayerRef layer, int* width, int* height);

/// Get layer animation duration
/// @param layer Handle to the layer
/// @return Duration in seconds (0 for static SVG)
FBFSVG_PLAYER_API double FBFSVGLayer_GetDuration(FBFSVGLayerRef layer);

/// Check if layer has animations
/// @param layer Handle to the layer
/// @return true if layer contains SMIL animations
FBFSVG_PLAYER_API bool FBFSVGLayer_HasAnimations(FBFSVGLayerRef layer);

/// Start or resume layer animation
/// @param layer Handle to the layer
FBFSVG_PLAYER_API void FBFSVGLayer_Play(FBFSVGLayerRef layer);

/// Pause layer animation
/// @param layer Handle to the layer
FBFSVG_PLAYER_API void FBFSVGLayer_Pause(FBFSVGLayerRef layer);

/// Stop layer animation and reset to beginning
/// @param layer Handle to the layer
FBFSVG_PLAYER_API void FBFSVGLayer_Stop(FBFSVGLayerRef layer);

/// Seek layer to specific time
/// @param layer Handle to the layer
/// @param timeSeconds Time in seconds
FBFSVG_PLAYER_API void FBFSVGLayer_SeekTo(FBFSVGLayerRef layer, double timeSeconds);

/// Update layer animation (call from render loop)
/// @param layer Handle to the layer
/// @param deltaTime Time since last update in seconds
/// @return true if layer needs re-render
FBFSVG_PLAYER_API bool FBFSVGLayer_Update(FBFSVGLayerRef layer, double deltaTime);

/// Update all layers at once
/// @param player Handle to the player
/// @param deltaTime Time since last update in seconds
/// @return true if any layer needs re-render
FBFSVG_PLAYER_API bool FBFSVGPlayer_UpdateAllLayers(FBFSVGPlayerRef player, double deltaTime);

/// Play all layers simultaneously
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_PlayAllLayers(FBFSVGPlayerRef player);

/// Pause all layers
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_PauseAllLayers(FBFSVGPlayerRef player);

/// Stop all layers and reset to beginning
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_StopAllLayers(FBFSVGPlayerRef player);

/// Render all visible layers composited together
/// Layers are rendered in z-order (lowest first)
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_RenderComposite(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale);

/// Render composite at a specific time
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor
/// @param timeSeconds Time in seconds (applied to all layers)
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_RenderCompositeAtTime(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height,
                                                     float scale, double timeSeconds);

// =============================================================================
// Section 21: Frame Rate and Timing Control
// =============================================================================
//
// These functions provide frame rate control and timing utilities for
// smooth animation playback. The actual VSync synchronization is handled
// by platform-specific display systems (CADisplayLink on iOS/macOS,
// EGL/vsync on Linux). This API provides the timing infrastructure.

/// Set target frame rate for animation playback
/// Affects frame pacing calculations
/// @param player Handle to the player
/// @param fps Target frames per second (e.g., 30, 60, 120)
FBFSVG_PLAYER_API void FBFSVGPlayer_SetTargetFrameRate(FBFSVGPlayerRef player, float fps);

/// Get target frame rate
/// @param player Handle to the player
/// @return Current target frame rate in FPS
FBFSVG_PLAYER_API float FBFSVGPlayer_GetTargetFrameRate(FBFSVGPlayerRef player);

/// Get ideal frame interval based on target frame rate
/// @param player Handle to the player
/// @return Frame interval in seconds (1.0 / targetFPS)
FBFSVG_PLAYER_API double FBFSVGPlayer_GetIdealFrameInterval(FBFSVGPlayerRef player);

/// Begin a new frame timing measurement
/// Call at the start of each render frame
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_BeginFrame(FBFSVGPlayerRef player);

/// End frame timing measurement
/// Call at the end of each render frame
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_EndFrame(FBFSVGPlayerRef player);

/// Get duration of the last completed frame
/// @param player Handle to the player
/// @return Frame duration in seconds
FBFSVG_PLAYER_API double FBFSVGPlayer_GetLastFrameDuration(FBFSVGPlayerRef player);

/// Get average frame duration (rolling average of last N frames)
/// @param player Handle to the player
/// @return Average frame duration in seconds
FBFSVG_PLAYER_API double FBFSVGPlayer_GetAverageFrameDuration(FBFSVGPlayerRef player);

/// Get measured frames per second (based on actual render times)
/// @param player Handle to the player
/// @return Measured FPS (1.0 / averageFrameDuration)
FBFSVG_PLAYER_API float FBFSVGPlayer_GetMeasuredFPS(FBFSVGPlayerRef player);

/// Check if enough time has passed to render the next frame
/// Useful for frame limiting/pacing without VSync
/// @param player Handle to the player
/// @param currentTimeSeconds Current time (e.g., from clock)
/// @return true if a new frame should be rendered
FBFSVG_PLAYER_API bool FBFSVGPlayer_ShouldRenderFrame(FBFSVGPlayerRef player, double currentTimeSeconds);

/// Mark that a frame was rendered at the specified time
/// Updates internal timing for frame pacing
/// @param player Handle to the player
/// @param renderTimeSeconds Time when frame was rendered
FBFSVG_PLAYER_API void FBFSVGPlayer_MarkFrameRendered(FBFSVGPlayerRef player, double renderTimeSeconds);

/// Get number of dropped/skipped frames
/// A frame is considered dropped if more than 1.5x the ideal interval passes
/// @param player Handle to the player
/// @return Number of dropped frames since last reset
FBFSVG_PLAYER_API int FBFSVGPlayer_GetDroppedFrameCount(FBFSVGPlayerRef player);

/// Reset frame statistics (dropped count, timing averages)
/// @param player Handle to the player
FBFSVG_PLAYER_API void FBFSVGPlayer_ResetFrameStats(FBFSVGPlayerRef player);

/// Get timestamp of last rendered frame
/// @param player Handle to the player
/// @return Time of last render in seconds (as passed to MarkFrameRendered)
FBFSVG_PLAYER_API double FBFSVGPlayer_GetLastRenderTime(FBFSVGPlayerRef player);

/// Get time since last frame was rendered
/// @param player Handle to the player
/// @param currentTimeSeconds Current time
/// @return Elapsed time since last render in seconds
FBFSVG_PLAYER_API double FBFSVGPlayer_GetTimeSinceLastRender(FBFSVGPlayerRef player, double currentTimeSeconds);

#ifdef __cplusplus
}
#endif

// =============================================================================
// C++ Convenience Wrapper (optional, header-only)
// =============================================================================

#ifdef __cplusplus
#ifdef FBFSVG_PLAYER_USE_CXX_WRAPPER

#include <functional>
#include <memory>
#include <string>

namespace fbfsvgplayer {

/// RAII wrapper for FBFSVGPlayerRef
class Player {
   public:
    Player() : handle_(FBFSVGPlayer_Create()) {}
    ~Player() {
        if (handle_) FBFSVGPlayer_Destroy(handle_);
    }

    // Non-copyable
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Movable
    Player(Player&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Player& operator=(Player&& other) noexcept {
        if (this != &other) {
            if (handle_) FBFSVGPlayer_Destroy(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Access underlying handle
    FBFSVGPlayerRef get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    // Convenience methods
    bool load(const std::string& path) { return FBFSVGPlayer_LoadSVG(handle_, path.c_str()); }
    bool loadData(const void* data, size_t len) { return FBFSVGPlayer_LoadSVGData(handle_, data, len); }
    void play() { FBFSVGPlayer_Play(handle_); }
    void pause() { FBFSVGPlayer_Pause(handle_); }
    void stop() { FBFSVGPlayer_Stop(handle_); }
    bool update(double dt) { return FBFSVGPlayer_Update(handle_, dt); }
    bool render(void* buf, int w, int h, float s = 1.0f) { return FBFSVGPlayer_Render(handle_, buf, w, h, s); }

   private:
    FBFSVGPlayerRef handle_;
};

}  // namespace fbfsvgplayer

#endif  // FBFSVG_PLAYER_USE_CXX_WRAPPER
#endif  // __cplusplus

#endif  // FBFSVG_PLAYER_API_H
