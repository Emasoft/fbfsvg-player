// SVGPlayerController.h - Low-level SVG animation controller
//
// This class provides direct access to the SVG rendering engine.
// Most users should use SVGPlayerView instead for UIKit integration.
//
// Use this class when you need:
// - Custom rendering to a pixel buffer
// - Integration with custom Metal/OpenGL pipelines
// - More control over the rendering process

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

NS_ASSUME_NONNULL_BEGIN

#pragma mark - Error Domain

/// Error domain for SVGPlayerController errors
extern NSString * const SVGPlayerControllerErrorDomain;

/// Error codes for SVGPlayerController
typedef NS_ENUM(NSInteger, SVGPlayerControllerErrorCode) {
    /// File not found
    SVGPlayerControllerErrorFileNotFound = 100,
    /// Invalid SVG data
    SVGPlayerControllerErrorInvalidData = 101,
    /// Parsing failed
    SVGPlayerControllerErrorParseFailed = 102,
    /// Rendering failed
    SVGPlayerControllerErrorRenderFailed = 103,
    /// Player not initialized
    SVGPlayerControllerErrorNotInitialized = 104,
    /// No SVG loaded
    SVGPlayerControllerErrorNoSVGLoaded = 105
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

/// Repeat mode for animation playback (mirrors view repeat mode)
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
typedef NS_ENUM(NSInteger, SVGLayerBlendMode) {
    /// Normal alpha blending (default)
    SVGLayerBlendModeNormal = 0,
    /// Multiply blend mode
    SVGLayerBlendModeMultiply,
    /// Screen blend mode
    SVGLayerBlendModeScreen,
    /// Overlay blend mode
    SVGLayerBlendModeOverlay,
    /// Darken blend mode
    SVGLayerBlendModeDarken,
    /// Lighten blend mode
    SVGLayerBlendModeLighten
};

#pragma mark - Forward Declarations

/// Forward declaration for SVG layer (compositing support)
@class SVGLayer;

#pragma mark - SVGPlayerController

/// Low-level controller for SVG rendering
///
/// This class wraps the C API and provides an Objective-C interface
/// for direct SVG manipulation and rendering to pixel buffers.
///
/// For UIKit integration, use SVGPlayerView instead.
@interface SVGPlayerController : NSObject

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

/// The intrinsic size of the loaded SVG (CGSizeZero if not loaded)
@property (nonatomic, readonly) CGSize intrinsicSize;

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
/// Returns the native frame rate of the SVG animation, or 60 for static SVGs
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
/// @param scale HiDPI scale factor (e.g., 2.0 for Retina, 3.0 for @3x)
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
- (nullable NSString *)hitTestAtPoint:(CGPoint)point viewSize:(CGSize)viewSize;

/// Get all subscribed elements at a point (for overlapping elements)
/// @param point Point in view coordinates
/// @param viewSize The current view size
/// @param maxElements Maximum number of elements to return
/// @return Array of element IDs that contain the point
- (NSArray<NSString *> *)elementsAtPoint:(CGPoint)point
                                viewSize:(CGSize)viewSize
                             maxElements:(NSInteger)maxElements;

/// Get the bounding rectangle of an element in SVG coordinates
/// @param objectID The SVG element ID
/// @return The bounding rect in SVG coordinates, or CGRectZero if not found
- (CGRect)boundingRectForElementID:(NSString *)objectID;

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
- (CGPoint)convertViewPointToSVG:(CGPoint)viewPoint viewSize:(CGSize)viewSize;

/// Convert a point from SVG coordinates to view coordinates
/// @param svgPoint Point in SVG coordinates
/// @param viewSize The current view size
/// @return The point in view coordinates
- (CGPoint)convertSVGPointToView:(CGPoint)svgPoint viewSize:(CGSize)viewSize;

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
- (void)setZoom:(CGFloat)zoom centeredAt:(CGPoint)center viewSize:(CGSize)viewSize;

/// Zoom in by a factor
/// @param factor Zoom factor (e.g., 1.5 = zoom in 50%)
/// @param viewSize Current view size
- (void)zoomInByFactor:(CGFloat)factor viewSize:(CGSize)viewSize;

/// Zoom out by a factor
/// @param factor Zoom factor (e.g., 1.5 = zoom out 50%)
/// @param viewSize Current view size
- (void)zoomOutByFactor:(CGFloat)factor viewSize:(CGSize)viewSize;

/// Zoom to show a specific rectangle in SVG coordinates
/// @param rect Rectangle in SVG coordinates to zoom to
- (void)zoomToRect:(CGRect)rect;

/// Zoom to show a specific element with optional padding
/// @param objectID The SVG element ID to zoom to
/// @param padding Padding around the element in SVG units
/// @return YES if element was found and zoom applied
- (BOOL)zoomToElementWithID:(NSString *)objectID padding:(CGFloat)padding;

/// Pan the view by a delta in view coordinates
/// @param delta Delta in view coordinates
/// @param viewSize Current view size
- (void)panByDelta:(CGPoint)delta viewSize:(CGSize)viewSize;

/// Minimum zoom level (default 0.1)
@property (nonatomic, assign) CGFloat minZoom;

/// Maximum zoom level (default 10.0)
@property (nonatomic, assign) CGFloat maxZoom;

#pragma mark - Multi-SVG Compositing

/// Create a new layer by loading an SVG file
/// @param path Path to the SVG file
/// @param error Output error if loading fails (optional)
/// @return New layer instance, or nil on failure
- (nullable SVGLayer *)createLayerFromPath:(NSString *)path error:(NSError * _Nullable * _Nullable)error;

/// Create a new layer from SVG data
/// @param data The SVG file data
/// @param error Output error if loading fails (optional)
/// @return New layer instance, or nil on failure
- (nullable SVGLayer *)createLayerFromData:(NSData *)data error:(NSError * _Nullable * _Nullable)error;

/// Destroy a layer and free its resources
/// @param layer The layer to destroy
- (void)destroyLayer:(SVGLayer *)layer;

/// Number of layers (including primary SVG as layer 0)
@property (nonatomic, readonly) NSInteger layerCount;

/// Get a layer by index (0 = primary SVG)
/// @param index Layer index (0-based)
/// @return Layer instance, or nil if index out of range
- (nullable SVGLayer *)layerAtIndex:(NSInteger)index;

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

/// Update all layers' animations
/// @param deltaTime Time elapsed since last update in seconds
/// @return YES if any layer needs re-render
- (BOOL)updateAllLayers:(NSTimeInterval)deltaTime;

/// Play all layers simultaneously
- (void)playAllLayers;

/// Pause all layers
- (void)pauseAllLayers;

/// Stop all layers and reset to beginning
- (void)stopAllLayers;

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

@end

#pragma mark - SVGLayer

/// Represents a single SVG layer in a composite scene
///
/// Each layer has its own SVG content, position, opacity, z-order, and transform.
/// Layers are rendered in z-order (lowest first) when using renderComposite.
@interface SVGLayer : NSObject

/// Position offset from origin
@property (nonatomic, assign) CGPoint position;

/// Opacity (0.0 = transparent, 1.0 = opaque)
@property (nonatomic, assign) CGFloat opacity;

/// Z-order for rendering (higher = on top)
@property (nonatomic, assign) NSInteger zOrder;

/// Visibility flag
@property (nonatomic, assign, getter=isVisible) BOOL visible;

/// Scale factors (1.0 = original size)
@property (nonatomic, assign) CGPoint scale;

/// Rotation angle in degrees (clockwise)
@property (nonatomic, assign) CGFloat rotation;

/// Blend mode for compositing
@property (nonatomic, assign) SVGLayerBlendMode blendMode;

/// Intrinsic size of the layer's SVG (readonly)
@property (nonatomic, readonly) CGSize size;

/// Animation duration in seconds (readonly)
@property (nonatomic, readonly) NSTimeInterval duration;

/// Current animation time in seconds (readonly)
@property (nonatomic, readonly) NSTimeInterval currentTime;

/// Whether the layer has animations (readonly)
@property (nonatomic, readonly) BOOL hasAnimations;

/// Start or resume layer animation
- (void)play;

/// Pause layer animation
- (void)pause;

/// Stop layer animation and reset to beginning
- (void)stop;

/// Seek to a specific time
/// @param time Time in seconds
- (void)seekToTime:(NSTimeInterval)time;

/// Update layer animation
/// @param deltaTime Time elapsed since last update in seconds
/// @return YES if layer needs re-render
- (BOOL)update:(NSTimeInterval)deltaTime;

@end

NS_ASSUME_NONNULL_END
