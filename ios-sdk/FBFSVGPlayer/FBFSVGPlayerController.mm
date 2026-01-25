// SVGPlayerController.mm - Objective-C wrapper around the unified C SVG player API
//
// This implementation bridges the unified C API (fbfsvg_player_api.h) to Objective-C,
// providing a clean interface for FBFSVGPlayerView and direct users.
//
// The unified API provides full cross-platform functionality, replacing
// platform-specific stubs with proper implementations.

#import "FBFSVGPlayerController.h"
#import "../../shared/fbfsvg_player_api.h"

// Error domain for FBFSVGPlayerController errors
NSString * const FBFSVGPlayerControllerErrorDomain = @"com.fbfsvgplayer.controller.error";

// Default frame rate for static SVGs or when not specified
static const CGFloat kDefaultFrameRate = 60.0;

// Default seek interval for rewind/fastforward
static const NSTimeInterval kDefaultSeekInterval = 5.0;

@interface FBFSVGPlayerController ()
// The underlying unified C API handle
@property (nonatomic, assign) FBFSVGPlayerRef handle;
// Current playback state
@property (nonatomic, assign) FBFSVGControllerPlaybackState internalPlaybackState;
// Cached error message
@property (nonatomic, copy, nullable) NSString *internalErrorMessage;
// Playback rate multiplier
@property (nonatomic, assign) CGFloat internalPlaybackRate;
// Repeat mode
@property (nonatomic, assign) FBFSVGControllerRepeatMode internalRepeatMode;
// Repeat count for counted mode
@property (nonatomic, assign) NSInteger internalRepeatCount;
// Current repeat iteration
@property (nonatomic, assign) NSInteger internalCurrentRepeatIteration;
// Whether playing forward (for ping-pong)
@property (nonatomic, assign) BOOL internalPlayingForward;
// Scrubbing state
@property (nonatomic, assign) BOOL internalScrubbing;
// Playback state before scrubbing (for restoration)
@property (nonatomic, assign) FBFSVGControllerPlaybackState stateBeforeScrubbing;
@end

#pragma mark - FBFSVGPlayerLayer Implementation

@interface FBFSVGPlayerLayer ()
@property (nonatomic, assign) FBFSVGLayerRef layerRef;
@end

@implementation FBFSVGPlayerLayer

- (instancetype)initWithLayerRef:(FBFSVGLayerRef)layerRef {
    if (self = [super init]) {
        _layerRef = layerRef;
    }
    return self;
}

#pragma mark - Position

- (void)setPosition:(CGPoint)position {
    if (!self.layerRef) return;
    FBFSVGLayer_SetPosition(self.layerRef, position.x, position.y);
}

- (CGPoint)position {
    if (!self.layerRef) return CGPointZero;
    float x = 0, y = 0;
    FBFSVGLayer_GetPosition(self.layerRef, &x, &y);
    return CGPointMake(x, y);
}

#pragma mark - Opacity

- (void)setOpacity:(CGFloat)opacity {
    if (!self.layerRef) return;
    FBFSVGLayer_SetOpacity(self.layerRef, (float)opacity);
}

- (CGFloat)opacity {
    if (!self.layerRef) return 1.0;
    return FBFSVGLayer_GetOpacity(self.layerRef);
}

#pragma mark - Z-Order

- (void)setZOrder:(NSInteger)zOrder {
    if (!self.layerRef) return;
    FBFSVGLayer_SetZOrder(self.layerRef, (int)zOrder);
}

- (NSInteger)zOrder {
    if (!self.layerRef) return 0;
    return FBFSVGLayer_GetZOrder(self.layerRef);
}

#pragma mark - Visibility

- (void)setVisible:(BOOL)visible {
    if (!self.layerRef) return;
    FBFSVGLayer_SetVisible(self.layerRef, visible);
}

- (BOOL)isVisible {
    if (!self.layerRef) return NO;
    return FBFSVGLayer_IsVisible(self.layerRef);
}

#pragma mark - Scale

- (void)setScale:(CGPoint)scale {
    if (!self.layerRef) return;
    FBFSVGLayer_SetScale(self.layerRef, scale.x, scale.y);
}

- (CGPoint)scale {
    if (!self.layerRef) return CGPointMake(1.0, 1.0);
    float scaleX = 1.0, scaleY = 1.0;
    FBFSVGLayer_GetScale(self.layerRef, &scaleX, &scaleY);
    return CGPointMake(scaleX, scaleY);
}

#pragma mark - Rotation

- (void)setRotation:(CGFloat)rotation {
    if (!self.layerRef) return;
    FBFSVGLayer_SetRotation(self.layerRef, (float)rotation);
}

- (CGFloat)rotation {
    if (!self.layerRef) return 0.0;
    return FBFSVGLayer_GetRotation(self.layerRef);
}

#pragma mark - Blend Mode

- (void)setBlendMode:(FBFSVGPlayerLayerBlendMode)blendMode {
    if (!self.layerRef) return;
    // Convert from Obj-C enum to C enum (same underlying values)
    FBFSVGLayer_SetBlendMode(self.layerRef, (FBFSVGLayerBlendMode)blendMode);
}

- (FBFSVGPlayerLayerBlendMode)blendMode {
    if (!self.layerRef) return FBFSVGPlayerLayerBlendModeNormal;
    // Convert from C enum to Obj-C enum (same underlying values)
    return (FBFSVGPlayerLayerBlendMode)FBFSVGLayer_GetBlendMode(self.layerRef);
}

#pragma mark - Size

- (CGSize)size {
    if (!self.layerRef) return CGSizeZero;
    int width = 0, height = 0;
    if (FBFSVGLayer_GetSize(self.layerRef, &width, &height)) {
        return CGSizeMake(width, height);
    }
    return CGSizeZero;
}

#pragma mark - Animation Properties

- (NSTimeInterval)duration {
    if (!self.layerRef) return 0;
    return FBFSVGLayer_GetDuration(self.layerRef);
}

- (NSTimeInterval)currentTime {
    if (!self.layerRef) return 0;
    // Note: currentTime getter is not in C API, would need to track it
    // For now, return 0 (this is a readonly property used for display)
    return 0;
}

- (BOOL)hasAnimations {
    if (!self.layerRef) return NO;
    return FBFSVGLayer_HasAnimations(self.layerRef);
}

#pragma mark - Playback Control

- (void)play {
    if (!self.layerRef) return;
    FBFSVGLayer_Play(self.layerRef);
}

- (void)pause {
    if (!self.layerRef) return;
    FBFSVGLayer_Pause(self.layerRef);
}

- (void)stop {
    if (!self.layerRef) return;
    FBFSVGLayer_Stop(self.layerRef);
}

- (void)seekToTime:(NSTimeInterval)time {
    if (!self.layerRef) return;
    FBFSVGLayer_SeekTo(self.layerRef, time);
}

- (BOOL)update:(NSTimeInterval)deltaTime {
    if (!self.layerRef) return NO;
    return FBFSVGLayer_Update(self.layerRef, deltaTime);
}

@end

@implementation FBFSVGPlayerController

#pragma mark - Initialization

+ (instancetype)controller {
    return [[self alloc] init];
}

- (instancetype)init {
    if (self = [super init]) {
        _handle = FBFSVGPlayer_Create();
        _internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
        _looping = YES;
        _internalPlaybackRate = 1.0;
        _internalRepeatMode = FBFSVGControllerRepeatModeLoop;
        _internalRepeatCount = 1;
        _internalCurrentRepeatIteration = 0;
        _internalPlayingForward = YES;
        _internalScrubbing = NO;

        if (_handle) {
            FBFSVGPlayer_SetLooping(_handle, YES);
        }
    }
    return self;
}

- (void)dealloc {
    if (_handle) {
        FBFSVGPlayer_Destroy(_handle);
        _handle = NULL;
    }
}

#pragma mark - Loading

- (BOOL)loadSVGFromPath:(NSString *)path error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:FBFSVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return NO;
    }

    // Check if file exists
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        NSString *message = [NSString stringWithFormat:@"SVG file not found: %@", path];
        [self setError:error code:FBFSVGPlayerControllerErrorFileNotFound message:message];
        return NO;
    }

    // Load the SVG
    BOOL success = FBFSVGPlayer_LoadSVG(self.handle, [path UTF8String]);

    if (!success) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to load SVG";
        self.internalErrorMessage = message;
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return NO;
    }

    // Apply settings
    FBFSVGPlayer_SetLooping(self.handle, self.looping);
    self.internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.internalErrorMessage = nil;

    return YES;
}

- (BOOL)loadSVGFromData:(NSData *)data error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:FBFSVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return NO;
    }

    if (!data || data.length == 0) {
        [self setError:error code:FBFSVGPlayerControllerErrorInvalidData message:@"Invalid SVG data"];
        return NO;
    }

    // Load the SVG from data
    BOOL success = FBFSVGPlayer_LoadSVGData(self.handle, data.bytes, data.length);

    if (!success) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to parse SVG data";
        self.internalErrorMessage = message;
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return NO;
    }

    // Apply settings
    FBFSVGPlayer_SetLooping(self.handle, self.looping);
    self.internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.internalErrorMessage = nil;

    return YES;
}

- (void)unload {
    if (self.handle) {
        // Recreate the handle to reset state
        FBFSVGPlayer_Destroy(self.handle);
        self.handle = FBFSVGPlayer_Create();
        if (self.handle) {
            FBFSVGPlayer_SetLooping(self.handle, self.looping);
        }
    }
    self.internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
}

#pragma mark - State Properties

- (BOOL)isLoaded {
    if (!self.handle) return NO;

    int width = 0, height = 0;
    return FBFSVGPlayer_GetSize(self.handle, &width, &height) && (width > 0 || height > 0);
}

- (CGSize)intrinsicSize {
    if (!self.handle) return CGSizeZero;

    int width = 0, height = 0;
    if (FBFSVGPlayer_GetSize(self.handle, &width, &height)) {
        return CGSizeMake(width, height);
    }
    return CGSizeZero;
}

- (NSTimeInterval)duration {
    if (!self.handle) return 0;
    return FBFSVGPlayer_GetDuration(self.handle);
}

- (void)setLooping:(BOOL)looping {
    _looping = looping;
    if (self.handle) {
        FBFSVGPlayer_SetLooping(self.handle, looping);
    }
    // Update repeat mode to match
    _internalRepeatMode = looping ? FBFSVGControllerRepeatModeLoop : FBFSVGControllerRepeatModeNone;
}

- (NSTimeInterval)currentTime {
    if (!self.handle) return 0;
    SVGRenderStats stats = FBFSVGPlayer_GetStats(self.handle);
    return stats.animationTimeMs / 1000.0;
}

- (FBFSVGControllerPlaybackState)playbackState {
    return self.internalPlaybackState;
}

- (FBFSVGRenderStatistics)statistics {
    FBFSVGRenderStatistics stats = {0};
    if (self.handle) {
        // Use unified C API - all fields now available
        SVGRenderStats cStats = FBFSVGPlayer_GetStats(self.handle);
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

- (FBFSVGControllerRepeatMode)repeatMode {
    return self.internalRepeatMode;
}

- (void)setRepeatMode:(FBFSVGControllerRepeatMode)repeatMode {
    self.internalRepeatMode = repeatMode;
    // Sync with looping property
    self.looping = (repeatMode == FBFSVGControllerRepeatModeLoop ||
                    repeatMode == FBFSVGControllerRepeatModeReverse ||
                    repeatMode == FBFSVGControllerRepeatModeCount);
    // Pass to unified C API
    if (self.handle) {
        SVGRepeatMode cMode = SVGRepeatMode_None;
        switch (repeatMode) {
            case FBFSVGControllerRepeatModeNone: cMode = SVGRepeatMode_None; break;
            case FBFSVGControllerRepeatModeLoop: cMode = SVGRepeatMode_Loop; break;
            case FBFSVGControllerRepeatModeReverse: cMode = SVGRepeatMode_Reverse; break;
            case FBFSVGControllerRepeatModeCount: cMode = SVGRepeatMode_Count; break;
        }
        FBFSVGPlayer_SetRepeatMode(self.handle, cMode);
    }
}

- (NSInteger)repeatCount {
    return self.internalRepeatCount;
}

- (void)setRepeatCount:(NSInteger)repeatCount {
    self.internalRepeatCount = MAX(1, repeatCount);
    // Pass to unified C API
    if (self.handle) {
        FBFSVGPlayer_SetRepeatCount(self.handle, (int)self.internalRepeatCount);
    }
}

- (NSInteger)currentRepeatIteration {
    // Read from unified C API
    if (self.handle) {
        return (NSInteger)FBFSVGPlayer_GetCompletedLoops(self.handle);
    }
    return 0;
}

- (BOOL)isPlayingForward {
    // Read from unified C API
    if (self.handle) {
        return FBFSVGPlayer_IsPlayingForward(self.handle);
    }
    return YES;
}

- (CGFloat)playbackRate {
    return self.internalPlaybackRate;
}

- (void)setPlaybackRate:(CGFloat)playbackRate {
    // Clamp to reasonable range
    self.internalPlaybackRate = MAX(0.1, MIN(10.0, playbackRate));
    // Pass to unified C API
    if (self.handle) {
        FBFSVGPlayer_SetPlaybackRate(self.handle, (float)self.internalPlaybackRate);
    }
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
    if (!self.handle) return 0;
    SVGRenderStats stats = FBFSVGPlayer_GetStats(self.handle);
    return stats.currentFrame;
}

- (NSInteger)totalFrames {
    if (!self.handle) return 0;
    SVGRenderStats stats = FBFSVGPlayer_GetStats(self.handle);
    return stats.totalFrames;
}

- (CGFloat)frameRate {
    // Get from unified C API
    if (self.handle) {
        float rate = FBFSVGPlayer_GetFrameRate(self.handle);
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

    FBFSVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Playing);
    self.internalPlaybackState = FBFSVGControllerPlaybackStatePlaying;
}

- (void)pause {
    if (!self.handle) return;

    FBFSVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Paused);
    self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
}

- (void)resume {
    // Alias for play when paused
    if (self.internalPlaybackState == FBFSVGControllerPlaybackStatePaused) {
        [self play];
    }
}

- (void)stop {
    if (!self.handle) return;

    FBFSVGPlayer_SetPlaybackState(self.handle, SVGPlaybackState_Stopped);
    FBFSVGPlayer_SeekTo(self.handle, 0);
    self.internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
}

- (void)togglePlayback {
    if (self.internalPlaybackState == FBFSVGControllerPlaybackStatePlaying) {
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
    if (!self.handle || self.internalPlaybackState != FBFSVGControllerPlaybackStatePlaying) return;

    // Apply playback rate (unified API handles negative rates for reverse)
    NSTimeInterval adjustedDelta = deltaTime * self.internalPlaybackRate;
    if (!forward) {
        adjustedDelta = -adjustedDelta;
    }

    // The unified API handles all update logic including:
    // - Playback rate
    // - Repeat modes (none, loop, reverse/ping-pong, count)
    // - Direction changes for ping-pong
    // - End detection and auto-stop
    FBFSVGPlayer_Update(self.handle, adjustedDelta);

    // Sync our internal state with the unified API state
    SVGPlaybackState cState = FBFSVGPlayer_GetPlaybackState(self.handle);
    switch (cState) {
        case SVGPlaybackState_Stopped:
            self.internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
            break;
        case SVGPlaybackState_Playing:
            self.internalPlaybackState = FBFSVGControllerPlaybackStatePlaying;
            break;
        case SVGPlaybackState_Paused:
            self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
            break;
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

    FBFSVGPlayer_SeekTo(self.handle, time);
}

- (void)seekToFrame:(NSInteger)frame {
    // Use unified API directly
    if (self.handle) {
        FBFSVGPlayer_SeekToFrame(self.handle, (int)frame);
    }
}

- (void)seekToProgress:(CGFloat)progress {
    // Use unified API directly
    if (self.handle) {
        FBFSVGPlayer_SeekToProgress(self.handle, (float)progress);
    }
}

- (void)seekToStart {
    // Use unified API
    if (self.handle) {
        FBFSVGPlayer_SeekToStart(self.handle);
    }
}

- (void)seekToEnd {
    // Use unified API
    if (self.handle) {
        FBFSVGPlayer_SeekToEnd(self.handle);
    }
}

#pragma mark - Frame Stepping

- (void)stepForward {
    // Use unified API (pauses playback automatically)
    if (self.handle) {
        FBFSVGPlayer_StepForward(self.handle);
        self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
    }
}

- (void)stepBackward {
    // Use unified API (pauses playback automatically)
    if (self.handle) {
        FBFSVGPlayer_StepBackward(self.handle);
        self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
    }
}

- (void)stepByFrames:(NSInteger)frameCount {
    // Use unified API (pauses playback automatically)
    if (self.handle) {
        FBFSVGPlayer_StepByFrames(self.handle, (int)frameCount);
        self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
    }
}

#pragma mark - Relative Seeking

- (void)seekForwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    // Use unified API
    if (self.handle) {
        FBFSVGPlayer_SeekForwardByTime(self.handle, seconds);
    }
}

- (void)seekBackwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    // Use unified API
    if (self.handle) {
        FBFSVGPlayer_SeekBackwardByTime(self.handle, seconds);
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
        FBFSVGPlayer_BeginScrubbing(self.handle);
        self.internalScrubbing = YES;
        // Update internal state to paused since scrubbing pauses playback
        self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
    }
}

- (void)scrubToProgress:(CGFloat)progress {
    if (self.handle) {
        if (!self.internalScrubbing) {
            [self beginScrubbing];
        }
        FBFSVGPlayer_ScrubToProgress(self.handle, (float)progress);
    }
}

- (void)endScrubbing:(BOOL)resume {
    if (self.handle) {
        FBFSVGPlayer_EndScrubbing(self.handle, resume);
        self.internalScrubbing = NO;
        // Sync playback state with unified API
        SVGPlaybackState cState = FBFSVGPlayer_GetPlaybackState(self.handle);
        switch (cState) {
            case SVGPlaybackState_Stopped:
                self.internalPlaybackState = FBFSVGControllerPlaybackStateStopped;
                break;
            case SVGPlaybackState_Playing:
                self.internalPlaybackState = FBFSVGControllerPlaybackStatePlaying;
                break;
            case SVGPlaybackState_Paused:
                self.internalPlaybackState = FBFSVGControllerPlaybackStatePaused;
                break;
        }
    }
}

- (BOOL)isScrubbing {
    if (self.handle) {
        return FBFSVGPlayer_IsScrubbing(self.handle);
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

    BOOL success = FBFSVGPlayer_Render(self.handle, buffer,
                                     (int)width, (int)height,
                                     (float)scale);

    if (!success) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
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
    return [FBFSVGPlayerController formatTime:self.currentTime];
}

- (NSString *)formattedRemainingTime {
    return [NSString stringWithFormat:@"-%@", [FBFSVGPlayerController formatTime:self.remainingTime]];
}

- (NSString *)formattedDuration {
    return [FBFSVGPlayerController formatTime:self.duration];
}

- (NSInteger)frameForTime:(NSTimeInterval)time {
    // Use unified API
    if (self.handle) {
        return (NSInteger)FBFSVGPlayer_TimeToFrame(self.handle, time);
    }
    return 0;
}

- (NSTimeInterval)timeForFrame:(NSInteger)frame {
    // Use unified API
    if (self.handle) {
        return FBFSVGPlayer_FrameToTime(self.handle, (int)frame);
    }
    return 0;
}

#pragma mark - Hit Testing - Element Subscription

- (void)subscribeToElementWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return;
    FBFSVGPlayer_SubscribeToElement(self.handle, [objectID UTF8String]);
}

- (void)unsubscribeFromElementWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return;
    FBFSVGPlayer_UnsubscribeFromElement(self.handle, [objectID UTF8String]);
}

- (void)unsubscribeFromAllElements {
    if (!self.handle) return;
    FBFSVGPlayer_UnsubscribeFromAllElements(self.handle);
}

#pragma mark - Hit Testing - Queries

- (nullable NSString *)hitTestAtPoint:(CGPoint)point viewSize:(CGSize)viewSize {
    if (!self.handle) return nil;

    const char *elementID = FBFSVGPlayer_HitTest(self.handle,
                                               (float)point.x, (float)point.y,
                                               (int)viewSize.width, (int)viewSize.height);
    if (elementID && strlen(elementID) > 0) {
        return @(elementID);
    }
    return nil;
}

- (NSArray<NSString *> *)elementsAtPoint:(CGPoint)point
                                viewSize:(CGSize)viewSize
                             maxElements:(NSInteger)maxElements {
    if (!self.handle || maxElements <= 0) return @[];

    // Allocate array for element IDs (C API uses const char**)
    const char **elements = (const char **)malloc(sizeof(const char *) * maxElements);
    if (!elements) return @[];

    int count = FBFSVGPlayer_GetElementsAtPoint(self.handle,
                                              (float)point.x, (float)point.y,
                                              (int)viewSize.width, (int)viewSize.height,
                                              elements, (int)maxElements);

    NSMutableArray<NSString *> *result = [NSMutableArray arrayWithCapacity:count];
    for (int i = 0; i < count; i++) {
        if (elements[i]) {
            [result addObject:@(elements[i])];
        }
    }

    free(elements);
    return [result copy];
}

- (CGRect)boundingRectForElementID:(NSString *)objectID {
    if (!self.handle || !objectID) return CGRectZero;

    SVGRect bounds;
    if (FBFSVGPlayer_GetElementBounds(self.handle, [objectID UTF8String], &bounds)) {
        return CGRectMake(bounds.x, bounds.y, bounds.width, bounds.height);
    }
    return CGRectZero;
}

- (BOOL)elementExistsWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return NO;
    return FBFSVGPlayer_ElementExists(self.handle, [objectID UTF8String]);
}

- (nullable NSString *)propertyValue:(NSString *)propertyName forElementID:(NSString *)objectID {
    if (!self.handle || !propertyName || !objectID) return nil;

    // API uses output buffer pattern
    char valueBuffer[1024];
    if (FBFSVGPlayer_GetElementProperty(self.handle,
                                      [objectID UTF8String],
                                      [propertyName UTF8String],
                                      valueBuffer,
                                      sizeof(valueBuffer))) {
        if (strlen(valueBuffer) > 0) {
            return @(valueBuffer);
        }
    }
    return nil;
}

#pragma mark - Coordinate Conversion

- (CGPoint)convertViewPointToSVG:(CGPoint)viewPoint viewSize:(CGSize)viewSize {
    if (!self.handle) return CGPointZero;

    float svgX = 0, svgY = 0;
    if (FBFSVGPlayer_ViewToSVG(self.handle,
                            (float)viewPoint.x, (float)viewPoint.y,
                            (int)viewSize.width, (int)viewSize.height,
                            &svgX, &svgY)) {
        return CGPointMake(svgX, svgY);
    }
    return CGPointZero;
}

- (CGPoint)convertSVGPointToView:(CGPoint)svgPoint viewSize:(CGSize)viewSize {
    if (!self.handle) return CGPointZero;

    float viewX = 0, viewY = 0;
    if (FBFSVGPlayer_SVGToView(self.handle,
                            (float)svgPoint.x, (float)svgPoint.y,
                            (int)viewSize.width, (int)viewSize.height,
                            &viewX, &viewY)) {
        return CGPointMake(viewX, viewY);
    }
    return CGPointZero;
}

#pragma mark - Zoom and ViewBox

- (BOOL)getViewBoxX:(CGFloat *)x y:(CGFloat *)y width:(CGFloat *)width height:(CGFloat *)height {
    if (!self.handle || !x || !y || !width || !height) return NO;

    float fx = 0, fy = 0, fw = 0, fh = 0;
    if (FBFSVGPlayer_GetViewBox(self.handle, &fx, &fy, &fw, &fh)) {
        *x = (CGFloat)fx;
        *y = (CGFloat)fy;
        *width = (CGFloat)fw;
        *height = (CGFloat)fh;
        return YES;
    }
    return NO;
}

- (void)setViewBoxX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    if (!self.handle) return;
    FBFSVGPlayer_SetViewBox(self.handle, (float)x, (float)y, (float)width, (float)height);
}

- (void)resetViewBox {
    if (!self.handle) return;
    FBFSVGPlayer_ResetViewBox(self.handle);
}

- (CGFloat)zoom {
    if (!self.handle) return 1.0;
    return (CGFloat)FBFSVGPlayer_GetZoom(self.handle);
}

- (void)setZoom:(CGFloat)zoom centeredAt:(CGPoint)center viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_SetZoom(self.handle, (float)zoom,
                      (float)center.x, (float)center.y,
                      (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomInByFactor:(CGFloat)factor viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_ZoomIn(self.handle, (float)factor, (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomOutByFactor:(CGFloat)factor viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_ZoomOut(self.handle, (float)factor, (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomToRect:(CGRect)rect {
    if (!self.handle) return;
    FBFSVGPlayer_ZoomToRect(self.handle, (float)rect.origin.x, (float)rect.origin.y,
                         (float)rect.size.width, (float)rect.size.height);
}

- (BOOL)zoomToElementWithID:(NSString *)objectID padding:(CGFloat)padding {
    if (!self.handle || !objectID) return NO;
    return FBFSVGPlayer_ZoomToElement(self.handle, [objectID UTF8String], (float)padding);
}

- (void)panByDelta:(CGPoint)delta viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_Pan(self.handle, (float)delta.x, (float)delta.y,
                  (int)viewSize.width, (int)viewSize.height);
}

- (CGFloat)minZoom {
    if (!self.handle) return 0.1;
    return (CGFloat)FBFSVGPlayer_GetMinZoom(self.handle);
}

- (void)setMinZoom:(CGFloat)minZoom {
    if (!self.handle) return;
    FBFSVGPlayer_SetMinZoom(self.handle, (float)minZoom);
}

- (CGFloat)maxZoom {
    if (!self.handle) return 10.0;
    return (CGFloat)FBFSVGPlayer_GetMaxZoom(self.handle);
}

- (void)setMaxZoom:(CGFloat)maxZoom {
    if (!self.handle) return;
    FBFSVGPlayer_SetMaxZoom(self.handle, (float)maxZoom);
}

#pragma mark - Frame Rate Control

- (CGFloat)targetFrameRate {
    if (!self.handle) return 60.0;
    return (CGFloat)FBFSVGPlayer_GetTargetFrameRate(self.handle);
}

- (void)setTargetFrameRate:(CGFloat)targetFrameRate {
    if (!self.handle) return;
    FBFSVGPlayer_SetTargetFrameRate(self.handle, (float)targetFrameRate);
}

- (NSTimeInterval)idealFrameInterval {
    if (!self.handle) return 1.0 / 60.0;
    return FBFSVGPlayer_GetIdealFrameInterval(self.handle);
}

- (NSTimeInterval)lastFrameDuration {
    if (!self.handle) return 0;
    return FBFSVGPlayer_GetLastFrameDuration(self.handle);
}

- (NSTimeInterval)averageFrameDuration {
    if (!self.handle) return 0;
    return FBFSVGPlayer_GetAverageFrameDuration(self.handle);
}

- (CGFloat)measuredFPS {
    if (!self.handle) return 0;
    return (CGFloat)FBFSVGPlayer_GetMeasuredFPS(self.handle);
}

- (NSInteger)droppedFrameCount {
    if (!self.handle) return 0;
    return FBFSVGPlayer_GetDroppedFrameCount(self.handle);
}

- (void)beginFrame {
    if (!self.handle) return;
    FBFSVGPlayer_BeginFrame(self.handle);
}

- (void)endFrame {
    if (!self.handle) return;
    FBFSVGPlayer_EndFrame(self.handle);
}

- (BOOL)shouldRenderFrameAtTime:(NSTimeInterval)currentTime {
    if (!self.handle) return YES;
    return FBFSVGPlayer_ShouldRenderFrame(self.handle, currentTime);
}

- (void)markFrameRenderedAtTime:(NSTimeInterval)renderTime {
    if (!self.handle) return;
    FBFSVGPlayer_MarkFrameRendered(self.handle, renderTime);
}

- (void)resetFrameStats {
    if (!self.handle) return;
    FBFSVGPlayer_ResetFrameStats(self.handle);
}

#pragma mark - Error Handling

- (void)setError:(NSError * _Nullable *)error code:(FBFSVGPlayerControllerErrorCode)code message:(NSString *)message {
    self.internalErrorMessage = message;

    if (error) {
        *error = [NSError errorWithDomain:FBFSVGPlayerControllerErrorDomain
                                     code:code
                                 userInfo:@{NSLocalizedDescriptionKey: message}];
    }
}

#pragma mark - Multi-SVG Compositing

- (FBFSVGPlayerLayer *)createLayerFromPath:(NSString *)path error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:FBFSVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return nil;
    }

    FBFSVGLayerRef layerRef = FBFSVGPlayer_CreateLayer(self.handle, [path UTF8String]);
    if (!layerRef) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to create layer";
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[FBFSVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (FBFSVGPlayerLayer *)createLayerFromData:(NSData *)data error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:FBFSVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return nil;
    }

    if (!data || data.length == 0) {
        [self setError:error code:FBFSVGPlayerControllerErrorInvalidData message:@"Invalid SVG data"];
        return nil;
    }

    FBFSVGLayerRef layerRef = FBFSVGPlayer_CreateLayerFromData(self.handle, data.bytes, data.length);
    if (!layerRef) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to create layer from data";
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[FBFSVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (void)destroyLayer:(FBFSVGPlayerLayer *)layer {
    if (!self.handle || !layer) return;
    FBFSVGPlayer_DestroyLayer(self.handle, layer.layerRef);
}

- (NSInteger)layerCount {
    if (!self.handle) return 0;
    return FBFSVGPlayer_GetLayerCount(self.handle);
}

- (FBFSVGPlayerLayer *)layerAtIndex:(NSInteger)index {
    if (!self.handle) return nil;

    FBFSVGLayerRef layerRef = FBFSVGPlayer_GetLayerAtIndex(self.handle, (int)index);
    if (!layerRef) return nil;

    return [[FBFSVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale {
    if (!self.handle || !buffer) return NO;
    return FBFSVGPlayer_RenderComposite(self.handle, buffer, (int)width, (int)height, (float)scale);
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale
                         atTime:(NSTimeInterval)time {
    if (!self.handle || !buffer) return NO;
    return FBFSVGPlayer_RenderCompositeAtTime(self.handle, buffer, (int)width, (int)height, (float)scale, time);
}

- (BOOL)updateAllLayers:(NSTimeInterval)deltaTime {
    if (!self.handle) return NO;
    return FBFSVGPlayer_UpdateAllLayers(self.handle, deltaTime);
}

- (void)playAllLayers {
    if (!self.handle) return;
    FBFSVGPlayer_PlayAllLayers(self.handle);
}

- (void)pauseAllLayers {
    if (!self.handle) return;
    FBFSVGPlayer_PauseAllLayers(self.handle);
}

- (void)stopAllLayers {
    if (!self.handle) return;
    FBFSVGPlayer_StopAllLayers(self.handle);
}

#pragma mark - Version Information

+ (NSString *)version {
    // Use unified API version function which uses version.h
    const char* ver = FBFSVGPlayer_GetVersion();
    // Check for NULL to prevent crash in stringWithUTF8String:
    return ver ? [NSString stringWithUTF8String:ver] : @"unknown";
}

+ (void)getVersionMajor:(NSInteger *)major minor:(NSInteger *)minor patch:(NSInteger *)patch {
    int maj = 0, min = 0, pat = 0;
    FBFSVGPlayer_GetVersionNumbers(&maj, &min, &pat);
    if (major) *major = maj;
    if (minor) *minor = min;
    if (patch) *patch = pat;
}

+ (NSString *)buildInfo {
    // Return build info from version.h
    // Check for NULL to prevent crash in stringWithUTF8String:
#ifdef FBFSVG_PLAYER_BUILD_INFO
    const char* info = FBFSVG_PLAYER_BUILD_INFO;
    return info ? [NSString stringWithUTF8String:info] : @"unknown";
#else
    return @"unknown";
#endif
}

@end
