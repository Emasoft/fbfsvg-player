// FBFSVGPlayerController.h - Low-level SVG animation controller for macOS
//
// This class provides direct access to the SVG rendering engine.
// Most users should use SVGPlayerView instead for AppKit integration.
//
// Use this class when you need:
// - Custom rendering to a pixel buffer
// - Integration with custom Metal/OpenGL pipelines
// - More control over the rendering process

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

NS_ASSUME_NONNULL_BEGIN

#pragma mark - Error Domain

/// Error domain for FBFSVGPlayerController errors
extern NSString * const FBFSVGPlayerControllerErrorDomain;

/// Error codes for FBFSVGPlayerController
typedef NS_ENUM(NSInteger, FBFSVGPlayerControllerErrorCode) {
    /// File not found
    FBFSVGPlayerControllerErrorFileNotFound = 100,
    /// Invalid SVG data
    FBFSVGPlayerControllerErrorInvalidData = 101,
    /// Parsing failed
    FBFSVGPlayerControllerErrorParseFailed = 102,
    /// Rendering failed
    FBFSVGPlayerControllerErrorRenderFailed = 103,
    /// Player not initialized
    FBFSVGPlayerControllerErrorNotInitialized = 104,
    /// No SVG loaded
    FBFSVGPlayerControllerErrorNoSVGLoaded = 105
};

#pragma mark - Data Structures

/// Rendering statistics from the SVG player
typedef struct {
    /// Time to render last frame in milliseconds
    double renderTimeMs;
    /// Time to update animation in milliseconds
    double updateTimeMs;
    /// Current animation time in milliseconds
    double animationTimeMs;
    /// Current frame index (0-based)
    int currentFrame;
    /// Total frames in animation
    int totalFrames;
    /// Current frames per second
    double fps;
    /// Peak memory usage in bytes (if available)
    size_t peakMemoryBytes;
    /// Number of SVG elements rendered
    int elementsRendered;
} SVGRenderStatistics;

/// Playback state for the controller
typedef NS_ENUM(NSInteger, SVGControllerPlaybackState) {
    /// Animation is stopped
    SVGControllerPlaybackStateStopped = 0,
    /// Animation is playing
    SVGControllerPlaybackStatePlaying,
    /// Animation is paused
    SVGControllerPlaybackStatePaused
};

/// Repeat mode for animation playback
typedef NS_ENUM(NSInteger, SVGControllerRepeatMode) {
    /// Play once and stop
    SVGControllerRepeatModeNone = 0,
    /// Loop continuously
    SVGControllerRepeatModeLoop,
    /// Ping-pong (forward then backward)
    SVGControllerRepeatModeReverse,
    /// Loop specific count
    SVGControllerRepeatModeCount
};

/// Layer blend mode for compositing
typedef NS_ENUM(NSInteger, FBFSVGPlayerLayerBlendMode) {
    /// Normal alpha blending (default)
    FBFSVGPlayerLayerBlendModeNormal = 0,
    /// Multiply blend mode
    FBFSVGPlayerLayerBlendModeMultiply,
    /// Screen blend mode
    FBFSVGPlayerLayerBlendModeScreen,
    /// Overlay blend mode
    FBFSVGPlayerLayerBlendModeOverlay,
    /// Darken blend mode
    FBFSVGPlayerLayerBlendModeDarken,
    /// Lighten blend mode
    FBFSVGPlayerLayerBlendModeLighten
};

// Forward declaration
@class FBFSVGPlayerLayer;

#pragma mark - FBFSVGPlayerController

/// Low-level controller for SVG rendering on macOS
///
/// This class wraps the unified C API and provides an Objective-C interface
/// for direct SVG manipulation and rendering to pixel buffers.
///
/// For AppKit integration, use SVGPlayerView instead.
@interface FBFSVGPlayerController : NSObject

#pragma mark - Initialization

/// Create a new SVG player controller
/// @return A new controller instance, or nil if creation failed
+ (nullable instancetype)controller;

/// Create a new SVG player controller (designated initializer)
- (instancetype)init NS_DESIGNATED_INITIALIZER;

#pragma mark - Loading

/// Load an SVG from file path
/// @param path Full path to the SVG file
/// @param error Output error if loading fails (optional)
/// @return YES if loading succeeded
- (BOOL)loadSVGFromPath:(NSString *)path error:(NSError * _Nullable * _Nullable)error;

/// Load an SVG from data
/// @param data The SVG file data
/// @param error Output error if loading fails (optional)
/// @return YES if loading succeeded
- (BOOL)loadSVGFromData:(NSData *)data error:(NSError * _Nullable * _Nullable)error;

/// Unload the current SVG and free resources
- (void)unload;

#pragma mark - State Properties

/// Whether an SVG is currently loaded
@property (nonatomic, readonly, getter=isLoaded) BOOL loaded;

/// The intrinsic size of the loaded SVG (NSZeroSize if not loaded)
@property (nonatomic, readonly) NSSize intrinsicSize;

/// Animation duration in seconds (0 if static SVG or not loaded)
@property (nonatomic, readonly) NSTimeInterval duration;

/// Whether the animation loops
@property (nonatomic, assign) BOOL looping;

/// Current animation time in seconds
@property (nonatomic, readonly) NSTimeInterval currentTime;

/// Current playback state
@property (nonatomic, readonly) SVGControllerPlaybackState playbackState;

/// Current rendering statistics
@property (nonatomic, readonly) SVGRenderStatistics statistics;

/// The last error message from the renderer (nil if no error)
@property (nonatomic, readonly, nullable) NSString *lastErrorMessage;

#pragma mark - Playback Mode Properties

/// Repeat mode for animation
@property (nonatomic, assign) SVGControllerRepeatMode repeatMode;

/// Number of repeats when using SVGControllerRepeatModeCount
@property (nonatomic, assign) NSInteger repeatCount;

/// Current repeat iteration (0-indexed)
@property (nonatomic, readonly) NSInteger currentRepeatIteration;

/// Whether currently playing forward (for ping-pong mode)
@property (nonatomic, readonly, getter=isPlayingForward) BOOL playingForward;

/// Playback rate multiplier (1.0 = normal speed)
/// Range: 0.1 to 10.0
@property (nonatomic, assign) CGFloat playbackRate;

#pragma mark - Timeline Properties

/// Progress through animation (0.0 to 1.0)
@property (nonatomic, readonly) CGFloat progress;

/// Elapsed time in seconds (same as currentTime)
@property (nonatomic, readonly) NSTimeInterval elapsedTime;

/// Remaining time in seconds
@property (nonatomic, readonly) NSTimeInterval remainingTime;

/// Current frame number (0-indexed)
@property (nonatomic, readonly) NSInteger currentFrame;

/// Total number of frames
@property (nonatomic, readonly) NSInteger totalFrames;

/// Frame rate (frames per second) of the animation
@property (nonatomic, readonly) CGFloat frameRate;

/// Time per frame in seconds
@property (nonatomic, readonly) NSTimeInterval timePerFrame;

#pragma mark - Basic Playback Control

/// Start or resume playback
- (void)play;

/// Pause playback
- (void)pause;

/// Resume playback (alias for play)
- (void)resume;

/// Stop playback and reset to beginning
- (void)stop;

/// Toggle between play and pause
- (void)togglePlayback;

#pragma mark - Animation Update

/// Update animation time (call from display link or timer)
/// @param deltaTime Time elapsed since last update in seconds
- (void)update:(NSTimeInterval)deltaTime;

/// Update animation with explicit direction control
/// @param deltaTime Time elapsed since last update in seconds
/// @param forward Whether to advance forward or backward
- (void)update:(NSTimeInterval)deltaTime forward:(BOOL)forward;

#pragma mark - Seeking

/// Seek to a specific time
/// @param time Time in seconds (clamped to valid range)
- (void)seekToTime:(NSTimeInterval)time;

/// Seek to a specific frame
/// @param frame Frame number (0-indexed, clamped to valid range)
- (void)seekToFrame:(NSInteger)frame;

/// Seek to a progress position
/// @param progress Value from 0.0 (start) to 1.0 (end)
- (void)seekToProgress:(CGFloat)progress;

/// Jump to start of animation
- (void)seekToStart;

/// Jump to end of animation
- (void)seekToEnd;

#pragma mark - Frame Stepping

/// Step forward by one frame
/// Pauses playback if currently playing.
- (void)stepForward;

/// Step backward by one frame
/// Pauses playback if currently playing.
- (void)stepBackward;

/// Step by a specific number of frames
/// @param frameCount Number of frames (positive = forward, negative = backward)
- (void)stepByFrames:(NSInteger)frameCount;

#pragma mark - Relative Seeking

/// Seek forward by a time interval
/// @param seconds Time to skip forward
- (void)seekForwardByTime:(NSTimeInterval)seconds;

/// Seek backward by a time interval
/// @param seconds Time to skip backward
- (void)seekBackwardByTime:(NSTimeInterval)seconds;

/// Seek forward by a percentage of duration
/// @param percentage Percentage to skip (e.g., 0.1 = 10%)
- (void)seekForwardByPercentage:(CGFloat)percentage;

/// Seek backward by a percentage of duration
/// @param percentage Percentage to skip (e.g., 0.1 = 10%)
- (void)seekBackwardByPercentage:(CGFloat)percentage;

#pragma mark - Scrubbing Support

/// Begin interactive scrubbing session
/// Stores playback state for restoration later.
- (void)beginScrubbing;

/// Update position during scrubbing
/// @param progress Progress value (0.0 to 1.0)
- (void)scrubToProgress:(CGFloat)progress;

/// End scrubbing session
/// @param resume Whether to resume previous playback state
- (void)endScrubbing:(BOOL)resume;

/// Whether currently in scrubbing mode
@property (nonatomic, readonly, getter=isScrubbing) BOOL scrubbing;

#pragma mark - Rendering

/// Render the current frame to a pixel buffer
///
/// The buffer must be pre-allocated with size: width * height * 4 bytes
/// Output format is RGBA with 8 bits per channel, premultiplied alpha.
///
/// @param buffer Pointer to RGBA pixel buffer
/// @param width Width of the buffer in pixels
/// @param height Height of the buffer in pixels
/// @param scale HiDPI scale factor (e.g., 2.0 for Retina)
/// @return YES if rendering succeeded
- (BOOL)renderToBuffer:(void *)buffer
                 width:(NSInteger)width
                height:(NSInteger)height
                 scale:(CGFloat)scale;

/// Render a specific frame to a pixel buffer
/// @param buffer Pointer to RGBA pixel buffer
/// @param width Width of the buffer in pixels
/// @param height Height of the buffer in pixels
/// @param scale HiDPI scale factor
/// @param time Time in seconds for the frame to render
/// @return YES if rendering succeeded
- (BOOL)renderToBuffer:(void *)buffer
                 width:(NSInteger)width
                height:(NSInteger)height
                 scale:(CGFloat)scale
                atTime:(NSTimeInterval)time;

#pragma mark - Utility Methods

/// Get formatted time string for a time value
/// @param time Time in seconds
/// @return Formatted string (MM:SS or HH:MM:SS)
+ (NSString *)formatTime:(NSTimeInterval)time;

/// Get formatted time string for current time
- (NSString *)formattedCurrentTime;

/// Get formatted time string for remaining time
- (NSString *)formattedRemainingTime;

/// Get formatted time string for duration
- (NSString *)formattedDuration;

/// Calculate frame number for a given time
/// @param time Time in seconds
/// @return Frame number (0-indexed)
- (NSInteger)frameForTime:(NSTimeInterval)time;

/// Calculate time for a given frame number
/// @param frame Frame number (0-indexed)
/// @return Time in seconds
- (NSTimeInterval)timeForFrame:(NSInteger)frame;

#pragma mark - Hit Testing - Element Subscription

/// Subscribe to an element for hit testing
/// Subscribed elements can be detected via hitTestAtPoint: and related methods.
/// @param objectID The SVG element ID to subscribe to (e.g., "button1")
- (void)subscribeToElementWithID:(NSString *)objectID;

/// Unsubscribe from a previously subscribed element
/// @param objectID The SVG element ID to unsubscribe from
- (void)unsubscribeFromElementWithID:(NSString *)objectID;

/// Unsubscribe from all currently subscribed elements
- (void)unsubscribeFromAllElements;

#pragma mark - Hit Testing - Queries

/// Perform hit test to find the topmost subscribed element at a point
/// @param point Point in view coordinates
/// @param viewSize The current view size (for coordinate transformation)
/// @return The element ID if a subscribed element was hit, nil otherwise
- (nullable NSString *)hitTestAtPoint:(NSPoint)point viewSize:(NSSize)viewSize;

/// Get all subscribed elements at a point (for overlapping elements)
/// @param point Point in view coordinates
/// @param viewSize The current view size
/// @param maxElements Maximum number of elements to return
/// @return Array of element IDs that contain the point
- (NSArray<NSString *> *)elementsAtPoint:(NSPoint)point
                                viewSize:(NSSize)viewSize
                             maxElements:(NSInteger)maxElements;

/// Get the bounding rectangle of an element in SVG coordinates
/// @param objectID The SVG element ID
/// @return The bounding rect in SVG coordinates, or NSZeroRect if not found
- (NSRect)boundingRectForElementID:(NSString *)objectID;

/// Check if an element exists in the current SVG
/// @param objectID The SVG element ID to check
/// @return YES if the element exists
- (BOOL)elementExistsWithID:(NSString *)objectID;

/// Get a property value for an SVG element
/// @param propertyName The property to query (e.g., "fill", "opacity", "transform")
/// @param objectID The SVG element ID
/// @return The property value as a string, or nil if not found
- (nullable NSString *)propertyValue:(NSString *)propertyName forElementID:(NSString *)objectID;

#pragma mark - Coordinate Conversion

/// Convert a point from view coordinates to SVG coordinates
/// @param viewPoint Point in view coordinates
/// @param viewSize The current view size
/// @return The point in SVG coordinates
- (NSPoint)convertViewPointToSVG:(NSPoint)viewPoint viewSize:(NSSize)viewSize;

/// Convert a point from SVG coordinates to view coordinates
/// @param svgPoint Point in SVG coordinates
/// @param viewSize The current view size
/// @return The point in view coordinates
- (NSPoint)convertSVGPointToView:(NSPoint)svgPoint viewSize:(NSSize)viewSize;

#pragma mark - Zoom and ViewBox

/// Get the current viewBox
/// @param x Output: x coordinate of viewBox origin
/// @param y Output: y coordinate of viewBox origin
/// @param width Output: width of viewBox
/// @param height Output: height of viewBox
/// @return YES if viewBox was retrieved successfully
- (BOOL)getViewBoxX:(CGFloat *)x y:(CGFloat *)y width:(CGFloat *)width height:(CGFloat *)height;

/// Set the viewBox directly (for custom zoom/pan)
/// @param x x coordinate of viewBox origin
/// @param y y coordinate of viewBox origin
/// @param width width of viewBox
/// @param height height of viewBox
- (void)setViewBoxX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;

/// Reset the viewBox to the original SVG viewBox
- (void)resetViewBox;

/// Current zoom level (1.0 = no zoom, >1.0 = zoomed in)
@property (nonatomic, readonly) CGFloat zoom;

/// Set zoom level centered on a point
/// @param zoom Zoom level (1.0 = no zoom)
/// @param center Center point in view coordinates
/// @param viewSize Current view size
- (void)setZoom:(CGFloat)zoom centeredAt:(NSPoint)center viewSize:(NSSize)viewSize;

/// Zoom in by a factor
/// @param factor Zoom factor (e.g., 1.5 = zoom in 50%)
/// @param viewSize Current view size
- (void)zoomInByFactor:(CGFloat)factor viewSize:(NSSize)viewSize;

/// Zoom out by a factor
/// @param factor Zoom factor (e.g., 1.5 = zoom out 50%)
/// @param viewSize Current view size
- (void)zoomOutByFactor:(CGFloat)factor viewSize:(NSSize)viewSize;

/// Zoom to show a specific rectangle in SVG coordinates
/// @param rect Rectangle in SVG coordinates to zoom to
- (void)zoomToRect:(NSRect)rect;

/// Zoom to show a specific element with optional padding
/// @param objectID The SVG element ID to zoom to
/// @param padding Padding around the element in SVG units
/// @return YES if element was found and zoom applied
- (BOOL)zoomToElementWithID:(NSString *)objectID padding:(CGFloat)padding;

/// Pan the view by a delta in view coordinates
/// @param delta Delta in view coordinates
/// @param viewSize Current view size
- (void)panByDelta:(NSPoint)delta viewSize:(NSSize)viewSize;

/// Minimum zoom level (default 0.1)
@property (nonatomic, assign) CGFloat minZoom;

/// Maximum zoom level (default 10.0)
@property (nonatomic, assign) CGFloat maxZoom;

#pragma mark - Frame Rate Control

/// Target frame rate for rendering (in frames per second)
/// Set this to match your display's refresh rate or a lower value for throttling.
/// Default: 60.0 fps
@property (nonatomic, assign) CGFloat targetFrameRate;

/// Ideal frame interval in seconds (1.0 / targetFrameRate)
/// This is the target time between frames. Read-only.
@property (nonatomic, readonly) NSTimeInterval idealFrameInterval;

/// Duration of the last rendered frame in seconds
/// Useful for performance monitoring. Read-only.
@property (nonatomic, readonly) NSTimeInterval lastFrameDuration;

/// Average frame duration over recent frames in seconds
/// Useful for detecting performance issues. Read-only.
@property (nonatomic, readonly) NSTimeInterval averageFrameDuration;

/// Measured frames per second based on actual render times
/// This is the actual FPS, not the target. Read-only.
@property (nonatomic, readonly) CGFloat measuredFPS;

/// Number of frames that were dropped due to timing constraints
/// Read-only. Reset with resetFrameStats.
@property (nonatomic, readonly) NSInteger droppedFrameCount;

/// Mark the beginning of a frame rendering cycle
/// Call this before rendering to track frame timing.
- (void)beginFrame;

/// Mark the end of a frame rendering cycle
/// Call this after rendering to update frame statistics.
- (void)endFrame;

/// Check if a frame should be rendered at the given time
/// @param currentTime Current time in seconds
/// @return YES if enough time has passed since last render
- (BOOL)shouldRenderFrameAtTime:(NSTimeInterval)currentTime;

/// Mark that a frame was rendered at a specific time
/// @param renderTime Time in seconds when the frame was rendered
- (void)markFrameRenderedAtTime:(NSTimeInterval)renderTime;

/// Reset all frame timing statistics
/// Clears dropped frame count, average duration, etc.
- (void)resetFrameStats;

#pragma mark - Version Information

/// Get the library version string (e.g., "0.9.0-alpha")
/// @return Version string
+ (NSString *)version;

/// Get the library version as separate components
/// @param major Output: major version number
/// @param minor Output: minor version number
/// @param patch Output: patch version number
+ (void)getVersionMajor:(NSInteger *)major minor:(NSInteger *)minor patch:(NSInteger *)patch;

/// Get detailed build information
/// @return Build info string including platform, architecture, and build date
+ (NSString *)buildInfo;

#pragma mark - Multi-SVG Compositing

/// Create a new layer from an SVG file
/// @param filepath Path to the SVG file
/// @param error Output error if loading fails (optional)
/// @return New layer instance, or nil on failure
- (nullable FBFSVGPlayerLayer *)createLayerFromPath:(NSString *)filepath error:(NSError * _Nullable * _Nullable)error;

/// Create a new layer from SVG data
/// @param data The SVG file data
/// @param error Output error if loading fails (optional)
/// @return New layer instance, or nil on failure
- (nullable FBFSVGPlayerLayer *)createLayerFromData:(NSData *)data error:(NSError * _Nullable * _Nullable)error;

/// Destroy a layer and free its resources
/// @param layer The layer to destroy
- (void)destroyLayer:(FBFSVGPlayerLayer *)layer;

/// Number of layers (including primary SVG as layer 0)
@property (nonatomic, readonly) NSInteger layerCount;

/// Get a layer by index (0 = primary SVG)
/// @param index Layer index (0-based)
/// @return Layer at index, or nil if out of range
- (nullable FBFSVGPlayerLayer *)layerAtIndex:(NSInteger)index;

/// Render all visible layers composited together
/// @param buffer Pointer to RGBA pixel buffer
/// @param width Width of the buffer in pixels
/// @param height Height of the buffer in pixels
/// @param scale HiDPI scale factor
/// @return YES if rendering succeeded
- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale;

/// Render composite at a specific time
/// @param buffer Pointer to RGBA pixel buffer
/// @param width Width of the buffer in pixels
/// @param height Height of the buffer in pixels
/// @param scale HiDPI scale factor
/// @param time Time in seconds (applied to all layers)
/// @return YES if rendering succeeded
- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale
                         atTime:(NSTimeInterval)time;

/// Update all layers at once
/// @param deltaTime Time since last update in seconds
/// @return YES if any layer needs re-render
- (BOOL)updateAllLayers:(NSTimeInterval)deltaTime;

/// Play all layers simultaneously
- (void)playAllLayers;

/// Pause all layers
- (void)pauseAllLayers;

/// Stop all layers and reset to beginning
- (void)stopAllLayers;

@end

#pragma mark - FBFSVGPlayerLayer

/// Represents a single SVG layer in a composite scene
@interface FBFSVGPlayerLayer : NSObject

/// Layer position offset from origin
@property (nonatomic, assign) NSPoint position;

/// Layer opacity (0.0 = transparent, 1.0 = opaque)
@property (nonatomic, assign) CGFloat opacity;

/// Layer z-order (higher = rendered on top)
@property (nonatomic, assign) NSInteger zOrder;

/// Layer visibility
@property (nonatomic, assign, getter=isVisible) BOOL visible;

/// Layer scale (scaleX, scaleY)
@property (nonatomic, assign) NSPoint scale;

/// Layer rotation in degrees (clockwise)
@property (nonatomic, assign) CGFloat rotation;

/// Layer blend mode for compositing
@property (nonatomic, assign) FBFSVGPlayerLayerBlendMode blendMode;

/// Intrinsic size of the layer's SVG (read-only)
@property (nonatomic, readonly) NSSize size;

/// Animation duration in seconds (read-only)
@property (nonatomic, readonly) NSTimeInterval duration;

/// Current animation time in seconds (read-only)
@property (nonatomic, readonly) NSTimeInterval currentTime;

/// Whether the layer has animations (read-only)
@property (nonatomic, readonly) BOOL hasAnimations;

/// Start or resume layer animation
- (void)play;

/// Pause layer animation
- (void)pause;

/// Stop layer animation and reset to beginning
- (void)stop;

/// Seek layer to specific time
/// @param time Time in seconds
- (void)seekToTime:(NSTimeInterval)time;

/// Update layer animation
/// @param deltaTime Time since last update in seconds
/// @return YES if layer needs re-render
- (BOOL)update:(NSTimeInterval)deltaTime;

@end

NS_ASSUME_NONNULL_END
