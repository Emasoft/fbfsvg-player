// SVGPlayerView.mm - @IBDesignable UIView implementation for SVG playback
//
// This implementation provides a complete UIKit component for rendering
// animated SVG files. It uses CADisplayLink for smooth animation and
// Metal for GPU-accelerated rendering at native Retina resolution.
//
// Many playback control methods are stubs awaiting implementation in the
// shared C++ core. These provide the API surface for developers to build
// custom player UIs.

#import "SVGPlayerView.h"
#import "SVGPlayerController.h"
#import "SVGPlayerMetalRenderer.h"

// Error domain for SVGPlayerView errors
NSString * const SVGPlayerViewErrorDomain = @"com.svgplayer.view.error";

// Zero viewport constant
const SVGViewport SVGViewportZero = {0, 0, 0, 0};

// Default seek interval for rewind/fastforward
static const NSTimeInterval kDefaultSeekInterval = 5.0;

// Default zoom factors
static const CGFloat kDefaultZoomInFactor = 2.0;
static const CGFloat kDefaultZoomOutFactor = 0.5;
static const CGFloat kDefaultMinZoomScale = 1.0;
static const CGFloat kDefaultMaxZoomScale = 10.0;
static const NSTimeInterval kDefaultViewportTransitionDuration = 0.3;

#pragma mark - SVGPresetView Implementation

@implementation SVGPresetView

+ (instancetype)presetWithIdentifier:(NSString *)identifier viewport:(SVGViewport)viewport {
    return [self presetWithIdentifier:identifier viewport:viewport displayName:nil];
}

+ (instancetype)presetWithIdentifier:(NSString *)identifier
                            viewport:(SVGViewport)viewport
                         displayName:(NSString *)displayName {
    SVGPresetView *preset = [[SVGPresetView alloc] init];
    if (preset) {
        preset->_identifier = [identifier copy];
        preset->_viewport = viewport;
        preset->_displayName = [displayName copy];
        preset->_transitionDuration = kDefaultViewportTransitionDuration;
    }
    return preset;
}

+ (instancetype)presetWithIdentifier:(NSString *)identifier rect:(CGRect)rect {
    return [self presetWithIdentifier:identifier viewport:SVGViewportFromRect(rect)];
}

- (id)copyWithZone:(NSZone *)zone {
    SVGPresetView *copy = [[SVGPresetView allocWithZone:zone] init];
    if (copy) {
        copy->_identifier = [_identifier copy];
        copy->_viewport = _viewport;
        copy->_displayName = [_displayName copy];
        copy->_transitionDuration = _transitionDuration;
    }
    return copy;
}

- (BOOL)isEqual:(id)object {
    if (self == object) return YES;
    if (![object isKindOfClass:[SVGPresetView class]]) return NO;
    SVGPresetView *other = (SVGPresetView *)object;
    return [self.identifier isEqualToString:other.identifier];
}

- (NSUInteger)hash {
    return [self.identifier hash];
}

- (NSString *)description {
    return [NSString stringWithFormat:@"<SVGPresetView: %@ viewport:(%.1f, %.1f, %.1f, %.1f)>",
            self.identifier, self.viewport.x, self.viewport.y, self.viewport.width, self.viewport.height];
}

@end

@interface SVGPlayerView () <UIGestureRecognizerDelegate>
// SVG controller for loading and rendering
@property (nonatomic, strong) SVGPlayerController *controller;
// Metal renderer
@property (nonatomic, strong) id<SVGPlayerRenderer> renderer;
// Display link for animation timing
@property (nonatomic, strong) CADisplayLink *displayLink;
// Last frame timestamp for delta time calculation
@property (nonatomic, assign) CFTimeInterval lastTimestamp;
// Internal playback state
@property (nonatomic, assign) SVGViewPlaybackState internalPlaybackState;
// Cached last error
@property (nonatomic, strong, nullable) NSError *internalLastError;
// Flag to track if view has appeared
@property (nonatomic, assign) BOOL hasAppeared;
// Repeat mode
@property (nonatomic, assign) SVGRepeatMode internalRepeatMode;
// Repeat count
@property (nonatomic, assign) NSInteger internalRepeatCount;
// Current repeat iteration
@property (nonatomic, assign) NSInteger internalCurrentRepeatIteration;
// Playing forward (for ping-pong)
@property (nonatomic, assign) BOOL internalPlayingForward;
// Scrubbing state
@property (nonatomic, assign) BOOL internalSeeking;
// Playback state before scrubbing
@property (nonatomic, assign) SVGViewPlaybackState stateBeforeSeeking;
// Fullscreen state
@property (nonatomic, assign) BOOL internalFullscreen;
// Orientation lock state
@property (nonatomic, assign) BOOL internalOrientationLocked;
// Original frame before fullscreen
@property (nonatomic, assign) CGRect frameBeforeFullscreen;
// Original superview before fullscreen
@property (nonatomic, weak) UIView *superviewBeforeFullscreen;
// Fullscreen window for fullscreen mode
@property (nonatomic, strong) UIWindow *fullscreenWindow;

// Viewport/Zoom properties
@property (nonatomic, assign) SVGViewport internalViewport;
@property (nonatomic, assign) CGFloat internalZoomScale;
@property (nonatomic, assign) CGFloat internalMinZoomScale;
@property (nonatomic, assign) CGFloat internalMaxZoomScale;
@property (nonatomic, assign) BOOL readyForScrubbing;

// Gesture recognizers for zoom/pan
@property (nonatomic, strong) UIPinchGestureRecognizer *pinchGestureRecognizer;
@property (nonatomic, strong) UIPanGestureRecognizer *panGestureRecognizer;
@property (nonatomic, strong) UITapGestureRecognizer *tapGestureRecognizer;
@property (nonatomic, strong) UITapGestureRecognizer *doubleTapGestureRecognizer;

// Preset views storage
@property (nonatomic, strong) NSMutableDictionary<NSString *, SVGPresetView *> *presetViewsStorage;

// Loop counter
@property (nonatomic, assign) NSInteger loopCount;

// Element touch subscription storage
@property (nonatomic, strong) NSMutableSet<NSString *> *subscribedObjectIDsStorage;
@property (nonatomic, assign) BOOL internalElementTouchTrackingEnabled;
@property (nonatomic, assign) NSTimeInterval internalLongPressDuration;
@end

@implementation SVGPlayerView

#pragma mark - Initialization

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        [self commonInit];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    if (self = [super initWithCoder:coder]) {
        [self commonInit];
    }
    return self;
}

- (instancetype)initWithFrame:(CGRect)frame svgFileName:(NSString *)svgFileName {
    if (self = [self initWithFrame:frame]) {
        if (svgFileName.length > 0) {
            self.svgFileName = svgFileName;
        }
    }
    return self;
}

- (void)commonInit {
    // Set default values
    _autoPlay = YES;
    _loop = YES;
    _playbackSpeed = 1.0;
    _svgContentMode = SVGContentModeScaleAspectFit;
    _internalPlaybackState = SVGViewPlaybackStateStopped;
    _hasAppeared = NO;
    _internalRepeatMode = SVGRepeatModeLoop;
    _internalRepeatCount = 1;
    _internalCurrentRepeatIteration = 0;
    _internalPlayingForward = YES;
    _internalSeeking = NO;
    _internalFullscreen = NO;
    _internalOrientationLocked = NO;
    _preferredOrientation = UIInterfaceOrientationMaskAll;

    // Viewport/Zoom defaults
    _internalViewport = SVGViewportZero;
    _internalZoomScale = 1.0;
    _internalMinZoomScale = kDefaultMinZoomScale;
    _internalMaxZoomScale = kDefaultMaxZoomScale;
    _pinchToZoomEnabled = NO;
    _panEnabled = YES;
    _tapToZoomEnabled = NO;
    _tapToZoomScale = kDefaultZoomInFactor;
    _doubleTapResetsZoom = YES;
    _readyForScrubbing = NO;
    _loopCount = 0;

    // Preset views storage
    _presetViewsStorage = [NSMutableDictionary dictionary];

    // Element touch subscription storage
    _subscribedObjectIDsStorage = [NSMutableSet set];
    _internalElementTouchTrackingEnabled = YES;
    _internalLongPressDuration = 0.5;

    // Set default background color
    self.backgroundColor = [UIColor whiteColor];

    // Initialize controller
    _controller = [SVGPlayerController controller];
    _controller.looping = _loop;

    // Initialize Metal renderer
    _renderer = [[SVGPlayerMetalRenderer alloc] initWithView:self controller:_controller];
    if (!_renderer) {
        NSLog(@"SVGPlayerView: Metal renderer initialization failed");
    }

    // Setup display link for animation
    [self setupDisplayLink];

    // Setup gesture recognizers
    [self setupGestureRecognizers];

    // Enable user interaction for potential gesture handling
    self.userInteractionEnabled = YES;

    // Clip to bounds by default
    self.clipsToBounds = YES;
}

- (void)setupGestureRecognizers {
    // Pinch gesture for zoom
    _pinchGestureRecognizer = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(handlePinchGesture:)];
    _pinchGestureRecognizer.delegate = self;
    _pinchGestureRecognizer.enabled = _pinchToZoomEnabled;
    [self addGestureRecognizer:_pinchGestureRecognizer];

    // Pan gesture for scrolling when zoomed
    _panGestureRecognizer = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePanGesture:)];
    _panGestureRecognizer.delegate = self;
    _panGestureRecognizer.enabled = _panEnabled && _internalZoomScale > 1.0;
    [self addGestureRecognizer:_panGestureRecognizer];

    // Single tap gesture for tap-to-zoom
    _tapGestureRecognizer = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTapGesture:)];
    _tapGestureRecognizer.numberOfTapsRequired = 1;
    _tapGestureRecognizer.delegate = self;
    _tapGestureRecognizer.enabled = _tapToZoomEnabled;
    [self addGestureRecognizer:_tapGestureRecognizer];

    // Double tap gesture for reset
    _doubleTapGestureRecognizer = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleDoubleTapGesture:)];
    _doubleTapGestureRecognizer.numberOfTapsRequired = 2;
    _doubleTapGestureRecognizer.delegate = self;
    _doubleTapGestureRecognizer.enabled = _doubleTapResetsZoom;
    [self addGestureRecognizer:_doubleTapGestureRecognizer];

    // Single tap should wait for double tap to fail
    [_tapGestureRecognizer requireGestureRecognizerToFail:_doubleTapGestureRecognizer];
}

- (void)dealloc {
    [_displayLink invalidate];
    [_renderer cleanup];
}

#pragma mark - Display Link

- (void)setupDisplayLink {
    _displayLink = [CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(displayLinkFired:)];
    _displayLink.paused = YES;

    // Add to common run loop modes to ensure updates during scrolling/touches
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)displayLinkFired:(CADisplayLink *)displayLink {
    if (self.internalPlaybackState != SVGViewPlaybackStatePlaying) {
        return;
    }

    // Calculate delta time
    CFTimeInterval currentTime = displayLink.timestamp;
    CFTimeInterval deltaTime = currentTime - self.lastTimestamp;
    self.lastTimestamp = currentTime;

    // Clamp delta time to prevent large jumps (e.g., when app returns from background)
    if (deltaTime > 0.1) {
        deltaTime = 1.0 / 60.0; // Default to 60 FPS frame time
    }

    // Apply playback speed
    deltaTime *= self.playbackSpeed;

    // Track previous time to detect loop completion
    NSTimeInterval previousTime = self.controller.currentTime;

    // Update animation
    [self.controller update:deltaTime];

    // Detect loop completion (time wrapped around)
    NSTimeInterval newTime = self.controller.currentTime;
    BOOL loopedAround = (self.loop && self.controller.duration > 0 &&
                         previousTime > newTime && previousTime > self.controller.duration * 0.9);

    if (loopedAround) {
        // Increment loop count for infinite loop mode
        self.loopCount++;

        // Notify delegate of loop completion
        if ([self.delegate respondsToSelector:@selector(svgPlayerView:didCompleteLoopIteration:)]) {
            [self.delegate svgPlayerView:self didCompleteLoopIteration:self.loopCount];
        }
    }

    // Check if animation finished (non-looping)
    if (!self.loop && self.controller.duration > 0) {
        if (self.controller.currentTime >= self.controller.duration) {
            // Handle repeat modes
            if (self.internalRepeatMode == SVGRepeatModeCount) {
                self.internalCurrentRepeatIteration++;
                self.loopCount++;  // Also increment loopCount
                if (self.internalCurrentRepeatIteration >= self.internalRepeatCount) {
                    [self handlePlaybackFinished];
                } else {
                    [self.controller seekToTime:0];
                    // Notify delegate of loop completion
                    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didCompleteLoopIteration:)]) {
                        [self.delegate svgPlayerView:self didCompleteLoopIteration:self.loopCount];
                    }
                }
            } else {
                [self handlePlaybackFinished];
            }
            return;
        }
    }

    // Render frame
    [self.renderer render];

    // Notify delegate of timeline update
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didUpdateTimeline:)]) {
        [self.delegate svgPlayerView:self didUpdateTimeline:self.timelineInfo];
    }

    // Notify delegate of frame render
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didRenderFrameAtTime:)]) {
        [self.delegate svgPlayerView:self didRenderFrameAtTime:self.controller.currentTime];
    }
}

- (void)handlePlaybackFinished {
    self.internalPlaybackState = SVGViewPlaybackStateEnded;
    self.displayLink.paused = YES;
    [self.controller pause];

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidFinishPlaying:)]) {
        [self.delegate svgPlayerViewDidFinishPlaying:self];
    }
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangePlaybackState:)]) {
        [self.delegate svgPlayerView:self didChangePlaybackState:SVGViewPlaybackStateEnded];
    }
}

#pragma mark - IBDesignable Support

- (void)prepareForInterfaceBuilder {
    [super prepareForInterfaceBuilder];
    [self drawPlaceholder];
}

- (void)drawPlaceholder {
    // Create a placeholder image for IB preview
    UIGraphicsBeginImageContextWithOptions(self.bounds.size, NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();

    if (!context) {
        UIGraphicsEndImageContext();
        return;
    }

    // Background
    UIColor *bgColor = self.svgBackgroundColor ?: [UIColor colorWithWhite:0.95 alpha:1.0];
    [bgColor setFill];
    CGContextFillRect(context, self.bounds);

    // Border
    [[UIColor colorWithWhite:0.8 alpha:1.0] setStroke];
    CGContextSetLineWidth(context, 2.0);
    CGContextStrokeRect(context, CGRectInset(self.bounds, 1, 1));

    // Draw SVG icon placeholder (circle with play triangle)
    CGFloat centerX = self.bounds.size.width / 2;
    CGFloat centerY = self.bounds.size.height / 2;
    CGFloat radius = MIN(self.bounds.size.width, self.bounds.size.height) / 4;

    // Circle background
    [[UIColor colorWithRed:0.2 green:0.6 blue:0.9 alpha:1.0] setFill];
    CGContextAddArc(context, centerX, centerY, radius, 0, M_PI * 2, YES);
    CGContextFillPath(context);

    // Play triangle
    [[UIColor whiteColor] setFill];
    CGContextMoveToPoint(context, centerX - radius/3, centerY - radius/2);
    CGContextAddLineToPoint(context, centerX + radius/2, centerY);
    CGContextAddLineToPoint(context, centerX - radius/3, centerY + radius/2);
    CGContextClosePath(context);
    CGContextFillPath(context);

    // Label text
    NSString *label = self.svgFileName.length > 0 ? self.svgFileName : @"SVGPlayerView";
    NSDictionary *attrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:12 weight:UIFontWeightMedium],
        NSForegroundColorAttributeName: [UIColor darkGrayColor]
    };
    CGSize textSize = [label sizeWithAttributes:attrs];
    CGPoint textPoint = CGPointMake(centerX - textSize.width / 2,
                                     centerY + radius + 16);
    [label drawAtPoint:textPoint withAttributes:attrs];

    // "SVG" badge
    NSString *badge = @"SVG";
    NSDictionary *badgeAttrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:10 weight:UIFontWeightBold],
        NSForegroundColorAttributeName: [UIColor colorWithRed:0.2 green:0.6 blue:0.9 alpha:1.0]
    };
    CGSize badgeSize = [badge sizeWithAttributes:badgeAttrs];
    CGPoint badgePoint = CGPointMake(centerX - badgeSize.width / 2,
                                      centerY - radius - badgeSize.height - 8);
    [badge drawAtPoint:badgePoint withAttributes:badgeAttrs];

    UIImage *placeholder = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    // Set as layer contents for IB preview
    self.layer.contents = (__bridge id)placeholder.CGImage;
}

#pragma mark - Property Setters

- (void)setSvgFileName:(NSString *)svgFileName {
    _svgFileName = [svgFileName copy];

    if (svgFileName.length > 0) {
        [self loadSVGNamed:svgFileName];
    } else {
        [self unloadSVG];
    }
}

- (void)setLoop:(BOOL)loop {
    _loop = loop;
    self.controller.looping = loop;
    self.internalRepeatMode = loop ? SVGRepeatModeLoop : SVGRepeatModeNone;
}

- (void)setSvgBackgroundColor:(UIColor *)svgBackgroundColor {
    _svgBackgroundColor = svgBackgroundColor;
    self.backgroundColor = svgBackgroundColor;
}

- (void)setPlaybackSpeed:(CGFloat)playbackSpeed {
    // Clamp to reasonable range
    _playbackSpeed = MAX(0.1, MIN(10.0, playbackSpeed));
}

- (void)setRepeatMode:(SVGRepeatMode)repeatMode {
    _internalRepeatMode = repeatMode;
    // Sync with loop property
    _loop = (repeatMode == SVGRepeatModeLoop ||
             repeatMode == SVGRepeatModeReverse ||
             repeatMode == SVGRepeatModeCount);
    self.controller.looping = _loop;
}

- (SVGRepeatMode)repeatMode {
    return _internalRepeatMode;
}

- (void)setRepeatCount:(NSInteger)repeatCount {
    _internalRepeatCount = MAX(1, repeatCount);
}

- (NSInteger)repeatCount {
    return _internalRepeatCount;
}

#pragma mark - Loading

- (BOOL)loadSVGNamed:(NSString *)fileName {
    // Try to find file in main bundle
    NSString *path = [[NSBundle mainBundle] pathForResource:fileName ofType:@"svg"];

    if (!path) {
        // Try without extension in case user included it
        NSString *nameWithoutExt = [fileName stringByDeletingPathExtension];
        path = [[NSBundle mainBundle] pathForResource:nameWithoutExt ofType:@"svg"];
    }

    if (!path) {
        NSString *message = [NSString stringWithFormat:@"SVG file not found in bundle: %@", fileName];
        [self handleErrorWithMessage:message code:404];
        return NO;
    }

    return [self loadSVGFromPath:path];
}

- (BOOL)loadSVGFromPath:(NSString *)filePath {
    NSError *error = nil;
    BOOL success = [self.controller loadSVGFromPath:filePath error:&error];

    if (!success) {
        [self handleError:error];
        return NO;
    }

    // Clear any previous error
    self.internalLastError = nil;

    // Reset playback state
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.loopCount = 0;  // Reset loop count on new load

    // Update renderer for current size at native Retina resolution
    [self.renderer updateForSize:self.bounds.size scale:self.contentScaleFactor];

    // Render initial frame
    [self setNeedsRender];

    // Mark as ready for scrubbing (SVG is now fully parsed and seekable)
    self.readyForScrubbing = YES;

    // Notify delegate that player is ready
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidBecomeReadyToPlay:)]) {
        [self.delegate svgPlayerViewDidBecomeReadyToPlay:self];
    }

    // Notify delegate that player is ready for scrubbing
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidBecomeReadyForScrubbing:)]) {
        [self.delegate svgPlayerViewDidBecomeReadyForScrubbing:self];
    }

    // Auto-play if enabled and view has appeared
    if (self.autoPlay && self.hasAppeared) {
        [self play];
    }

    return YES;
}

- (BOOL)loadSVGFromData:(NSData *)data {
    NSError *error = nil;
    BOOL success = [self.controller loadSVGFromData:data error:&error];

    if (!success) {
        [self handleError:error];
        return NO;
    }

    // Clear any previous error
    self.internalLastError = nil;

    // Reset playback state
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.loopCount = 0;  // Reset loop count on new load

    // Update renderer for current size at native Retina resolution
    [self.renderer updateForSize:self.bounds.size scale:self.contentScaleFactor];

    // Render initial frame
    [self setNeedsRender];

    // Mark as ready for scrubbing (SVG is now fully parsed and seekable)
    self.readyForScrubbing = YES;

    // Notify delegate that player is ready
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidBecomeReadyToPlay:)]) {
        [self.delegate svgPlayerViewDidBecomeReadyToPlay:self];
    }

    // Notify delegate that player is ready for scrubbing
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidBecomeReadyForScrubbing:)]) {
        [self.delegate svgPlayerViewDidBecomeReadyForScrubbing:self];
    }

    // Auto-play if enabled and view has appeared
    if (self.autoPlay && self.hasAppeared) {
        [self play];
    }

    return YES;
}

- (void)unloadSVG {
    [self stop];
    [self.controller unload];
    self.readyForScrubbing = NO;  // No longer ready for scrubbing after unload
    [self setNeedsDisplay];
}

#pragma mark - Basic Playback Control

- (void)play {
    if (!self.controller.isLoaded) return;

    SVGViewPlaybackState previousState = self.internalPlaybackState;
    self.internalPlaybackState = SVGViewPlaybackStatePlaying;
    self.lastTimestamp = CACurrentMediaTime();
    self.displayLink.paused = NO;
    [self.controller play];

    // Notify delegate if state changed
    if (previousState != SVGViewPlaybackStatePlaying) {
        if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangePlaybackState:)]) {
            [self.delegate svgPlayerView:self didChangePlaybackState:SVGViewPlaybackStatePlaying];
        }
    }
}

- (void)pause {
    SVGViewPlaybackState previousState = self.internalPlaybackState;
    self.internalPlaybackState = SVGViewPlaybackStatePaused;
    self.displayLink.paused = YES;
    [self.controller pause];

    // Notify delegate if state changed
    if (previousState != SVGViewPlaybackStatePaused) {
        if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangePlaybackState:)]) {
            [self.delegate svgPlayerView:self didChangePlaybackState:SVGViewPlaybackStatePaused];
        }

        // Notify pause event
        if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidPause:)]) {
            [self.delegate svgPlayerViewDidPause:self];
        }
    }
}

- (void)resume {
    if (self.internalPlaybackState == SVGViewPlaybackStatePaused ||
        self.internalPlaybackState == SVGViewPlaybackStateEnded) {
        [self play];
    }
}

- (void)stop {
    SVGViewPlaybackState previousState = self.internalPlaybackState;
    self.internalPlaybackState = SVGViewPlaybackStateStopped;
    self.displayLink.paused = YES;
    [self.controller stop];
    self.internalCurrentRepeatIteration = 0;
    self.internalPlayingForward = YES;
    self.loopCount = 0;  // Reset loop count when stopped
    [self setNeedsRender];

    // Notify delegate if state changed
    if (previousState != SVGViewPlaybackStateStopped) {
        if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangePlaybackState:)]) {
            [self.delegate svgPlayerView:self didChangePlaybackState:SVGViewPlaybackStateStopped];
        }

        // Notify reset to start event
        if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidResetToStart:)]) {
            [self.delegate svgPlayerViewDidResetToStart:self];
        }
    }
}

- (void)togglePlayback {
    if (self.playbackState == SVGViewPlaybackStatePlaying) {
        [self pause];
    } else {
        [self play];
    }
}

#pragma mark - Navigation Control

- (void)goToStart {
    [self.controller seekToTime:0];
    [self setNeedsRender];
}

- (void)goToEnd {
    [self.controller seekToTime:self.controller.duration];
    [self setNeedsRender];
}

- (void)rewindBySeconds:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    NSTimeInterval newTime = self.controller.currentTime - seconds;
    [self.controller seekToTime:MAX(0, newTime)];
    [self setNeedsRender];
}

- (void)fastForwardBySeconds:(NSTimeInterval)seconds {
    if (seconds <= 0) seconds = kDefaultSeekInterval;
    NSTimeInterval newTime = self.controller.currentTime + seconds;
    [self.controller seekToTime:MIN(newTime, self.controller.duration)];
    [self setNeedsRender];
}

- (void)rewind {
    [self rewindBySeconds:kDefaultSeekInterval];
}

- (void)fastForward {
    [self fastForwardBySeconds:kDefaultSeekInterval];
}

- (void)stepForward {
    // Pause if playing
    if (self.internalPlaybackState == SVGViewPlaybackStatePlaying) {
        [self pause];
    }
    [self.controller stepForward];
    [self setNeedsRender];
}

- (void)stepBackward {
    // Pause if playing
    if (self.internalPlaybackState == SVGViewPlaybackStatePlaying) {
        [self pause];
    }
    [self.controller stepBackward];
    [self setNeedsRender];
}

- (void)stepByFrames:(NSInteger)count {
    // Pause if playing
    if (self.internalPlaybackState == SVGViewPlaybackStatePlaying) {
        [self pause];
    }
    [self.controller stepByFrames:count];
    [self setNeedsRender];
}

#pragma mark - Seeking / Scrubbing

- (void)seekToTime:(NSTimeInterval)time {
    [self.controller seekToTime:time];
    [self setNeedsRender];
}

- (void)seekToFrame:(NSInteger)frame {
    NSTimeInterval duration = self.controller.duration;
    NSInteger totalFrames = self.totalFrames;

    if (duration > 0 && totalFrames > 0) {
        NSTimeInterval time = (duration / totalFrames) * frame;
        [self seekToTime:time];
    }
}

- (void)seekToProgress:(CGFloat)progress {
    progress = MAX(0, MIN(1.0, progress));
    NSTimeInterval time = self.controller.duration * progress;
    [self seekToTime:time];
}

- (void)beginScrubbing {
    self.stateBeforeSeeking = self.internalPlaybackState;
    self.internalSeeking = YES;

    if (self.internalPlaybackState == SVGViewPlaybackStatePlaying) {
        [self pause];
    }

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidBeginSeeking:)]) {
        [self.delegate svgPlayerViewDidBeginSeeking:self];
    }
}

- (void)scrubToProgress:(CGFloat)progress {
    if (!self.internalSeeking) {
        [self beginScrubbing];
    }
    [self seekToProgress:progress];
}

- (void)endScrubbingAndResume:(BOOL)shouldResume {
    self.internalSeeking = NO;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didEndSeekingAtTime:)]) {
        [self.delegate svgPlayerView:self didEndSeekingAtTime:self.controller.currentTime];
    }

    if (shouldResume && self.stateBeforeSeeking == SVGViewPlaybackStatePlaying) {
        [self play];
    }
}

#pragma mark - Playback Rate Control

- (void)setPlaybackRate:(CGFloat)rate {
    self.playbackSpeed = rate;
}

- (CGFloat)playbackRate {
    return self.playbackSpeed;
}

- (void)resetPlaybackRate {
    self.playbackSpeed = 1.0;
}

#pragma mark - Display Mode Control

- (BOOL)isFullscreen {
    return self.internalFullscreen;
}

- (void)setFullscreen:(BOOL)fullscreen {
    if (fullscreen) {
        [self enterFullscreenAnimated:NO];
    } else {
        [self exitFullscreenAnimated:NO];
    }
}

- (void)enterFullscreenAnimated:(BOOL)animated {
    if (self.internalFullscreen) return;

    // Store original state
    self.frameBeforeFullscreen = self.frame;
    self.superviewBeforeFullscreen = self.superview;

    // Create fullscreen window
    // TODO: Implement proper fullscreen presentation
    // This is a stub - actual implementation requires view controller coordination
    self.internalFullscreen = YES;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeFullscreenMode:)]) {
        [self.delegate svgPlayerView:self didChangeFullscreenMode:YES];
    }
}

- (void)exitFullscreenAnimated:(BOOL)animated {
    if (!self.internalFullscreen) return;

    // TODO: Implement proper fullscreen exit
    // This is a stub - actual implementation requires view controller coordination
    self.internalFullscreen = NO;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeFullscreenMode:)]) {
        [self.delegate svgPlayerView:self didChangeFullscreenMode:NO];
    }
}

- (void)toggleFullscreenAnimated:(BOOL)animated {
    if (self.internalFullscreen) {
        [self exitFullscreenAnimated:animated];
    } else {
        [self enterFullscreenAnimated:animated];
    }
}

- (BOOL)isOrientationLocked {
    return self.internalOrientationLocked;
}

- (void)setOrientationLocked:(BOOL)orientationLocked {
    if (orientationLocked) {
        [self lockOrientation];
    } else {
        [self unlockOrientation];
    }
}

- (void)lockOrientation {
    // TODO: Implement orientation lock via view controller
    // This is a stub - requires AppDelegate/SceneDelegate coordination
    self.internalOrientationLocked = YES;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeOrientationLock:)]) {
        [self.delegate svgPlayerView:self didChangeOrientationLock:YES];
    }
}

- (void)unlockOrientation {
    // TODO: Implement orientation unlock
    self.internalOrientationLocked = NO;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeOrientationLock:)]) {
        [self.delegate svgPlayerView:self didChangeOrientationLock:NO];
    }
}

- (void)lockToOrientation:(UIInterfaceOrientationMask)orientation {
    self.preferredOrientation = orientation;
    [self lockOrientation];
}

#pragma mark - Rendering

- (void)setNeedsRender {
    [self.renderer render];
}

- (UIImage *)captureCurrentFrame {
    return [self.renderer captureImage];
}

- (UIImage *)captureFrameAtTime:(NSTimeInterval)time {
    // Save current time
    NSTimeInterval savedTime = self.controller.currentTime;

    // Seek to requested time
    [self.controller seekToTime:time];

    // Render and capture
    [self.renderer render];
    UIImage *image = [self.renderer captureImage];

    // Restore original time
    [self.controller seekToTime:savedTime];

    return image;
}

- (UIImage *)captureFrameAtTime:(NSTimeInterval)time
                           size:(CGSize)size
                          scale:(CGFloat)scale {
    // TODO: Implement custom size capture
    // For now, just capture at current size
    return [self captureFrameAtTime:time];
}

#pragma mark - Properties (Readonly)

- (SVGViewPlaybackState)playbackState {
    return self.internalPlaybackState;
}

- (CGSize)intrinsicSVGSize {
    return self.controller.intrinsicSize;
}

- (NSTimeInterval)duration {
    return self.controller.duration;
}

- (NSTimeInterval)currentTime {
    return self.controller.currentTime;
}

- (NSTimeInterval)elapsedTime {
    return self.controller.currentTime;
}

- (NSTimeInterval)remainingTime {
    NSTimeInterval duration = self.controller.duration;
    if (duration <= 0) return 0;
    return MAX(0, duration - self.controller.currentTime);
}

- (CGFloat)progress {
    NSTimeInterval duration = self.controller.duration;
    if (duration <= 0) return 0;
    return (CGFloat)(self.controller.currentTime / duration);
}

- (NSInteger)currentFrame {
    return self.controller.statistics.currentFrame;
}

- (NSInteger)totalFrames {
    return self.controller.statistics.totalFrames;
}

- (CGFloat)currentFPS {
    return self.controller.statistics.fps;
}

- (SVGTimelineInfo)timelineInfo {
    SVGTimelineInfo info;
    info.currentTime = self.controller.currentTime;
    info.duration = self.controller.duration;
    info.elapsedTime = info.currentTime;
    info.remainingTime = MAX(0, info.duration - info.currentTime);
    info.progress = info.duration > 0 ? (CGFloat)(info.currentTime / info.duration) : 0;
    info.currentFrame = self.controller.statistics.currentFrame;
    info.totalFrames = self.controller.statistics.totalFrames;
    info.fps = self.controller.statistics.fps;
    info.isPlayingForward = self.internalPlayingForward;
    return info;
}

- (BOOL)isLoaded {
    return self.controller.isLoaded;
}

- (BOOL)isReadyToPlay {
    return self.controller.isLoaded;
}

- (BOOL)isPlaying {
    return self.playbackState == SVGViewPlaybackStatePlaying;
}

- (BOOL)isPaused {
    return self.playbackState == SVGViewPlaybackStatePaused;
}

- (BOOL)isStopped {
    return self.playbackState == SVGViewPlaybackStateStopped;
}

- (BOOL)isSeeking {
    return self.internalSeeking;
}

- (NSInteger)currentRepeatIteration {
    return self.internalCurrentRepeatIteration;
}

- (NSInteger)loopCount {
    return _loopCount;
}

- (BOOL)isReadyForScrubbing {
    return _readyForScrubbing;
}

- (BOOL)isPlayingForward {
    return self.internalPlayingForward;
}

- (NSError *)lastError {
    return self.internalLastError;
}

- (CGFloat)displayScale {
    return self.contentScaleFactor;
}

- (CGSize)renderPixelSize {
    CGSize pointSize = self.bounds.size;
    CGFloat scale = self.contentScaleFactor;
    return CGSizeMake(pointSize.width * scale, pointSize.height * scale);
}

#pragma mark - Formatted Time Strings

- (NSString *)formattedElapsedTime {
    return [SVGPlayerView formatTime:self.currentTime];
}

- (NSString *)formattedRemainingTime {
    return [NSString stringWithFormat:@"-%@", [SVGPlayerView formatTime:self.remainingTime]];
}

- (NSString *)formattedDuration {
    return [SVGPlayerView formatTime:self.duration];
}

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

#pragma mark - Frame/Time Conversion Utilities

- (NSInteger)frameForTime:(NSTimeInterval)time {
    // Delegate to controller for consistent calculation
    return [self.controller frameForTime:time];
}

- (NSTimeInterval)timeForFrame:(NSInteger)frame {
    // Delegate to controller for consistent calculation
    return [self.controller timeForFrame:frame];
}

- (NSTimeInterval)frameDuration {
    // Duration of a single frame = 1 / frameRate
    CGFloat rate = self.frameRate;
    if (rate <= 0) {
        return 0;
    }
    return 1.0 / rate;
}

- (CGFloat)frameRate {
    // Delegate to controller
    return self.controller.frameRate;
}

- (BOOL)isValidFrame:(NSInteger)frame {
    if (!self.controller.isLoaded) {
        return NO;
    }
    NSInteger total = self.totalFrames;
    return frame >= 0 && frame < total;
}

- (BOOL)isValidTime:(NSTimeInterval)time {
    if (!self.controller.isLoaded) {
        return NO;
    }
    return time >= 0 && time <= self.duration;
}

- (NSDictionary<NSString *, id> *)infoForFrame:(NSInteger)frame {
    if (!self.controller.isLoaded) {
        return nil;
    }

    NSInteger total = self.totalFrames;
    if (total <= 0) {
        return nil;
    }

    // Clamp frame to valid range
    NSInteger clampedFrame = MAX(0, MIN(frame, total - 1));

    NSTimeInterval time = [self timeForFrame:clampedFrame];
    NSTimeInterval dur = self.duration;
    CGFloat progress = (dur > 0) ? (time / dur) : 0;

    return @{
        @"frameNumber": @(clampedFrame),
        @"timeInSeconds": @(time),
        @"progress": @(progress),
        @"isFirstFrame": @(clampedFrame == 0),
        @"isLastFrame": @(clampedFrame == total - 1),
        @"totalFrames": @(total),
        @"duration": @(dur),
        @"frameRate": @(self.frameRate),
        @"frameDuration": @(self.frameDuration)
    };
}

- (NSDictionary<NSString *, id> *)infoForTime:(NSTimeInterval)time {
    if (!self.controller.isLoaded) {
        return nil;
    }

    // Convert time to frame and get info
    NSInteger frame = [self frameForTime:time];
    return [self infoForFrame:frame];
}

- (NSDictionary<NSString *, id> *)currentFrameInfo {
    if (!self.controller.isLoaded) {
        return nil;
    }

    // Get current frame from statistics for accuracy
    NSInteger currentFrame = self.currentFrame;
    return [self infoForFrame:currentFrame];
}

#pragma mark - Layout

- (void)layoutSubviews {
    [super layoutSubviews];

    // Update renderer for new size at native Retina resolution
    [self.renderer updateForSize:self.bounds.size scale:self.contentScaleFactor];

    // Re-render if loaded
    if (self.controller.isLoaded) {
        [self setNeedsRender];
    }
}

#pragma mark - View Lifecycle

- (void)willMoveToWindow:(UIWindow *)newWindow {
    [super willMoveToWindow:newWindow];

    if (newWindow == nil) {
        // View is being removed from window - pause animation
        if (self.internalPlaybackState == SVGViewPlaybackStatePlaying) {
            self.displayLink.paused = YES;
        }
    }
}

- (void)didMoveToWindow {
    [super didMoveToWindow];

    if (self.window != nil) {
        self.hasAppeared = YES;

        // Resume animation if it was playing
        if (self.internalPlaybackState == SVGViewPlaybackStatePlaying) {
            self.lastTimestamp = CACurrentMediaTime();
            self.displayLink.paused = NO;
        }

        // Auto-play on first appearance if enabled
        if (self.autoPlay && self.controller.isLoaded &&
            self.internalPlaybackState == SVGViewPlaybackStateStopped) {
            [self play];
        }
    }
}

- (void)didMoveToSuperview {
    [super didMoveToSuperview];

    // Load SVG file if specified (useful for programmatic setup)
    if (self.superview && self.svgFileName.length > 0 && !self.controller.isLoaded) {
        [self loadSVGNamed:self.svgFileName];
    }
}

#pragma mark - Error Handling

- (void)handleError:(NSError *)error {
    self.internalLastError = error;

    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didFailWithError:)]) {
        [self.delegate svgPlayerView:self didFailWithError:error];
    }
}

- (void)handleErrorWithMessage:(NSString *)message code:(NSInteger)code {
    NSError *error = [NSError errorWithDomain:SVGPlayerViewErrorDomain
                                         code:code
                                     userInfo:@{NSLocalizedDescriptionKey: message}];
    [self handleError:error];
}

#pragma mark - Intrinsic Content Size

- (CGSize)intrinsicContentSize {
    CGSize svgSize = self.controller.intrinsicSize;
    if (svgSize.width > 0 && svgSize.height > 0) {
        return svgSize;
    }
    return CGSizeMake(UIViewNoIntrinsicMetric, UIViewNoIntrinsicMetric);
}

#pragma mark - Viewport/Zoom Control

- (CGFloat)minimumZoomScale {
    return self.internalMinZoomScale;
}

- (void)setMinimumZoomScale:(CGFloat)minimumZoomScale {
    self.internalMinZoomScale = MAX(0.1, minimumZoomScale);
}

- (CGFloat)maximumZoomScale {
    return self.internalMaxZoomScale;
}

- (void)setMaximumZoomScale:(CGFloat)maximumZoomScale {
    self.internalMaxZoomScale = MAX(self.internalMinZoomScale, maximumZoomScale);
}

- (CGFloat)zoomScale {
    return self.internalZoomScale;
}

- (SVGViewport)currentViewport {
    return self.internalViewport;
}

- (SVGViewport)defaultViewport {
    // Return the full SVG bounds as the default viewport
    CGSize svgSize = self.controller.intrinsicSize;
    if (svgSize.width > 0 && svgSize.height > 0) {
        return SVGViewportMake(0, 0, svgSize.width, svgSize.height);
    }
    return SVGViewportZero;
}

- (BOOL)isZoomed {
    return self.internalZoomScale > 1.0 + 0.01; // Small epsilon for floating point
}

- (void)setPinchToZoomEnabled:(BOOL)pinchToZoomEnabled {
    _pinchToZoomEnabled = pinchToZoomEnabled;
    self.pinchGestureRecognizer.enabled = pinchToZoomEnabled;
}

- (void)setPanEnabled:(BOOL)panEnabled {
    _panEnabled = panEnabled;
    self.panGestureRecognizer.enabled = panEnabled && self.isZoomed;
}

- (void)setTapToZoomEnabled:(BOOL)tapToZoomEnabled {
    _tapToZoomEnabled = tapToZoomEnabled;
    self.tapGestureRecognizer.enabled = tapToZoomEnabled;
}

- (void)setDoubleTapResetsZoom:(BOOL)doubleTapResetsZoom {
    _doubleTapResetsZoom = doubleTapResetsZoom;
    self.doubleTapGestureRecognizer.enabled = doubleTapResetsZoom;
}

- (void)setViewport:(SVGViewport)viewport animated:(BOOL)animated {
    SVGViewport previousViewport = self.internalViewport;

    // Use the shared library to set the viewBox directly
    [self.controller setViewBoxX:viewport.x y:viewport.y width:viewport.width height:viewport.height];

    // Sync internal state with shared library
    self.internalZoomScale = self.controller.zoom;
    [self syncViewportFromController];

    // Update pan gesture state
    self.panGestureRecognizer.enabled = self.panEnabled && self.isZoomed;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeViewport:)]) {
        SVGZoomInfo info;
        info.previousViewport = previousViewport;
        info.newViewport = self.internalViewport;
        info.zoomScale = self.internalZoomScale;
        info.isUserGesture = NO;
        info.zoomCenter = CGPointMake(self.bounds.size.width / 2, self.bounds.size.height / 2);
        [self.delegate svgPlayerView:self didChangeViewport:info];
    }

    [self setNeedsRender];
}

- (void)setViewportRect:(CGRect)rect animated:(BOOL)animated {
    [self setViewport:SVGViewportFromRect(rect) animated:animated];
}

- (void)zoomToScale:(CGFloat)scale animated:(BOOL)animated {
    CGPoint center = CGPointMake(self.bounds.size.width / 2, self.bounds.size.height / 2);
    [self zoomToScale:scale centeredAt:center animated:animated];
}

- (void)zoomToScale:(CGFloat)scale centeredAt:(CGPoint)center animated:(BOOL)animated {
    // Clamp scale to local limits (will also be clamped by shared library)
    scale = MAX(self.internalMinZoomScale, MIN(scale, self.internalMaxZoomScale));

    SVGViewport previousViewport = self.internalViewport;

    // Use the shared library zoom API - this handles all viewBox calculation and clamping
    [self.controller setZoom:scale centeredAt:center viewSize:self.bounds.size];

    // Sync internal state with shared library
    self.internalZoomScale = self.controller.zoom;
    [self syncViewportFromController];

    // Update pan gesture state
    self.panGestureRecognizer.enabled = self.panEnabled && self.isZoomed;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeViewport:)]) {
        SVGZoomInfo info;
        info.previousViewport = previousViewport;
        info.newViewport = self.internalViewport;
        info.zoomScale = self.internalZoomScale;
        info.isUserGesture = NO;
        info.zoomCenter = center;
        [self.delegate svgPlayerView:self didChangeViewport:info];
    }

    [self setNeedsRender];
}

- (void)zoomToRect:(CGRect)rect animated:(BOOL)animated {
    [self setViewportRect:rect animated:animated];
}

- (void)zoomInAnimated:(BOOL)animated {
    CGFloat newScale = self.internalZoomScale * kDefaultZoomInFactor;
    [self zoomToScale:newScale animated:animated];
}

- (void)zoomOutAnimated:(BOOL)animated {
    CGFloat newScale = self.internalZoomScale * kDefaultZoomOutFactor;
    [self zoomToScale:newScale animated:animated];
}

- (void)resetZoomAnimated:(BOOL)animated {
    SVGViewport previousViewport = self.internalViewport;

    // Use the shared library reset API - this restores the original viewBox
    [self.controller resetViewBox];

    // Sync internal state with shared library
    self.internalZoomScale = self.controller.zoom;
    [self syncViewportFromController];

    // Update pan gesture state
    self.panGestureRecognizer.enabled = NO;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerViewDidResetZoom:)]) {
        [self.delegate svgPlayerViewDidResetZoom:self];
    }

    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didChangeViewport:)]) {
        SVGZoomInfo info;
        info.previousViewport = previousViewport;
        info.newViewport = self.internalViewport;
        info.zoomScale = self.internalZoomScale;
        info.isUserGesture = NO;
        info.zoomCenter = CGPointMake(self.bounds.size.width / 2, self.bounds.size.height / 2);
        [self.delegate svgPlayerView:self didChangeViewport:info];
    }

    [self setNeedsRender];
}

- (CGPoint)convertPointToSVGCoordinates:(CGPoint)point {
    // Coordinate conversion using current viewport (handles zoom/pan)
    // The controller's convertViewPointToSVG: is also available but this local
    // implementation handles our custom viewport state including zoom/pan
    CGSize viewSize = self.bounds.size;
    SVGViewport vp = self.internalViewport;

    if (viewSize.width <= 0 || viewSize.height <= 0) {
        return CGPointZero;
    }

    if (vp.width <= 0 || vp.height <= 0) {
        vp = self.defaultViewport;
    }

    CGFloat svgX = vp.x + (point.x / viewSize.width) * vp.width;
    CGFloat svgY = vp.y + (point.y / viewSize.height) * vp.height;

    return CGPointMake(svgX, svgY);
}

- (CGPoint)convertPointFromSVGCoordinates:(CGPoint)point {
    CGSize viewSize = self.bounds.size;
    SVGViewport vp = self.internalViewport;

    if (vp.width <= 0 || vp.height <= 0) {
        vp = self.defaultViewport;
    }

    if (vp.width <= 0 || vp.height <= 0) {
        return CGPointZero;
    }

    CGFloat viewX = ((point.x - vp.x) / vp.width) * viewSize.width;
    CGFloat viewY = ((point.y - vp.y) / vp.height) * viewSize.height;

    return CGPointMake(viewX, viewY);
}

- (CGRect)convertRectToSVGCoordinates:(CGRect)rect {
    CGPoint origin = [self convertPointToSVGCoordinates:rect.origin];
    CGPoint corner = [self convertPointToSVGCoordinates:CGPointMake(CGRectGetMaxX(rect), CGRectGetMaxY(rect))];
    return CGRectMake(origin.x, origin.y, corner.x - origin.x, corner.y - origin.y);
}

- (CGRect)convertRectFromSVGCoordinates:(CGRect)rect {
    CGPoint origin = [self convertPointFromSVGCoordinates:rect.origin];
    CGPoint corner = [self convertPointFromSVGCoordinates:CGPointMake(CGRectGetMaxX(rect), CGRectGetMaxY(rect))];
    return CGRectMake(origin.x, origin.y, corner.x - origin.x, corner.y - origin.y);
}

#pragma mark - Shared Library Sync

/// Sync internal viewport state from shared library viewBox
/// Call this after any zoom/pan operation that modifies the shared library viewBox
- (void)syncViewportFromController {
    CGFloat x = 0, y = 0, width = 0, height = 0;
    if ([self.controller getViewBoxX:&x y:&y width:&width height:&height]) {
        self.internalViewport = SVGViewportMake(x, y, width, height);
    }
}

/// Sync shared library viewBox from internal viewport state
/// Call this to push internal viewport changes to the shared library
- (void)syncViewportToController {
    SVGViewport vp = self.internalViewport;
    [self.controller setViewBoxX:vp.x y:vp.y width:vp.width height:vp.height];
}

#pragma mark - Gesture Handlers

- (void)handlePinchGesture:(UIPinchGestureRecognizer *)gesture {
    if (!self.pinchToZoomEnabled) return;

    CGPoint center = [gesture locationInView:self];
    SVGViewport previousViewport = self.internalViewport;

    if (gesture.state == UIGestureRecognizerStateBegan ||
        gesture.state == UIGestureRecognizerStateChanged) {

        // Calculate new zoom level based on gesture scale
        CGFloat currentZoom = self.controller.zoom;
        CGFloat newZoom = currentZoom * gesture.scale;
        newZoom = MAX(self.controller.minZoom, MIN(newZoom, self.controller.maxZoom));

        // Use the shared library zoom API - this modifies the viewBox
        [self.controller setZoom:newZoom centeredAt:center viewSize:self.bounds.size];

        // Sync internal state with shared library
        self.internalZoomScale = self.controller.zoom;
        [self syncViewportFromController];

        gesture.scale = 1.0; // Reset for incremental changes

        // Update pan gesture state based on zoom
        self.panGestureRecognizer.enabled = self.panEnabled && self.isZoomed;

        [self setNeedsRender];
    }

    if (gesture.state == UIGestureRecognizerStateEnded ||
        gesture.state == UIGestureRecognizerStateCancelled) {

        // Notify delegate of zoom gesture completion
        if ([self.delegate respondsToSelector:@selector(svgPlayerView:didZoom:)]) {
            SVGZoomInfo info;
            info.previousViewport = previousViewport;
            info.newViewport = self.internalViewport;
            info.zoomScale = self.internalZoomScale;
            info.isUserGesture = YES;
            info.zoomCenter = center;
            [self.delegate svgPlayerView:self didZoom:info];
        }
    }
}

- (void)handlePanGesture:(UIPanGestureRecognizer *)gesture {
    if (!self.panEnabled || !self.isZoomed) return;

    CGPoint translation = [gesture translationInView:self];

    if (gesture.state == UIGestureRecognizerStateChanged) {
        // Use the shared library pan API - this modifies the viewBox with bounds clamping
        // Note: pan delta is negated because dragging right should move content left (viewport moves right)
        [self.controller panByDelta:CGPointMake(-translation.x, -translation.y) viewSize:self.bounds.size];

        // Sync internal state with shared library
        [self syncViewportFromController];

        [gesture setTranslation:CGPointZero inView:self];
        [self setNeedsRender];

        // Notify delegate
        if ([self.delegate respondsToSelector:@selector(svgPlayerView:didPan:)]) {
            [self.delegate svgPlayerView:self didPan:translation];
        }
    }
}

- (void)handleTapGesture:(UITapGestureRecognizer *)gesture {
    if (!self.tapToZoomEnabled) return;

    CGPoint point = [gesture locationInView:self];
    [self handleTapAtPoint:point animated:YES];
}

- (void)handleDoubleTapGesture:(UITapGestureRecognizer *)gesture {
    if (!self.doubleTapResetsZoom) return;

    CGPoint point = [gesture locationInView:self];
    [self handleDoubleTapAtPoint:point animated:YES];
}

- (void)handleTapAtPoint:(CGPoint)point animated:(BOOL)animated {
    if (self.isZoomed) {
        // Already zoomed - zoom out
        [self resetZoomAnimated:animated];
    } else {
        // Zoom in at tap point
        [self zoomToScale:self.tapToZoomScale centeredAt:point animated:animated];
    }
}

- (void)handleDoubleTapAtPoint:(CGPoint)point animated:(BOOL)animated {
    if (self.isZoomed) {
        [self resetZoomAnimated:animated];
    } else {
        [self zoomToScale:self.tapToZoomScale centeredAt:point animated:animated];
    }
}

#pragma mark - UIGestureRecognizerDelegate

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer {
    // Allow pinch and pan to work together
    return YES;
}

#pragma mark - Preset Views

- (NSArray<SVGPresetView *> *)presetViews {
    return [self.presetViewsStorage allValues];
}

- (void)registerPresetView:(SVGPresetView *)preset {
    if (preset && preset.identifier.length > 0) {
        self.presetViewsStorage[preset.identifier] = [preset copy];
    }
}

- (void)registerPresetViews:(NSArray<SVGPresetView *> *)presets {
    for (SVGPresetView *preset in presets) {
        [self registerPresetView:preset];
    }
}

- (void)unregisterPresetViewWithIdentifier:(NSString *)identifier {
    if (identifier.length > 0) {
        [self.presetViewsStorage removeObjectForKey:identifier];
    }
}

- (void)unregisterAllPresetViews {
    [self.presetViewsStorage removeAllObjects];
}

- (SVGPresetView *)presetViewWithIdentifier:(NSString *)identifier {
    return self.presetViewsStorage[identifier];
}

- (void)transitionToPreset:(SVGPresetView *)preset animated:(BOOL)animated {
    if (!preset) return;

    // Notify delegate
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:willTransitionToPreset:)]) {
        [self.delegate svgPlayerView:self willTransitionToPreset:preset];
    }

    [self setViewport:preset.viewport animated:animated];

    // Notify delegate of completion
    // TODO: Animate and call completion after animation
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didTransitionToPreset:)]) {
        [self.delegate svgPlayerView:self didTransitionToPreset:preset];
    }
}

- (BOOL)transitionToPresetWithIdentifier:(NSString *)identifier animated:(BOOL)animated {
    SVGPresetView *preset = [self presetViewWithIdentifier:identifier];
    if (preset) {
        [self transitionToPreset:preset animated:animated];
        return YES;
    }
    return NO;
}

- (void)transitionToDefaultViewAnimated:(BOOL)animated {
    [self resetZoomAnimated:animated];
}

#pragma mark - Element Touch Subscription

- (BOOL)isElementTouchTrackingEnabled {
    return self.internalElementTouchTrackingEnabled;
}

- (void)setElementTouchTrackingEnabled:(BOOL)enabled {
    self.internalElementTouchTrackingEnabled = enabled;
}

- (NSTimeInterval)longPressDuration {
    return self.internalLongPressDuration;
}

- (void)setLongPressDuration:(NSTimeInterval)duration {
    self.internalLongPressDuration = MAX(0.1, duration);  // Minimum 0.1 seconds
}

- (NSArray<NSString *> *)subscribedObjectIDs {
    return [self.subscribedObjectIDsStorage allObjects];
}

- (void)subscribeToTouchEventsForObjectID:(NSString *)objectID {
    if (objectID.length == 0) return;
    [self.subscribedObjectIDsStorage addObject:objectID];
    // Register with the C++ hit testing system via the controller
    [_controller subscribeToElementWithID:objectID];
}

- (void)subscribeToTouchEventsForObjectIDs:(NSArray<NSString *> *)objectIDs {
    for (NSString *objectID in objectIDs) {
        [self subscribeToTouchEventsForObjectID:objectID];
    }
}

- (void)unsubscribeFromTouchEventsForObjectID:(NSString *)objectID {
    if (objectID.length == 0) return;
    [self.subscribedObjectIDsStorage removeObject:objectID];
    // Unregister from the C++ hit testing system via the controller
    [_controller unsubscribeFromElementWithID:objectID];
}

- (void)unsubscribeFromTouchEventsForObjectIDs:(NSArray<NSString *> *)objectIDs {
    for (NSString *objectID in objectIDs) {
        [self unsubscribeFromTouchEventsForObjectID:objectID];
    }
}

- (void)unsubscribeFromAllElementTouchEvents {
    [self.subscribedObjectIDsStorage removeAllObjects];
    // Clear all subscriptions in the C++ hit testing system via the controller
    [_controller unsubscribeFromAllElements];
}

- (BOOL)isSubscribedToObjectID:(NSString *)objectID {
    return [self.subscribedObjectIDsStorage containsObject:objectID];
}

- (nullable NSString *)hitTestSubscribedElementAtPoint:(CGPoint)point {
    // Query C++ hit testing system via the controller
    // The controller converts view coords to SVG coords and performs hit testing
    CGSize viewSize = self.bounds.size;
    NSString *hitElement = [_controller hitTestAtPoint:point viewSize:viewSize];

    // Only return if the element is in our subscribed set
    if (hitElement && [self.subscribedObjectIDsStorage containsObject:hitElement]) {
        return hitElement;
    }
    return nil;
}

- (NSArray<NSString *> *)hitTestAllSubscribedElementsAtPoint:(CGPoint)point {
    // Query C++ hit testing system via the controller for all elements at the point
    CGSize viewSize = self.bounds.size;
    NSInteger maxElements = self.subscribedObjectIDsStorage.count;
    if (maxElements == 0) return @[];

    NSArray<NSString *> *allHits = [_controller elementsAtPoint:point
                                                        viewSize:viewSize
                                                     maxElements:maxElements];

    // Filter to only return subscribed elements
    NSMutableArray<NSString *> *subscribedHits = [NSMutableArray array];
    for (NSString *elementID in allHits) {
        if ([self.subscribedObjectIDsStorage containsObject:elementID]) {
            [subscribedHits addObject:elementID];
        }
    }
    return [subscribedHits copy];
}

- (BOOL)elementWithObjectID:(NSString *)objectID containsPoint:(CGPoint)point {
    // Check if the element exists and get its bounds
    if (![_controller elementExistsWithID:objectID]) {
        return NO;
    }

    // Get all elements at the point and check if our element is among them
    CGSize viewSize = self.bounds.size;
    NSArray<NSString *> *elements = [_controller elementsAtPoint:point
                                                         viewSize:viewSize
                                                      maxElements:100];
    return [elements containsObject:objectID];
}

- (CGRect)boundingRectForObjectID:(NSString *)objectID {
    // Get the bounding rect in SVG coordinates from the controller
    CGRect svgRect = [_controller boundingRectForElementID:objectID];
    if (CGRectIsEmpty(svgRect)) {
        return CGRectNull;
    }

    // Transform from SVG coordinates to view coordinates
    return [self convertRectFromSVGCoordinates:svgRect];
}

- (CGRect)svgBoundingRectForObjectID:(NSString *)objectID {
    // Get the bounding rect in SVG coordinates directly from the controller
    CGRect svgRect = [_controller boundingRectForElementID:objectID];
    if (CGRectIsEmpty(svgRect)) {
        return CGRectNull;
    }
    return svgRect;
}

#pragma mark - Element Touch Handling (Internal)

// TODO: Override touchesBegan:withEvent:, touchesMoved:withEvent:,
// touchesEnded:withEvent:, touchesCancelled:withEvent: to detect touches
// on subscribed elements and dispatch to delegate.
//
// Implementation outline:
// 1. Get touch location in view coordinates
// 2. Call hitTestSubscribedElementAtPoint: to find hit element
// 3. If element is hit, create SVGElementTouchInfo and call delegate
// 4. Track enter/exit for drag events across element boundaries
// 5. Track long press timer for long press events
//
// This will require coordination with the cross-platform C++ hit testing
// system that actually knows the element geometry.

#pragma mark - Element Touch Event Dispatch (Internal Stubs)

/// Create a dual point from view coordinates (internal helper)
/// @param viewPoint The point in view coordinates
/// @return SVGDualPoint with both view and SVG coordinates
- (SVGDualPoint)dualPointFromViewPoint:(CGPoint)viewPoint {
    // Convert view coordinates to SVG coordinates using current viewport/zoom
    CGPoint svgPoint = [self convertPointToSVGCoordinates:viewPoint];
    return SVGDualPointMake(viewPoint, svgPoint);
}

/// Dispatch a tap event to the delegate (called from touch handling)
/// @param objectID The objectID of the tapped element
/// @param viewPoint The tap location in view coordinates
- (void)dispatchTapEventForObjectID:(NSString *)objectID atViewPoint:(CGPoint)viewPoint {
    // TODO: This will be called by the touch handling system when a tap is detected
    // (touch down + touch up without significant movement, and no second tap follows)
    // MUTUALLY EXCLUSIVE: Will not fire if double-tap or drag is detected
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didTapElementWithID:atLocation:)]) {
        SVGDualPoint location = [self dualPointFromViewPoint:viewPoint];
        [self.delegate svgPlayerView:self didTapElementWithID:objectID atLocation:location];
    }
}

/// Dispatch a double-tap event to the delegate (called from touch handling)
/// @param objectID The objectID of the double-tapped element
/// @param viewPoint The double-tap location in view coordinates
- (void)dispatchDoubleTapEventForObjectID:(NSString *)objectID atViewPoint:(CGPoint)viewPoint {
    // TODO: This will be called when two taps occur in quick succession
    // MUTUALLY EXCLUSIVE: If this fires, single tap will NOT fire
    // iOS handles this via UITapGestureRecognizer with numberOfTapsRequired = 2
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didDoubleTapElementWithID:atLocation:)]) {
        SVGDualPoint location = [self dualPointFromViewPoint:viewPoint];
        [self.delegate svgPlayerView:self didDoubleTapElementWithID:objectID atLocation:location];
    }
}

/// Dispatch a long press event to the delegate (called from touch handling)
/// @param objectID The objectID of the long-pressed element
/// @param viewPoint The long press location in view coordinates
- (void)dispatchLongPressEventForObjectID:(NSString *)objectID atViewPoint:(CGPoint)viewPoint {
    // TODO: This will be called by a timer when touch is held for longPressDuration
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didLongPressElementWithID:atLocation:)]) {
        SVGDualPoint location = [self dualPointFromViewPoint:viewPoint];
        [self.delegate svgPlayerView:self didLongPressElementWithID:objectID atLocation:location];
    }
}

/// Dispatch a drag event to the delegate (called continuously during drag)
/// @param objectID The objectID of the dragged element
/// @param currentViewPoint The current drag location in view coordinates
/// @param startViewPoint The original touch-down point (for translation calculation)
- (void)dispatchDragEventForObjectID:(NSString *)objectID
                    currentViewPoint:(CGPoint)currentViewPoint
                      startViewPoint:(CGPoint)startViewPoint {
    // TODO: This will be called on touchesMoved when drag threshold is exceeded
    // MUTUALLY EXCLUSIVE: Once drag fires, tap will NOT fire for this touch sequence
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didDragElementWithID:currentLocation:translation:)]) {
        SVGDualPoint currentLocation = [self dualPointFromViewPoint:currentViewPoint];

        // Calculate translation in both coordinate systems
        CGPoint viewTranslation = CGPointMake(currentViewPoint.x - startViewPoint.x,
                                               currentViewPoint.y - startViewPoint.y);
        CGPoint svgStart = [self convertPointToSVGCoordinates:startViewPoint];
        CGPoint svgTranslation = CGPointMake(currentLocation.svgPoint.x - svgStart.x,
                                              currentLocation.svgPoint.y - svgStart.y);
        SVGDualPoint translation = SVGDualPointMake(viewTranslation, svgTranslation);

        [self.delegate svgPlayerView:self
              didDragElementWithID:objectID
                   currentLocation:currentLocation
                       translation:translation];
    }
}

/// Dispatch a drop event to the delegate (called when finger lifts after drag)
/// @param objectID The objectID of the dropped element
/// @param dropViewPoint The final location where the finger lifted
/// @param startViewPoint The original touch-down point (for total translation)
- (void)dispatchDropEventForObjectID:(NSString *)objectID
                       dropViewPoint:(CGPoint)dropViewPoint
                      startViewPoint:(CGPoint)startViewPoint {
    // TODO: This will be called on touchesEnded after a drag sequence
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didDropElementWithID:atLocation:totalTranslation:)]) {
        SVGDualPoint dropLocation = [self dualPointFromViewPoint:dropViewPoint];

        // Calculate total translation in both coordinate systems
        CGPoint viewTranslation = CGPointMake(dropViewPoint.x - startViewPoint.x,
                                               dropViewPoint.y - startViewPoint.y);
        CGPoint svgStart = [self convertPointToSVGCoordinates:startViewPoint];
        CGPoint svgTranslation = CGPointMake(dropLocation.svgPoint.x - svgStart.x,
                                              dropLocation.svgPoint.y - svgStart.y);
        SVGDualPoint totalTranslation = SVGDualPointMake(viewTranslation, svgTranslation);

        [self.delegate svgPlayerView:self
              didDropElementWithID:objectID
                        atLocation:dropLocation
                  totalTranslation:totalTranslation];
    }
}

/// Dispatch a detailed touch event (for advanced use cases)
/// @param objectID The objectID of the touched element
/// @param touchInfo The full touch info structure
- (void)dispatchDetailedTouchEventForObjectID:(NSString *)objectID touchInfo:(SVGElementTouchInfo)touchInfo {
    // TODO: This provides full touch details for advanced use cases
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didTouchElement:touchInfo:)]) {
        [self.delegate svgPlayerView:self didTouchElement:objectID touchInfo:touchInfo];
    }
}

/// Dispatch element enter event (during drag across element boundaries)
/// @param objectID The objectID of the entered element
- (void)dispatchEnterEventForObjectID:(NSString *)objectID {
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didEnterElementWithID:)]) {
        [self.delegate svgPlayerView:self didEnterElementWithID:objectID];
    }
}

/// Dispatch element exit event (during drag across element boundaries)
/// @param objectID The objectID of the exited element
- (void)dispatchExitEventForObjectID:(NSString *)objectID {
    if ([self.delegate respondsToSelector:@selector(svgPlayerView:didExitElementWithID:)]) {
        [self.delegate svgPlayerView:self didExitElementWithID:objectID];
    }
}

@end
