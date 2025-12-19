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

@end

NS_ASSUME_NONNULL_END
