// file_dialog_macos.mm - macOS file dialog implementation using Cocoa NSOpenPanel
// Requires linking with -framework Cocoa and -framework UniformTypeIdentifiers

#include "file_dialog.h"

#if defined(FILE_DIALOG_MACOS)

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string>
#include <unistd.h>
#include <limits.h>

// SDL includes for window configuration
#include <SDL.h>
#include <SDL_syswm.h>

std::string openSVGFileDialog(const char* title, const char* initialPath) {
    // NSOpenPanel must be created and run on the main thread
    // Since we're in an SDL app, we need to use @autoreleasepool
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        // Configure panel for single SVG file selection
        [panel setTitle:[NSString stringWithUTF8String:title]];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanCreateDirectories:NO];

        // Filter for SVG files only using modern UTType API (macOS 11+)
        UTType* svgType = [UTType typeWithFilenameExtension:@"svg"];
        if (svgType) {
            [panel setAllowedContentTypes:@[svgType]];
        }

        // Set initial directory - use provided path or current working directory
        if (initialPath && initialPath[0] != '\0') {
            NSString* path = [NSString stringWithUTF8String:initialPath];
            NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
            [panel setDirectoryURL:url];
        } else {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                NSString* path = [NSString stringWithUTF8String:cwd];
                NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
                [panel setDirectoryURL:url];
            }
        }

        // Run the panel modally (blocks until user responds)
        // Note: This works with SDL because SDL's event loop is paused during modal dialogs
        NSModalResponse response = [panel runModal];

        if (response == NSModalResponseOK) {
            NSURL* selectedURL = [[panel URLs] firstObject];
            if (selectedURL) {
                return std::string([[selectedURL path] UTF8String]);
            }
        }

        // User cancelled or error
        return std::string();
    }
}

std::string openFolderDialog(const char* title, const char* initialPath) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        // Configure panel for folder selection
        [panel setTitle:[NSString stringWithUTF8String:title]];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanCreateDirectories:NO];

        // Set initial directory - use provided path or current working directory
        if (initialPath && initialPath[0] != '\0') {
            NSString* path = [NSString stringWithUTF8String:initialPath];
            NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
            [panel setDirectoryURL:url];
        } else {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                NSString* path = [NSString stringWithUTF8String:cwd];
                NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
                [panel setDirectoryURL:url];
            }
        }

        NSModalResponse response = [panel runModal];

        if (response == NSModalResponseOK) {
            NSURL* selectedURL = [[panel URLs] firstObject];
            if (selectedURL) {
                return std::string([[selectedURL path] UTF8String]);
            }
        }

        return std::string();
    }
}

// Custom window delegate that implements zoom behavior
@interface SDLWindowZoomDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) NSRect originalFrame;
@property (nonatomic, assign) BOOL isZoomed;
@end

@implementation SDLWindowZoomDelegate

- (NSRect)windowWillUseStandardFrame:(NSWindow *)window defaultFrame:(NSRect)newFrame {
    // Return the screen's visible frame (minus dock/menu) for zoom
    NSScreen *screen = [window screen];
    if (screen) {
        return [screen visibleFrame];
    }
    return newFrame;
}

@end

// Static delegate instance (one per app is fine)
static SDLWindowZoomDelegate* g_zoomDelegate = nil;

void configureWindowForZoom(SDL_Window* window) {
    // On macOS, configure the green titlebar button to zoom (maximize) instead of fullscreen
    // This requires accessing the native NSWindow and modifying its collectionBehavior
    @autoreleasepool {
        // Get the native window info from SDL
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);

        if (SDL_GetWindowWMInfo(window, &wmInfo)) {
            // Get the NSWindow from the SDL window info
            NSWindow* nsWindow = wmInfo.info.cocoa.window;

            if (nsWindow) {
                // Remove fullscreen capability from the window entirely
                // This makes the green button perform zoom (maximize) instead of fullscreen
                NSWindowCollectionBehavior behavior = [nsWindow collectionBehavior];

                // Remove all fullscreen behaviors
                behavior &= ~NSWindowCollectionBehaviorFullScreenPrimary;
                behavior &= ~NSWindowCollectionBehaviorFullScreenAuxiliary;

                // Add the behavior that explicitly disables fullscreen for this window
                // This makes the green button show the + (zoom) icon instead of arrows
                behavior |= NSWindowCollectionBehaviorFullScreenNone;

                [nsWindow setCollectionBehavior:behavior];

                // Also ensure the zoom button is enabled in the style mask
                NSWindowStyleMask styleMask = [nsWindow styleMask];
                // Make sure resizable is set (required for zoom to work)
                styleMask |= NSWindowStyleMaskResizable;
                [nsWindow setStyleMask:styleMask];

                // Create and set a custom delegate that implements windowWillUseStandardFrame:
                // This tells macOS what size to use when the zoom button is clicked
                if (!g_zoomDelegate) {
                    g_zoomDelegate = [[SDLWindowZoomDelegate alloc] init];
                }
                [nsWindow setDelegate:g_zoomDelegate];
            }
        }
    }
}

bool toggleWindowMaximize(SDL_Window* window) {
    @autoreleasepool {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);

        if (SDL_GetWindowWMInfo(window, &wmInfo)) {
            NSWindow* nsWindow = wmInfo.info.cocoa.window;
            if (nsWindow) {
                __block BOOL result = NO;

                // macOS UI operations must run on the main thread
                // Use dispatch_sync to wait for completion and get result
                void (^zoomBlock)(void) = ^{
                    // Toggle zoom state using macOS native zoom
                    [nsWindow zoom:nil];
                    // Get the new zoom state
                    result = [nsWindow isZoomed];
                };

                // Check if we're already on the main thread to avoid deadlock
                if ([NSThread isMainThread]) {
                    zoomBlock();
                } else {
                    dispatch_sync(dispatch_get_main_queue(), zoomBlock);
                }

                return result;
            }
        }
        return false;
    }
}

bool isWindowMaximized(SDL_Window* window) {
    @autoreleasepool {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);

        if (SDL_GetWindowWMInfo(window, &wmInfo)) {
            NSWindow* nsWindow = wmInfo.info.cocoa.window;
            if (nsWindow) {
                __block BOOL result = NO;

                // macOS UI operations must run on the main thread
                void (^checkBlock)(void) = ^{
                    result = [nsWindow isZoomed];
                };

                // Check if we're already on the main thread to avoid deadlock
                if ([NSThread isMainThread]) {
                    checkBlock();
                } else {
                    dispatch_sync(dispatch_get_main_queue(), checkBlock);
                }

                return result;
            }
        }
        return false;
    }
}

#endif // FILE_DIALOG_MACOS
