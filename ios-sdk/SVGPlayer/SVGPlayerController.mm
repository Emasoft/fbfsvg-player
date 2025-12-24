// SVGPlayerController.mm - Objective-C wrapper around the unified C SVG player API
//
// This implementation bridges the unified C API (svg_player_api.h) to Objective-C,
// providing a clean interface for SVGPlayerView and direct users.
//
// The unified API provides full cross-platform functionality, replacing
// platform-specific stubs with proper implementations.

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
        // Use unified C API - all fields now available
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
    // Sync with looping property
    self.looping = (repeatMode == SVGControllerRepeatModeLoop ||
                    repeatMode == SVGControllerRepeatModeReverse ||
                    repeatMode == SVGControllerRepeatModeCount);
    // Pass to unified C API
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
    // Pass to unified C API
    if (self.handle) {
        SVGPlayer_SetRepeatCount(self.handle, (int)self.internalRepeatCount);
    }
}

- (NSInteger)currentRepeatIteration {
    // Read from unified C API
    if (self.handle) {
        return (NSInteger)SVGPlayer_GetCompletedLoops(self.handle);
    }
    return 0;
}

- (BOOL)isPlayingForward {
    // Read from unified C API
    if (self.handle) {
        return SVGPlayer_IsPlayingForward(self.handle);
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
        SVGPlayer_SetPlaybackRate(self.handle, (float)self.internalPlaybackRate);
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
    SVGRenderStats stats = SVGPlayer_GetStats(self.handle);
    return stats.currentFrame;
}

- (NSInteger)totalFrames {
    SVGRenderStats stats = SVGPlayer_GetStats(self.handle);
    return stats.totalFrames;
}

- (CGFloat)frameRate {
    // Get from unified C API
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
    // Use unified API directly
    if (self.handle) {
        SVGPlayer_SeekToFrame(self.handle, (int)frame);
    }
}

- (void)seekToProgress:(CGFloat)progress {
    // Use unified API directly
    if (self.handle) {
        SVGPlayer_SeekToProgress(self.handle, (float)progress);
    }
}

- (void)seekToStart {
    // Use unified API
    if (self.handle) {
        SVGPlayer_SeekToStart(self.handle);
    }
}

- (void)seekToEnd {
    // Use unified API
    if (self.handle) {
        SVGPlayer_SeekToEnd(self.handle);
    }
}

#pragma mark - Frame Stepping

- (void)stepForward {
    // Use unified API (pauses playback automatically)
    if (self.handle) {
        SVGPlayer_StepForward(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)stepBackward {
    // Use unified API (pauses playback automatically)
    if (self.handle) {
        SVGPlayer_StepBackward(self.handle);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

- (void)stepByFrames:(NSInteger)frameCount {
    // Use unified API (pauses playback automatically)
    if (self.handle) {
        SVGPlayer_StepByFrames(self.handle, (int)frameCount);
        self.internalPlaybackState = SVGControllerPlaybackStatePaused;
    }
}

#pragma mark - Relative Seeking

- (void)seekForwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    // Use unified API
    if (self.handle) {
        SVGPlayer_SeekForwardByTime(self.handle, seconds);
    }
}

- (void)seekBackwardByTime:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    // Use unified API
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
        // Update internal state to paused since scrubbing pauses playback
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
        // Sync playback state with unified API
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
    // Use unified API
    if (self.handle) {
        return (NSInteger)SVGPlayer_TimeToFrame(self.handle, time);
    }
    return 0;
}

- (NSTimeInterval)timeForFrame:(NSInteger)frame {
    // Use unified API
    if (self.handle) {
        return SVGPlayer_FrameToTime(self.handle, (int)frame);
    }
    return 0;
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

- (nullable NSString *)hitTestAtPoint:(CGPoint)point viewSize:(CGSize)viewSize {
    if (!self.handle) return nil;

    const char *elementID = SVGPlayer_HitTest(self.handle,
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

    int count = SVGPlayer_GetElementsAtPoint(self.handle,
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
    if (SVGPlayer_GetElementBounds(self.handle, [objectID UTF8String], &bounds)) {
        return CGRectMake(bounds.x, bounds.y, bounds.width, bounds.height);
    }
    return CGRectZero;
}

- (BOOL)elementExistsWithID:(NSString *)objectID {
    if (!self.handle || !objectID) return NO;
    return SVGPlayer_ElementExists(self.handle, [objectID UTF8String]);
}

- (nullable NSString *)propertyValue:(NSString *)propertyName forElementID:(NSString *)objectID {
    if (!self.handle || !propertyName || !objectID) return nil;

    // API uses output buffer pattern
    char valueBuffer[1024];
    if (SVGPlayer_GetElementProperty(self.handle,
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
    if (SVGPlayer_ViewToSVG(self.handle,
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
    if (SVGPlayer_SVGToView(self.handle,
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
    if (SVGPlayer_GetViewBox(self.handle, &fx, &fy, &fw, &fh)) {
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

- (void)setZoom:(CGFloat)zoom centeredAt:(CGPoint)center viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_SetZoom(self.handle, (float)zoom,
                      (float)center.x, (float)center.y,
                      (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomInByFactor:(CGFloat)factor viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_ZoomIn(self.handle, (float)factor, (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomOutByFactor:(CGFloat)factor viewSize:(CGSize)viewSize {
    if (!self.handle) return;
    SVGPlayer_ZoomOut(self.handle, (float)factor, (int)viewSize.width, (int)viewSize.height);
}

- (void)zoomToRect:(CGRect)rect {
    if (!self.handle) return;
    SVGPlayer_ZoomToRect(self.handle, (float)rect.origin.x, (float)rect.origin.y,
                         (float)rect.size.width, (float)rect.size.height);
}

- (BOOL)zoomToElementWithID:(NSString *)objectID padding:(CGFloat)padding {
    if (!self.handle || !objectID) return NO;
    return SVGPlayer_ZoomToElement(self.handle, [objectID UTF8String], (float)padding);
}

- (void)panByDelta:(CGPoint)delta viewSize:(CGSize)viewSize {
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

#pragma mark - Error Handling

- (void)setError:(NSError * _Nullable *)error code:(SVGPlayerControllerErrorCode)code message:(NSString *)message {
    self.internalErrorMessage = message;

    if (error) {
        *error = [NSError errorWithDomain:SVGPlayerControllerErrorDomain
                                     code:code
                                 userInfo:@{NSLocalizedDescriptionKey: message}];
    }
}

#pragma mark - Multi-SVG Compositing

- (SVGLayer *)createLayerFromPath:(NSString *)path error:(NSError * _Nullable *)error {
    if (!self.handle) {
        [self setError:error code:SVGPlayerControllerErrorNotInitialized message:@"Player not initialized"];
        return nil;
    }

    SVGLayerRef layerRef = SVGPlayer_CreateLayer(self.handle, [path UTF8String]);
    if (!layerRef) {
        const char *errorMsg = SVGPlayer_GetLastError(self.handle);
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to create layer";
        [self setError:error code:SVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[SVGLayer alloc] initWithLayerRef:layerRef];
}

- (SVGLayer *)createLayerFromData:(NSData *)data error:(NSError * _Nullable *)error {
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
        NSString *message = errorMsg ? @(errorMsg) : @"Failed to create layer from data";
        [self setError:error code:SVGPlayerControllerErrorParseFailed message:message];
        return nil;
    }

    return [[SVGLayer alloc] initWithLayerRef:layerRef];
}

- (void)destroyLayer:(SVGLayer *)layer {
    if (!self.handle || !layer) return;
    SVGPlayer_DestroyLayer(self.handle, layer.layerRef);
}

- (NSInteger)layerCount {
    if (!self.handle) return 0;
    return SVGPlayer_GetLayerCount(self.handle);
}

- (SVGLayer *)layerAtIndex:(NSInteger)index {
    if (!self.handle) return nil;

    SVGLayerRef layerRef = SVGPlayer_GetLayerAtIndex(self.handle, (int)index);
    if (!layerRef) return nil;

    return [[SVGLayer alloc] initWithLayerRef:layerRef];
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale {
    if (!self.handle || !buffer) return NO;
    return SVGPlayer_RenderComposite(self.handle, buffer, (int)width, (int)height, (float)scale);
}

- (BOOL)renderCompositeToBuffer:(void *)buffer
                          width:(NSInteger)width
                         height:(NSInteger)height
                          scale:(CGFloat)scale
                         atTime:(NSTimeInterval)time {
    if (!self.handle || !buffer) return NO;
    return SVGPlayer_RenderCompositeAtTime(self.handle, buffer, (int)width, (int)height, (float)scale, time);
}

- (BOOL)updateAllLayers:(NSTimeInterval)deltaTime {
    if (!self.handle) return NO;
    return SVGPlayer_UpdateAllLayers(self.handle, deltaTime);
}

- (void)playAllLayers {
    if (!self.handle) return;
    SVGPlayer_PlayAllLayers(self.handle);
}

- (void)pauseAllLayers {
    if (!self.handle) return;
    SVGPlayer_PauseAllLayers(self.handle);
}

- (void)stopAllLayers {
    if (!self.handle) return;
    SVGPlayer_StopAllLayers(self.handle);
}

#pragma mark - Version Information

+ (NSString *)version {
    // Use unified API version function which uses version.h
    const char* ver = SVGPlayer_GetVersion();
    return [NSString stringWithUTF8String:ver];
}

+ (void)getVersionMajor:(NSInteger *)major minor:(NSInteger *)minor patch:(NSInteger *)patch {
    int maj = 0, min = 0, pat = 0;
    SVGPlayer_GetVersionNumbers(&maj, &min, &pat);
    if (major) *major = maj;
    if (minor) *minor = min;
    if (patch) *patch = pat;
}

+ (NSString *)buildInfo {
    // Return build info from version.h
    return [NSString stringWithUTF8String:SVG_PLAYER_BUILD_INFO];
}

@end

#pragma mark - SVGLayer Implementation

@interface SVGLayer ()
@property (nonatomic, assign) SVGLayerRef layerRef;
@end

@implementation SVGLayer

- (instancetype)initWithLayerRef:(SVGLayerRef)layerRef {
    if (self = [super init]) {
        _layerRef = layerRef;
    }
    return self;
}

#pragma mark - Position

- (void)setPosition:(CGPoint)position {
    if (!self.layerRef) return;
    SVGLayer_SetPosition(self.layerRef, position.x, position.y);
}

- (CGPoint)position {
    if (!self.layerRef) return CGPointZero;
    float x = 0, y = 0;
    SVGLayer_GetPosition(self.layerRef, &x, &y);
    return CGPointMake(x, y);
}

#pragma mark - Opacity

- (void)setOpacity:(CGFloat)opacity {
    if (!self.layerRef) return;
    SVGLayer_SetOpacity(self.layerRef, (float)opacity);
}

- (CGFloat)opacity {
    if (!self.layerRef) return 1.0;
    return SVGLayer_GetOpacity(self.layerRef);
}

#pragma mark - Z-Order

- (void)setZOrder:(NSInteger)zOrder {
    if (!self.layerRef) return;
    SVGLayer_SetZOrder(self.layerRef, (int)zOrder);
}

- (NSInteger)zOrder {
    if (!self.layerRef) return 0;
    return SVGLayer_GetZOrder(self.layerRef);
}

#pragma mark - Visibility

- (void)setVisible:(BOOL)visible {
    if (!self.layerRef) return;
    SVGLayer_SetVisible(self.layerRef, visible);
}

- (BOOL)isVisible {
    if (!self.layerRef) return NO;
    return SVGLayer_IsVisible(self.layerRef);
}

#pragma mark - Scale

- (void)setScale:(CGPoint)scale {
    if (!self.layerRef) return;
    SVGLayer_SetScale(self.layerRef, scale.x, scale.y);
}

- (CGPoint)scale {
    if (!self.layerRef) return CGPointMake(1.0, 1.0);
    float scaleX = 1.0, scaleY = 1.0;
    SVGLayer_GetScale(self.layerRef, &scaleX, &scaleY);
    return CGPointMake(scaleX, scaleY);
}

#pragma mark - Rotation

- (void)setRotation:(CGFloat)rotation {
    if (!self.layerRef) return;
    SVGLayer_SetRotation(self.layerRef, (float)rotation);
}

- (CGFloat)rotation {
    if (!self.layerRef) return 0.0;
    return SVGLayer_GetRotation(self.layerRef);
}

#pragma mark - Blend Mode

- (void)setBlendMode:(SVGLayerBlendMode)blendMode {
    if (!self.layerRef) return;
    SVGLayer_SetBlendMode(self.layerRef, (SVGLayerBlendMode)blendMode);
}

- (SVGLayerBlendMode)blendMode {
    if (!self.layerRef) return SVGLayerBlendModeNormal;
    return (SVGLayerBlendMode)SVGLayer_GetBlendMode(self.layerRef);
}

#pragma mark - Size

- (CGSize)size {
    if (!self.layerRef) return CGSizeZero;
    int width = 0, height = 0;
    if (SVGLayer_GetSize(self.layerRef, &width, &height)) {
        return CGSizeMake(width, height);
    }
    return CGSizeZero;
}

#pragma mark - Animation Properties

- (NSTimeInterval)duration {
    if (!self.layerRef) return 0;
    return SVGLayer_GetDuration(self.layerRef);
}

- (NSTimeInterval)currentTime {
    if (!self.layerRef) return 0;
    // Note: currentTime getter is not in C API, would need to track it
    // For now, return 0 (this is a readonly property used for display)
    return 0;
}

- (BOOL)hasAnimations {
    if (!self.layerRef) return NO;
    return SVGLayer_HasAnimations(self.layerRef);
}

#pragma mark - Playback Control

- (void)play {
    if (!self.layerRef) return;
    SVGLayer_Play(self.layerRef);
}

- (void)pause {
    if (!self.layerRef) return;
    SVGLayer_Pause(self.layerRef);
}

- (void)stop {
    if (!self.layerRef) return;
    SVGLayer_Stop(self.layerRef);
}

- (void)seekToTime:(NSTimeInterval)time {
    if (!self.layerRef) return;
    SVGLayer_SeekTo(self.layerRef, time);
}

- (BOOL)update:(NSTimeInterval)deltaTime {
    if (!self.layerRef) return NO;
    return SVGLayer_Update(self.layerRef, deltaTime);
}

@end
