// file_dialog.h - Cross-platform file dialog abstraction
// Provides native file picker dialogs for each platform

#pragma once

#include <string>

//==============================================================================
// File Dialog API
//==============================================================================

/**
 * Opens a native file picker dialog to select an SVG file.
 *
 * On macOS: Uses NSOpenPanel with Cocoa
 * On Linux: Uses zenity or kdialog if available, falls back to console
 * On Windows: Uses GetOpenFileName with Win32 API
 *
 * @param title       Dialog title (e.g., "Open SVG File")
 * @param initialPath Optional starting directory (empty string for default)
 * @return            Selected file path, or empty string if cancelled
 */
std::string openSVGFileDialog(const char* title = "Open SVG File",
                               const char* initialPath = "");

/**
 * Opens a native folder picker dialog.
 *
 * @param title       Dialog title (e.g., "Select Folder")
 * @param initialPath Optional starting directory (empty string for default)
 * @return            Selected folder path, or empty string if cancelled
 */
std::string openFolderDialog(const char* title = "Select Folder",
                              const char* initialPath = "");

//==============================================================================
// Platform Detection (mirrors platform.h)
//==============================================================================

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC && !TARGET_OS_IOS
        #define FILE_DIALOG_MACOS 1
    #endif
#elif defined(__linux__)
    #define FILE_DIALOG_LINUX 1
#elif defined(_WIN32)
    #define FILE_DIALOG_WINDOWS 1
#endif

