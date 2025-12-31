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

// Safe string conversion helper (Issue 12)
static NSString *safeStringFromCString(const char *cString) {
    if (!cString) return nil;
    NSString *str = [NSString stringWithUTF8String:cString];
    if (str) return str;
    return [NSString stringWithCString:cString encoding:NSISOLatin1StringEncoding];
}

#pragma mark - SVGPlayerLayer Implementation

@interface SVGPlayerLayer ()
@property (nonatomic, assign) SVGLayerRef layerRef;
@end

@implementation SVGPlayerLayer

- (instancetype)initWithLayerRef:(SVGLayerRef)layerRef {
    if (self = [super init]) {
        _layerRef = layerRef;
    }
    return self;
}

// Position
- (void)setPosition:(NSPoint)position {
    if (self.layerRef) {
        SVGLayer_SetPosition(self.layerRef, position.x, position.y);
    }
}

- (NSPoint)position {
    NSPoint pos = NSZeroPoint;
    if (self.layerRef) {
        float x = 0, y = 0;
        SVGLayer_GetPosition(self.layerRef, &x, &y);
        pos = NSMakePoint(x, y);
    }
    return pos;
}

// Opacity
- (void)setOpacity:(CGFloat)opacity {
    if (self.layerRef) {
        SVGLayer_SetOpacity(self.layerRef, opacity);
    }
}

- (CGFloat)opacity {
    if (self.layerRef) {
        return SVGLayer_GetOpacity(self.layerRef);
    }
    return 1.0;
}

// Z-Order
- (void)setZOrder:(NSInteger)zOrder {
    if (self.layerRef) {
        if (zOrder < INT_MIN) zOrder = INT_MIN;
        if (zOrder > INT_MAX) zOrder = INT_MAX;
        SVGLayer_SetZOrder(self.layerRef, (int)zOrder);
    }
}

- (NSInteger)zOrder {
    if (self.layerRef) {
        return SVGLayer_GetZOrder(self.layerRef);
    }
    return 0;
}

// Visibility
- (void)setVisible:(BOOL)visible {
    if (self.layerRef) {
        SVGLayer_SetVisible(self.layerRef, visible);
    }
}

- (BOOL)isVisible {
    if (self.layerRef) {
        return SVGLayer_IsVisible(self.layerRef);
    }
    return YES;
}

// Scale
- (void)setScale:(NSPoint)scale {
    if (self.layerRef) {
        SVGLayer_SetScale(self.layerRef, scale.x, scale.y);
    }
}

- (NSPoint)scale {
    NSPoint scale = NSMakePoint(1.0, 1.0);
    if (self.layerRef) {
        float scaleX = 1.0, scaleY = 1.0;
        SVGLayer_GetScale(self.layerRef, &scaleX, &scaleY);
        scale = NSMakePoint(scaleX, scaleY);
    }
    return scale;
}

// Rotation
- (void)setRotation:(CGFloat)rotation {
    if (self.layerRef) {
        SVGLayer_SetRotation(self.layerRef, rotation);
    }
}

- (CGFloat)rotation {
    if (self.layerRef) {
        return SVGLayer_GetRotation(self.layerRef);
    }
    return 0.0;
}

// Blend mode
- (void)setBlendMode:(SVGPlayerLayerBlendMode)blendMode {
    if (self.layerRef) {
        // Convert from Obj-C enum to C enum (same underlying values)
        SVGLayer_SetBlendMode(self.layerRef, (SVGLayerBlendMode)blendMode);
    }
}

- (SVGPlayerLayerBlendMode)blendMode {
    if (self.layerRef) {
        // Convert from C enum to Obj-C enum (same underlying values)
        return (SVGPlayerLayerBlendMode)SVGLayer_GetBlendMode(self.layerRef);
    }
    return SVGPlayerLayerBlendModeNormal;
}

// Size (read-only)
- (NSSize)size {
    if (self.layerRef) {
        int width = 0, height = 0;
        if (SVGLayer_GetSize(self.layerRef, &width, &height)) {
            return NSMakeSize(width, height);
        }
    }
    return NSZeroSize;
}

// Duration (read-only)
- (NSTimeInterval)duration {
    if (self.layerRef) {
        return SVGLayer_GetDuration(self.layerRef);
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
        return SVGLayer_HasAnimations(self.layerRef);
    }
    return NO;
}

// Playback control
- (void)play {
    if (self.layerRef) {
        SVGLayer_Play(self.layerRef);
    }
}

- (void)pause {
    if (self.layerRef) {
        SVGLayer_Pause(self.layerRef);
    }
}

- (void)stop {
    if (self.layerRef) {
        SVGLayer_Stop(self.layerRef);
    }
}

- (void)seekToTime:(NSTimeInterval)time {
    if (self.layerRef) {
        SVGLayer_SeekTo(self.layerRef, time);
    }
}

- (BOOL)update:(NSTimeInterval)deltaTime {
    if (self.layerRef) {
        return SVGLayer_Update(self.layerRef, deltaTime);
    }
    return NO;
}

@end

#pragma mark - SVGPlayerController

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
// Thread safety queue (Issue 1)
@property (nonatomic, strong) dispatch_queue_t apiQueue;
@end

@implementation SVGPlayerController

#pragma mark - Initialization

+ (instancetype)controller {
    return [[self alloc] init];
}

- (instancetype)init {
    if (self = [super init]) {
        _apiQueue = dispatch_queue_create("com.svgplayer.controller.api", DISPATCH_QUEUE_SERIAL);
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
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to load SVG";
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
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to parse SVG data";
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
    dispatch_sync(self.apiQueue, ^{
        if (!self.handle || !self.isLoaded) return;
        SVGPlayer_Play(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePlaying;
    });
}

- (void)pause {
    dispatch_sync(self.apiQueue, ^{
        if (!self.handle) return;
        SVGPlayer_Pause(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    });
}

- (void)resume {
    if (self.internalPlaybackState == SVGControllerPlaybackStatePaused) {
        [self play];
    }
}

- (void)stop {
    dispatch_sync(self.apiQueue, ^{
        if (!self.handle) return;
        SVGPlayer_Stop(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStateStopped;
    });
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
    if (!self.handle) return;
    NSInteger total = self.totalFrames;
    if (frame < 0) frame = 0;
    if (total > 0 && frame >= total) frame = total - 1;
    SVGPlayer_SeekToFrame(self.handle, (int)frame);
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

    BOOL success = SVGPlayer_RenderAtTime(self.handle, buffer,
                                           (int)width, (int)height,
                                           (float)scale, time);

    if (!success) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
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

#pragma mark - Hit Testing - Element Subscription

- (void)subscribeToElementWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return;
    SVGPlayer_SubscribeToElement(self.handle, [objectID UTF8String]);
}

- (void)unsubscribeFromElementWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return;
    SVGPlayer_UnsubscribeFromElement(self.handle, [objectID UTF8String]);
}

- (void)unsubscribeFromAllElements {
    if (!self.handle) return;
    SVGPlayer_UnsubscribeFromAllElements(self.handle);
}

#pragma mark - Hit Testing - Queries

- (nullable NSString *)hitTestAtPoint:(NSPoint)point viewSize:(NSSize)viewSize {
    if (!self.handle) return nil;

    const char *elementID = SVGPlayer_HitTest(self.handle,
                                               (float)point.x, (float)point.y,
                                               (int)viewSize.width, (int)viewSize.height);
    if (elementID && strlen(elementID) > 0) {
        return safeStringFromCString(elementID);
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

    int foundCount = SVGPlayer_GetElementsAtPoint(self.handle,
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
    if (SVGPlayer_GetElementBounds(self.handle, [objectID UTF8String], &bounds)) {
        return NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
    }
    return NSZeroRect;
}

- (BOOL)elementExistsWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return NO;
    return SVGPlayer_ElementExists(self.handle, [objectID UTF8String]);
}

- (nullable NSString *)propertyValue:(NSString *)propertyName forElementID:(NSString *)objectID {
    if (!self.handle || !propertyName || !objectID) return nil;

    char valueBuffer[4096];
    memset(valueBuffer, 0, sizeof(valueBuffer));
    if (SVGPlayer_GetElementProperty(self.handle,
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
    if (SVGPlayer_ViewToSVG(self.handle,
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
    if (SVGPlayer_SVGToView(self.handle,
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
    if (SVGPlayer_GetViewBox(self.handle, &fx, &fy, &fw, &fh)) {
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
    SVGPlayer_SetViewBox(self.handle, (float)x, (float)y, (float)width, (float)height);
}

- (void)resetViewBox {
    if (!self.handle) return;
    SVGPlayer_ResetViewBox(self.handle);
}

- (CGFloat)zoom {
    if (!self.handle) return 1.0;
    return (CGFloat)SVGPlayer_GetZoom(self.handle);
}

- (void)setZoom:(CGFloat)zoom centeredAt:(NSPoint)center viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_SetZoom(self.handle, (float)zoom,
                      (float)center.x, (float)center.y,
                      (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomInByFactor:(CGFloat)factor viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_ZoomIn(self.handle, (float)factor,
                     (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomOutByFactor:(CGFloat)factor viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_ZoomOut(self.handle, (float)factor,
                      (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomToRect:(NSRect)rect {
    if (!self.handle) return;
    SVGPlayer_ZoomToRect(self.handle,
                         (float)NSMinX(rect), (float)NSMinY(rect),
                         (float)NSWidth(rect), (float)NSHeight(rect));
}

- (BOOL)zoomToElementWithID:(NSString *)objectID padding:(CGFloat)padding {
    if (!self.handle || !objectID) return NO;
    return SVGPlayer_ZoomToElement(self.handle, [objectID UTF8String], (float)padding);
}

- (void)panByDelta:(NSPoint)delta viewSize:(NSSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_Pan(self.handle, (float)delta.x, (float)delta.y,
                  (int)viewSize.width, (int)viewSize.height);
}

- (CGFloat)minZoom {
    if (!self.handle) return 0.1;
    return (CGFloat)SVGPlayer_GetMinZoom(self.handle);
}

- (void)setMinZoom:(CGFloat)minZoom {
    if (!self.handle) return;
    SVGPlayer_SetMinZoom(self.handle, (float)minZoom);
}

- (CGFloat)maxZoom {
    if (!self.handle) return 10.0;
    return (CGFloat)SVGPlayer_GetMaxZoom(self.handle);
}

- (void)setMaxZoom:(CGFloat)maxZoom {
    if (!self.handle) return;
    SVGPlayer_SetMaxZoom(self.handle, (float)maxZoom);
}

#pragma mark - Frame Rate Control

- (void)setTargetFrameRate:(CGFloat)targetFrameRate {
    if (!self.handle) return;
    SVGPlayer_SetTargetFrameRate(self.handle, (float)targetFrameRate);
}

- (CGFloat)targetFrameRate {
    if (!self.handle) return kDefaultFrameRate;
    float rate = SVGPlayer_GetTargetFrameRate(self.handle);
    return rate > 0 ? (CGFloat)rate : kDefaultFrameRate;
}

- (NSTimeInterval)idealFrameInterval {
    if (!self.handle) return 1.0 / kDefaultFrameRate;
    return SVGPlayer_GetIdealFrameInterval(self.handle);
}

- (NSTimeInterval)lastFrameDuration {
    if (!self.handle) return 0;
    return SVGPlayer_GetLastFrameDuration(self.handle);
}

- (NSTimeInterval)averageFrameDuration {
    if (!self.handle) return 0;
    return SVGPlayer_GetAverageFrameDuration(self.handle);
}

- (CGFloat)measuredFPS {
    if (!self.handle) return 0;
    return (CGFloat)SVGPlayer_GetMeasuredFPS(self.handle);
}

- (NSInteger)droppedFrameCount {
    if (!self.handle) return 0;
    return (NSInteger)SVGPlayer_GetDroppedFrameCount(self.handle);
}

- (void)beginFrame {
    if (!self.handle) return;
    SVGPlayer_BeginFrame(self.handle);
}

- (void)endFrame {
    if (!self.handle) return;
    SVGPlayer_EndFrame(self.handle);
}

- (BOOL)shouldRenderFrameAtTime:(NSTimeInterval)currentTime {
    if (!self.handle) return YES;
    return SVGPlayer_ShouldRenderFrame(self.handle, currentTime);
}

- (void)markFrameRenderedAtTime:(NSTimeInterval)renderTime {
    if (!self.handle) return;
    SVGPlayer_MarkFrameRendered(self.handle, renderTime);
}

- (void)resetFrameStats {
    if (!self.handle) return;
    SVGPlayer_ResetFrameStats(self.handle);
}

#pragma mark - Version Information

+ (NSString *)version {
    const char *versionStr = SVGPlayer_GetVersion();
    return safeStringFromCString(versionStr) ?: @"unknown";
}

+ (void)getVersionMajor:(NSInteger *)major minor:(NSInteger *)minor patch:(NSInteger *)patch {
    int maj = 0, min = 0, pat = 0;
    SVGPlayer_GetVersionNumbers(&maj, &min, &pat);
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

- (SVGPlayerLayer *)createLayerFromPath:(NSString *)filepath error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:SVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return nil;
    }

    // Check if file exists
    if (![[NSFileManager defaultManager] fileExistsAtPath:filepath]) {
        NSString *message = [NSString stringWithFormat:@"SVG file not found: %@", filepath];
        [self setError:error code:SVGPlayerControllerErrorFileNotFound message:message];
        return nil;
    }

    SVGLayerRef layerRef = SVGPlayer_CreateLayer(self.handle, [filepath UTF8String]);
    if (!layerRef) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to create layer from file";
        [self setError:error code:SVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[SVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (SVGPlayerLayer *)createLayerFromData:(NSData *)data error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:SVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return nil;
    }

    if (!data || data.length == 0) {
        [self setError:error code:SVGPlayerControllerErrorInvalidData message:@"Invalid SVG data"];
        return nil;
    }

    SVGLayerRef layerRef = SVGPlayer_CreateLayerFromData(self.handle, data.bytes, data.length);
    if (!layerRef) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        NSString *message = safeStringFromCString(errorMsg) ?: @"Failed to create layer from data";
        [self setError:error code:SVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[SVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (void)destroyLayer:(SVGPlayerLayer *)layer {
    if (!self.handle || !layer) return;

    SVGLayerRef layerRef = layer.layerRef;
    if (layerRef) {
        SVGPlayer_DestroyLayer(self.handle, layerRef);
        layer.layerRef = NULL;
    }
}

- (NSInteger)layerCount {
    if (!self.handle) return 0;
    return SVGPlayer_GetLayerCount(self.handle);
}

- (SVGPlayerLayer *)layerAtIndex:(NSInteger)index {
    if (!self.handle) return nil;

    SVGLayerRef layerRef = SVGPlayer_GetLayerAtIndex(self.handle, (int)index);
    if (!layerRef) return nil;

    return [[SVGPlayerLayer alloc] initWithLayerRef:layerRef];
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale {
    if (!self.handle || !buffer) return NO;
    return SVGPlayer_RenderComposite(self.handle, buffer, (int)width, (int)height, scale);
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale
                         atTime:(NSTimeInterval)time {
    if (!self.handle || !buffer) return NO;
    return SVGPlayer_RenderCompositeAtTime(self.handle, buffer, (int)width, (int)height, scale, time);
}

- (BOOL)updateAllLayers:(NSTimeInterval)deltaTime {
    if (!self.handle) return NO;
    return SVGPlayer_UpdateAllLayers(self.handle, deltaTime);
}

- (void)playAllLayers {
    if (self.handle) {
        SVGPlayer_PlayAllLayers(self.handle);
    }
}

- (void)pauseAllLayers {
    if (self.handle) {
        SVGPlayer_PauseAllLayers(self.handle);
    }
}

- (void)stopAllLayers {
    if (self.handle) {
        SVGPlayer_StopAllLayers(self.handle);
    }
}

@end
