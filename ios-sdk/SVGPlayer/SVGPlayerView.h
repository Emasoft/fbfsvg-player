// SVGPlayerView.h - @IBDesignable UIView for SVG animation playback
//
// This view can be used in Interface Builder with live preview support.
// Simply drag a UIView onto your storyboard/XIB and set its class to SVGPlayerView.
//
// Features:
// - Metal GPU-accelerated rendering at native Retina resolution
// - SMIL animation support
// - CADisplayLink-based smooth animation
// - Full playback controls for custom UI integration
// - Frame capture capability
// - Fullscreen and rotation lock support

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

#pragma mark - Error Domain

/// Error domain for SVGPlayerView errors
extern NSString * const SVGPlayerViewErrorDomain;

#pragma mark - Enumerations

/// Content mode options for how SVG fits within the view bounds
typedef NS_ENUM(NSInteger, SVGContentMode) {
    /// Preserve aspect ratio, fit within bounds (default)
    SVGContentModeScaleAspectFit = 0,
    /// Preserve aspect ratio, fill bounds (may clip)
    SVGContentModeScaleAspectFill,
    /// Stretch to fill bounds exactly
    SVGContentModeScaleToFill,
    /// Center at original size (may clip or have margins)
    SVGContentModeCenter
};

/// Playback state for the SVG animation
typedef NS_ENUM(NSInteger, SVGViewPlaybackState) {
    /// Animation is stopped and reset to beginning
    SVGViewPlaybackStateStopped = 0,
    /// Animation is actively playing
    SVGViewPlaybackStatePlaying,
    /// Animation is paused at current position
    SVGViewPlaybackStatePaused,
    /// Animation is buffering/loading (future use)
    SVGViewPlaybackStateBuffering,
    /// Animation playback ended (non-looping mode)
    SVGViewPlaybackStateEnded
};

/// Repeat mode for animation playback
typedef NS_ENUM(NSInteger, SVGRepeatMode) {
    /// Play once and stop at end
    SVGRepeatModeNone = 0,
    /// Loop continuously from start
    SVGRepeatModeLoop,
    /// Play forward, then backward, then forward (ping-pong)
    SVGRepeatModeReverse,
    /// Loop a specific number of times (set via repeatCount)
    SVGRepeatModeCount
};

/// Seek direction for relative seeking
typedef NS_ENUM(NSInteger, SVGSeekDirection) {
    /// Seek forward in time
    SVGSeekDirectionForward = 0,
    /// Seek backward in time
    SVGSeekDirectionBackward
};

#pragma mark - Timeline Info Structure

/// Structure containing timeline information for UI display
typedef struct {
    /// Current playback time in seconds
    NSTimeInterval currentTime;
    /// Total duration in seconds
    NSTimeInterval duration;
    /// Elapsed time (same as currentTime, for clarity)
    NSTimeInterval elapsedTime;
    /// Remaining time until end
    NSTimeInterval remainingTime;
    /// Progress as percentage (0.0 to 1.0)
    CGFloat progress;
    /// Current frame number (0-indexed)
    NSInteger currentFrame;
    /// Total frame count
    NSInteger totalFrames;
    /// Current playback FPS
    CGFloat fps;
    /// Whether playback direction is forward
    BOOL isPlayingForward;
} SVGTimelineInfo;

#pragma mark - Viewport Structure

/// Structure representing a viewport/viewbox for the SVG
/// This defines which portion of the SVG is visible
typedef struct {
    /// X coordinate of the viewport origin (in SVG coordinate space)
    CGFloat x;
    /// Y coordinate of the viewport origin (in SVG coordinate space)
    CGFloat y;
    /// Width of the viewport (in SVG coordinate space)
    CGFloat width;
    /// Height of the viewport (in SVG coordinate space)
    CGFloat height;
} SVGViewport;

/// Create an SVGViewport from values
NS_INLINE SVGViewport SVGViewportMake(CGFloat x, CGFloat y, CGFloat width, CGFloat height) {
    SVGViewport v; v.x = x; v.y = y; v.width = width; v.height = height; return v;
}

/// Create an SVGViewport from a CGRect
NS_INLINE SVGViewport SVGViewportFromRect(CGRect rect) {
    return SVGViewportMake(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
}

/// Convert an SVGViewport to a CGRect
NS_INLINE CGRect SVGViewportToRect(SVGViewport viewport) {
    return CGRectMake(viewport.x, viewport.y, viewport.width, viewport.height);
}

/// Check if two viewports are equal
NS_INLINE BOOL SVGViewportEqualToViewport(SVGViewport v1, SVGViewport v2) {
    return v1.x == v2.x && v1.y == v2.y && v1.width == v2.width && v1.height == v2.height;
}

/// Zero viewport (invalid/unset)
extern const SVGViewport SVGViewportZero;

#pragma mark - Zoom Info Structure

/// Structure containing zoom/viewport change information
typedef struct {
    /// Previous viewport before the change
    SVGViewport previousViewport;
    /// New viewport after the change
    SVGViewport newViewport;
    /// Current zoom scale (1.0 = no zoom, 2.0 = 2x zoom, etc.)
    CGFloat zoomScale;
    /// Whether this was a user gesture (pinch) vs programmatic change
    BOOL isUserGesture;
    /// Center point of the zoom in view coordinates
    CGPoint zoomCenter;
} SVGZoomInfo;

#pragma mark - Preset View

/// A named preset view for quick viewport switching
/// Use presets to define points of interest in the SVG that users can jump to
@interface SVGPresetView : NSObject <NSCopying>

/// Unique identifier for this preset
@property (nonatomic, copy, readonly) NSString *identifier;

/// Display name for UI (optional)
@property (nonatomic, copy, nullable) NSString *displayName;

/// The viewport this preset represents
@property (nonatomic, assign, readonly) SVGViewport viewport;

/// Animation duration when transitioning to this preset (0 = instant)
@property (nonatomic, assign) NSTimeInterval transitionDuration;

/// Create a preset with identifier and viewport
+ (instancetype)presetWithIdentifier:(NSString *)identifier viewport:(SVGViewport)viewport;

/// Create a preset with identifier, viewport and display name
+ (instancetype)presetWithIdentifier:(NSString *)identifier
                            viewport:(SVGViewport)viewport
                         displayName:(nullable NSString *)displayName;

/// Create a preset from a rect (convenience)
+ (instancetype)presetWithIdentifier:(NSString *)identifier rect:(CGRect)rect;

@end

#pragma mark - Element Touch Types

/// Touch phase for element touch events
typedef NS_ENUM(NSInteger, SVGElementTouchPhase) {
    /// Touch began on the element
    SVGElementTouchPhaseBegan = 0,
    /// Touch moved while on the element
    SVGElementTouchPhaseMoved,
    /// Touch ended on the element
    SVGElementTouchPhaseEnded,
    /// Touch was cancelled
    SVGElementTouchPhaseCancelled,
    /// Touch entered the element bounds (while dragging)
    SVGElementTouchPhaseEntered,
    /// Touch exited the element bounds (while dragging)
    SVGElementTouchPhaseExited
};

/// Structure containing touch event information for SVG elements
typedef struct {
    /// The touch phase (began, moved, ended, cancelled, entered, exited)
    SVGElementTouchPhase phase;
    /// Touch location in view coordinates
    CGPoint locationInView;
    /// Touch location in SVG coordinate space
    CGPoint locationInSVG;
    /// Previous touch location in view coordinates (for moved events)
    CGPoint previousLocationInView;
    /// Previous touch location in SVG coordinates (for moved events)
    CGPoint previousLocationInSVG;
    /// Number of taps (1 for single tap, 2 for double tap, etc.)
    NSInteger tapCount;
    /// Timestamp of the touch event
    NSTimeInterval timestamp;
    /// Touch force (0.0-1.0 on devices with force touch, 0.0 otherwise)
    CGFloat force;
    /// Maximum possible force for this device
    CGFloat maximumPossibleForce;
} SVGElementTouchInfo;

/// Create an SVGElementTouchInfo with basic values
NS_INLINE SVGElementTouchInfo SVGElementTouchInfoMake(SVGElementTouchPhase phase,
                                                       CGPoint locationInView,
                                                       CGPoint locationInSVG,
                                                       NSInteger tapCount) {
    SVGElementTouchInfo info;
    info.phase = phase;
    info.locationInView = locationInView;
    info.locationInSVG = locationInSVG;
    info.previousLocationInView = locationInView;
    info.previousLocationInSVG = locationInSVG;
    info.tapCount = tapCount;
    info.timestamp = 0;
    info.force = 0;
    info.maximumPossibleForce = 0;
    return info;
}

#pragma mark - Dual Coordinate Point

/// A point in both view (screen) and SVG coordinate spaces
///
/// All element touch events provide coordinates in both systems:
/// - `viewPoint`: Standard UIKit screen coordinates (points)
/// - `svgPoint`: Coordinates in the SVG viewbox space
typedef struct {
    /// Location in view/screen coordinates (UIKit points)
    CGPoint viewPoint;
    /// Location in SVG viewbox coordinate space
    CGPoint svgPoint;
} SVGDualPoint;

/// Create an SVGDualPoint from view and SVG coordinates
NS_INLINE SVGDualPoint SVGDualPointMake(CGPoint viewPoint, CGPoint svgPoint) {
    SVGDualPoint p; p.viewPoint = viewPoint; p.svgPoint = svgPoint; return p;
}

/// Zero dual point
NS_INLINE SVGDualPoint SVGDualPointZero(void) {
    return SVGDualPointMake(CGPointZero, CGPointZero);
}

#pragma mark - Delegate Protocol

@class SVGPlayerView;

/// Delegate protocol for SVGPlayerView events
///
/// Implement these methods to respond to player events and update your custom UI.
/// All delegate methods are called on the main thread.
@protocol SVGPlayerViewDelegate <NSObject>
@optional

#pragma mark Playback State Events

/// Called when animation playback completes (non-looping mode only)
/// @param playerView The player view that finished playing
- (void)svgPlayerViewDidFinishPlaying:(SVGPlayerView *)playerView;

/// Called when playback state changes
/// @param playerView The player view
/// @param state The new playback state
- (void)svgPlayerView:(SVGPlayerView *)playerView didChangePlaybackState:(SVGViewPlaybackState)state;

/// Called when the player is ready to play (SVG loaded successfully)
/// @param playerView The player view that is ready
- (void)svgPlayerViewDidBecomeReadyToPlay:(SVGPlayerView *)playerView;

/// Called when a loop iteration completes (in loop mode)
/// @param playerView The player view
/// @param loopCount The number of loops completed so far
- (void)svgPlayerView:(SVGPlayerView *)playerView didCompleteLoopIteration:(NSInteger)loopCount;

#pragma mark Timeline Events

/// Called periodically during playback with timeline information
/// Use this to update timeline UI elements (elapsed time, progress bar, etc.)
/// @param playerView The player view
/// @param timelineInfo Current timeline information
/// @note This is called on every frame. Keep your implementation lightweight.
- (void)svgPlayerView:(SVGPlayerView *)playerView didUpdateTimeline:(SVGTimelineInfo)timelineInfo;

/// Called when seeking begins (scrubbing started)
/// @param playerView The player view
- (void)svgPlayerViewDidBeginSeeking:(SVGPlayerView *)playerView;

/// Called when seeking ends (scrubbing finished)
/// @param playerView The player view
/// @param time The final seek position in seconds
- (void)svgPlayerView:(SVGPlayerView *)playerView didEndSeekingAtTime:(NSTimeInterval)time;

#pragma mark Frame Events

/// Called on each frame render (use sparingly, performance sensitive)
/// @param playerView The player view that rendered a frame
/// @param time The current animation time in seconds
- (void)svgPlayerView:(SVGPlayerView *)playerView didRenderFrameAtTime:(NSTimeInterval)time;

#pragma mark Error Events

/// Called when an SVG file fails to load
/// @param playerView The player view that encountered an error
/// @param error The error that occurred
- (void)svgPlayerView:(SVGPlayerView *)playerView didFailWithError:(NSError *)error;

#pragma mark Display Events

/// Called when fullscreen mode changes
/// @param playerView The player view
/// @param isFullscreen Whether the player is now in fullscreen mode
- (void)svgPlayerView:(SVGPlayerView *)playerView didChangeFullscreenMode:(BOOL)isFullscreen;

/// Called when orientation lock state changes
/// @param playerView The player view
/// @param isLocked Whether orientation is now locked
- (void)svgPlayerView:(SVGPlayerView *)playerView didChangeOrientationLock:(BOOL)isLocked;

#pragma mark Reset Events

/// Called when playback is reset to the start frame
/// @param playerView The player view that was reset
/// @note This is called when stop() is invoked or when looping back to start
- (void)svgPlayerViewDidResetToStart:(SVGPlayerView *)playerView;

/// Called when the player is paused
/// @param playerView The player view that was paused
/// @note For more context, use didChangePlaybackState: instead
- (void)svgPlayerViewDidPause:(SVGPlayerView *)playerView;

#pragma mark Processing Events

/// Called when SVG processing is complete and the player is ready for scrubbing
/// @param playerView The player view
/// @note SVG parsing can be time-consuming. This event indicates when scrubbing is safe.
///       This is distinct from didBecomeReadyToPlay which fires earlier.
- (void)svgPlayerViewDidBecomeReadyForScrubbing:(SVGPlayerView *)playerView;

/// Called periodically during SVG loading/processing with progress
/// @param playerView The player view
/// @param progress Loading progress from 0.0 to 1.0
- (void)svgPlayerView:(SVGPlayerView *)playerView loadingProgress:(CGFloat)progress;

#pragma mark Viewport/Zoom Events

/// Called when the viewport changes (programmatic or user gesture)
/// @param playerView The player view
/// @param zoomInfo Information about the viewport change
- (void)svgPlayerView:(SVGPlayerView *)playerView didChangeViewport:(SVGZoomInfo)zoomInfo;

/// Called when user performs a pinch-to-zoom gesture
/// @param playerView The player view
/// @param zoomInfo Information about the zoom gesture
- (void)svgPlayerView:(SVGPlayerView *)playerView didZoom:(SVGZoomInfo)zoomInfo;

/// Called when user performs a pan gesture while zoomed
/// @param playerView The player view
/// @param translation The translation in view coordinates
- (void)svgPlayerView:(SVGPlayerView *)playerView didPan:(CGPoint)translation;

/// Called when zoom is reset to default (1.0x, full SVG visible)
/// @param playerView The player view
- (void)svgPlayerViewDidResetZoom:(SVGPlayerView *)playerView;

/// Called when a preset view transition begins
/// @param playerView The player view
/// @param preset The preset being transitioned to
- (void)svgPlayerView:(SVGPlayerView *)playerView willTransitionToPreset:(SVGPresetView *)preset;

/// Called when a preset view transition completes
/// @param playerView The player view
/// @param preset The preset that was transitioned to
- (void)svgPlayerView:(SVGPlayerView *)playerView didTransitionToPreset:(SVGPresetView *)preset;

#pragma mark Element Touch Events

/// Called when a subscribed SVG element is tapped (touch + release, no drag)
///
/// MUTUALLY EXCLUSIVE: If the user drags, this will NOT fire - didDrag/didDrop fire instead.
/// Also mutually exclusive with double-tap: if user double-taps, only didDoubleTap fires.
/// A tap is a quick touch and release without significant finger movement.
///
/// @param playerView The player view
/// @param objectID The objectID of the tapped element
/// @param location The tap location in both view and SVG coordinates
- (void)svgPlayerView:(SVGPlayerView *)playerView
  didTapElementWithID:(NSString *)objectID
           atLocation:(SVGDualPoint)location;

/// Called when a subscribed SVG element is double-tapped
///
/// MUTUALLY EXCLUSIVE: If this fires, single tap will NOT fire.
/// iOS automatically delays single-tap recognition to detect double-taps.
///
/// @param playerView The player view
/// @param objectID The objectID of the double-tapped element
/// @param location The double-tap location in both view and SVG coordinates
- (void)svgPlayerView:(SVGPlayerView *)playerView
  didDoubleTapElementWithID:(NSString *)objectID
                 atLocation:(SVGDualPoint)location;

/// Called when a subscribed SVG element receives a long press
///
/// Long press fires after holding without movement for `longPressDuration` seconds.
///
/// @param playerView The player view
/// @param objectID The objectID of the long-pressed element
/// @param location The long press location in both view and SVG coordinates
- (void)svgPlayerView:(SVGPlayerView *)playerView
  didLongPressElementWithID:(NSString *)objectID
                 atLocation:(SVGDualPoint)location;

/// Called when a subscribed SVG element is being dragged
///
/// MUTUALLY EXCLUSIVE with tap: If this fires, didTap will NOT fire.
/// This fires continuously while the user drags on the element.
/// When the finger lifts, didDrop fires with the final location.
///
/// @param playerView The player view
/// @param objectID The objectID of the dragged element
/// @param currentLocation The current drag location in both view and SVG coordinates
/// @param translation The total translation from the drag start point in both coordinate systems
- (void)svgPlayerView:(SVGPlayerView *)playerView
  didDragElementWithID:(NSString *)objectID
       currentLocation:(SVGDualPoint)currentLocation
           translation:(SVGDualPoint)translation;

/// Called when a drag ends (finger lifted after dragging)
///
/// This is the counterpart to didDrag - always fires after a drag sequence ends.
/// Contains the final drop location where the finger was lifted.
///
/// @param playerView The player view
/// @param objectID The objectID of the element that was dragged
/// @param location The final location where the finger lifted in both view and SVG coordinates
/// @param totalTranslation The total translation from drag start to drop in both coordinate systems
- (void)svgPlayerView:(SVGPlayerView *)playerView
  didDropElementWithID:(NSString *)objectID
            atLocation:(SVGDualPoint)location
      totalTranslation:(SVGDualPoint)totalTranslation;

#pragma mark Element Touch Events (Detailed - Optional)

/// Called with full touch details (optional - for advanced use cases)
/// @param playerView The player view
/// @param objectID The objectID of the touched element
/// @param touchInfo Structure containing touch details (phase, location, tap count, etc.)
- (void)svgPlayerView:(SVGPlayerView *)playerView
      didTouchElement:(NSString *)objectID
            touchInfo:(SVGElementTouchInfo)touchInfo;

/// Called when touch enters element bounds during drag (optional)
/// @param playerView The player view
/// @param objectID The objectID of the element entered
- (void)svgPlayerView:(SVGPlayerView *)playerView didEnterElementWithID:(NSString *)objectID;

/// Called when touch exits element bounds during drag (optional)
/// @param playerView The player view
/// @param objectID The objectID of the element exited
- (void)svgPlayerView:(SVGPlayerView *)playerView didExitElementWithID:(NSString *)objectID;

@end

#pragma mark - SVGPlayerView

/// A UIView subclass that renders animated SVG files using Skia with Metal acceleration.
///
/// This view provides a complete API for building custom video player UIs:
/// - Playback controls (play, pause, stop, resume, rewind, fast forward)
/// - Timeline scrubbing and seeking
/// - Playback modes (loop, once, reverse)
/// - Frame stepping
/// - Fullscreen and rotation lock
/// - Comprehensive delegate callbacks for UI updates
///
/// All rendering is performed at the native Retina resolution of the device
/// for crisp, pixel-perfect SVG display.
///
/// Usage in Interface Builder:
/// 1. Drag a UIView onto your storyboard
/// 2. Set the Custom Class to "SVGPlayerView"
/// 3. Configure properties in the Attributes Inspector
/// 4. Connect your UI controls to the playback methods
///
/// Usage in Code (Swift):
/// ```swift
/// let player = SVGPlayerView(frame: bounds, svgFileName: "animation")
/// player.delegate = self
/// view.addSubview(player)
///
/// // Connect to your custom UI
/// playButton.addTarget(player, action: #selector(SVGPlayerView.togglePlayback), for: .touchUpInside)
/// ```
IB_DESIGNABLE
@interface SVGPlayerView : UIView

#pragma mark - IBInspectable Properties

/// The name of the SVG file to load from the app bundle (without .svg extension)
/// Example: "animation" will load "animation.svg" from the main bundle
@property (nonatomic, copy, nullable) IBInspectable NSString *svgFileName;

/// Whether to automatically start playback when the view appears
/// Default: YES
@property (nonatomic, assign) IBInspectable BOOL autoPlay;

/// Whether the animation should loop continuously
/// Default: YES
/// Note: For more control, use repeatMode property instead
@property (nonatomic, assign) IBInspectable BOOL loop;

/// Background color shown behind the SVG content
/// Note: This is an alias for the standard backgroundColor property
@property (nonatomic, strong, nullable) IBInspectable UIColor *svgBackgroundColor;

/// Playback speed multiplier (1.0 = normal speed)
/// Range: 0.1 to 10.0
/// Default: 1.0
@property (nonatomic, assign) IBInspectable CGFloat playbackSpeed;

#pragma mark - Playback Mode Properties

/// Repeat mode for animation playback
/// Default: SVGRepeatModeLoop (when loop=YES) or SVGRepeatModeNone (when loop=NO)
@property (nonatomic, assign) SVGRepeatMode repeatMode;

/// Number of times to repeat when repeatMode is SVGRepeatModeCount
/// Default: 1
@property (nonatomic, assign) NSInteger repeatCount;

/// Number of completed repeat iterations (readonly)
@property (nonatomic, readonly) NSInteger currentRepeatIteration;

/// Total number of loops completed since playback started
/// Resets to 0 on stop or when loading new SVG
@property (nonatomic, readonly) NSInteger loopCount;

/// Whether the player is ready for interactive scrubbing
/// YES after SVG is loaded and parsed; NO when unloaded
@property (nonatomic, readonly, getter=isReadyForScrubbing) BOOL readyForScrubbing;

/// Whether playback direction is currently forward (for ping-pong mode)
@property (nonatomic, readonly, getter=isPlayingForward) BOOL playingForward;

#pragma mark - Runtime Properties (Readonly State)

/// Content mode for SVG rendering (how the SVG fits within bounds)
@property (nonatomic, assign) SVGContentMode svgContentMode;

/// Current playback state (readonly)
@property (nonatomic, readonly) SVGViewPlaybackState playbackState;

/// The intrinsic size of the loaded SVG (CGSizeZero if no SVG loaded)
@property (nonatomic, readonly) CGSize intrinsicSVGSize;

/// Total duration of the animation in seconds (0 if no animation or static SVG)
@property (nonatomic, readonly) NSTimeInterval duration;

/// Current playback time in seconds
@property (nonatomic, readonly) NSTimeInterval currentTime;

/// Elapsed time since playback start (same as currentTime)
/// Provided for UI consistency
@property (nonatomic, readonly) NSTimeInterval elapsedTime;

/// Remaining time until animation ends
@property (nonatomic, readonly) NSTimeInterval remainingTime;

/// Current playback progress (0.0 to 1.0)
/// Useful for binding to progress bars/sliders
@property (nonatomic, readonly) CGFloat progress;

/// Current frame number (for frame-based animations)
@property (nonatomic, readonly) NSInteger currentFrame;

/// Total number of frames (for frame-based animations)
@property (nonatomic, readonly) NSInteger totalFrames;

/// Current rendering FPS (frames per second)
@property (nonatomic, readonly) CGFloat currentFPS;

/// Get full timeline info in a single call (more efficient for UI updates)
@property (nonatomic, readonly) SVGTimelineInfo timelineInfo;

/// Delegate for receiving playback events
@property (nonatomic, weak, nullable) IBOutlet id<SVGPlayerViewDelegate> delegate;

/// Check if an SVG is currently loaded
@property (nonatomic, readonly, getter=isLoaded) BOOL loaded;

/// Check if the player is ready to play (SVG loaded and initialized)
@property (nonatomic, readonly, getter=isReadyToPlay) BOOL readyToPlay;

/// Check if the view is currently playing
@property (nonatomic, readonly, getter=isPlaying) BOOL playing;

/// Check if the view is paused
@property (nonatomic, readonly, getter=isPaused) BOOL paused;

/// Check if the view is stopped
@property (nonatomic, readonly, getter=isStopped) BOOL stopped;

/// Check if user is currently scrubbing/seeking
@property (nonatomic, readonly, getter=isSeeking) BOOL seeking;

/// Get the last error that occurred (nil if no error)
@property (nonatomic, readonly, nullable) NSError *lastError;

#pragma mark - Display Mode Properties

/// Whether the player is in fullscreen mode
@property (nonatomic, assign, getter=isFullscreen) BOOL fullscreen;

/// Whether device orientation is locked during playback
@property (nonatomic, assign, getter=isOrientationLocked) BOOL orientationLocked;

/// The preferred orientation when orientation is locked
/// Default: UIInterfaceOrientationMaskAll
@property (nonatomic, assign) UIInterfaceOrientationMask preferredOrientation;

/// The display scale factor (readonly - automatically detected from device)
/// This is the Retina multiplier (1.0, 2.0, 3.0, etc.)
@property (nonatomic, readonly) CGFloat displayScale;

/// The actual pixel dimensions being rendered (readonly)
/// This is bounds.size * displayScale for true native resolution rendering
@property (nonatomic, readonly) CGSize renderPixelSize;

#pragma mark - Initialization

/// Initialize with a frame (use loadSVGNamed: to load content)
/// @param frame The frame rectangle for the view
- (instancetype)initWithFrame:(CGRect)frame NS_DESIGNATED_INITIALIZER;

/// Initialize from Interface Builder
/// @param coder The coder to decode from
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_DESIGNATED_INITIALIZER;

/// Initialize with a frame and immediately load an SVG file
/// @param frame The frame rectangle for the view
/// @param svgFileName Name of SVG file in bundle (without .svg extension)
- (instancetype)initWithFrame:(CGRect)frame svgFileName:(nullable NSString *)svgFileName;

#pragma mark - Loading

/// Load an SVG file from the app bundle
/// @param fileName Name of the SVG file (without .svg extension)
/// @return YES if loading succeeded, NO otherwise (check lastError for details)
- (BOOL)loadSVGNamed:(NSString *)fileName;

/// Load an SVG file from a file path
/// @param filePath Full path to the SVG file
/// @return YES if loading succeeded, NO otherwise (check lastError for details)
- (BOOL)loadSVGFromPath:(NSString *)filePath;

/// Load an SVG from raw data
/// @param data The SVG file data (must be valid SVG XML)
/// @return YES if loading succeeded, NO otherwise (check lastError for details)
- (BOOL)loadSVGFromData:(NSData *)data;

/// Unload the current SVG and free resources
- (void)unloadSVG;

#pragma mark - Basic Playback Control

/// Start playback from current position
/// If stopped, starts from beginning. If paused, resumes from current position.
- (void)play;

/// Pause playback at current position
/// Call play to resume from where you left off.
- (void)pause;

/// Resume playback (alias for play when paused)
/// This is provided for UI clarity - functionally identical to play when paused.
- (void)resume;

/// Stop playback and reset to the beginning
- (void)stop;

/// Toggle between playing and paused states
/// Use this for a single play/pause button in your UI.
- (void)togglePlayback;

#pragma mark - Navigation Control

/// Jump to the first frame (beginning of animation)
- (void)goToStart;

/// Jump to the last frame (end of animation)
- (void)goToEnd;

/// Rewind by a specified amount of time
/// @param seconds Number of seconds to rewind (default 5 seconds if 0)
- (void)rewindBySeconds:(NSTimeInterval)seconds;

/// Fast forward by a specified amount of time
/// @param seconds Number of seconds to fast forward (default 5 seconds if 0)
- (void)fastForwardBySeconds:(NSTimeInterval)seconds;

/// Rewind by 5 seconds (convenience method)
- (void)rewind;

/// Fast forward by 5 seconds (convenience method)
- (void)fastForward;

/// Step forward by one frame (for frame-by-frame navigation)
/// Automatically pauses playback.
- (void)stepForward;

/// Step backward by one frame (for frame-by-frame navigation)
/// Automatically pauses playback.
- (void)stepBackward;

/// Step forward by a specified number of frames
/// @param count Number of frames to step (negative values step backward)
- (void)stepByFrames:(NSInteger)count;

#pragma mark - Seeking / Scrubbing

/// Seek to a specific time in the animation
/// @param time Time in seconds to seek to (clamped to valid range)
- (void)seekToTime:(NSTimeInterval)time;

/// Seek to a specific frame (for frame-based animations)
/// @param frame Frame number to seek to (0-indexed, clamped to valid range)
- (void)seekToFrame:(NSInteger)frame;

/// Seek to a progress position
/// @param progress Progress value from 0.0 (start) to 1.0 (end)
- (void)seekToProgress:(CGFloat)progress;

/// Begin scrubbing (call when user starts dragging timeline)
/// This pauses playback and prepares for interactive seeking.
- (void)beginScrubbing;

/// Update scrubbing position (call during timeline drag)
/// @param progress Progress value from 0.0 to 1.0
- (void)scrubToProgress:(CGFloat)progress;

/// End scrubbing (call when user finishes dragging timeline)
/// @param shouldResume Whether to resume playback after scrubbing
- (void)endScrubbingAndResume:(BOOL)shouldResume;

#pragma mark - Playback Rate Control

/// Set the playback rate (speed multiplier)
/// @param rate Rate multiplier (1.0 = normal, 2.0 = 2x speed, 0.5 = half speed)
/// @note Rate is clamped to 0.1 - 10.0 range
- (void)setPlaybackRate:(CGFloat)rate;

/// Get current playback rate
/// @return Current rate multiplier
- (CGFloat)playbackRate;

/// Reset playback rate to normal speed (1.0x)
- (void)resetPlaybackRate;

#pragma mark - Display Mode Control

/// Enter fullscreen mode
/// @param animated Whether to animate the transition
- (void)enterFullscreenAnimated:(BOOL)animated;

/// Exit fullscreen mode
/// @param animated Whether to animate the transition
- (void)exitFullscreenAnimated:(BOOL)animated;

/// Toggle fullscreen mode
/// @param animated Whether to animate the transition
- (void)toggleFullscreenAnimated:(BOOL)animated;

/// Lock the current device orientation
- (void)lockOrientation;

/// Unlock device orientation
- (void)unlockOrientation;

/// Lock to a specific orientation
/// @param orientation The orientation mask to lock to
- (void)lockToOrientation:(UIInterfaceOrientationMask)orientation;

#pragma mark - Viewport/Zoom Control

/// Whether pinch-to-zoom gesture is enabled
/// Default: NO (disable for video-only use, enable for interactive content)
@property (nonatomic, assign, getter=isPinchToZoomEnabled) BOOL pinchToZoomEnabled;

/// Whether pan gesture is enabled when zoomed
/// Default: YES (only active when zoom scale > 1.0)
@property (nonatomic, assign, getter=isPanEnabled) BOOL panEnabled;

/// Minimum zoom scale (default: 1.0 = full SVG visible)
@property (nonatomic, assign) CGFloat minimumZoomScale;

/// Maximum zoom scale (default: 10.0 = 10x zoom)
@property (nonatomic, assign) CGFloat maximumZoomScale;

/// Current zoom scale (1.0 = no zoom)
@property (nonatomic, readonly) CGFloat zoomScale;

/// Current viewport in SVG coordinate space
/// This represents the visible portion of the SVG
@property (nonatomic, readonly) SVGViewport currentViewport;

/// The full/default viewport showing the entire SVG
/// Use this to reset zoom or as reference for preset calculations
@property (nonatomic, readonly) SVGViewport defaultViewport;

/// Whether the view is currently zoomed (zoomScale > 1.0)
@property (nonatomic, readonly, getter=isZoomed) BOOL zoomed;

/// Set the viewport to display a specific region of the SVG
/// @param viewport The viewport in SVG coordinate space
/// @param animated Whether to animate the transition
- (void)setViewport:(SVGViewport)viewport animated:(BOOL)animated;

/// Set the viewport using a CGRect (convenience method)
/// @param rect The rect in SVG coordinate space
/// @param animated Whether to animate the transition
- (void)setViewportRect:(CGRect)rect animated:(BOOL)animated;

/// Zoom to a specific scale at the center of the view
/// @param scale The zoom scale (1.0 = no zoom)
/// @param animated Whether to animate the zoom
- (void)zoomToScale:(CGFloat)scale animated:(BOOL)animated;

/// Zoom to a specific scale centered on a point
/// @param scale The zoom scale (1.0 = no zoom)
/// @param center The center point in view coordinates
/// @param animated Whether to animate the zoom
- (void)zoomToScale:(CGFloat)scale centeredAt:(CGPoint)center animated:(BOOL)animated;

/// Zoom to fit a specific rect from the SVG in the view
/// @param rect The rect in SVG coordinate space to fit
/// @param animated Whether to animate the zoom
- (void)zoomToRect:(CGRect)rect animated:(BOOL)animated;

/// Zoom in by a factor (default 2x)
/// @param animated Whether to animate the zoom
- (void)zoomInAnimated:(BOOL)animated;

/// Zoom out by a factor (default 0.5x)
/// @param animated Whether to animate the zoom
- (void)zoomOutAnimated:(BOOL)animated;

/// Reset zoom to default (1.0x, full SVG visible)
/// @param animated Whether to animate the reset
- (void)resetZoomAnimated:(BOOL)animated;

/// Convert a point from view coordinates to SVG coordinates
/// @param point The point in view coordinates
/// @return The corresponding point in SVG coordinate space
- (CGPoint)convertPointToSVGCoordinates:(CGPoint)point;

/// Convert a point from SVG coordinates to view coordinates
/// @param point The point in SVG coordinates
/// @return The corresponding point in view coordinate space
- (CGPoint)convertPointFromSVGCoordinates:(CGPoint)point;

/// Convert a rect from view coordinates to SVG coordinates
/// @param rect The rect in view coordinates
/// @return The corresponding rect in SVG coordinate space
- (CGRect)convertRectToSVGCoordinates:(CGRect)rect;

/// Convert a rect from SVG coordinates to view coordinates
/// @param rect The rect in SVG coordinates
/// @return The corresponding rect in view coordinate space
- (CGRect)convertRectFromSVGCoordinates:(CGRect)rect;

#pragma mark - Preset Views

/// All registered preset views
@property (nonatomic, readonly) NSArray<SVGPresetView *> *presetViews;

/// Register a preset view for quick navigation
/// @param preset The preset to register
- (void)registerPresetView:(SVGPresetView *)preset;

/// Register multiple preset views
/// @param presets Array of presets to register
- (void)registerPresetViews:(NSArray<SVGPresetView *> *)presets;

/// Unregister a preset view
/// @param identifier The identifier of the preset to remove
- (void)unregisterPresetViewWithIdentifier:(NSString *)identifier;

/// Unregister all preset views
- (void)unregisterAllPresetViews;

/// Get a preset view by identifier
/// @param identifier The identifier of the preset
/// @return The preset view, or nil if not found
- (nullable SVGPresetView *)presetViewWithIdentifier:(NSString *)identifier;

/// Transition to a preset view
/// @param preset The preset to transition to
/// @param animated Whether to animate the transition (uses preset's transitionDuration)
- (void)transitionToPreset:(SVGPresetView *)preset animated:(BOOL)animated;

/// Transition to a preset view by identifier
/// @param identifier The identifier of the preset
/// @param animated Whether to animate the transition
/// @return YES if preset was found and transition started
- (BOOL)transitionToPresetWithIdentifier:(NSString *)identifier animated:(BOOL)animated;

/// Transition to the default view (full SVG, reset zoom)
/// @param animated Whether to animate the transition
- (void)transitionToDefaultViewAnimated:(BOOL)animated;

#pragma mark - Interactive Tap-to-Zoom

/// Whether tap-to-zoom is enabled
/// When enabled, single tap at a point will zoom to that location
/// Default: NO
@property (nonatomic, assign, getter=isTapToZoomEnabled) BOOL tapToZoomEnabled;

/// Zoom scale used when tapping to zoom
/// Default: 2.0 (2x zoom on tap)
@property (nonatomic, assign) CGFloat tapToZoomScale;

/// Whether double-tap resets zoom (when already zoomed)
/// Default: YES
@property (nonatomic, assign) BOOL doubleTapResetsZoom;

/// Handle a tap at a specific point (for custom gesture handling)
/// @param point The tap point in view coordinates
/// @param animated Whether to animate the zoom
/// @note Use this if you're handling taps yourself instead of enabling tapToZoomEnabled
- (void)handleTapAtPoint:(CGPoint)point animated:(BOOL)animated;

/// Handle a double-tap at a specific point
/// @param point The double-tap point in view coordinates
/// @param animated Whether to animate
- (void)handleDoubleTapAtPoint:(CGPoint)point animated:(BOOL)animated;

#pragma mark - Element Touch Subscription

/// Subscribe to touch events for a specific SVG element by its objectID
///
/// When subscribed, any touch on the element triggers delegate callbacks.
/// Just pass the objectID string matching the element's id attribute in the SVG.
///
/// @param objectID The id attribute of the SVG element (e.g., "button1", "playIcon")
- (void)subscribeToTouchEventsForObjectID:(NSString *)objectID;

/// Unsubscribe from touch events for a specific objectID
/// @param objectID The objectID to unsubscribe
- (void)unsubscribeFromTouchEventsForObjectID:(NSString *)objectID;

/// Unsubscribe from all element touch events
- (void)unsubscribeFromAllElementTouchEvents;

/// Array of currently subscribed objectIDs (readonly)
@property (nonatomic, readonly) NSArray<NSString *> *subscribedObjectIDs;

#pragma mark - Element Touch Subscription (Optional Configuration)

/// Whether element touch tracking is enabled (default: YES)
@property (nonatomic, assign, getter=isElementTouchTrackingEnabled) BOOL elementTouchTrackingEnabled;

/// Minimum duration for long press recognition (default: 0.5 seconds)
@property (nonatomic, assign) NSTimeInterval longPressDuration;

/// Subscribe to multiple objectIDs at once
/// @param objectIDs Array of objectID strings to subscribe
- (void)subscribeToTouchEventsForObjectIDs:(NSArray<NSString *> *)objectIDs;

/// Unsubscribe from multiple objectIDs at once
/// @param objectIDs Array of objectID strings to unsubscribe
- (void)unsubscribeFromTouchEventsForObjectIDs:(NSArray<NSString *> *)objectIDs;

/// Check if an objectID is currently subscribed
/// @param objectID The objectID to check
/// @return YES if subscribed, NO otherwise
- (BOOL)isSubscribedToObjectID:(NSString *)objectID;

#pragma mark - Element Hit Testing (Optional - Advanced)

/// Check if a point hits any subscribed element
/// @param point The point in view coordinates
/// @return The objectID of the hit element, or nil if none hit
- (nullable NSString *)hitTestSubscribedElementAtPoint:(CGPoint)point;

/// Get all subscribed elements hit at a point (in z-order, topmost first)
/// @param point The point in view coordinates
/// @return Array of objectIDs for all subscribed elements at that point
- (NSArray<NSString *> *)hitTestAllSubscribedElementsAtPoint:(CGPoint)point;

/// Check if a specific objectID element contains a point
/// @param objectID The objectID of the element to test
/// @param point The point in view coordinates
/// @return YES if the element contains the point, NO otherwise
- (BOOL)elementWithObjectID:(NSString *)objectID containsPoint:(CGPoint)point;

/// Get the bounding rect of an element by objectID (in view coordinates)
/// @param objectID The objectID of the element
/// @return The bounding rect, or CGRectNull if not found
- (CGRect)boundingRectForObjectID:(NSString *)objectID;

/// Get the bounding rect of an element in SVG coordinates
/// @param objectID The objectID of the element
/// @return The bounding rect in SVG coordinates, or CGRectNull if not found
- (CGRect)svgBoundingRectForObjectID:(NSString *)objectID;

#pragma mark - Rendering

/// Force an immediate re-render of the current frame
- (void)setNeedsRender;

/// Capture the current frame as a UIImage
/// @return UIImage of the current frame at native resolution, or nil if no SVG is loaded
- (nullable UIImage *)captureCurrentFrame;

/// Capture a specific frame as a UIImage
/// @param time Time in seconds to capture
/// @return UIImage of the specified frame at native resolution, or nil if no SVG is loaded
- (nullable UIImage *)captureFrameAtTime:(NSTimeInterval)time;

/// Capture a frame at specific resolution
/// @param time Time in seconds to capture
/// @param size Desired output size in points
/// @param scale Scale factor for the output (0 = use screen scale)
/// @return UIImage at the specified size and scale
- (nullable UIImage *)captureFrameAtTime:(NSTimeInterval)time
                                    size:(CGSize)size
                                   scale:(CGFloat)scale;

#pragma mark - Formatted Time Strings (for UI Display)

/// Get formatted elapsed time string (e.g., "01:23")
/// @return Formatted string in MM:SS or HH:MM:SS format
- (NSString *)formattedElapsedTime;

/// Get formatted remaining time string (e.g., "-01:23")
/// @return Formatted string with leading minus sign
- (NSString *)formattedRemainingTime;

/// Get formatted duration string (e.g., "02:45")
/// @return Formatted string in MM:SS or HH:MM:SS format
- (NSString *)formattedDuration;

/// Get formatted time for a specific value
/// @param time Time in seconds
/// @return Formatted string in MM:SS or HH:MM:SS format
+ (NSString *)formatTime:(NSTimeInterval)time;

#pragma mark - Frame/Time Conversion Utilities

/// Get the frame number corresponding to a specific time
/// @param time Time in seconds from the start of the animation
/// @return Frame number (0-indexed), or 0 if no SVG is loaded
/// @note Frame numbers are clamped to valid range [0, totalFrames-1]
- (NSInteger)frameForTime:(NSTimeInterval)time;

/// Get the time in seconds corresponding to a specific frame number
/// @param frame Frame number (0-indexed)
/// @return Time in seconds from the start, or 0 if no SVG is loaded
/// @note Frame numbers are clamped to valid range [0, totalFrames-1]
- (NSTimeInterval)timeForFrame:(NSInteger)frame;

/// Get the duration of a single frame in seconds
/// @return Duration of one frame in seconds (1.0 / frameRate)
/// @note Returns 0 if no SVG is loaded or animation has no frames
@property (nonatomic, readonly) NSTimeInterval frameDuration;

/// Get the frame rate of the animation in frames per second
/// @return Frame rate in fps (typically 24, 30, or 60)
/// @note Returns 60 for static SVGs, actual rate for animations
@property (nonatomic, readonly) CGFloat frameRate;

/// Check if a frame number is valid for the current animation
/// @param frame Frame number to validate (0-indexed)
/// @return YES if the frame is within valid range [0, totalFrames-1]
- (BOOL)isValidFrame:(NSInteger)frame;

/// Check if a time value is valid for the current animation
/// @param time Time in seconds to validate
/// @return YES if the time is within valid range [0, duration]
- (BOOL)isValidTime:(NSTimeInterval)time;

/// Get frame info for a specific frame number
/// @param frame Frame number (0-indexed)
/// @return Dictionary with frame details (frameNumber, timeInSeconds, progress, isFirstFrame, isLastFrame)
/// @note Returns nil if frame is invalid or no SVG is loaded
- (nullable NSDictionary<NSString *, id> *)infoForFrame:(NSInteger)frame;

/// Get frame info for a specific time
/// @param time Time in seconds
/// @return Dictionary with frame details (frameNumber, timeInSeconds, progress, isFirstFrame, isLastFrame)
/// @note Returns nil if time is invalid or no SVG is loaded
- (nullable NSDictionary<NSString *, id> *)infoForTime:(NSTimeInterval)time;

/// Get the current playback position as a frame info dictionary
/// @return Dictionary with current frame details, or nil if no SVG is loaded
/// @note This provides a snapshot of the current position regardless of playback state
- (nullable NSDictionary<NSString *, id> *)currentFrameInfo;

@end

NS_ASSUME_NONNULL_END
