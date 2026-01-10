// file_dialog_linux.cpp - Linux file dialog implementation
// Uses zenity (GTK), kdialog (KDE), or yad as GUI file picker
// Falls back to console message if no dialog tool is available

#if defined(__linux__) || defined(__unix__)

#include "file_dialog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <array>
#include <memory>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

// SDL includes for window control functions
#include <SDL.h>

namespace {

// Execute a command and capture its stdout (blocking)
// Returns empty string on error or if command returns non-zero
std::string executeCommand(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string result;

    // Open pipe to command (read mode)
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "";
    }

    // Read all output
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    // Remove trailing newline if present
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }

    return result;
}

// Check if a command exists in PATH
bool commandExists(const char* cmd) {
    std::string checkCmd = "which ";
    checkCmd += cmd;
    checkCmd += " > /dev/null 2>&1";
    return system(checkCmd.c_str()) == 0;
}

// Escape a string for shell command (single quotes with escaping)
std::string shellEscape(const std::string& str) {
    std::string escaped = "'";
    for (char c : str) {
        if (c == '\'') {
            escaped += "'\\''";  // End quote, escaped single quote, start quote
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

// Get current working directory
std::string getCurrentDirectory() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return std::string(cwd);
    }
    return "";
}

// Check if path is a directory
bool isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

// Detect which dialog tool is available (order: zenity, kdialog, yad)
enum class DialogTool {
    None,
    Zenity,     // GTK-based (GNOME, XFCE, etc.)
    KDialog,    // Qt-based (KDE)
    Yad         // Yet Another Dialog (fork of zenity with more features)
};

DialogTool detectDialogTool() {
    // Check environment for desktop hints
    const char* desktop = getenv("XDG_CURRENT_DESKTOP");
    const char* kde = getenv("KDE_FULL_SESSION");

    // Prefer kdialog on KDE
    if ((kde && strcmp(kde, "true") == 0) ||
        (desktop && (strstr(desktop, "KDE") != nullptr || strstr(desktop, "Plasma") != nullptr))) {
        if (commandExists("kdialog")) {
            return DialogTool::KDialog;
        }
    }

    // Otherwise prefer zenity (most common)
    if (commandExists("zenity")) {
        return DialogTool::Zenity;
    }

    // Fall back to kdialog if zenity not available
    if (commandExists("kdialog")) {
        return DialogTool::KDialog;
    }

    // Try yad as last resort
    if (commandExists("yad")) {
        return DialogTool::Yad;
    }

    return DialogTool::None;
}

// Build zenity file selection command
std::string buildZenityFileCmd(const char* title, const char* initialPath, bool folderMode) {
    std::string cmd = "zenity --file-selection";

    if (folderMode) {
        cmd += " --directory";
    } else {
        // Filter for SVG files only
        cmd += " --file-filter='SVG files (*.svg)|*.svg'";
        cmd += " --file-filter='All files|*'";
    }

    cmd += " --title=" + shellEscape(title);

    // Set initial directory/file
    std::string startPath;
    if (initialPath && initialPath[0] != '\0') {
        startPath = initialPath;
    } else {
        startPath = getCurrentDirectory();
    }

    if (!startPath.empty()) {
        // If it's a directory, use --filename with trailing slash
        // If it's a file, use --filename with the file path
        if (isDirectory(startPath)) {
            if (startPath.back() != '/') {
                startPath += '/';
            }
        }
        cmd += " --filename=" + shellEscape(startPath);
    }

    // Redirect stderr to /dev/null to suppress GTK warnings
    cmd += " 2>/dev/null";

    return cmd;
}

// Build kdialog file selection command
std::string buildKDialogFileCmd(const char* title, const char* initialPath, bool folderMode) {
    std::string cmd = "kdialog";

    if (folderMode) {
        cmd += " --getexistingdirectory";
    } else {
        cmd += " --getopenfilename";
    }

    // Set initial directory/file
    std::string startPath;
    if (initialPath && initialPath[0] != '\0') {
        startPath = initialPath;
    } else {
        startPath = getCurrentDirectory();
    }

    if (!startPath.empty()) {
        cmd += " " + shellEscape(startPath);
    } else {
        cmd += " .";
    }

    // Add filter for SVG files (only for file mode)
    if (!folderMode) {
        cmd += " 'SVG files (*.svg)'";
    }

    cmd += " --title " + shellEscape(title);

    // Redirect stderr to /dev/null
    cmd += " 2>/dev/null";

    return cmd;
}

// Build yad file selection command (similar to zenity)
std::string buildYadFileCmd(const char* title, const char* initialPath, bool folderMode) {
    std::string cmd = "yad --file";

    if (folderMode) {
        cmd += " --directory";
    } else {
        cmd += " --file-filter='SVG files|*.svg'";
        cmd += " --file-filter='All files|*'";
    }

    cmd += " --title=" + shellEscape(title);

    std::string startPath;
    if (initialPath && initialPath[0] != '\0') {
        startPath = initialPath;
    } else {
        startPath = getCurrentDirectory();
    }

    if (!startPath.empty()) {
        cmd += " --filename=" + shellEscape(startPath);
    }

    cmd += " 2>/dev/null";

    return cmd;
}

} // anonymous namespace

std::string openSVGFileDialog(const char* title, const char* initialPath) {
    DialogTool tool = detectDialogTool();

    std::string cmd;
    switch (tool) {
        case DialogTool::Zenity:
            cmd = buildZenityFileCmd(title, initialPath, false);
            break;
        case DialogTool::KDialog:
            cmd = buildKDialogFileCmd(title, initialPath, false);
            break;
        case DialogTool::Yad:
            cmd = buildYadFileCmd(title, initialPath, false);
            break;
        case DialogTool::None:
        default:
            fprintf(stderr, "Note: No GUI file dialog available (install zenity, kdialog, or yad).\n");
            fprintf(stderr, "      Pass file path as command line argument instead.\n");
            return "";
    }

    std::string result = executeCommand(cmd);

    // Validate result is an existing file with .svg extension
    if (!result.empty()) {
        struct stat st;
        if (stat(result.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            // Check for .svg extension (case-insensitive)
            size_t len = result.length();
            if (len >= 4) {
                std::string ext = result.substr(len - 4);
                for (char& c : ext) {
                    c = tolower(c);
                }
                if (ext == ".svg") {
                    return result;
                }
            }
            // Not an SVG file but user selected it - return anyway
            // (filter should have prevented this, but be permissive)
            return result;
        }
    }

    return "";  // Cancelled or invalid selection
}

std::string openFolderDialog(const char* title, const char* initialPath) {
    DialogTool tool = detectDialogTool();

    std::string cmd;
    switch (tool) {
        case DialogTool::Zenity:
            cmd = buildZenityFileCmd(title, initialPath, true);
            break;
        case DialogTool::KDialog:
            cmd = buildKDialogFileCmd(title, initialPath, true);
            break;
        case DialogTool::Yad:
            cmd = buildYadFileCmd(title, initialPath, true);
            break;
        case DialogTool::None:
        default:
            fprintf(stderr, "Note: No GUI folder dialog available (install zenity, kdialog, or yad).\n");
            fprintf(stderr, "      Pass folder path as command line argument instead.\n");
            return "";
    }

    std::string result = executeCommand(cmd);

    // Validate result is an existing directory
    if (!result.empty() && isDirectory(result)) {
        return result;
    }

    return "";  // Cancelled or invalid selection
}

// Stub for Linux - green button zoom configuration is macOS-specific
void configureWindowForZoom(SDL_Window* /* window */) {
    // No-op on Linux - window managers handle maximize behavior natively
}

bool toggleWindowMaximize(SDL_Window* window) {
    // On Linux, use SDL's cross-platform maximize/restore
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_MAXIMIZED) {
        SDL_RestoreWindow(window);
        return false;
    } else {
        SDL_MaximizeWindow(window);
        return true;
    }
}

bool isWindowMaximized(SDL_Window* window) {
    Uint32 flags = SDL_GetWindowFlags(window);
    return (flags & SDL_WINDOW_MAXIMIZED) != 0;
}

#endif // __linux__ || __unix__
