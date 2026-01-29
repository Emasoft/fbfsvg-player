// FBFSVGPlayerController.mm - macOS Objective-C wrapper around unified C SVG player API
//
// This implementation bridges the unified C API (svg_player_api.h) to Objective-C,
// providing a clean interface for SVGPlayerView and direct users on macOS.
//
// The unified API provides full cross-platform functionality.

#import "FBFSVGPlayerController.h"
#import "../../shared/fbfsvg_player_api.h"

// Error domain for FBFSVGPlayerController errors
NSString * const FBFSVGPlayerControllerErrorDomain = @"com.svgplayer.controller.error";

// Default frame rate for static SVGs or when not specified
static const CGFloat kDefaultFrameRate = 60.0;

// Default seek interval for rewind/fastforward
static const NSTimeInterval kDefaultSeekInterval = 5.0;

// Safe string conversion helper (Issue 12)
static NSString *safeStringFromCString(const char *cString) {
    if (!cString) return nil;
    NSString *str = [NSString stringWithUTF8String:cString];
    if (str) return str;
    return [NSString stringWithCString:cString encoding:NSISOLatin1StringEncoding];
}

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

// Position
- (void)setPosition:(NSPoint)position {
    if (self.layerRef) {
        FBFSVGLayer_SetPosition(self.layerRef, position.x, position.y);
    }
}

- (NSPoint)position {
    NSPoint pos = NSZeroPoint;
    if (self.layerRef) {
        float x = 0, y = 0;
        FBFSVGLayer_GetPosition(self.layerRef, &x, &y);
        pos = NSMakePoint(x, y);
    }
    return pos;
}

// Opacity
- (void)setOpacity:(CGFloat)opacity {
    if (self.layerRef) {
        FBFSVGLayer_SetOpacity(self.layerRef, opacity);
    }
}

- (CGFloat)opacity {
    if (self.layerRef) {
        return FBFSVGLayer_GetOpacity(self.layerRef);
    }
    return 1.0;
}

// Z-Order
- (void)setZOrder:(NSInteger)zOrder {
    if (self.layerRef) {
        if (zOrder < INT_MIN) zOrder = INT_MIN;
        if (zOrder > INT_MAX) zOrder = INT_MAX;
        FBFSVGLayer_SetZOrder(self.layerRef, (int)zOrder);
    }
}

- (NSInteger)zOrder {
    if (self.layerRef) {
        return FBFSVGLayer_GetZOrder(self.layerRef);
    }
    return 0;
}

// Visibility
- (void)setVisible:(BOOL)visible {
    if (self.layerRef) {
        FBFSVGLayer_SetVisible(self.layerRef, visible);
    }
}

- (BOOL)isVisible {
    if (self.layerRef) {
        return FBFSVGLayer_IsVisible(self.layerRef);
    }
    return YES;
}

// Scale
- (void)setScale:(NSPoint)scale {
    if (self.layerRef) {
        FBFSVGLayer_SetScale(self.layerRef, scale.x, scale.y);
    }
}

- (NSPoint)scale {
    NSPoint scale = NSMakePoint(1.0, 1.0);
    if (self.layerRef) {
        float scaleX = 1.0, scaleY = 1.0;
        FBFSVGLayer_GetScale(self.layerRef, &scaleX, &scaleY);
        scale = NSMakePoint(scaleX, scaleY);
    }
    return scale;
}

// Rotation
- (void)setRotation:(CGFloat)rotation {
    if (self.layerRef) {
        FBFSVGLayer_SetRotation(self.layerRef, rotation);
    }
}

- (CGFloat)rotation {
    if (self.layerRef) {
        return FBFSVGLayer_GetRotation(self.layerRef);
    }
    return 0.0;
}

// Blend mode
- (void)setBlendMode:(FBFSVGPlayerLayerBlendMode)blendMode {
    if (self.layerRef) {
        // Convert from Obj-C enum to C enum (same underlying values)
        FBFSVGLayer_SetBlendMode(self.layerRef, (FBFSVGLayerBlendMode)blendMode);
    }
}

- (FBFSVGPlayerLayerBlendMode)blendMode {
    if (self.layerRef) {
        // Convert from C enum to Obj-C enum (same underlying values)
        return (FBFSVGPlayerLayerBlendMode)FBFSVGLayer_GetBlendMode(self.layerRef);
    }
    return FBFSVGPlayerLayerBlendModeNormal;
}

// Size (read-only)
- (NSSize)size {
    if (self.layerRef) {
        int width = 0, height = 0;
        if (FBFSVGLayer_GetSize(self.layerRef, &width, &height)) {
            return NSMakeSize(width, height);
        }
    }
    return NSZeroSize;
}

// Duration (read-only)
- (NSTimeInterval)duration {
    if (self.layerRef) {
        return FBFSVGLayer_GetDuration(self.layerRef);
    }
    return 0.0;
}

// Current time (read-only)
- (NSTimeInterval)currentTime {
    // Not directly available from C API, would need to be tracked in layer state
    return 0.0;
}

// Has animations (read-only)
- (BOOL)hasAnimations {
    if (self.layerRef) {
        return FBFSVGLayer_HasAnimations(self.layerRef);
    }
    return NO;
}

// Playback control
- (void)play {
    if (self.layerRef) {
        FBFSVGLayer_Play(self.layerRef);
    }
}

- (void)pause {
    if (self.layerRef) {
        FBFSVGLayer_Pause(self.layerRef);
    }
}

- (void)stop {
    if (self.layerRef) {
        FBFSVGLayer_Stop(self.layerRef);
    }
}

- (void)seekToTime:(NSTimeInterval)time {
    if (self.layerRef) {
        FBFSVGLayer_SeekTo(self.layerRef, time);
    }
}

- (BOOL)update:(NSTimeInterval)deltaTime {
    if (self.layerRef) {
        return FBFSVGLayer_Update(self.layerRef, deltaTime);
    }
    return NO;
}

@end

#pragma mark - FBFSVGPlayerController

@interface FBFSVGPlayerController ()
// The underlying unified C API handle
@property (nonatomic, assign) FBFSVGPlayerRef handle;
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
// Thread safety queue (Issue 1)
@property (nonatomic, strong) dispatch_queue_t apiQueue;
// Track last loop count to detect new loops
@property (nonatomic, assign) NSInteger lastLoopCount;
@end

@implementation FBFSVGPlayerController

#pragma mark - Initialization

+ (instancetype)controller {
    return [[self alloc] init];
}

- (instancetype)init {
    if (self = [super init]) {
        _apiQueue = dispatch_queue_create("com.svgplayer.controller.api", DISPATCH_QUEUE_SERIAL);
        _handle = FBFSVGPlayer_Create();
        _internalPlaybackState = SVGControllerPlaybackStateStopped;
        _looping = YES;
        _internalPlaybackRate = 1.0;
        _internalRepeatMode = SVGControllerRepeatModeLoop;
        _internalRepeatCount = 1;
        _internalScrubbing = NO;
        _lastLoopCount = 0;

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
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to load SVG";
        self.internalErrorMessage = message;
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return NO;
    }

    // Apply settings
    FBFSVGPlayer_SetLooping(self.handle, self.looping);
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    self.internalErrorMessage = nil;
    self.lastLoopCount = 0;

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
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to parse SVG data";
        self.internalErrorMessage = message;
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return NO;
    }

    // Apply settings
    FBFSVGPlayer_SetLooping(self.handle, self.looping);
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    self.internalErrorMessage = nil;
    self.lastLoopCount = 0;

    return YES;
}

- (void)unload {
    if (self.handle) {
        FBFSVGPlayer_Unload(self.handle);
    }
    self.internalPlaybackState = SVGControllerPlaybackStateStopped;
}

#pragma mark - State Properties

- (BOOL)isLoaded {
    if (!self.handle) return NO;
    return FBFSVGPlayer_IsLoaded(self.handle);
}

- (NSSize)intrinsicSize {
    if (!self.handle) return NSZeroSize;

    int width = 0, height = 0;
    if (FBFSVGPlayer_GetSize(self.handle, &width, &height)) {
        return NSMakeSize(width, height);
    }
    return NSZeroSize;
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
    _internalRepeatMode = looping ? SVGControllerRepeatModeLoop : SVGControllerRepeatModeNone;
}

- (NSTimeInterval)currentTime {
    if (!self.handle) return 0;
    return FBFSVGPlayer_GetCurrentTime(self.handle);
}

- (SVGControllerPlaybackState)playbackState {
    return self.internalPlaybackState;
}

- (SVGRenderStatistics)statistics {
    SVGRenderStatistics stats = {0};
    if (self.handle) {
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
        FBFSVGPlayer_SetRepeatMode(self.handle, cMode);
    }
}

- (NSInteger)repeatCount {
    return self.internalRepeatCount;
}

- (void)setRepeatCount:(NSInteger)repeatCount {
    self.internalRepeatCount = MAX(1, repeatCount);
    if (self.handle) {
        FBFSVGPlayer_SetRepeatCount(self.handle, (int)self.internalRepeatCount);
    }
}

- (NSInteger)currentRepeatIteration {
    if (self.handle) {
        return (NSInteger)FBFSVGPlayer_GetCompletedLoops(self.handle);
    }
    return 0;
}

- (BOOL)isPlayingForward {
    if (self.handle) {
        return FBFSVGPlayer_IsPlayingForward(self.handle);
    }
    return YES;
}

- (CGFloat)playbackRate {
    return self.internalPlaybackRate;
}

- (void)setPlaybackRate:(CGFloat)playbackRate {
    self.internalPlaybackRate = MAX(0.1, MIN(10.0, playbackRate));
    if (self.handle) {
        FBFSVGPlayer_SetPlaybackRate(self.handle, (float)self.internalPlaybackRate);
    }
}

#pragma mark - Timeline Properties

- (CGFloat)progress {
    if (self.handle) {
        return (CGFloat)FBFSVGPlayer_GetProgress(self.handle);
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
        return (NSInteger)FBFSVGPlayer_GetCurrentFrame(self.handle);
    }
    return 0;
}

- (NSInteger)totalFrames {
    if (self.handle) {
        return (NSInteger)FBFSVGPlayer_GetTotalFrames(self.handle);
    }
    return 0;
}

- (CGFloat)frameRate {
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
    __block SVGControllerPlaybackState oldState;
    dispatch_sync(self.apiQueue, ^{
        if (!self.handle || !self.isLoaded) return;
        oldState = self.internalPlaybackState;
        FBFSVGPlayer_Play(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
    });
    // Notify delegate if state changed
    if (oldState != SVGControllerPlaybackStatePlaying) {
        [self notifyDelegateOfPlaybackStateChange:SVGControllerPlaybackStatePlaying];
    }
}

- (void)pause {
    __block SVGControllerPlaybackState oldState;
    dispatch_sync(self.apiQueue, ^{
        if (!self.handle) return;
        oldState = self.internalPlaybackState;
        FBFSVGPlayer_Pause(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    });
    // Notify delegate if state changed
    if (oldState != SVGControllerPlaybackStatePaused) {
        [self notifyDelegateOfPlaybackStateChange:SVGControllerPlaybackStatePaused];
    }
}

- (void)resume {
    if (self.internalPlaybackState == SVGControllerPlaybackStatePaused) {
        [self play];
    }
}

- (void)stop {
    __block SVGControllerPlaybackState oldState;
    dispatch_sync(self.apiQueue, ^{
        if (!self.handle) return;
        oldState = self.internalPlaybackState;
        FBFSVGPlayer_Stop(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStateStopped;
        self.lastLoopCount = 0;
    });
    // Notify delegate if state changed
    if (oldState != SVGControllerPlaybackStateStopped) {
        [self notifyDelegateOfPlaybackStateChange:SVGControllerPlaybackStateStopped];
    }
}

- (void)togglePlayback {
    if (!self.handle) return;

    SVGControllerPlaybackState oldState = self.internalPlaybackState;
    FBFSVGPlayer_TogglePlayback(self.handle);
    SVGPlaybackState cState = FBFSVGPlayer_GetPlaybackState(self.handle);
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
    // Notify delegate if state changed
    if (oldState != self.internalPlaybackState) {
        [self notifyDelegateOfPlaybackStateChange:self.internalPlaybackState];
    }
}

#pragma mark - Animation Update

- (void)update:(NSTimeInterval)deltaTime {
    [self update:deltaTime forward:YES];
}

- (void)update:(NSTimeInterval)deltaTime forward:(BOOL)forward {
    if (!self.handle || self.internalPlaybackState != SVGControllerPlaybackStatePlaying) return;

    SVGControllerPlaybackState oldState = self.internalPlaybackState;
    NSTimeInterval adjustedDelta = deltaTime * self.internalPlaybackRate;
    if (!forward) {
        adjustedDelta = -adjustedDelta;
    }

    FBFSVGPlayer_Update(self.handle, adjustedDelta);

    // Check for loop completion
    NSInteger currentLoopCount = (NSInteger)FBFSVGPlayer_GetCompletedLoops(self.handle);
    if (currentLoopCount > self.lastLoopCount) {
        self.lastLoopCount = currentLoopCount;
        [self notifyDelegateOfLoop:currentLoopCount];
    }

    // Sync our internal state with the unified API state
    SVGPlaybackState cState = FBFSVGPlayer_GetPlaybackState(self.handle);
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

    // Check if animation ended (non-looping mode: state changed from playing to stopped)
    if (oldState == SVGControllerPlaybackStatePlaying &&
        self.internalPlaybackState == SVGControllerPlaybackStateStopped &&
        self.internalRepeatMode == SVGControllerRepeatModeNone) {
        [self notifyDelegateOfEnd];
    }

    // Notify delegate if state changed
    if (oldState != self.internalPlaybackState) {
        [self notifyDelegateOfPlaybackStateChange:self.internalPlaybackState];
    }
}

#pragma mark - Seeking

- (void)seekToTime:(NSTimeInterval)time {
    if (!self.handle) return;
    FBFSVGPlayer_SeekTo(self.handle, time);
}

- (void)seekToFrame:(NSInteger)frame {
    if (!self.handle) return;
    NSInteger total = self.totalFrames;
    if (frame < 0) frame = 0;
    if (total > 0 && frame >= total) frame = total - 1;
    FBFSVGPlayer_SeekToFrame(self.handle, (int)frame);
}

- (void)seekToProgress:(CGFloat)progress {
    if (self.handle) {
        FBFSVGPlayer_SeekToProgress(self.handle, (float)progress);
    }
}

- (void)seekToStart {
    if (self.handle) {
        FBFSVGPlayer_SeekToStart(self.handle);
    }
}

- (void)seekToEnd {
    if (self.handle) {
        FBFSVGPlayer_SeekToEnd(self.handle);
    }
}

#pragma mark - Frame Stepping

- (void)stepForward {
    if (self.handle) {
        FBFSVGPlayer_StepForward(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)stepBackward {
    if (self.handle) {
        FBFSVGPlayer_StepBackward(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)stepByFrames:(NSInteger)frameCount {
    if (self.handle) {
        FBFSVGPlayer_StepByFrames(self.handle, (int)frameCount);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

#pragma mark - Relative Seeking

- (void)seekForwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    if (self.handle) {
        FBFSVGPlayer_SeekForwardByTime(self.handle, seconds);
    }
}

- (void)seekBackwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
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
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
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
        SVGPlaybackState cState = FBFSVGPlayer_GetPlaybackState(self.handle);
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
            self.internalErrorMessage = safeStringFromCString(errorMsg);
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

    BOOL success = FBFSVGPlayer_RenderAtTime(self.handle, buffer,
                                           (int)width, (int)height,
                                           (float)scale, time);

    if (!success) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
        if (errorMsg) {
            self.internalErrorMessage = safeStringFromCString(errorMsg);
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
    return [FBFSVGPlayerController formatTime:self.currentTime];
}

- (NSString *)formattedRemainingTime {
    return [NSString stringWithFormat:@"-%@", [FBFSVGPlayerController formatTime:self.remainingTime]];
}

- (NSString *)formattedDuration {
    return [FBFSVGPlayerController formatTime:self.duration];
}

- (NSInteger)frameForTime:(NSTimeInterval)time {
    if (self.handle) {
        return (NSInteger)FBFSVGPlayer_TimeToFrame(self.handle, time);
    }
    return 0;
}

- (NSTimeInterval)timeForFrame:(NSInteger)frame {
    if (self.handle) {
        return FBFSVGPlayer_FrameToTime(self.handle, (int)frame);
    }
    return 0;
}

#pragma mark - Error Handling

- (void)setError:(NSError * _Nullable *)error code:(FBFSVGPlayerControllerErrorCode)code message:(NSString *)message {
    self.internalErrorMessage = message;

    if (error) {
        *error = [NSError errorWithDomain:FBFSVGPlayerControllerErrorDomain
                                     code:code
                                 userInfo:@{NSLocalizedDescriptionKey: message}];
    }

    // Notify delegate of error
    [self notifyDelegateOfError:code message:message];
}

#pragma mark - Delegate Notifications

// Notify delegate of playback state change on main thread
- (void)notifyDelegateOfPlaybackStateChange:(SVGControllerPlaybackState)newState {
    if (!self.delegate) return;
    if (![self.delegate respondsToSelector:@selector(svgPlayerController:didChangePlaybackState:)]) return;

    if ([NSThread isMainThread]) {
        [self.delegate svgPlayerController:self didChangePlaybackState:newState];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate svgPlayerController:self didChangePlaybackState:newState];
        });
    }
}

// Notify delegate of loop completion on main thread
- (void)notifyDelegateOfLoop:(NSInteger)loopCount {
    if (!self.delegate) return;
    if (![self.delegate respondsToSelector:@selector(svgPlayerController:didLoopWithCount:)]) return;

    if ([NSThread isMainThread]) {
        [self.delegate svgPlayerController:self didLoopWithCount:loopCount];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate svgPlayerController:self didLoopWithCount:loopCount];
        });
    }
}

// Notify delegate of animation end on main thread
- (void)notifyDelegateOfEnd {
    if (!self.delegate) return;
    if (![self.delegate respondsToSelector:@selector(svgPlayerControllerDidReachEnd:)]) return;

    if ([NSThread isMainThread]) {
        [self.delegate svgPlayerControllerDidReachEnd:self];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate svgPlayerControllerDidReachEnd:self];
        });
    }
}

// Notify delegate of error on main thread
- (void)notifyDelegateOfError:(NSInteger)errorCode message:(NSString *)message {
    if (!self.delegate) return;
    if (![self.delegate respondsToSelector:@selector(svgPlayerController:didEncounterError:message:)]) return;

    if ([NSThread isMainThread]) {
        [self.delegate svgPlayerController:self didEncounterError:errorCode message:message];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate svgPlayerController:self didEncounterError:errorCode message:message];
        });
    }
}

// Notify delegate of element touch on main thread
- (void)notifyDelegateOfElementTouch:(NSString *)elementID viewPoint:(NSPoint)viewPoint svgPoint:(NSPoint)svgPoint {
    if (!self.delegate) return;
    if (![self.delegate respondsToSelector:@selector(svgPlayerController:didTouchElementWithID:atViewPoint:svgPoint:)]) return;

    if ([NSThread isMainThread]) {
        [self.delegate svgPlayerController:self didTouchElementWithID:elementID atViewPoint:viewPoint svgPoint:svgPoint];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate svgPlayerController:self didTouchElementWithID:elementID atViewPoint:viewPoint svgPoint:svgPoint];
        });
    }
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

- (nullable NSString *)hitTestAtPoint:(NSPoint)point viewSize:(NSSize)viewSize {
    if (!self.handle) return nil;

    const char *elementID = FBFSVGPlayer_HitTest(self.handle,
                                               (float)point.x, (float)point.y,
                                               (int)viewSize.width, (int)viewSize.height);
    if (elementID && strlen(elementID) > 0) {
        NSString *elementIDStr = safeStringFromCString(elementID);
        // Convert to SVG coordinates for delegate notification
        NSPoint svgPoint = [self convertViewPointToSVG:point viewSize:viewSize];
        [self notifyDelegateOfElementTouch:elementIDStr viewPoint:point svgPoint:svgPoint];
        return elementIDStr;
    }
    return nil;
}

- (NSArray<NSString *> *)elementsAtPoint:(NSPoint)point
                                viewSize:(NSSize)viewSize
                             maxElements:(NSInteger)maxElements {
    if (!self.handle) return @[];

    // Allocate array for element IDs - use unified API function
    const char **elementIDs = (const char **)malloc(sizeof(const char *) * maxElements);
    if (!elementIDs) return @[];

    int foundCount = FBFSVGPlayer_GetElementsAtPoint(self.handle,
                                                   (float)point.x, (float)point.y,
                                                   (int)viewSize.width, (int)viewSize.height,
                                                   elementIDs, (int)maxElements);

    NSMutableArray<NSString *> *result = [NSMutableArray arrayWithCapacity:foundCount];
    for (int i = 0; i < foundCount; i++) {
        if (elementIDs[i] && strlen(elementIDs[i]) > 0) {
            NSString *elementIDStr = safeStringFromCString(elementIDs[i]);
            if (elementIDStr) {
                [result addObject:elementIDStr];
            }
        }
    }

    free(elementIDs);
    return [result copy];
}

- (NSRect)boundingRectForElementID:(NSString *)objectID {
    if (!self.handle || !objectID) return NSZeroRect;

    // Use unified API with SVGRect struct
    SVGRect bounds;
    if (FBFSVGPlayer_GetElementBounds(self.handle, [objectID UTF8String], &bounds)) {
        return NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
    }
    return NSZeroRect;
}

- (BOOL)elementExistsWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return NO;
    return FBFSVGPlayer_ElementExists(self.handle, [objectID UTF8String]);
}

- (nullable NSString *)propertyValue:(NSString *)propertyName forElementID:(NSString *)objectID {
    if (!self.handle || !propertyName || !objectID) return nil;

    char valueBuffer[4096];
    memset(valueBuffer, 0, sizeof(valueBuffer));
    if (FBFSVGPlayer_GetElementProperty(self.handle,
                                      [objectID UTF8String],
                                      [propertyName UTF8String],
                                      valueBuffer, sizeof(valueBuffer) - 1)) {
        valueBuffer[sizeof(valueBuffer) - 1] = '\0';
        if (strlen(valueBuffer) > 0) {
            return safeStringFromCString(valueBuffer);
        }
    }
    return nil;
}

#pragma mark - Coordinate Conversion

- (NSPoint)convertViewPointToSVG:(NSPoint)viewPoint viewSize:(NSSize)viewSize {
    if (!self.handle) return NSZeroPoint;

    // Use unified API for coordinate conversion
    float svgX = 0, svgY = 0;
    if (FBFSVGPlayer_ViewToSVG(self.handle,
                             (float)viewPoint.x, (float)viewPoint.y,
                             (int)viewSize.width, (int)viewSize.height,
                             &svgX, &svgY)) {
        return NSMakePoint(svgX, svgY);
    }
    return NSZeroPoint;
}

- (NSPoint)convertSVGPointToView:(NSPoint)svgPoint viewSize:(NSSize)viewSize {
    if (!self.handle) return NSZeroPoint;

    // Use unified API for coordinate conversion
    float viewX = 0, viewY = 0;
    if (FBFSVGPlayer_SVGToView(self.handle,
                             (float)svgPoint.x, (float)svgPoint.y,
                             (int)viewSize.width, (int)viewSize.height,
                             &viewX, &viewY)) {
        return NSMakePoint(viewX, viewY);
    }
    return NSZeroPoint;
}

#pragma mark - Zoom and ViewBox

- (BOOL)getViewBoxX:(CGFloat *)x y:(CGFloat *)y width:(CGFloat *)width height:(CGFloat *)height {
    if (!self.handle) return NO;
    float fx = 0, fy = 0, fw = 0, fh = 0;
    if (FBFSVGPlayer_GetViewBox(self.handle, &fx, &fy, &fw, &fh)) {
        if (x) *x = fx;
        if (y) *y = fy;
        if (width) *width = fw;
        if (height) *height = fh;
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

- (void)setZoom:(CGFloat)zoom centeredAt:(NSPoint)center viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_SetZoom(self.handle, (float)zoom,
                      (float)center.x, (float)center.y,
                      (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomInByFactor:(CGFloat)factor viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_ZoomIn(self.handle, (float)factor,
                     (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomOutByFactor:(CGFloat)factor viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    FBFSVGPlayer_ZoomOut(self.handle, (float)factor,
                      (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomToRect:(NSRect)rect {
    if (!self.handle) return;
    FBFSVGPlayer_ZoomToRect(self.handle,
                         (float)NSMinX(rect), (float)NSMinY(rect),
                         (float)NSWidth(rect), (float)NSHeight(rect));
}

- (BOOL)zoomToElementWithID:(NSString *)objectID padding:(CGFloat)padding {
    if (!self.handle || !objectID) return NO;
    return FBFSVGPlayer_ZoomToElement(self.handle, [objectID UTF8String], (float)padding);
}

- (void)panByDelta:(NSPoint)delta viewSize:(NSSize)viewSize {
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

- (void)setTargetFrameRate:(CGFloat)targetFrameRate {
    if (!self.handle) return;
    FBFSVGPlayer_SetTargetFrameRate(self.handle, (float)targetFrameRate);
}

- (CGFloat)targetFrameRate {
    if (!self.handle) return kDefaultFrameRate;
    float rate = FBFSVGPlayer_GetTargetFrameRate(self.handle);
    return rate > 0 ? (CGFloat)rate : kDefaultFrameRate;
}

- (NSTimeInterval)idealFrameInterval {
    if (!self.handle) return 1.0 / kDefaultFrameRate;
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
    return (NSInteger)FBFSVGPlayer_GetDroppedFrameCount(self.handle);
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

#pragma mark - Version Information

+ (NSString *)version {
    const char *versionStr = FBFSVGPlayer_GetVersion();
    return safeStringFromCString(versionStr) ?: @"unknown";
}

+ (void)getVersionMajor:(NSInteger *)major minor:(NSInteger *)minor patch:(NSInteger *)patch {
    int maj = 0, min = 0, pat = 0;
    FBFSVGPlayer_GetVersionNumbers(&maj, &min, &pat);
    if (major) *major = maj;
    if (minor) *minor = min;
    if (patch) *patch = pat;
}

+ (NSString *)buildInfo {
    // Build info from version numbers and platform
    NSInteger major = 0, minor = 0, patch = 0;
    [self getVersionMajor:&major minor:&minor patch:&patch];
    return [NSString stringWithFormat:@"SVGPlayer %@ (macOS arm64)", [self version]];
}

#pragma mark - Multi-SVG Compositing

- (FBFSVGPlayerLayer *)createLayerFromPath:(NSString *)filepath error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:FBFSVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return nil;
    }

    // Check if file exists
    if (![[NSFileManager defaultManager] fileExistsAtPath:filepath]) {
        NSString *message = [NSString stringWithFormat:@"SVG file not found: %@", filepath];
        [self setError:error code:FBFSVGPlayerControllerErrorFileNotFound message:message];
        return nil;
    }

    FBFSVGLayerRef layerRef = FBFSVGPlayer_CreateLayer(self.handle, [filepath UTF8String]);
    if (!layerRef) {
        const char *errorMsg = FBFSVGPlayer_GetLastError(self.handle);
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to create layer from file";
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
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to create layer from data";
        [self setError:error code:FBFSVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[FBFSVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (void)destroyLayer:(FBFSVGPlayerLayer *)layer {
    if (!self.handle || !layer) return;

    FBFSVGLayerRef layerRef = layer.layerRef;
    if (layerRef) {
        FBFSVGPlayer_DestroyLayer(self.handle, layerRef);
        layer.layerRef = NULL;
    }
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
    return FBFSVGPlayer_RenderComposite(self.handle, buffer, (int)width, (int)height, scale);
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale
                         atTime:(NSTimeInterval)time {
    if (!self.handle || !buffer) return NO;
    return FBFSVGPlayer_RenderCompositeAtTime(self.handle, buffer, (int)width, (int)height, scale, time);
}

- (BOOL)updateAllLayers:(NSTimeInterval)deltaTime {
    if (!self.handle) return NO;
    return FBFSVGPlayer_UpdateAllLayers(self.handle, deltaTime);
}

- (void)playAllLayers {
    if (self.handle) {
        FBFSVGPlayer_PlayAllLayers(self.handle);
    }
}

- (void)pauseAllLayers {
    if (self.handle) {
        FBFSVGPlayer_PauseAllLayers(self.handle);
    }
}

- (void)stopAllLayers {
    if (self.handle) {
        FBFSVGPlayer_StopAllLayers(self.handle);
    }
}

@end
