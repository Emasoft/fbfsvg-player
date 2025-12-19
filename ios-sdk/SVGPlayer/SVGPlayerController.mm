// SVGPlayerController.mm - Objective-C wrapper around the C SVG player API
//
// This implementation bridges the C API (svg_player_ios.h) to Objective-C,
// providing a clean interface for SVGPlayerView and direct users.
//
// Many methods are stubs awaiting implementation in the shared C++ core.
// These stubs provide the API surface for developers to build custom UIs.

#import "SVGPlayerController.h"
#import "svg_player_ios.h"

// Error domain for SVGPlayerController errors
NSString * const SVGPlayerControllerErrorDomain = @"com.svgplayer.controller.error";

// Default frame rate for static SVGs or when not specified
static const CGFloat kDefaultFrameRate = 60.0;

// Default seek interval for rewind/fastforward
static const NSTimeInterval kDefaultSeekInterval = 5.0;

@interface SVGPlayerController ()
// The underlying C API handle
@property (nonatomic, assign) SVGPlayerHandle handle;
// Current playback state
@property (nonatomic, assign) SVGControllerPlaybackState internalPlaybackState;
// Cached error message
@property (nonatomic, copy, nullable) NSString *internalErrorMessage;
// Playback rate multiplier
@property (nonatomic, assign) CGFloat internalPlaybackRate;
// Repeat mode
@property (nonatomic, assign) SVGControllerRepeatMode internalRepeatMode;
// Repeat count for counted mode
@property (nonatomic, assign) NSInteger internalRepeatCount;
// Current repeat iteration
@property (nonatomic, assign) NSInteger internalCurrentRepeatIteration;
// Whether playing forward (for ping-pong)
@property (nonatomic, assign) BOOL internalPlayingForward;
// Scrubbing state
@property (nonatomic, assign) BOOL internalScrubbing;
// Playback state before scrubbing (for restoration)
@property (nonatomic, assign) SVGControllerPlaybackState stateBeforeScrubbing;
@end

@implementation SVGPlayerController

#pragma mark - Initialization

+ (instancetype)controller {
    return [[self alloc] init];
}

- (instancetype)init {
    if (self = [super init]) {
        _handle = SVGPlayer_Create();
        _internalPlaybackState = SVGControllerPlaybackStateStopped;
        _looping = YES;
        _internalPlaybackRate = 1.0;
        _internalRepeatMode = SVGControllerRepeatModeLoop;
        _internalRepeatCount = 1;
        _internalCurrentRepeatIteration = 0;
        _internalPlayingForward = YES;
        _internalScrubbing = NO;

        if (_handle) {
            SVGPlayer_SetLooping(_handle, YES);
        }
    }
    return self;
}

- (void)dealloc {
    if (_handle) {
        SVGPlayer_Destroy(_handle);
        _handle = NULL;
    }
}

#pragma mark - Loading

- (BOOL)loadSVGFromPath:(NSString *)path error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:SVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return NO;
    }

    // Check if file exists
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        NSString *message = [NSString stringWithFormat:@"SVG file not found: %@", path];
        [self setError:error code:SVGPlayerControllerErrorFileNotFound message:message];
        return NO;
    }

    // Load the SVG
    BOOL success = SVGPlayer_LoadSVG(self.handle, [path UTF8String]);

    if (!success) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to load SVG";
        self.internalErrorMessage = message;
        [self setError:error code:SVGPlayerControllerErrorParseFailed message:message];
        return NO;
    }

    // Apply settings
    SVGPlayer_SetLooping(self.handle, self.looping);
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.internalErrorMessage = nil;

    return YES;
}

- (BOOL)loadSVGFromData:(NSData *)data error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:SVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return NO;
    }

    if (!data || data.length == 0) {
        [self setError:error code:SVGPlayerControllerErrorInvalidData message:@"Invalid SVG data"];
        return NO;
    }

    // Load the SVG from data
    BOOL success = SVGPlayer_LoadSVGData(self.handle, data.bytes, data.length);

    if (!success) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to parse SVG data";
        self.internalErrorMessage = message;
        [self setError:error code:SVGPlayerControllerErrorParseFailed message:message];
        return NO;
    }

    // Apply settings
    SVGPlayer_SetLooping(self.handle, self.looping);
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.internalErrorMessage = nil;

    return YES;
}

- (void)unload {
    if (self.handle) {
        // Recreate the handle to reset state
        SVGPlayer_Destroy(self.handle);
        self.handle = SVGPlayer_Create();
        if (self.handle) {
            SVGPlayer_SetLooping(self.handle, self.looping);
        }
    }
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
}

#pragma mark - State Properties

- (BOOL)isLoaded {
    if (!self.handle) return NO;

    int width = 0, height = 0;
    return SVGPlayer_GetSize(self.handle, &width, &height) && (width > 0 || height > 0);
}

- (CGSize)intrinsicSize {
    if (!self.handle) return CGSizeZero;

    int width = 0, height = 0;
    if (SVGPlayer_GetSize(self.handle, &width, &height)) {
        return CGSizeMake(width, height);
    }
    return CGSizeZero;
}

- (NSTimeInterval)duration {
    if (!self.handle) return 0;
    return SVGPlayer_GetDuration(self.handle);
}

- (void)setLooping:(BOOL)looping {
    _looping = looping;
    if (self.handle) {
        SVGPlayer_SetLooping(self.handle, looping);
    }
    // Update repeat mode to match
    _internalRepeatMode = looping ? SVGControllerRepeatModeLoop : SVGControllerRepeatModeNone;
}

- (NSTimeInterval)currentTime {
    if (!self.handle) return 0;
    SVGRenderStats stats = SVGPlayer_GetStats(self.handle);
    return stats.animationTimeMs / 1000.0;
}

- (SVGControllerPlaybackState)playbackState {
    return self.internalPlaybackState;
}

- (SVGRenderStatistics)statistics {
    SVGRenderStatistics stats = {0};
    if (self.handle) {
        SVGRenderStats cStats = SVGPlayer_GetStats(self.handle);
        stats.renderTimeMs = cStats.renderTimeMs;
        stats.updateTimeMs = 0; // TODO: Add to C API
        stats.animationTimeMs = cStats.animationTimeMs;
        stats.currentFrame = cStats.currentFrame;
        stats.totalFrames = cStats.totalFrames;
        stats.fps = cStats.fps;
        stats.peakMemoryBytes = 0; // TODO: Add to C API
        stats.elementsRendered = 0; // TODO: Add to C API
    }
    return stats;
}

- (NSString *)lastErrorMessage {
    return self.internalErrorMessage;
}

#pragma mark - Playback Mode Properties

- (SVGControllerRepeatMode)repeatMode {
    return self.internalRepeatMode;
}

- (void)setRepeatMode:(SVGControllerRepeatMode)repeatMode {
    self.internalRepeatMode = repeatMode;
    // Sync with looping property
    self.looping = (repeatMode == SVGControllerRepeatModeLoop ||
                    repeatMode == SVGControllerRepeatModeReverse ||
                    repeatMode == SVGControllerRepeatModeCount);
}

- (NSInteger)repeatCount {
    return self.internalRepeatCount;
}

- (void)setRepeatCount:(NSInteger)repeatCount {
    self.internalRepeatCount = MAX(1, repeatCount);
}

- (NSInteger)currentRepeatIteration {
    return self.internalCurrentRepeatIteration;
}

- (BOOL)isPlayingForward {
    return self.internalPlayingForward;
}

- (CGFloat)playbackRate {
    return self.internalPlaybackRate;
}

- (void)setPlaybackRate:(CGFloat)playbackRate {
    // Clamp to reasonable range
    self.internalPlaybackRate = MAX(0.1, MIN(10.0, playbackRate));
    // TODO: Pass to C API when implemented
}

#pragma mark - Timeline Properties

- (CGFloat)progress {
    NSTimeInterval duration = self.duration;
    if (duration <= 0) return 0;
    return (CGFloat)(self.currentTime / duration);
}

- (NSTimeInterval)elapsedTime {
    return self.currentTime;
}

- (NSTimeInterval)remainingTime {
    NSTimeInterval duration = self.duration;
    if (duration <= 0) return 0;
    return MAX(0, duration - self.currentTime);
}

- (NSInteger)currentFrame {
    SVGRenderStats stats = SVGPlayer_GetStats(self.handle);
    return stats.currentFrame;
}

- (NSInteger)totalFrames {
    SVGRenderStats stats = SVGPlayer_GetStats(self.handle);
    return stats.totalFrames;
}

- (CGFloat)frameRate {
    // TODO: Get from C API when available
    // For now, estimate from total frames and duration
    NSTimeInterval duration = self.duration;
    NSInteger totalFrames = self.totalFrames;
    if (duration > 0 && totalFrames > 0) {
        return (CGFloat)totalFrames / duration;
    }
    return kDefaultFrameRate;
}

- (NSTimeInterval)timePerFrame {
    CGFloat rate = self.frameRate;
    if (rate <= 0) return 1.0 / kDefaultFrameRate;
    return 1.0 / rate;
}

#pragma mark - Basic Playback Control

- (void)play {
    if (!self.handle || !self.isLoaded) return;

    SVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Playing);
    self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
}

- (void)pause {
    if (!self.handle) return;

    SVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Paused);
    self.internalPlaybackState = SVGControllerPlaybackStatePaused;
}

- (void)resume {
    // Alias for play when paused
    if (self.internalPlaybackState == SVGControllerPlaybackStatePaused) {
        [self play];
    }
}

- (void)stop {
    if (!self.handle) return;

    SVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Stopped);
    SVGPlayer_SeekTo(self.handle, 0);
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
}

- (void)togglePlayback {
    if (self.internalPlaybackState == SVGControllerPlaybackStatePlaying) {
        [self pause];
    } else {
        [self play];
    }
}

#pragma mark - Animation Update

- (void)update:(NSTimeInterval)deltaTime {
    [self update:deltaTime forward:self.internalPlayingForward];
}

- (void)update:(NSTimeInterval)deltaTime forward:(BOOL)forward {
    if (!self.handle || self.internalPlaybackState != SVGControllerPlaybackStatePlaying) return;

    // Apply playback rate
    NSTimeInterval adjustedDelta = deltaTime * self.internalPlaybackRate;
    if (!forward) {
        adjustedDelta = -adjustedDelta;
    }

    // TODO: Support backward playback in C API
    // For now, only forward playback is supported
    if (adjustedDelta > 0) {
        SVGPlayer_Update(self.handle, adjustedDelta);
    }

    // Handle repeat modes
    if (!self.looping && self.internalRepeatMode == SVGControllerRepeatModeNone) {
        NSTimeInterval duration = self.duration;
        NSTimeInterval current = self.currentTime;
        if (duration > 0 && current >= duration) {
            self.internalPlaybackState = SVGControllerPlaybackStateStopped;
            SVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Stopped);
        }
    } else if (self.internalRepeatMode == SVGControllerRepeatModeCount) {
        // TODO: Track loop completions and stop after repeatCount iterations
        NSTimeInterval duration = self.duration;
        NSTimeInterval current = self.currentTime;
        if (duration > 0 && current >= duration) {
            self.internalCurrentRepeatIteration++;
            if (self.internalCurrentRepeatIteration >= self.internalRepeatCount) {
                self.internalPlaybackState = SVGControllerPlaybackStateStopped;
                SVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Stopped);
            }
        }
    } else if (self.internalRepeatMode == SVGControllerRepeatModeReverse) {
        // TODO: Implement ping-pong in C API
        // For now, just loop normally
    }
}

#pragma mark - Seeking

- (void)seekToTime:(NSTimeInterval)time {
    if (!self.handle) return;

    // Clamp time to valid range
    NSTimeInterval duration = self.duration;
    if (duration > 0) {
        time = MAX(0, MIN(time, duration));
    } else {
        time = MAX(0, time);
    }

    SVGPlayer_SeekTo(self.handle, time);
}

- (void)seekToFrame:(NSInteger)frame {
    NSTimeInterval time = [self timeForFrame:frame];
    [self seekToTime:time];
}

- (void)seekToProgress:(CGFloat)progress {
    progress = MAX(0, MIN(1.0, progress));
    NSTimeInterval duration = self.duration;
    [self seekToTime:progress * duration];
}

- (void)seekToStart {
    [self seekToTime:0];
}

- (void)seekToEnd {
    [self seekToTime:self.duration];
}

#pragma mark - Frame Stepping

- (void)stepForward {
    [self stepByFrames:1];
}

- (void)stepBackward {
    [self stepByFrames:-1];
}

- (void)stepByFrames:(NSInteger)frameCount {
    // Pause if playing
    if (self.internalPlaybackState == SVGControllerPlaybackStatePlaying) {
        [self pause];
    }

    NSInteger currentFrame = self.currentFrame;
    NSInteger targetFrame = currentFrame + frameCount;

    // Clamp to valid range
    NSInteger totalFrames = self.totalFrames;
    if (totalFrames > 0) {
        targetFrame = MAX(0, MIN(targetFrame, totalFrames - 1));
    } else {
        targetFrame = MAX(0, targetFrame);
    }

    [self seekToFrame:targetFrame];
}

#pragma mark - Relative Seeking

- (void)seekForwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    NSTimeInterval newTime = self.currentTime + seconds;
    [self seekToTime:newTime];
}

- (void)seekBackwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    NSTimeInterval newTime = self.currentTime - seconds;
    [self seekToTime:newTime];
}

- (void)seekForwardByPercentage:(CGFloat)percentage {
    percentage = MAX(0, MIN(1.0, percentage));
    NSTimeInterval skipTime = self.duration * percentage;
    [self seekForwardByTime:skipTime];
}

- (void)seekBackwardByPercentage:(CGFloat)percentage {
    percentage = MAX(0, MIN(1.0, percentage));
    NSTimeInterval skipTime = self.duration * percentage;
    [self seekBackwardByTime:skipTime];
}

#pragma mark - Scrubbing Support

- (void)beginScrubbing {
    self.stateBeforeScrubbing = self.internalPlaybackState;
    self.internalScrubbing = YES;
    if (self.internalPlaybackState == SVGControllerPlaybackStatePlaying) {
        [self pause];
    }
}

- (void)scrubToProgress:(CGFloat)progress {
    if (!self.internalScrubbing) {
        [self beginScrubbing];
    }
    [self seekToProgress:progress];
}

- (void)endScrubbing:(BOOL)resume {
    self.internalScrubbing = NO;
    if (resume && self.stateBeforeScrubbing == SVGControllerPlaybackStatePlaying) {
        [self play];
    }
}

- (BOOL)isScrubbing {
    return self.internalScrubbing;
}

#pragma mark - Rendering

- (BOOL)renderToBuffer:(void *)buffer
                 width:(NSInteger)width
                height:(NSInteger)height
                 scale:(CGFloat)scale {
    if (!self.handle || !buffer || width <= 0 || height <= 0) {
        return NO;
    }

    BOOL success = SVGPlayer_Render(self.handle, buffer,
                                     (int)width, (int)height,
                                     (float)scale);

    if (!success) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        if (errorMsg) {
            self.internalErrorMessage = @(errorMsg);
        }
    }

    return success;
}

- (BOOL)renderToBuffer:(void *)buffer
                 width:(NSInteger)width
                height:(NSInteger)height
                 scale:(CGFloat)scale
                atTime:(NSTimeInterval)time {
    // Save current time
    NSTimeInterval savedTime = self.currentTime;

    // Seek to requested time
    [self seekToTime:time];

    // Render
    BOOL success = [self renderToBuffer:buffer width:width height:height scale:scale];

    // Restore original time
    [self seekToTime:savedTime];

    return success;
}

#pragma mark - Utility Methods

+ (NSString *)formatTime:(NSTimeInterval)time {
    if (time < 0) time = 0;

    NSInteger hours = (NSInteger)(time / 3600);
    NSInteger minutes = (NSInteger)((time - hours * 3600) / 60);
    NSInteger seconds = (NSInteger)(time - hours * 3600 - minutes * 60);

    if (hours > 0) {
        return [NSString stringWithFormat:@"%ld:%02ld:%02ld", (long)hours, (long)minutes, (long)seconds];
    } else {
        return [NSString stringWithFormat:@"%02ld:%02ld", (long)minutes, (long)seconds];
    }
}

- (NSString *)formattedCurrentTime {
    return [SVGPlayerController formatTime:self.currentTime];
}

- (NSString *)formattedRemainingTime {
    return [NSString stringWithFormat:@"-%@", [SVGPlayerController formatTime:self.remainingTime]];
}

- (NSString *)formattedDuration {
    return [SVGPlayerController formatTime:self.duration];
}

- (NSInteger)frameForTime:(NSTimeInterval)time {
    NSTimeInterval tpf = self.timePerFrame;
    if (tpf <= 0) return 0;
    return (NSInteger)(time / tpf);
}

- (NSTimeInterval)timeForFrame:(NSInteger)frame {
    NSTimeInterval tpf = self.timePerFrame;
    return frame * tpf;
}

#pragma mark - Error Handling

- (void)setError:(NSError * _Nullable *)error code:(SVGPlayerControllerErrorCode)code message:(NSString *)message {
    self.internalErrorMessage = message;

    if (error) {
        *error = [NSError errorWithDomain:SVGPlayerControllerErrorDomain
                                     code:code
                                 userInfo:@{NSLocalizedDescriptionKey: message}];
    }
}

@end
