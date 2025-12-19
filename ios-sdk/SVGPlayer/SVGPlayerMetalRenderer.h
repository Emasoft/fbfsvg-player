// SVGPlayerMetalRenderer.h - Metal-based GPU renderer for SVGPlayerView
//
// This class handles Metal rendering for the SVGPlayerView.
// It creates an MTKView, manages Metal resources, and renders
// SVG content using Skia's Metal backend.

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@class SVGPlayerController;

#pragma mark - SVGPlayerRenderer Protocol

/// Protocol for SVG renderers (allows future extension to other backends)
@protocol SVGPlayerRenderer <NSObject>

/// Render the current frame
- (void)render;

/// Update renderer for new size
/// @param size The new view size in points
/// @param scale The display scale factor
- (void)updateForSize:(CGSize)size scale:(CGFloat)scale;

/// Capture current frame as UIImage
/// @return The current frame as a UIImage, or nil if not available
- (nullable UIImage *)captureImage;

/// Clean up renderer resources
- (void)cleanup;

@end

#pragma mark - SVGPlayerMetalRenderer

/// Metal-based GPU renderer for SVGPlayerView
///
/// This renderer uses MTKView and Metal to render SVG content
/// with GPU acceleration. It renders Skia content to a Metal texture
/// and displays it efficiently.
@interface SVGPlayerMetalRenderer : NSObject <SVGPlayerRenderer>

/// Initialize with a parent view and controller
/// @param view The parent UIView to render into
/// @param controller The SVG controller providing content
/// @return A new renderer, or nil if Metal is not available
- (nullable instancetype)initWithView:(UIView *)view
                           controller:(SVGPlayerController *)controller NS_DESIGNATED_INITIALIZER;

/// Unavailable - use initWithView:controller: instead
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// The Metal view used for rendering (added as subview of parent)
@property (nonatomic, readonly, nullable) UIView *metalView;

/// Whether Metal rendering is available on this device
@property (class, nonatomic, readonly) BOOL isMetalAvailable;

@end

NS_ASSUME_NONNULL_END
