// SVGPlayerController.mm - macOS Objective-C wrapper around unified C SVG player API
//
// This implementation bridges the unified C API (svg_player_api.h) to Objective-C,
// providing a clean interface for SVGPlayerView and direct users on macOS.
//
// The unified API provides full cross-platform functionality.

#import "SVGPlayerController.h"
#import "../../shared/svg_player_api.h"

// Error domain for SVGPlayerController errors
NSString * const SVGPlayerControllerErrorDomain = @"com.svgplayer.controller.error";

// Default frame rate for static SVGs or when not specified
static const CGFloat kDefaultFrameRate = 60.0;

// Default seek interval for rewind/fastforward
static const NSTimeInterval kDefaultSeekInterval = 5.0;

@interface SVGPlayerController ()
// The underlying unified C API handle
@property (nonatomic, assign) SVGPlayerRef handle;
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
// Scrubbing state
@property (nonatomic, assign) BOOL internalScrubbing;
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
    self.internalErrorMessage = nil;

    return YES;
}

- (void)unload {
    if (self.handle) {
        SVGPlayer_Unload(self.handle);
    }
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
}

#pragma mark - State Properties

- (BOOL)isLoaded {
    if (!self.handle) return NO;
    return SVGPlayer_IsLoaded(self.handle);
}

- (NSSize)intrinsicSize {
    if (!self.handle) return NSZeroSize;

    int width = 0, height = 0;
    if (SVGPlayer_GetSize(self.handle, &width, &height)) {
        return NSMakeSize(width, height);
    }
    return NSZeroSize;
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
    _internalRepeatMode = looping ? SVGControllerRepeatModeLoop : SVGControllerRepeatModeNone;
}

- (NSTimeInterval)currentTime {
    if (!self.handle) return 0;
    return SVGPlayer_GetCurrentTime(self.handle);
}

- (SVGControllerPlaybackState)playbackState {
    return self.internalPlaybackState;
}

- (SVGRenderStatistics)statistics {
    SVGRenderStatistics stats = {0};
    if (self.handle) {
        SVGRenderStats cStats = SVGPlayer_GetStats(self.handle);
        stats.renderTimeMs = cStats.renderTimeMs;
        stats.updateTimeMs = cStats.updateTimeMs;
        stats.animationTimeMs = cStats.animationTimeMs;
        stats.currentFrame = cStats.currentFrame;
        stats.totalFrames = cStats.totalFrames;
        stats.fps = cStats.fps;
        stats.peakMemoryBytes = cStats.peakMemoryBytes;
        stats.elementsRendered = cStats.elementsRendered;
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
    self.looping = (repeatMode == SVGControllerRepeatModeLoop ||
                    repeatMode == SVGControllerRepeatModeReverse ||
                    repeatMode == SVGControllerRepeatModeCount);
    if (self.handle) {
        SVGRepeatMode cMode = SVGRepeatMode_None;
        switch (repeatMode) {
            case SVGControllerRepeatModeNone: cMode = SVGRepeatMode_None; break;
            case SVGControllerRepeatModeLoop: cMode = SVGRepeatMode_Loop; break;
            case SVGControllerRepeatModeReverse: cMode = SVGRepeatMode_Reverse; break;
            case SVGControllerRepeatModeCount: cMode = SVGRepeatMode_Count; break;
        }
        SVGPlayer_SetRepeatMode(self.handle, cMode);
    }
}

- (NSInteger)repeatCount {
    return self.internalRepeatCount;
}

- (void)setRepeatCount:(NSInteger)repeatCount {
    self.internalRepeatCount = MAX(1, repeatCount);
    if (self.handle) {
        SVGPlayer_SetRepeatCount(self.handle, (int)self.internalRepeatCount);
    }
}

- (NSInteger)currentRepeatIteration {
    if (self.handle) {
        return (NSInteger)SVGPlayer_GetCompletedLoops(self.handle);
    }
    return 0;
}

- (BOOL)isPlayingForward {
    if (self.handle) {
        return SVGPlayer_IsPlayingForward(self.handle);
    }
    return YES;
}

- (CGFloat)playbackRate {
    return self.internalPlaybackRate;
}

- (void)setPlaybackRate:(CGFloat)playbackRate {
    self.internalPlaybackRate = MAX(0.1, MIN(10.0, playbackRate));
    if (self.handle) {
        SVGPlayer_SetPlaybackRate(self.handle, (float)self.internalPlaybackRate);
    }
}

#pragma mark - Timeline Properties

- (CGFloat)progress {
    if (self.handle) {
        return (CGFloat)SVGPlayer_GetProgress(self.handle);
    }
    return 0;
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
    if (self.handle) {
        return (NSInteger)SVGPlayer_GetCurrentFrame(self.handle);
    }
    return 0;
}

- (NSInteger)totalFrames {
    if (self.handle) {
        return (NSInteger)SVGPlayer_GetTotalFrames(self.handle);
    }
    return 0;
}

- (CGFloat)frameRate {
    if (self.handle) {
        float rate = SVGPlayer_GetFrameRate(self.handle);
        if (rate > 0) {
            return (CGFloat)rate;
        }
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

    SVGPlayer_Play(self.handle);
    self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
}

- (void)pause {
    if (!self.handle) return;

    SVGPlayer_Pause(self.handle);
    self.internalPlaybackState = SVGControllerPlaybackStatePaused;
}

- (void)resume {
    if (self.internalPlaybackState == SVGControllerPlaybackStatePaused) {
        [self play];
    }
}

- (void)stop {
    if (!self.handle) return;

    SVGPlayer_Stop(self.handle);
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
}

- (void)togglePlayback {
    if (!self.handle) return;

    SVGPlayer_TogglePlayback(self.handle);
    SVGPlaybackState cState = SVGPlayer_GetPlaybackState(self.handle);
    switch (cState) {
        case SVGPlaybackState_Stopped:
            self.internalPlaybackState = SVGControllerPlaybackStateStopped;
            break;
        case SVGPlaybackState_Playing:
            self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
            break;
        case SVGPlaybackState_Paused:
            self.internalPlaybackState = SVGControllerPlaybackStatePaused;
            break;
    }
}

#pragma mark - Animation Update

- (void)update:(NSTimeInterval)deltaTime {
    [self update:deltaTime forward:YES];
}

- (void)update:(NSTimeInterval)deltaTime forward:(BOOL)forward {
    if (!self.handle || self.internalPlaybackState != SVGControllerPlaybackStatePlaying) return;

    NSTimeInterval adjustedDelta = deltaTime * self.internalPlaybackRate;
    if (!forward) {
        adjustedDelta = -adjustedDelta;
    }

    SVGPlayer_Update(self.handle, adjustedDelta);

    // Sync our internal state with the unified API state
    SVGPlaybackState cState = SVGPlayer_GetPlaybackState(self.handle);
    switch (cState) {
        case SVGPlaybackState_Stopped:
            self.internalPlaybackState = SVGControllerPlaybackStateStopped;
            break;
        case SVGPlaybackState_Playing:
            self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
            break;
        case SVGPlaybackState_Paused:
            self.internalPlaybackState = SVGControllerPlaybackStatePaused;
            break;
    }
}

#pragma mark - Seeking

- (void)seekToTime:(NSTimeInterval)time {
    if (!self.handle) return;
    SVGPlayer_SeekTo(self.handle, time);
}

- (void)seekToFrame:(NSInteger)frame {
    if (self.handle) {
        SVGPlayer_SeekToFrame(self.handle, (int)frame);
    }
}

- (void)seekToProgress:(CGFloat)progress {
    if (self.handle) {
        SVGPlayer_SeekToProgress(self.handle, (float)progress);
    }
}

- (void)seekToStart {
    if (self.handle) {
        SVGPlayer_SeekToStart(self.handle);
    }
}

- (void)seekToEnd {
    if (self.handle) {
        SVGPlayer_SeekToEnd(self.handle);
    }
}

#pragma mark - Frame Stepping

- (void)stepForward {
    if (self.handle) {
        SVGPlayer_StepForward(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)stepBackward {
    if (self.handle) {
        SVGPlayer_StepBackward(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)stepByFrames:(NSInteger)frameCount {
    if (self.handle) {
        SVGPlayer_StepByFrames(self.handle, (int)frameCount);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

#pragma mark - Relative Seeking

- (void)seekForwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    if (self.handle) {
        SVGPlayer_SeekForwardByTime(self.handle, seconds);
    }
}

- (void)seekBackwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    if (self.handle) {
        SVGPlayer_SeekBackwardByTime(self.handle, seconds);
    }
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
    if (self.handle) {
        SVGPlayer_BeginScrubbing(self.handle);
        self.internalScrubbing = YES;
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)scrubToProgress:(CGFloat)progress {
    if (self.handle) {
        if (!self.internalScrubbing) {
            [self beginScrubbing];
        }
        SVGPlayer_ScrubToProgress(self.handle, (float)progress);
    }
}

- (void)endScrubbing:(BOOL)resume {
    if (self.handle) {
        SVGPlayer_EndScrubbing(self.handle, resume);
        self.internalScrubbing = NO;
        SVGPlaybackState cState = SVGPlayer_GetPlaybackState(self.handle);
        switch (cState) {
            case SVGPlaybackState_Stopped:
                self.internalPlaybackState = SVGControllerPlaybackStateStopped;
                break;
            case SVGPlaybackState_Playing:
                self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
                break;
            case SVGPlaybackState_Paused:
                self.internalPlaybackState = SVGControllerPlaybackStatePaused;
                break;
        }
    }
}

- (BOOL)isScrubbing {
    if (self.handle) {
        return SVGPlayer_IsScrubbing(self.handle);
    }
    return NO;
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
    if (!self.handle || !buffer || width <= 0 || height <= 0) {
        return NO;
    }

    BOOL success = SVGPlayer_RenderAtTime(self.handle, buffer,
                                           (int)width, (int)height,
                                           (float)scale, time);

    if (!success) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        if (errorMsg) {
            self.internalErrorMessage = @(errorMsg);
        }
    }

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
    if (self.handle) {
        return (NSInteger)SVGPlayer_TimeToFrame(self.handle, time);
    }
    return 0;
}

- (NSTimeInterval)timeForFrame:(NSInteger)frame {
    if (self.handle) {
        return SVGPlayer_FrameToTime(self.handle, (int)frame);
    }
    return 0;
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
