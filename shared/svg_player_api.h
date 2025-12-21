// svg_player_api.h - Unified Cross-Platform SVG Player C API
//
// This is the single source of truth for the SVG Player API.
// All platform SDKs (iOS, macOS, Linux, Windows) use this header.
//
// Design principles:
// - Pure C API for maximum ABI compatibility
// - Opaque handle pattern (SVGPlayerRef) for type safety
// - No exceptions - use return codes and error strings
// - Thread-safe for single-writer access (caller manages synchronization)
//
// Usage:
//   1. Create player: SVGPlayer_Create()
//   2. Load SVG: SVGPlayer_LoadSVG() or SVGPlayer_LoadSVGData()
//   3. In render loop: SVGPlayer_Update() + SVGPlayer_Render()
//   4. Cleanup: SVGPlayer_Destroy()
//
// Copyright (c) 2024. MIT License.

#ifndef SVG_PLAYER_API_H
#define SVG_PLAYER_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Include shared type definitions
#include "SVGTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// API Version and Export Macros
// =============================================================================

#define SVG_PLAYER_API_VERSION_MAJOR 1
#define SVG_PLAYER_API_VERSION_MINOR 0
#define SVG_PLAYER_API_VERSION_PATCH 0

// Export/import macros for shared library builds
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

// =============================================================================
// Opaque Handle Type
// =============================================================================

/// Opaque handle to SVG player instance
/// All API functions operate on this handle
typedef struct SVGPlayer* SVGPlayerRef;

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

/// Create a new SVG player instance
/// @return Handle to the player, or NULL on failure
SVG_PLAYER_API SVGPlayerRef SVGPlayer_Create(void);

/// Destroy an SVG player instance and free all resources
/// @param player Handle to destroy (safe to pass NULL)
SVG_PLAYER_API void SVGPlayer_Destroy(SVGPlayerRef player);

/// Get the library version as a string
/// @return Version string (e.g., "1.0.0") - static, do not free
SVG_PLAYER_API const char* SVGPlayer_GetVersion(void);

/// Get detailed version numbers
/// @param major Output: major version (can be NULL)
/// @param minor Output: minor version (can be NULL)
/// @param patch Output: patch version (can be NULL)
SVG_PLAYER_API void SVGPlayer_GetVersionNumbers(int* major, int* minor, int* patch);

// =============================================================================
// Section 2: Loading Functions
// =============================================================================

/// Load an SVG file from disk
/// @param player Handle to the player
/// @param filepath Path to the SVG file
/// @return true on success, false on failure (check SVGPlayer_GetLastError)
SVG_PLAYER_API bool SVGPlayer_LoadSVG(SVGPlayerRef player, const char* filepath);

/// Load SVG from memory buffer
/// @param player Handle to the player
/// @param data Pointer to SVG data (UTF-8 encoded XML)
/// @param length Length of data in bytes
/// @return true on success, false on failure
SVG_PLAYER_API bool SVGPlayer_LoadSVGData(SVGPlayerRef player, const void* data, size_t length);

/// Unload the current SVG and free associated resources
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Unload(SVGPlayerRef player);

/// Check if an SVG is currently loaded
/// @param player Handle to the player
/// @return true if an SVG is loaded and ready for playback
SVG_PLAYER_API bool SVGPlayer_IsLoaded(SVGPlayerRef player);

/// Check if the loaded SVG has animations
/// @param player Handle to the player
/// @return true if SVG contains SMIL animations
SVG_PLAYER_API bool SVGPlayer_HasAnimations(SVGPlayerRef player);

// =============================================================================
// Section 3: Size and Dimension Functions
// =============================================================================

/// Get the intrinsic size of the loaded SVG
/// @param player Handle to the player
/// @param width Output: width in SVG units (can be NULL)
/// @param height Output: height in SVG units (can be NULL)
/// @return true if SVG is loaded, false otherwise
SVG_PLAYER_API bool SVGPlayer_GetSize(SVGPlayerRef player, int* width, int* height);

/// Get detailed size information including viewBox
/// @param player Handle to the player
/// @param info Output: size information structure
/// @return true if SVG is loaded, false otherwise
SVG_PLAYER_API bool SVGPlayer_GetSizeInfo(SVGPlayerRef player, SVGSizeInfo* info);

/// Set the viewport size for rendering
/// @param player Handle to the player
/// @param width Viewport width in pixels
/// @param height Viewport height in pixels
SVG_PLAYER_API void SVGPlayer_SetViewport(SVGPlayerRef player, int width, int height);

// =============================================================================
// Section 4: Playback Control Functions
// =============================================================================

/// Start or resume playback
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Play(SVGPlayerRef player);

/// Pause playback at current position
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Pause(SVGPlayerRef player);

/// Stop playback and reset to beginning
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_Stop(SVGPlayerRef player);

/// Toggle between play and pause
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_TogglePlayback(SVGPlayerRef player);

/// Set playback state directly
/// @param player Handle to the player
/// @param state New playback state
SVG_PLAYER_API void SVGPlayer_SetPlaybackState(SVGPlayerRef player, SVGPlaybackState state);

/// Get current playback state
/// @param player Handle to the player
/// @return Current playback state
SVG_PLAYER_API SVGPlaybackState SVGPlayer_GetPlaybackState(SVGPlayerRef player);

/// Check if currently playing
/// @param player Handle to the player
/// @return true if state is Playing
SVG_PLAYER_API bool SVGPlayer_IsPlaying(SVGPlayerRef player);

/// Check if currently paused
/// @param player Handle to the player
/// @return true if state is Paused
SVG_PLAYER_API bool SVGPlayer_IsPaused(SVGPlayerRef player);

/// Check if currently stopped
/// @param player Handle to the player
/// @return true if state is Stopped
SVG_PLAYER_API bool SVGPlayer_IsStopped(SVGPlayerRef player);

// =============================================================================
// Section 5: Repeat Mode Functions
// =============================================================================

/// Set repeat mode
/// @param player Handle to the player
/// @param mode Repeat mode (None, Loop, Reverse, Count)
SVG_PLAYER_API void SVGPlayer_SetRepeatMode(SVGPlayerRef player, SVGRepeatMode mode);

/// Get current repeat mode
/// @param player Handle to the player
/// @return Current repeat mode
SVG_PLAYER_API SVGRepeatMode SVGPlayer_GetRepeatMode(SVGPlayerRef player);

/// Set repeat count (used with SVGRepeatMode_Count)
/// @param player Handle to the player
/// @param count Number of times to repeat (minimum 1)
SVG_PLAYER_API void SVGPlayer_SetRepeatCount(SVGPlayerRef player, int count);

/// Get current repeat count setting
/// @param player Handle to the player
/// @return Current repeat count
SVG_PLAYER_API int SVGPlayer_GetRepeatCount(SVGPlayerRef player);

/// Get number of completed loop iterations
/// @param player Handle to the player
/// @return Number of completed loops
SVG_PLAYER_API int SVGPlayer_GetCompletedLoops(SVGPlayerRef player);

/// Check if currently playing forward (false during reverse phase of ping-pong)
/// @param player Handle to the player
/// @return true if playing forward
SVG_PLAYER_API bool SVGPlayer_IsPlayingForward(SVGPlayerRef player);

// Legacy looping API (for backward compatibility)
SVG_PLAYER_API bool SVGPlayer_IsLooping(SVGPlayerRef player);
SVG_PLAYER_API void SVGPlayer_SetLooping(SVGPlayerRef player, bool looping);

// =============================================================================
// Section 6: Playback Rate Functions
// =============================================================================

/// Set playback rate (speed multiplier)
/// @param player Handle to the player
/// @param rate Speed multiplier (1.0 = normal, 2.0 = 2x, 0.5 = half)
/// @note Rate is clamped to 0.1 - 10.0 range. Negative values play in reverse.
SVG_PLAYER_API void SVGPlayer_SetPlaybackRate(SVGPlayerRef player, float rate);

/// Get current playback rate
/// @param player Handle to the player
/// @return Current playback rate
SVG_PLAYER_API float SVGPlayer_GetPlaybackRate(SVGPlayerRef player);

// =============================================================================
// Section 7: Timeline Functions
// =============================================================================

/// Update animation time (call from render loop)
/// @param player Handle to the player
/// @param deltaTime Time since last update in seconds
/// @return true if animation state changed (needs re-render)
SVG_PLAYER_API bool SVGPlayer_Update(SVGPlayerRef player, double deltaTime);

/// Get animation duration
/// @param player Handle to the player
/// @return Duration in seconds (0 for static SVG)
SVG_PLAYER_API double SVGPlayer_GetDuration(SVGPlayerRef player);

/// Get current time position
/// @param player Handle to the player
/// @return Current time in seconds
SVG_PLAYER_API double SVGPlayer_GetCurrentTime(SVGPlayerRef player);

/// Get current progress (0.0 to 1.0)
/// @param player Handle to the player
/// @return Progress from 0.0 to 1.0
SVG_PLAYER_API float SVGPlayer_GetProgress(SVGPlayerRef player);

/// Get current frame number (0-indexed)
/// @param player Handle to the player
/// @return Current frame number
SVG_PLAYER_API int SVGPlayer_GetCurrentFrame(SVGPlayerRef player);

/// Get total frame count
/// @param player Handle to the player
/// @return Total number of frames
SVG_PLAYER_API int SVGPlayer_GetTotalFrames(SVGPlayerRef player);

/// Get frame rate
/// @param player Handle to the player
/// @return Frame rate in FPS
SVG_PLAYER_API float SVGPlayer_GetFrameRate(SVGPlayerRef player);

// =============================================================================
// Section 8: Seeking Functions
// =============================================================================

/// Seek to a specific time
/// @param player Handle to the player
/// @param timeSeconds Time in seconds (clamped to valid range)
SVG_PLAYER_API void SVGPlayer_SeekTo(SVGPlayerRef player, double timeSeconds);

/// Seek to a specific frame (0-indexed)
/// @param player Handle to the player
/// @param frame Frame number (clamped to valid range)
SVG_PLAYER_API void SVGPlayer_SeekToFrame(SVGPlayerRef player, int frame);

/// Seek to a progress position
/// @param player Handle to the player
/// @param progress Value from 0.0 (start) to 1.0 (end)
SVG_PLAYER_API void SVGPlayer_SeekToProgress(SVGPlayerRef player, float progress);

/// Seek to start (time = 0)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_SeekToStart(SVGPlayerRef player);

/// Seek to end (time = duration)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_SeekToEnd(SVGPlayerRef player);

/// Seek forward by time interval
/// @param player Handle to the player
/// @param seconds Time to skip forward
SVG_PLAYER_API void SVGPlayer_SeekForwardByTime(SVGPlayerRef player, double seconds);

/// Seek backward by time interval
/// @param player Handle to the player
/// @param seconds Time to skip backward
SVG_PLAYER_API void SVGPlayer_SeekBackwardByTime(SVGPlayerRef player, double seconds);

// =============================================================================
// Section 9: Frame Stepping Functions
// =============================================================================

/// Step forward by one frame (pauses playback)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_StepForward(SVGPlayerRef player);

/// Step backward by one frame (pauses playback)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_StepBackward(SVGPlayerRef player);

/// Step by specific number of frames
/// @param player Handle to the player
/// @param frames Number of frames (positive = forward, negative = backward)
SVG_PLAYER_API void SVGPlayer_StepByFrames(SVGPlayerRef player, int frames);

// =============================================================================
// Section 10: Scrubbing Functions
// =============================================================================

/// Begin interactive scrubbing session (saves state, pauses playback)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_BeginScrubbing(SVGPlayerRef player);

/// Update position during scrubbing
/// @param player Handle to the player
/// @param progress Progress value (0.0 to 1.0)
SVG_PLAYER_API void SVGPlayer_ScrubToProgress(SVGPlayerRef player, float progress);

/// End scrubbing session
/// @param player Handle to the player
/// @param resume Whether to resume previous playback state
SVG_PLAYER_API void SVGPlayer_EndScrubbing(SVGPlayerRef player, bool resume);

/// Check if currently in scrubbing mode
/// @param player Handle to the player
/// @return true if scrubbing
SVG_PLAYER_API bool SVGPlayer_IsScrubbing(SVGPlayerRef player);

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
SVG_PLAYER_API bool SVGPlayer_Render(SVGPlayerRef player,
                                      void* pixelBuffer,
                                      int width,
                                      int height,
                                      float scale);

/// Render a specific time to a pixel buffer
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor
/// @param timeSeconds Time in seconds for the frame to render
/// @return true on success
SVG_PLAYER_API bool SVGPlayer_RenderAtTime(SVGPlayerRef player,
                                            void* pixelBuffer,
                                            int width,
                                            int height,
                                            float scale,
                                            double timeSeconds);

/// Render a specific frame to a pixel buffer
/// @param player Handle to the player
/// @param pixelBuffer Pointer to RGBA pixel buffer
/// @param width Width in pixels
/// @param height Height in pixels
/// @param scale HiDPI scale factor
/// @param frame Frame number (0-indexed)
/// @return true on success
SVG_PLAYER_API bool SVGPlayer_RenderFrame(SVGPlayerRef player,
                                           void* pixelBuffer,
                                           int width,
                                           int height,
                                           float scale,
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
SVG_PLAYER_API bool SVGPlayer_ViewToSVG(SVGPlayerRef player,
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
SVG_PLAYER_API bool SVGPlayer_SVGToView(SVGPlayerRef player,
                                         float svgX, float svgY,
                                         int viewWidth, int viewHeight,
                                         float* viewX, float* viewY);

// =============================================================================
// Section 13: Hit Testing Functions
// =============================================================================

/// Subscribe to touch events for an SVG element by its ID
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
SVG_PLAYER_API void SVGPlayer_SubscribeToElement(SVGPlayerRef player, const char* objectID);

/// Unsubscribe from touch events for an element
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
SVG_PLAYER_API void SVGPlayer_UnsubscribeFromElement(SVGPlayerRef player, const char* objectID);

/// Unsubscribe from all element events
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_UnsubscribeFromAllElements(SVGPlayerRef player);

/// Hit test to find which subscribed element is at a point
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @return The objectID of the hit element, or NULL if none
/// @note The returned string is valid until the next call to this function
SVG_PLAYER_API const char* SVGPlayer_HitTest(SVGPlayerRef player,
                                              float viewX, float viewY,
                                              int viewWidth, int viewHeight);

/// Get the bounding rect of an element in SVG coordinates
/// @param player Handle to the player
/// @param objectID The id attribute of the SVG element
/// @param bounds Output: bounding rectangle
/// @return true if element found, false otherwise
SVG_PLAYER_API bool SVGPlayer_GetElementBounds(SVGPlayerRef player,
                                                const char* objectID,
                                                SVGRect* bounds);

/// Get all elements at a point (for layered SVGs)
/// @param player Handle to the player
/// @param viewX X in view coordinates
/// @param viewY Y in view coordinates
/// @param viewWidth Width of the view
/// @param viewHeight Height of the view
/// @param outElements Output: array of element IDs (caller allocates)
/// @param maxElements Maximum number of elements to return
/// @return Number of elements found
SVG_PLAYER_API int SVGPlayer_GetElementsAtPoint(SVGPlayerRef player,
                                                 float viewX, float viewY,
                                                 int viewWidth, int viewHeight,
                                                 const char** outElements,
                                                 int maxElements);

// =============================================================================
// Section 14: Element Information Functions
// =============================================================================

/// Get an element by its ID
/// @param player Handle to the player
/// @param elementID The id attribute of the element
/// @return true if element exists
SVG_PLAYER_API bool SVGPlayer_ElementExists(SVGPlayerRef player, const char* elementID);

/// Get a property value from an element
/// @param player Handle to the player
/// @param elementID The id attribute of the element
/// @param propertyName The property name (e.g., "fill", "stroke")
/// @param outValue Output: buffer for property value
/// @param maxLength Maximum length of outValue buffer
/// @return true if property was found
SVG_PLAYER_API bool SVGPlayer_GetElementProperty(SVGPlayerRef player,
                                                  const char* elementID,
                                                  const char* propertyName,
                                                  char* outValue,
                                                  int maxLength);

// =============================================================================
// Section 15: Callback Functions
// =============================================================================

/// Set callback for playback state changes
/// @param player Handle to the player
/// @param callback Function to call on state change (NULL to remove)
/// @param userData Context pointer passed to callback
SVG_PLAYER_API void SVGPlayer_SetStateChangeCallback(SVGPlayerRef player,
                                                      SVGStateChangeCallback callback,
                                                      void* userData);

/// Set callback for loop events
/// @param player Handle to the player
/// @param callback Function to call when animation loops (NULL to remove)
/// @param userData Context pointer passed to callback
SVG_PLAYER_API void SVGPlayer_SetLoopCallback(SVGPlayerRef player,
                                               SVGLoopCallback callback,
                                               void* userData);

/// Set callback for end events (non-looping mode)
/// @param player Handle to the player
/// @param callback Function to call when animation ends (NULL to remove)
/// @param userData Context pointer passed to callback
SVG_PLAYER_API void SVGPlayer_SetEndCallback(SVGPlayerRef player,
                                              SVGEndCallback callback,
                                              void* userData);

/// Set callback for error events
/// @param player Handle to the player
/// @param callback Function to call on errors (NULL to remove)
/// @param userData Context pointer passed to callback
SVG_PLAYER_API void SVGPlayer_SetErrorCallback(SVGPlayerRef player,
                                                SVGErrorCallback callback,
                                                void* userData);

/// Set callback for element touch events
/// @param player Handle to the player
/// @param callback Function to call when subscribed element is touched (NULL to remove)
/// @param userData Context pointer passed to callback
SVG_PLAYER_API void SVGPlayer_SetElementTouchCallback(SVGPlayerRef player,
                                                       SVGElementTouchCallback callback,
                                                       void* userData);

// =============================================================================
// Section 16: Statistics and Diagnostics
// =============================================================================

/// Get rendering statistics
/// @param player Handle to the player
/// @return Rendering statistics structure
SVG_PLAYER_API SVGRenderStats SVGPlayer_GetStats(SVGPlayerRef player);

/// Reset statistics counters
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_ResetStats(SVGPlayerRef player);

/// Get the last error message
/// @param player Handle to the player
/// @return Error message string (empty if no error)
SVG_PLAYER_API const char* SVGPlayer_GetLastError(SVGPlayerRef player);

/// Clear the last error
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_ClearError(SVGPlayerRef player);

// =============================================================================
// Section 17: Pre-buffering Functions (for performance)
// =============================================================================

/// Enable or disable frame pre-buffering
/// @param player Handle to the player
/// @param enable true to enable pre-buffering
SVG_PLAYER_API void SVGPlayer_EnablePreBuffer(SVGPlayerRef player, bool enable);

/// Check if pre-buffering is enabled
/// @param player Handle to the player
/// @return true if pre-buffering is enabled
SVG_PLAYER_API bool SVGPlayer_IsPreBufferEnabled(SVGPlayerRef player);

/// Set number of frames to pre-buffer ahead
/// @param player Handle to the player
/// @param frameCount Number of frames to buffer (default: 3)
SVG_PLAYER_API void SVGPlayer_SetPreBufferFrames(SVGPlayerRef player, int frameCount);

/// Get number of frames currently buffered
/// @param player Handle to the player
/// @return Number of buffered frames ready for display
SVG_PLAYER_API int SVGPlayer_GetBufferedFrames(SVGPlayerRef player);

/// Clear the pre-buffer (e.g., after seeking)
/// @param player Handle to the player
SVG_PLAYER_API void SVGPlayer_ClearPreBuffer(SVGPlayerRef player);

// =============================================================================
// Section 18: Debug Overlay Functions
// =============================================================================

/// Enable or disable debug overlay
/// @param player Handle to the player
/// @param enable true to show debug overlay
SVG_PLAYER_API void SVGPlayer_EnableDebugOverlay(SVGPlayerRef player, bool enable);

/// Check if debug overlay is enabled
/// @param player Handle to the player
/// @return true if debug overlay is enabled
SVG_PLAYER_API bool SVGPlayer_IsDebugOverlayEnabled(SVGPlayerRef player);

/// Set debug overlay flags (what to display)
/// @param player Handle to the player
/// @param flags Bitwise OR of SVGDebugFlags
SVG_PLAYER_API void SVGPlayer_SetDebugFlags(SVGPlayerRef player, uint32_t flags);

/// Get current debug flags
/// @param player Handle to the player
/// @return Current debug flags
SVG_PLAYER_API uint32_t SVGPlayer_GetDebugFlags(SVGPlayerRef player);

// =============================================================================
// Section 19: Utility Functions
// =============================================================================

/// Format time as string (MM:SS.mmm)
/// @param timeSeconds Time in seconds
/// @param outBuffer Output buffer for formatted string
/// @param bufferSize Size of output buffer
/// @return Pointer to outBuffer
SVG_PLAYER_API const char* SVGPlayer_FormatTime(double timeSeconds,
                                                 char* outBuffer,
                                                 int bufferSize);

/// Convert time to frame number
/// @param player Handle to the player
/// @param timeSeconds Time in seconds
/// @return Frame number (0-indexed)
SVG_PLAYER_API int SVGPlayer_TimeToFrame(SVGPlayerRef player, double timeSeconds);

/// Convert frame number to time
/// @param player Handle to the player
/// @param frame Frame number (0-indexed)
/// @return Time in seconds
SVG_PLAYER_API double SVGPlayer_FrameToTime(SVGPlayerRef player, int frame);

#ifdef __cplusplus
}
#endif

// =============================================================================
// C++ Convenience Wrapper (optional, header-only)
// =============================================================================

#ifdef __cplusplus
#ifdef SVG_PLAYER_USE_CXX_WRAPPER

#include <string>
#include <memory>
#include <functional>

namespace svgplayer {

/// RAII wrapper for SVGPlayerRef
class Player {
public:
    Player() : handle_(SVGPlayer_Create()) {}
    ~Player() { if (handle_) SVGPlayer_Destroy(handle_); }

    // Non-copyable
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Movable
    Player(Player&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Player& operator=(Player&& other) noexcept {
        if (this != &other) {
            if (handle_) SVGPlayer_Destroy(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Access underlying handle
    SVGPlayerRef get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    // Convenience methods
    bool load(const std::string& path) { return SVGPlayer_LoadSVG(handle_, path.c_str()); }
    bool loadData(const void* data, size_t len) { return SVGPlayer_LoadSVGData(handle_, data, len); }
    void play() { SVGPlayer_Play(handle_); }
    void pause() { SVGPlayer_Pause(handle_); }
    void stop() { SVGPlayer_Stop(handle_); }
    bool update(double dt) { return SVGPlayer_Update(handle_, dt); }
    bool render(void* buf, int w, int h, float s = 1.0f) { return SVGPlayer_Render(handle_, buf, w, h, s); }

private:
    SVGPlayerRef handle_;
};

} // namespace svgplayer

#endif // SVG_PLAYER_USE_CXX_WRAPPER
#endif // __cplusplus

#endif // SVG_PLAYER_API_H
