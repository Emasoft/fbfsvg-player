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

// Forward declaration for SDL_Window (avoid including SDL headers in this header)
struct SDL_Window;

/**
 * Configures window so green button maximizes instead of going fullscreen.
 * macOS only - the green titlebar button should zoom/maximize, not enter fullscreen.
 * Fullscreen mode should only be triggered by explicit key press (F key).
 *
 * @param window SDL window to configure
 */
void configureWindowForZoom(SDL_Window* window);

/**
 * Toggle window between maximized (zoomed) and normal state.
 * On macOS: Uses native zoom behavior (fills screen minus dock/menu)
 * On other platforms: Uses SDL_MaximizeWindow/SDL_RestoreWindow
 *
 * @param window SDL window to toggle
 * @return true if window is now maximized, false if restored to normal
 */
bool toggleWindowMaximize(SDL_Window* window);

/**
 * Get current maximize state of the window.
 *
 * @param window SDL window to check
 * @return true if window is maximized/zoomed, false otherwise
 */
bool isWindowMaximized(SDL_Window* window);

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

