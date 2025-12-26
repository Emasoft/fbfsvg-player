// file_dialog_macos.mm - macOS file dialog implementation using Cocoa NSOpenPanel
// Requires linking with -framework Cocoa and -framework UniformTypeIdentifiers

#include "file_dialog.h"

#if defined(FILE_DIALOG_MACOS)

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string>
#include <unistd.h>
#include <limits.h>

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

#endif // FILE_DIALOG_MACOS
