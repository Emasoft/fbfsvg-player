// svg_player_animated.cpp - Real-time SVG renderer with SMIL animation support
// Usage: svg_player_animated <input.svg>
// Supports discrete frame animations (xlink:href switching)

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkExecutor.h"  // Skia's thread pool abstraction
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
// Note: Platform-specific font manager is provided by platform.h
#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>   // For O_CREAT, O_WRONLY, O_TRUNC in signal handler
#include <limits.h>
#include <vector>
#include <algorithm>   // For std::sort
#include <dirent.h>    // For directory scanning
#include <regex>       // For frame number extraction
#include <execinfo.h>  // For backtrace() on macOS/Linux

#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "src/core/SkTaskGroup.h"  // Required for parallel tile rendering (see line 384)

// Shared animation controller for cross-platform SMIL parsing and playback
#include "../shared/SVGAnimationController.h"

// Dirty region tracking for partial rendering optimization
#include "../shared/DirtyRegionTracker.h"
#include "../shared/ElementBoundsExtractor.h"

// Centralized version management for all platforms
#include "../shared/version.h"

// Cross-platform abstractions for CPU monitoring, font management, etc.
#include "platform.h"

// Native file dialog for opening SVG files
#include "file_dialog.h"

// Visual folder browser for file navigation
#include "folder_browser.h"

// Thumbnail cache for background-threaded SVG thumbnail loading
#include "thumbnail_cache.h"

// Remote control server for programmatic control via TCP/JSON
#include "remote_control.h"

// Metal GPU backend for macOS (optional, enabled with --metal flag)
#ifdef __APPLE__
#include "metal_context.h"
#include "graphite_context.h"  // Graphite next-gen GPU backend (--graphite flag)
#endif

// =============================================================================
// Global shutdown flag for graceful termination
// =============================================================================
static std::atomic<bool> g_shutdownRequested{false};

// =============================================================================
// Folder Browser State Globals
//
// The folder browser runs with asynchronous operations to keep the UI responsive:
//
// Thread Safety Model:
// - g_browserDomParseMutex: Protects DOM parsing state (check-and-start atomicity)
// - g_browserPendingAnimMutex: Protects pending animations being parsed in background
// - g_browserAnimMutex: Protects active animations being read by render loop
// - g_browserDomMutex: Protects pending DOM being parsed in background
// - g_browserScanMessageMutex: Protects scan progress messages
//
// Async Operations:
// 1. Directory Scanning: Background thread scans folder for SVG files
//    - g_browserAsyncScanning: Indicates scan in progress
//    - g_browserScanProgress: Atomic progress counter (0.0 to 1.0)
//    - g_browserScanMessage: Status message (mutex-protected)
//
// 2. DOM Parsing: Background thread parses browser SVG (see startAsyncBrowserDomParse)
//    - g_browserDomParsing: Atomic flag indicating parse in progress
//    - g_browserDomReady: Atomic flag indicating new DOM ready to swap
//    - g_browserPendingDom: DOM being parsed (mutex-protected)
//    - g_browserPendingSvg: SVG content being parsed (mutex-protected)
//
// 3. Animation Extraction: Background thread extracts SMIL animations
//    - g_browserPendingAnimations: Parsed animations (mutex-protected)
//    - g_browserAnimations: Active animations for render loop (mutex-protected)
//    - g_browserAnimStartTime: Time origin for animation playback
//
// Main Thread Responsibilities:
// - Never blocks on parsing or scanning (all done in background threads)
// - Atomically swaps DOM/animations when ready (see main loop)
// - Handles user input and rendering
// =============================================================================
static bool g_browserMode = false;
static bool g_browserAsyncScanning = false;  // True when async scan in progress
static svgplayer::FolderBrowser g_folderBrowser;
static sk_sp<SkSVGDOM> g_browserSvgDom;  // Parsed browser SVG for rendering

// Atomic progress values (updated from scan thread, read from main thread)
static std::atomic<float> g_browserScanProgress{0.0f};
static std::string g_browserScanMessage;
static std::mutex g_browserScanMessageMutex;

// Async DOM parsing infrastructure - main thread NEVER blocks on parsing
static std::thread g_browserDomParseThread;
static std::atomic<bool> g_browserDomParsing{false};  // True when background parse in progress
static std::atomic<bool> g_browserDomReady{false};    // True when new DOM ready to swap
static sk_sp<SkSVGDOM> g_browserPendingDom;           // DOM being parsed in background
static std::mutex g_browserDomMutex;                  // Protects g_browserPendingDom
static std::string g_browserPendingSvg;               // SVG being parsed
static std::mutex g_browserPendingSvgMutex;           // Protects g_browserPendingSvg
static std::mutex g_browserDomParseMutex;             // Protects check-and-start sequence in startAsyncBrowserDomParse

// Browser animation support - composite SVG with all cells animating live
// Animation extraction happens in background thread, animations swapped atomically with DOM
static std::vector<svgplayer::SMILAnimation> g_browserAnimations;   // Active animations (render loop reads)
static std::vector<svgplayer::SMILAnimation> g_browserPendingAnimations;  // Parsed in background thread
static std::chrono::steady_clock::time_point g_browserAnimStartTime; // Animation time origin
static std::mutex g_browserAnimMutex;                                // Protects active animations
static std::mutex g_browserPendingAnimMutex;                         // Protects pending animations

// Double-click detection for folder browser
static const Uint32 DOUBLE_CLICK_THRESHOLD_MS = 400;  // Max time between clicks for double-click
static Uint64 g_browserLastClickTime = 0;             // SDL_GetTicks64() at last click (no 49-day wraparound)
static int g_browserLastClickIndex = -1;              // Last clicked entry index

// Benchmark mode settings (global for access from threads)
static bool g_jsonOutput = false;  // Output benchmark stats as JSON (suppress stdout messages)

// Black screen detection - verifies actual content is being rendered
static std::atomic<int> g_lastNonBlackPixelCount{0};  // Count of non-black pixels in last frame
static std::atomic<bool> g_blackScreenDetected{false};  // True if last frame was all black
static std::atomic<int> g_consecutiveBlackFrames{0};  // How many frames in a row were black

// Check if a pixel buffer contains visible (non-black) content
// Returns the count of non-black pixels (sampling every 100th pixel for speed)
// excludeOverlayRect: area to exclude from check (debug overlay region)
inline int countNonBlackPixels(const uint32_t* pixels, int width, int height,
                               int excludeX = 0, int excludeY = 0,
                               int excludeW = 0, int excludeH = 0) {
    if (!pixels || width <= 0 || height <= 0) return 0;

    int nonBlackCount = 0;
    const int sampleStep = 100;  // Sample every 100th pixel for speed

    for (int i = 0; i < width * height; i += sampleStep) {
        int x = i % width;
        int y = i / width;

        // Skip pixels in the exclude rectangle (debug overlay area)
        if (excludeW > 0 && excludeH > 0 &&
            x >= excludeX && x < excludeX + excludeW &&
            y >= excludeY && y < excludeY + excludeH) {
            continue;
        }

        // Check if pixel is non-black (any color channel > 10 to allow for near-black)
        uint32_t pixel = pixels[i];
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;

        if (r > 10 || g > 10 || b > 10) {
            nonBlackCount++;
        }
    }

    return nonBlackCount;
}

// Convert RepeatMode enum to human-readable string for debug overlay
inline const char* repeatModeToString(svgplayer::RepeatMode mode) {
    switch (mode) {
        case svgplayer::RepeatMode::None:    return "Once";
        case svgplayer::RepeatMode::Loop:    return "Loop";
        case svgplayer::RepeatMode::Reverse: return "PingPong";
        case svgplayer::RepeatMode::Count:   return "Count";
        default:                              return "Unknown";
    }
}

// Signal handler for graceful shutdown (SIGINT, SIGTERM)
void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_shutdownRequested.store(true);
        // Use write() for signal-safe debug output
        const char* msg = "[SIGNAL] Shutdown requested\n";
        write(STDERR_FILENO, msg, strlen(msg));
        // Also create a marker file to verify handler was called (signal-safe)
        int fd = open("/tmp/svg_signal_marker.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, msg, strlen(msg));
            close(fd);
        }
    }
}

// Print stack trace for debugging critical errors (freeze detection, crashes)
// Uses backtrace() on macOS/Linux - max 64 stack frames
void printStackTrace(const char* context) {
    constexpr int MAX_FRAMES = 64;
    void* callstack[MAX_FRAMES];
    int numFrames = backtrace(callstack, MAX_FRAMES);
    char** symbols = backtrace_symbols(callstack, numFrames);

    std::cerr << "\n=== STACK TRACE (" << context << ") ===" << std::endl;
    if (symbols != nullptr) {
        for (int i = 0; i < numFrames; i++) {
            std::cerr << "  [" << i << "] " << symbols[i] << std::endl;
        }
        free(symbols);  // backtrace_symbols allocates memory
    } else {
        std::cerr << "  (Failed to get stack trace symbols)" << std::endl;
    }
    std::cerr << "=== END STACK TRACE ===\n" << std::endl;
}

// =============================================================================
// Async DOM Parsing - Main thread NEVER blocks on SVG parsing
// =============================================================================

// Forward declaration (defined after global font manager setup)
sk_sp<SkSVGDOM> makeSVGDOMWithFontSupport(SkStream& stream);

// Start async parsing of browser SVG (called from main thread, non-blocking)
void startAsyncBrowserDomParse(const std::string& svgContent) {
    // CRITICAL: Protect entire check-and-start sequence to prevent race condition
    // where two threads both check g_browserDomParsing (both false), then both try to start
    std::lock_guard<std::mutex> lock(g_browserDomParseMutex);

    // If already parsing, skip (current parse will complete)
    if (g_browserDomParsing.load()) {
        return;
    }

    // Join any previous thread before starting new one
    if (g_browserDomParseThread.joinable()) {
        g_browserDomParseThread.join();
    }

    // Store SVG content for background thread
    {
        std::lock_guard<std::mutex> svgLock(g_browserPendingSvgMutex);
        g_browserPendingSvg = svgContent;
    }

    g_browserDomParsing.store(true);
    g_browserDomReady.store(false);

    // Start background parsing thread
    g_browserDomParseThread = std::thread([]() {
        std::string svgToParse;
        {
            std::lock_guard<std::mutex> lock(g_browserPendingSvgMutex);
            svgToParse = std::move(g_browserPendingSvg);
        }

        if (!svgToParse.empty()) {
            // CRITICAL: Preprocess SVG FIRST to inject synthetic IDs into <use> elements
            // without id attributes. This must happen BEFORE DOM parsing so both the DOM
            // and animation controller see the same content with synthetic IDs.
            //
            // WHY PREPROCESSING HAPPENS TWICE (intentional):
            // 1. HERE: getPreprocessedContent() adds synthetic IDs to DOM structure
            // 2. BELOW (loadFromContent): Preprocessing is skipped (content already preprocessed)
            //    but SMIL animation extraction requires loadFromContent() call
            //
            // This is NOT redundant - the first preprocessing modifies the DOM, the second
            // extracts animations from that modified DOM. Both operations need the same
            // preprocessed content to maintain ID consistency.
            svgplayer::SVGAnimationController localController;
            std::string preprocessedSvg = localController.getPreprocessedContent(svgToParse);

            // Debug: Check if any <animate> tags exist in the browser SVG
            size_t animateCount = 0;
            size_t pos = 0;
            while ((pos = preprocessedSvg.find("<animate", pos)) != std::string::npos) {
                animateCount++;
                pos++;
            }
            if (animateCount > 0 && !g_jsonOutput) {
                std::cout << "DEBUG: Found " << animateCount << " <animate> tags in browser SVG" << std::endl;
            }

            // Parse SVG DOM from PREPROCESSED content (includes synthetic IDs)
            sk_sp<SkData> browserData = SkData::MakeWithCopy(preprocessedSvg.data(), preprocessedSvg.size());
            std::unique_ptr<SkMemoryStream> browserStream = SkMemoryStream::Make(browserData);
            sk_sp<SkSVGDOM> newDom = makeSVGDOMWithFontSupport(*browserStream);

            // Extract SMIL animations from same preprocessed content
            // Note: loadFromContent() detects content is already preprocessed and skips re-preprocessing
            localController.loadFromContent(preprocessedSvg);
            std::vector<svgplayer::SMILAnimation> parsedAnimations = localController.getAnimations();

            // Store DOM result for main thread
            {
                std::lock_guard<std::mutex> lock(g_browserDomMutex);
                g_browserPendingDom = std::move(newDom);
            }

            // Store animations for main thread (separate mutex for atomicity)
            {
                std::lock_guard<std::mutex> lock(g_browserPendingAnimMutex);
                g_browserPendingAnimations = std::move(parsedAnimations);
            }

            g_browserDomReady.store(true);
        }

        g_browserDomParsing.store(false);
    });
}

// Check if async parse completed and swap DOM (called from main thread)
// Both DOM and animations were parsed in background thread - just swap pointers here
bool trySwapBrowserDom() {
    if (!g_browserDomReady.load()) {
        return false;
    }

    // Swap in the new DOM (fast pointer swap only)
    {
        std::lock_guard<std::mutex> lock(g_browserDomMutex);
        if (g_browserPendingDom) {
            g_browserSvgDom = std::move(g_browserPendingDom);
            g_browserPendingDom = nullptr;
        }
    }

    // Swap pre-parsed animations (background thread already extracted them)
    // This is a fast vector swap, no regex parsing on main thread
    // CRITICAL: Use std::scoped_lock to acquire both mutexes atomically (deadlock-free)
    {
        std::scoped_lock lock(g_browserPendingAnimMutex, g_browserAnimMutex);
        g_browserAnimations = std::move(g_browserPendingAnimations);
        g_browserAnimStartTime = std::chrono::steady_clock::now();

        if (!g_browserAnimations.empty()) {
            std::cout << "Browser: Swapped " << g_browserAnimations.size()
                      << " pre-parsed animations" << std::endl;
        }
    }

    g_browserDomReady.store(false);
    return true;
}

// Stop async DOM parsing (cleanup on shutdown or mode change)
void stopAsyncBrowserDomParse() {
    if (g_browserDomParseThread.joinable()) {
        // Wait for current parse to complete (can't cancel mid-parse)
        g_browserDomParseThread.join();
    }
    g_browserDomParsing.store(false);
    g_browserDomReady.store(false);
}

// Clear browser animation state (called when exiting browser mode)
void clearBrowserAnimations() {
    std::lock_guard<std::mutex> lock(g_browserAnimMutex);
    g_browserAnimations.clear();
}

// Install signal handlers for graceful shutdown using sigaction (more reliable than signal)
void installSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    // SA_RESTART: Restart interrupted system calls automatically
    // This is more reliable for catching signals during Metal/SDL operations
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

// =============================================================================
// File validation helpers
// =============================================================================

// Check if file exists and is readable
bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Check if path is a directory
bool isDirectory(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Get file size in bytes
size_t getFileSize(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
    return 0;
}

// Maximum SVG file size - effectively unlimited (8 GB practical limit)
// With modern systems having 64GB+ RAM, no practical limit is needed
static constexpr size_t MAX_SVG_FILE_SIZE = 8ULL * 1024 * 1024 * 1024;

// Debug overlay scaling factor (40% larger than original to match font size)
static constexpr float DEBUG_OVERLAY_SCALE = 1.4f;

// Validate SVG content (basic check for SVG structure)
bool validateSVGContent(const std::string& content) {
    // Check minimum length
    if (content.length() < 20) {
        return false;
    }
    // Check for SVG tag (case-insensitive search for <svg)
    size_t pos = content.find("<svg");
    if (pos == std::string::npos) {
        pos = content.find("<SVG");
    }
    return pos != std::string::npos;
}

// =============================================================================
// SVG Image Sequence (folder of individual SVG frames) support
// =============================================================================

// Extract frame number from filename (e.g., "frame_0001.svg" -> 1)
int extractFrameNumber(const std::string& filename) {
    // Try pattern: name_NNNN.svg (underscore before number)
    std::regex pattern("_(\\d+)\\.svg$", std::regex::icase);
    std::smatch match;
    if (std::regex_search(filename, match, pattern)) {
        return std::stoi(match[1].str());
    }
    // Try fallback: NNNN.svg (just number before extension)
    std::regex fallback("(\\d+)\\.svg$", std::regex::icase);
    if (std::regex_search(filename, match, fallback)) {
        return std::stoi(match[1].str());
    }
    return -1;  // No number found
}

// Scan folder for SVG files and return sorted list of paths
std::vector<std::string> scanFolderForSVGSequence(const std::string& folderPath) {
    std::vector<std::pair<int, std::string>> frameFiles;

    DIR* dir = opendir(folderPath.c_str());
    if (!dir) {
        std::cerr << "Error: Cannot open folder: " << folderPath << std::endl;
        return {};
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // Check for .svg extension (case-insensitive)
        if (name.size() > 4) {
            std::string ext = name.substr(name.size() - 4);
            if (ext == ".svg" || ext == ".SVG") {
                int frameNum = extractFrameNumber(name);
                std::string fullPath = folderPath + "/" + name;
                frameFiles.push_back({frameNum, fullPath});
            }
        }
    }
    closedir(dir);

    if (frameFiles.empty()) {
        std::cerr << "Error: No SVG files found in folder: " << folderPath << std::endl;
        return {};
    }

    // Sort by frame number (files without numbers sorted alphabetically at end)
    std::sort(frameFiles.begin(), frameFiles.end(), [](const auto& a, const auto& b) {
        if (a.first == -1 && b.first == -1) return a.second < b.second;  // Both no number: alphabetical
        if (a.first == -1) return false;  // No number goes after numbered
        if (b.first == -1) return true;   // Numbered goes before no number
        return a.first < b.first;         // Both numbered: sort by number
    });

    // Extract sorted paths
    std::vector<std::string> result;
    result.reserve(frameFiles.size());
    for (const auto& f : frameFiles) {
        result.push_back(f.second);
    }

    std::cerr << "Found " << result.size() << " SVG frames in sequence" << std::endl;
    return result;
}

// Print extensive help screen
void printHelp(const char* programName) {
    std::cerr << SVGPlayerVersion::getVersionBanner() << "\n\n";
    std::cerr << "USAGE:\n";
    std::cerr << "    " << programName << " <input.svg|folder> [OPTIONS]\n\n";
    std::cerr << "DESCRIPTION:\n";
    std::cerr << "    Real-time SVG renderer with SMIL animation support.\n";
    std::cerr << "    Plays animated SVG files with discrete frame animations\n";
    std::cerr << "    (xlink:href switching) using hardware-accelerated rendering.\n";
    std::cerr << "    Can also play a folder of individual SVG files as an image sequence.\n\n";
    std::cerr << "OPTIONS:\n";
    std::cerr << "    -h, --help        Show this help message and exit\n";
    std::cerr << "    -v, --version     Show version information and exit\n";
    std::cerr << "    -w, --windowed    Start in windowed mode (default is fullscreen)\n";
    std::cerr << "    -f, --fullscreen  Start in fullscreen mode (default)\n";
    std::cerr << "    -m, --maximize    Start in maximized (zoomed) windowed mode\n";
    std::cerr << "    --pos=X,Y         Set initial window position (e.g., --pos=100,200)\n";
    std::cerr << "    --size=WxH        Set initial window size (e.g., --size=800x600)\n";
    std::cerr << "    --sequential      Sequential frame mode: render frames 0,1,2,3... as fast\n";
    std::cerr << "                      as possible, ignoring SMIL wall-clock timing. Useful for\n";
    std::cerr << "                      benchmarking raw rendering throughput.\n";
    std::cerr << "    --remote-control[=PORT]  Enable remote control server (default port: 9999)\n";
    std::cerr << "    --duration=SECS   Benchmark mode: run for N seconds then exit\n";
    std::cerr << "    --json            Output benchmark stats as JSON (for scripting)\n";
#ifdef __APPLE__
    std::cerr << "    --metal           Enable Metal GPU backend (Ganesh)\n";
    std::cerr << "    --graphite        Enable Graphite GPU backend (next-gen, Metal)\n";
#endif
    std::cerr << "\n";
    std::cerr << "KEYBOARD CONTROLS:\n";
    std::cerr << "    Space         Play/Pause animation\n";
    std::cerr << "    R             Restart animation from beginning\n";
    std::cerr << "    F/G           Toggle fullscreen mode\n";
    std::cerr << "    M             Toggle maximize/restore (zoom)\n";
    std::cerr << "    T             Toggle frame limiter\n";
    std::cerr << "    Left/Right    Seek backward/forward 1 second\n";
    std::cerr << "    Up/Down       Speed up/slow down playback\n";
    std::cerr << "    L             Toggle loop mode\n";
    std::cerr << "    P             Toggle parallel rendering mode\n";
    std::cerr << "    S             Show/hide statistics overlay\n";
    std::cerr << "    Q, Escape     Quit player\n\n";
    std::cerr << "SUPPORTED FORMATS:\n";
    std::cerr << "    - SVG 1.1 with SMIL animations\n";
    std::cerr << "    - Discrete frame animations via xlink:href\n";
    std::cerr << "    - FBF (Frame-by-Frame) SVG format\n";
    std::cerr << "    - Folder of numbered SVG files (image sequence)\n\n";
    std::cerr << "EXAMPLES:\n";
    std::cerr << "    " << programName << " animation.svg              # Starts in fullscreen (default)\n";
    std::cerr << "    " << programName << " animation.svg --windowed   # Starts in a window\n";
    std::cerr << "    " << programName << " ./frames/                  # Play SVG image sequence from folder\n";
    std::cerr << "    " << programName << " animation.svg --sequential # Benchmark: ignore SMIL timing\n";
    std::cerr << "    " << programName << " --version\n\n";
    std::cerr << "TIPS:\n";
    std::cerr << "    Assign player to a specific Desktop (macOS):\n";
    std::cerr << "      1. Start the player with any SVG file\n";
    std::cerr << "      2. Right-click the player icon in the Dock\n";
    std::cerr << "      3. Select Options > Assign To > Desktop 2 (or desired desktop)\n";
    std::cerr << "      4. The player will now always open on that desktop\n";
    std::cerr << "    This is useful for running tests on a separate desktop.\n\n";
    std::cerr << "BUILD INFO:\n";
    std::cerr << "    " << SVG_PLAYER_BUILD_INFO << "\n";
}

// Use shared types from the animation controller
using svgplayer::AnimationState;
using svgplayer::SMILAnimation;

// CRITICAL: Use steady_clock for animation timing
// - steady_clock is MONOTONIC (never goes backwards, immune to system clock changes)
// - This guarantees perfect synchronization regardless of system load
// - If rendering is slow, frames are SKIPPED but timing stays correct
// - This is the key principle of SMIL: time-based, not frame-based
using SteadyClock = std::chrono::steady_clock;
using Clock = std::chrono::high_resolution_clock;  // For performance measurement only
using DurationMs = std::chrono::duration<double, std::milli>;
using DurationSec = std::chrono::duration<double>;

// Global font manager and text shaping factory for SVG text rendering
// These must be set up before any SVG DOM is created to ensure text elements render properly
static sk_sp<SkFontMgr> g_fontMgr;
static sk_sp<SkShapers::Factory> g_shaperFactory;

// Initialize font support for SVG text rendering (call once at startup)
void initializeFontSupport() {
    // Platform-specific font manager (CoreText on macOS/iOS, FontConfig on Linux)
    g_fontMgr = createPlatformFontMgr();
    // Use the best available text shaper
    g_shaperFactory = SkShapers::BestAvailable();
}

// Create SVG DOM with proper font support for text rendering
// This must be used instead of SkSVGDOM::MakeFromStream to enable SVG <text> elements
sk_sp<SkSVGDOM> makeSVGDOMWithFontSupport(SkStream& stream) {
    return SkSVGDOM::Builder().setFontManager(g_fontMgr).setTextShapingFactory(g_shaperFactory).make(stream);
}

// SMILAnimation struct is now provided by shared/SVGAnimationController.h
// Using declaration at top: using svgplayer::SMILAnimation;

// Parallel rendering modes
// NOTE: Tile-based modes (TileParallel, PreBufferTiled) have been removed because:
// 1. They cause deadlock due to nested parallelism on shared executor
// 2. Each tile requires parsing entire SVG DOM = extreme overhead for animated SVGs
// 3. Tile DOMs don't receive animation state updates, causing wrong frames
// For animated SVGs, PreBuffer mode provides the best performance.
enum class ParallelMode {
    Off,       // No parallelism, direct single-threaded rendering
    PreBuffer  // Pre-render frames ahead into buffer (best for animations)
};

// Get mode name for display
const char* getParallelModeName(ParallelMode mode) {
    switch (mode) {
        case ParallelMode::Off:
            return "Off";
        case ParallelMode::PreBuffer:
            return "PreBuffer";
    }
    return "Unknown";
}

// Skia-based Parallel Renderer using SkTaskGroup
// Supports two modes: Off and PreBuffer (pre-render animation frames ahead)
class SkiaParallelRenderer {
   public:
    ParallelMode mode{ParallelMode::Off};
    std::atomic<bool> modeChanging{false};  // Prevents race condition during mode transitions
    std::atomic<int> activeWorkers{0};
    int totalCores;
    int reservedForSystem;

    // Thread pool executor (Skia's thread pool)
    std::unique_ptr<SkExecutor> executor;

    // === Mode A: Pre-buffer data ===
    struct RenderedFrame {
        size_t frameIndex;
        double elapsedTimeSeconds;  // Time-based sync for multi-animation support
        std::vector<uint32_t> pixels;
        int width;
        int height;
        std::atomic<bool> ready{false};
    };
    std::mutex bufferMutex;
    std::map<size_t, std::shared_ptr<RenderedFrame>> frameBuffer;
    static constexpr size_t MAX_BUFFER_SIZE = 30;    // 30 frames @ 1920x1080 RGBA = ~240MB peak memory
    static constexpr size_t LOOKAHEAD_FRAMES = 10;   // Pre-render up to 10 frames ahead (~80MB @ 1080p)

    // Shared rendering resources
    std::string svgData;
    int renderWidth{0};
    int renderHeight{0};
    int svgWidth{0};
    int svgHeight{0};

    // Animation info for pre-buffered frames (supports multiple simultaneous animations)
    std::vector<SMILAnimation> animations;
    double totalDuration{1.0};  // Total animation cycle duration for time-based sync
    size_t totalFrameCount{1};  // Total frames for frame-to-time conversion

    // Per-worker cached DOM and surface (parse SVG once per thread, not per frame!)
    struct WorkerCache {
        sk_sp<SkSVGDOM> dom;
        sk_sp<SkSurface> surface;
        int surfaceWidth{0};
        int surfaceHeight{0};
    };
    std::mutex workerCacheMutex;
    std::map<std::thread::id, WorkerCache> workerCaches;

    // Include thread header for std::this_thread::get_id()
    SkiaParallelRenderer() : reservedForSystem(1) {
        totalCores = static_cast<int>(std::thread::hardware_concurrency());
        if (totalCores <= 0) totalCores = 4;
    }

    int getWorkerCount() const {
        int workers = totalCores - reservedForSystem;
        return (workers > 0) ? workers : 1;
    }

    bool isEnabled() const { return mode != ParallelMode::Off; }

    // Cycle to next mode: Off -> PreBuffer -> Off
    ParallelMode cycleMode() {
        // Set flag to block any concurrent access during mode transition
        modeChanging = true;

        // Save current mode BEFORE stop() clears it
        ParallelMode currentMode = mode;

        stop();  // Clean up current mode (this sets mode = Off)

        // Toggle between Off and PreBuffer
        if (currentMode == ParallelMode::Off) {
            mode = ParallelMode::PreBuffer;
            startExecutor();
        } else {
            mode = ParallelMode::Off;
        }

        // Allow concurrent access again now that mode change is complete
        modeChanging = false;

        return mode;
    }

    void configure(const std::string& svgContent, int width, int height, int svgW, int svgH,
                   const std::vector<SMILAnimation>& anims = {},
                   double animDuration = 1.0, size_t animFrames = 1) {
        svgData = svgContent;
        renderWidth = width;
        renderHeight = height;
        svgWidth = svgW;
        svgHeight = svgH;
        animations = anims;
        // Store duration and frame count for time-based frame calculation
        totalDuration = animDuration > 0 ? animDuration : 1.0;
        totalFrameCount = animFrames > 0 ? animFrames : 1;
    }

    // Update render dimensions on window resize - clears cached frames since they're wrong size
    void resize(int width, int height) {
        if (width == renderWidth && height == renderHeight) return;

        renderWidth = width;
        renderHeight = height;

        // Clear all pre-buffered frames since they're now the wrong size
        std::lock_guard<std::mutex> lock(bufferMutex);
        frameBuffer.clear();
    }

    void start(const std::string& svgContent, int width, int height, int svgW, int svgH,
               ParallelMode initialMode = ParallelMode::PreBuffer) {
        if (mode != ParallelMode::Off) return;

        svgData = svgContent;
        renderWidth = width;
        renderHeight = height;
        svgWidth = svgW;
        svgHeight = svgH;
        mode = initialMode;

        if (mode != ParallelMode::Off) {
            startExecutor();
        }
    }

    void stop() {
        if (mode == ParallelMode::Off && !executor) return;

        // Clear pre-buffer
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            frameBuffer.clear();
        }

        // Clear executor (CRITICAL: reset() blocks until ALL worker threads have joined)
        // SkExecutor destructor ensures all threads are stopped before returning.
        // Only after this completes is it safe to clear workerCaches.
        if (executor) {
            executor.reset();
            SkExecutor::SetDefault(nullptr);
        }

        // Clear worker caches (safe now that executor threads have joined)
        {
            std::lock_guard<std::mutex> lock(workerCacheMutex);
            workerCaches.clear();
        }

        activeWorkers = 0;
        mode = ParallelMode::Off;
    }

    // === Pre-buffer API ===
    // Pre-render animation frames ahead for smooth playback

    // Request frames ahead of current position
    void requestFramesAhead(size_t currentFrame, size_t totalFrames) {
        // Skip if mode change is in progress to avoid race condition
        if (modeChanging.load()) return;
        if (mode != ParallelMode::PreBuffer || !executor) return;

        // FIX: Clear old frames BEFORE requesting new ones
        // Otherwise if buffer is at MAX_BUFFER_SIZE, new frame requests are dropped
        // silently, causing the animation to freeze when buffered frames run out
        clearOldFrames(currentFrame);

        // Request next LOOKAHEAD_FRAMES frames
        for (size_t i = 1; i <= LOOKAHEAD_FRAMES; i++) {
            size_t frameIdx = (currentFrame + i) % totalFrames;
            requestFrame(frameIdx);
        }
    }

    void requestFrame(size_t frameIndex) {
        // Skip if mode change is in progress to avoid race condition
        if (modeChanging.load()) return;
        if (mode != ParallelMode::PreBuffer || !executor) return;

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (frameBuffer.count(frameIndex) > 0) return;
        }

        auto framePtr = std::make_shared<RenderedFrame>();
        framePtr->frameIndex = frameIndex;
        // Calculate elapsed time for this frame: time = (frameIndex / totalFrames) * duration
        // This ensures each animation can calculate its own correct frame based on time
        framePtr->elapsedTimeSeconds = (static_cast<double>(frameIndex) / totalFrameCount) * totalDuration;
        framePtr->width = renderWidth;
        framePtr->height = renderHeight;

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (frameBuffer.size() >= MAX_BUFFER_SIZE) {
                // Buffer is full - this can happen during rapid seeking or loop wraparound
                // The direct render fallback will handle this case
                return;
            }
            frameBuffer[frameIndex] = framePtr;
        }

        // Schedule frame rendering on thread pool
        executor->add([this, framePtr]() { renderSingleFrame(framePtr); });
    }

    bool getFrame(size_t frameIndex, std::vector<uint32_t>& outPixels) {
        if (mode != ParallelMode::PreBuffer) return false;

        std::lock_guard<std::mutex> lock(bufferMutex);
        auto it = frameBuffer.find(frameIndex);
        if (it != frameBuffer.end() && it->second->ready) {
            outPixels = it->second->pixels;
            return true;
        }
        return false;
    }

    size_t getBufferedFrameCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(bufferMutex));
        size_t count = 0;
        for (const auto& pair : frameBuffer) {
            if (pair.second->ready) count++;
        }
        return count;
    }

    void clearOldFrames(size_t currentFrame) {
        std::lock_guard<std::mutex> lock(bufferMutex);
        std::vector<size_t> toRemove;
        for (const auto& pair : frameBuffer) {
            // Remove frames more than LOOKAHEAD_FRAMES behind
            if (currentFrame > pair.first && currentFrame - pair.first > LOOKAHEAD_FRAMES) {
                toRemove.push_back(pair.first);
            }
        }
        for (size_t idx : toRemove) {
            frameBuffer.erase(idx);
        }
    }

   private:
    void startExecutor() {
        int numWorkers = getWorkerCount();
        executor = SkExecutor::MakeFIFOThreadPool(numWorkers, true);
        SkExecutor::SetDefault(executor.get());
        activeWorkers = numWorkers;
    }

    // Render a single pre-buffered frame (called from worker thread)
    // Uses per-thread cached DOM to avoid re-parsing SVG for each frame
    void renderSingleFrame(std::shared_ptr<RenderedFrame> frame) {
        // Abort early if mode change is in progress (cache may be cleared soon)
        // This prevents using potentially-invalidated cache pointers during shutdown
        if (modeChanging.load()) return;

        std::thread::id threadId = std::this_thread::get_id();

        // CRITICAL: Hold lock for entire cache access to prevent use-after-free
        // If we release lock early, another thread could call workerCaches.clear()
        // invalidating our cache pointer while we're still using it.
        std::lock_guard<std::mutex> lock(workerCacheMutex);

        // Double-check under lock - modeChanging means imminent clear()
        if (modeChanging.load()) return;

        WorkerCache* cache = &workerCaches[threadId];

        // Parse SVG once per worker thread (first call only)
        // Use makeSVGDOMWithFontSupport to ensure SVG text elements render properly
        if (!cache->dom) {
            auto stream = SkMemoryStream::MakeDirect(svgData.data(), svgData.size());
            cache->dom = makeSVGDOMWithFontSupport(*stream);
            if (!cache->dom) return;
        }

        // Recreate surface if size changed
        if (!cache->surface || cache->surfaceWidth != renderWidth || cache->surfaceHeight != renderHeight) {
            cache->surface = SkSurfaces::Raster(
                SkImageInfo::Make(renderWidth, renderHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
            cache->surfaceWidth = renderWidth;
            cache->surfaceHeight = renderHeight;
            if (!cache->surface) return;
        }

        // Apply ALL animation states for this specific time point
        // Each animation calculates its own frame based on elapsed time, not frame index
        // This correctly handles animations with different durations and frame counts
        for (const auto& anim : animations) {
            if (!anim.targetId.empty() && !anim.attributeName.empty() && !anim.values.empty()) {
                // Use time-based calculation: each animation determines its frame from elapsed time
                std::string value = anim.getCurrentValue(frame->elapsedTimeSeconds);
                sk_sp<SkSVGNode>* nodePtr = cache->dom->findNodeById(anim.targetId.c_str());
                if (nodePtr && *nodePtr) {
                    (*nodePtr)->setAttribute(anim.attributeName.c_str(), value.c_str());
                }
            }
        }

        SkCanvas* canvas = cache->surface->getCanvas();
        canvas->clear(SK_ColorBLACK);

        // Calculate scale to fit SVG in render area while preserving aspect ratio
        // This matches the GPU rendering paths (Graphite and Ganesh Metal)
        int effectiveSvgW = (svgWidth > 0) ? svgWidth : renderWidth;
        int effectiveSvgH = (svgHeight > 0) ? svgHeight : renderHeight;
        float scale = std::min(static_cast<float>(renderWidth) / effectiveSvgW,
                              static_cast<float>(renderHeight) / effectiveSvgH);
        float offsetX = (renderWidth - effectiveSvgW * scale) / 2.0f;
        float offsetY = (renderHeight - effectiveSvgH * scale) / 2.0f;

        // Apply transform to preserve aspect ratio and center content
        canvas->save();
        canvas->translate(offsetX, offsetY);
        canvas->scale(scale, scale);
        cache->dom->setContainerSize(SkSize::Make(effectiveSvgW, effectiveSvgH));
        cache->dom->render(canvas);
        canvas->restore();

        SkPixmap pixmap;
        if (cache->surface->peekPixels(&pixmap)) {
            frame->pixels.resize(renderWidth * renderHeight);
            memcpy(frame->pixels.data(), pixmap.addr(), renderWidth * renderHeight * sizeof(uint32_t));
            frame->ready = true;
        }
    }
};

// =============================================================================
// THREADED RENDERER - Keeps UI responsive by rendering in background thread
// =============================================================================
// This class ensures the main event loop NEVER blocks on rendering.
// - Render thread does all heavy SVG work in background
// - Main thread only blits completed frames and handles input
// - Watchdog timeout prevents infinite freezes
// - Mode changes are instant (non-blocking)
class ThreadedRenderer {
   public:
    // Render state flags
    std::atomic<bool> running{true};
    std::atomic<bool> frameReady{false};
    std::atomic<bool> renderInProgress{false};
    std::atomic<bool> modeChangeRequested{false};

    // Render timeout watchdog (500ms max render time)
    static constexpr int RENDER_TIMEOUT_MS = 500;
    std::atomic<bool> renderTimedOut{false};

    // Double buffer for thread-safe frame handoff
    std::mutex bufferMutex;
    std::vector<uint32_t> frontBuffer;  // Main thread reads this
    std::vector<uint32_t> backBuffer;   // Render thread writes this

    // Render parameters (thread-safe)
    std::mutex paramsMutex;
    int renderWidth{0};
    int renderHeight{0};
    int svgWidth{0};
    int svgHeight{0};
    std::string svgData;
    size_t currentFrameIndex{0};

    // Animation states (for applying to render thread's DOM)
    // Supports multiple simultaneous animations - each has targetId, attributeName, currentValue
    struct AnimState {
        std::string targetId;
        std::string attributeName;
        std::string currentValue;
    };
    std::vector<AnimState> animationStates;

    // Statistics
    std::atomic<double> lastRenderTimeMs{0};
    std::atomic<int> droppedFrames{0};
    std::atomic<int> timeoutCount{0};

    // Cached values for non-blocking access from main thread
    std::atomic<bool> cachedPreBufferMode{false};
    std::atomic<int> cachedActiveWorkers{0};

    // Total animation frames (for pre-buffering)
    std::atomic<size_t> totalAnimationFrames{1};

    // The render thread
    std::thread renderThread;
    std::condition_variable renderCV;
    std::mutex renderCVMutex;
    std::atomic<bool> newFrameRequested{false};

    // Reference to parallel renderer for PreBuffer mode
    SkiaParallelRenderer* parallelRenderer{nullptr};

    // Dirty region tracking for partial rendering optimization
    svgplayer::DirtyRegionTracker dirtyTracker_;
    std::atomic<bool> dirtyTrackingInitialized_{false};  // Atomic for thread-safe access

    // Stats for benchmarking partial vs full render
    std::atomic<uint64_t> partialRenderCount_{0};
    std::atomic<uint64_t> fullRenderCount_{0};
    std::atomic<double> partialRenderSavedRatio_{0.0};  // Accumulated (1 - dirty_ratio) for partial renders

    // Track SVG content hash to detect changes and force DOM recreation
    size_t lastSvgDataHash_{0};

    // Frame changes from last animation update (for dirty tracking)
    std::vector<svgplayer::AnimationFrameChange> lastFrameChanges_;

    ThreadedRenderer() = default;

    ~ThreadedRenderer() { stop(); }

    void configure(SkiaParallelRenderer* pr, const std::string& svg, int rw, int rh, int sw, int sh) {
        std::lock_guard<std::mutex> lock(paramsMutex);
        parallelRenderer = pr;
        svgData = svg;
        renderWidth = rw;
        renderHeight = rh;
        svgWidth = sw;
        svgHeight = sh;

        // Compute hash of SVG content to detect changes (for DOM recreation)
        lastSvgDataHash_ = std::hash<std::string>{}(svg);

        // Reset dirty tracking state for new SVG
        // This ensures old animation bounds don't persist across hot-reloads
        dirtyTracker_.reset();
        dirtyTrackingInitialized_ = false;

        // Reset partial render stats for new SVG
        partialRenderCount_ = 0;
        fullRenderCount_ = 0;
        partialRenderSavedRatio_ = 0.0;

        // Allocate buffers
        size_t bufferSize = rw * rh;
        frontBuffer.resize(bufferSize, 0xFFFFFFFF);  // White
        backBuffer.resize(bufferSize, 0xFFFFFFFF);
    }

    void start() {
        running = true;
        renderThread = std::thread(&ThreadedRenderer::renderLoop, this);
    }

    void stop() {
        running = false;
        newFrameRequested = true;  // Wake up thread
        renderCV.notify_all();
        if (renderThread.joinable()) {
            renderThread.join();
        }
    }

    // Initialize dirty tracking with animation bounds from SVG content
    void initializeDirtyTracking(const std::vector<svgplayer::SMILAnimation>& animations) {
        std::lock_guard<std::mutex> lock(paramsMutex);

        // Reset any existing state (safety measure for re-initialization)
        dirtyTracker_.reset();

        // Extract bounds for all animated elements from the SVG
        auto bounds = svgplayer::ElementBoundsExtractor::extractAnimationBounds(svgData, animations);

        // Log bounds extraction results for debugging (suppress in JSON benchmark mode)
        if (!g_jsonOutput) {
            std::cout << "Dirty tracking: extracted bounds for " << bounds.size()
                      << " of " << animations.size() << " animations" << std::endl;
        }

        // Set bounds in dirty tracker
        for (const auto& [id, rect] : bounds) {
            dirtyTracker_.setAnimationBounds(id, rect);
        }

        // Initialize tracker with animation count
        dirtyTracker_.initialize(animations.size());
        dirtyTrackingInitialized_ = true;
    }

    // Get partial render statistics for benchmarking
    void getPartialRenderStats(uint64_t& partialCount, uint64_t& fullCount, double& avgSavedRatio) const {
        partialCount = partialRenderCount_.load();
        fullCount = fullRenderCount_.load();
        if (partialCount > 0) {
            avgSavedRatio = partialRenderSavedRatio_.load() / static_cast<double>(partialCount);
        } else {
            avgSavedRatio = 0.0;
        }
    }

    // Update frame changes from animation controller (call before requesting render)
    void setFrameChanges(const std::vector<svgplayer::AnimationFrameChange>& changes) {
        std::lock_guard<std::mutex> lock(paramsMutex);
        lastFrameChanges_ = changes;
    }

    // Called from main thread - update animation states (non-blocking!)
    // Accepts all animation states at once to ensure they're applied together
    void setAnimationStates(const std::vector<AnimState>& states) {
        std::lock_guard<std::mutex> lock(paramsMutex);
        animationStates = states;
    }

    // Convenience method - add/update a single animation state
    // For backward compatibility and simpler single-animation cases
    void setAnimationState(const std::string& targetId, const std::string& attrName, const std::string& value) {
        std::lock_guard<std::mutex> lock(paramsMutex);
        // Find existing or add new
        for (auto& state : animationStates) {
            if (state.targetId == targetId && state.attributeName == attrName) {
                state.currentValue = value;
                return;
            }
        }
        animationStates.push_back({targetId, attrName, value});
    }

    // Called from main thread - request a new frame (non-blocking!)
    void requestFrame(size_t frameIndex) {
        {
            std::lock_guard<std::mutex> lock(paramsMutex);
            currentFrameIndex = frameIndex;
        }
        newFrameRequested = true;
        renderCV.notify_one();
    }

    // Called from main thread - check if frame is ready (non-blocking!)
    bool tryGetFrame(std::vector<uint32_t>& outPixels) {
        if (!frameReady) return false;

        std::lock_guard<std::mutex> lock(bufferMutex);
        outPixels = frontBuffer;  // Copy front buffer
        frameReady = false;
        return true;
    }

    // Called from main thread - get front buffer pointer for direct blit
    // Uses atomic exchange to avoid race condition where we might count the same frame twice
    const uint32_t* getFrontBufferIfReady() {
        // Atomically check AND clear frameReady - returns previous value
        bool wasReady = frameReady.exchange(false);
        if (!wasReady) return nullptr;
        return frontBuffer.data();
    }

    // Called from main thread - get current frame for screenshot (non-blocking, returns copy)
    // This does NOT affect frameReady flag - screenshot is independent of render state
    bool getFrameForScreenshot(std::vector<uint32_t>& outPixels, int& outWidth, int& outHeight) {
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (frontBuffer.empty()) return false;
        outPixels = frontBuffer;  // Copy current front buffer
        {
            std::lock_guard<std::mutex> plock(paramsMutex);
            outWidth = renderWidth;
            outHeight = renderHeight;
        }
        return true;
    }

    // Called from main thread - handle mode change request (non-blocking!)
    void requestModeChange() {
        modeChangeRequested = true;
        renderCV.notify_one();
    }

    // Called from main thread - check current mode (non-blocking, uses atomic cache)
    bool isPreBufferMode() const { return cachedPreBufferMode.load(); }

    // Called from main thread - get cached active workers count (non-blocking)
    int getCachedActiveWorkers() const { return cachedActiveWorkers.load(); }

    // Called from main thread - set total animation frames (for pre-buffering)
    void setTotalAnimationFrames(size_t total) { totalAnimationFrames = total; }

    // Resize buffers (call from main thread when window resizes)
    void resize(int newWidth, int newHeight) {
        {
            std::lock_guard<std::mutex> lock(paramsMutex);
            renderWidth = newWidth;
            renderHeight = newHeight;
        }
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            size_t bufferSize = newWidth * newHeight;
            frontBuffer.resize(bufferSize, 0xFFFFFFFF);
            backBuffer.resize(bufferSize, 0xFFFFFFFF);
        }
    }

   private:
    void renderLoop() {
        // Create thread-local Skia DOM for rendering
        sk_sp<SkSVGDOM> threadDom;
        sk_sp<SkSurface> threadSurface;

        // Debug logging for render thread (enabled by RENDER_DEBUG env var)
        static bool debugRenderLoop = (std::getenv("RENDER_DEBUG") != nullptr);
        static uint64_t loopIterations = 0;
        static uint64_t renderAttempts = 0;

        while (running) {
            loopIterations++;
            // Wait for render request with timeout
            {
                std::unique_lock<std::mutex> lock(renderCVMutex);
                renderCV.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return newFrameRequested.load() || modeChangeRequested.load() || !running.load();
                });
            }

            if (!running) break;

            // Handle mode change request (instant, non-blocking for main thread)
            if (modeChangeRequested) {
                modeChangeRequested = false;
                if (parallelRenderer) {
                    parallelRenderer->cycleMode();
                    // Update cached values for main thread to read without blocking
                    cachedPreBufferMode = (parallelRenderer->mode == ParallelMode::PreBuffer);
                    cachedActiveWorkers = parallelRenderer->activeWorkers.load();
                    if (!g_jsonOutput) {
                        std::cout << "Parallel mode: " << getParallelModeName(parallelRenderer->mode);
                        if (parallelRenderer->mode != ParallelMode::Off) {
                            std::cout << " (" << cachedActiveWorkers.load() << " threads)";
                        }
                        std::cout << std::endl;
                    }
                }
                continue;
            }

            if (!newFrameRequested) continue;
            newFrameRequested = false;
            renderAttempts++;

            if (debugRenderLoop && renderAttempts <= 5) {
                std::cerr << "[RENDER_DEBUG] Attempt #" << renderAttempts
                          << " (loop iter=" << loopIterations << ")" << std::endl;
            }

            // Get render parameters and animation states
            std::string localSvgData;  // Will be populated from shared svgData
            int localWidth, localHeight, localSvgW, localSvgH;
            size_t localFrameIndex;
            std::vector<AnimState> localAnimStates;
            std::vector<svgplayer::AnimationFrameChange> localFrameChanges;
            {
                std::lock_guard<std::mutex> lock(paramsMutex);
                localSvgData = svgData;
                localWidth = renderWidth;
                localHeight = renderHeight;
                localSvgW = svgWidth;
                localSvgH = svgHeight;
                localFrameIndex = currentFrameIndex;
                localAnimStates = animationStates;
                localFrameChanges = lastFrameChanges_;
            }

            // Integer overflow protection: validate dimensions before buffer calculations
            // Maximum dimension matches saveScreenshotPPM limit (32768x32768 = 1 gigapixel)
            constexpr int MAX_RENDER_DIM = 32768;
            if (localWidth <= 0 || localHeight <= 0 ||
                localWidth > MAX_RENDER_DIM || localHeight > MAX_RENDER_DIM) {
                if (debugRenderLoop && renderAttempts <= 5) {
                    std::cerr << "[RENDER_DEBUG] Skip: invalid dims " << localWidth << "x" << localHeight << std::endl;
                }
                continue;
            }

            // Also check for empty SVG data
            if (localSvgData.empty()) {
                if (debugRenderLoop && renderAttempts <= 5) {
                    std::cerr << "[RENDER_DEBUG] Skip: empty SVG data" << std::endl;
                }
                continue;
            }

            renderInProgress = true;
            renderTimedOut = false;
            auto renderStart = Clock::now();

            // === RENDER WITH TIMEOUT WATCHDOG ===
            bool renderSuccess = false;
            bool usedPartialRender = false;  // Track which render path was used for stats

            // Try to use pre-buffered frame first (instant, no rendering needed)
            if (parallelRenderer && parallelRenderer->mode == ParallelMode::PreBuffer) {
                std::vector<uint32_t> preBuffered;
                if (parallelRenderer->getFrame(localFrameIndex, preBuffered)) {
                    // Got pre-buffered frame - use it directly
                    std::lock_guard<std::mutex> lock(bufferMutex);
                    backBuffer = std::move(preBuffered);
                    renderSuccess = true;
                }
            }

            // If no pre-buffered frame, render directly
            if (!renderSuccess) {
                // Recreate surface if needed
                if (!threadSurface || threadSurface->width() != localWidth || threadSurface->height() != localHeight) {
                    threadSurface = SkSurfaces::Raster(
                        SkImageInfo::Make(localWidth, localHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
                }

                // Track SVG data hash for hot-reload detection
                // Force DOM recreation when SVG content changes
                static size_t lastLocalSvgHash = 0;
                size_t currentSvgHash = std::hash<std::string>{}(localSvgData);

                // Recreate DOM if needed (first time or SVG content changed)
                // Use makeSVGDOMWithFontSupport to ensure SVG text elements render properly
                // NOTE: DOM creation can take seconds for large SVGs - this is one-time cost per SVG
                if (!threadDom || currentSvgHash != lastLocalSvgHash) {
                    if (debugRenderLoop) {
                        std::cerr << "[RENDER_DEBUG] Creating DOM (first time or hash changed)..." << std::endl;
                    }
                    auto domStart = Clock::now();
                    auto stream = SkMemoryStream::MakeDirect(localSvgData.data(), localSvgData.size());
                    threadDom = makeSVGDOMWithFontSupport(*stream);
                    lastLocalSvgHash = currentSvgHash;
                    auto domEnd = Clock::now();
                    auto domMs = std::chrono::duration<double, std::milli>(domEnd - domStart).count();
                    if (debugRenderLoop) {
                        std::cerr << "[RENDER_DEBUG] DOM created in " << domMs << "ms" << std::endl;
                    }
                    // CRITICAL FIX: Reset renderStart AFTER DOM creation
                    // DOM parsing is one-time cost and should NOT count against render timeout
                    // Only the actual SVG rendering should be subject to the timeout
                    renderStart = Clock::now();
                    if (debugRenderLoop && domMs > 100) {
                        std::cerr << "[RENDER_DEBUG] Render timer reset after DOM creation (DOM took "
                                  << domMs << "ms)" << std::endl;
                    }
                }

                if (threadSurface && threadDom) {
                    // Apply ALL animation states to render thread's DOM (sync with main thread)
                    // This ensures multiple simultaneous animations are rendered correctly
                    for (const auto& animState : localAnimStates) {
                        if (!animState.targetId.empty() && !animState.attributeName.empty()) {
                            sk_sp<SkSVGNode>* nodePtr = threadDom->findNodeById(animState.targetId.c_str());
                            if (nodePtr && *nodePtr) {
                                (*nodePtr)->setAttribute(animState.attributeName.c_str(),
                                                         animState.currentValue.c_str());
                            }
                        }
                    }

                    // Update dirty tracker with frame changes for partial rendering
                    if (dirtyTrackingInitialized_) {
                        for (const auto& change : localFrameChanges) {
                            dirtyTracker_.markDirty(change.targetId, change.currentFrame);
                        }
                    }

                    SkCanvas* canvas = threadSurface->getCanvas();

                    // Calculate uniform scale to fit SVG in render area while preserving aspect ratio
                    // This matches the GPU rendering paths (Graphite and Ganesh Metal)
                    int effectiveSvgW = (localSvgW > 0) ? localSvgW : localWidth;
                    int effectiveSvgH = (localSvgH > 0) ? localSvgH : localHeight;
                    float uniformScale = std::min(static_cast<float>(localWidth) / effectiveSvgW,
                                                  static_cast<float>(localHeight) / effectiveSvgH);
                    float offsetX = (localWidth - effectiveSvgW * uniformScale) / 2.0f;
                    float offsetY = (localHeight - effectiveSvgH * uniformScale) / 2.0f;

                    // Decide partial vs full render based on dirty region analysis
                    // Note: dirty rects are in SVG coordinate space, need scaling to render space
                    bool usePartialRender = dirtyTrackingInitialized_ &&
                                            !dirtyTracker_.shouldUseFullRender(
                                                static_cast<float>(effectiveSvgW),
                                                static_cast<float>(effectiveSvgH)) &&
                                            dirtyTracker_.getDirtyCount() > 0;
                    usedPartialRender = usePartialRender;  // Copy to outer scope for stats

                    if (usePartialRender) {
                        // PARTIAL RENDER PATH - only clear and render dirty region
                        auto unionRect = dirtyTracker_.getUnionDirtyRect();

                        // Scale dirty rect from SVG coordinates to render coordinates
                        // Apply uniform scale and offset for aspect-ratio preserving transform
                        // Add 1 pixel margin for rounding/anti-aliasing safety
                        SkRect clipRect = SkRect::MakeXYWH(
                            offsetX + unionRect.x * uniformScale - 1.0f,
                            offsetY + unionRect.y * uniformScale - 1.0f,
                            unionRect.width * uniformScale + 2.0f,
                            unionRect.height * uniformScale + 2.0f);

                        // Clamp to canvas bounds
                        clipRect.intersect(SkRect::MakeWH(localWidth, localHeight));

                        canvas->save();
                        canvas->clipRect(clipRect);
                        canvas->clear(SK_ColorBLACK);
                        // partialRenderCount_ incremented in success path below
                    } else {
                        // FULL RENDER PATH - clear entire canvas
                        canvas->clear(SK_ColorBLACK);
                        // fullRenderCount_ incremented in success path below
                    }

                    // Check timeout before expensive render
                    auto elapsed =
                        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - renderStart).count();

                    if (debugRenderLoop) {
                        std::cerr << "[RENDER_DEBUG] Pre-render elapsed: " << elapsed << "ms (timeout="
                                  << RENDER_TIMEOUT_MS << "ms)" << std::endl;
                    }

                    if (elapsed < RENDER_TIMEOUT_MS) {
                        // Render the SVG with aspect-ratio preserving transform
                        auto svgRenderStart = Clock::now();
                        canvas->save();
                        canvas->translate(offsetX, offsetY);
                        canvas->scale(uniformScale, uniformScale);
                        threadDom->setContainerSize(SkSize::Make(effectiveSvgW, effectiveSvgH));
                        threadDom->render(canvas);
                        canvas->restore();
                        auto svgRenderEnd = Clock::now();
                        renderSuccess = true;
                        if (debugRenderLoop) {
                            auto svgMs = std::chrono::duration<double, std::milli>(svgRenderEnd - svgRenderStart).count();
                            std::cerr << "[RENDER_DEBUG] SVG render completed in " << svgMs << "ms" << std::endl;
                        }
                    } else {
                        renderTimedOut = true;
                        timeoutCount++;
                        if (debugRenderLoop) {
                            std::cerr << "[RENDER_DEBUG] TIMEOUT! elapsed=" << elapsed << "ms >= "
                                      << RENDER_TIMEOUT_MS << "ms" << std::endl;
                        }
                    }

                    // Restore canvas state if we used partial render
                    if (usePartialRender) {
                        canvas->restore();
                    }

                    // Clear dirty flags for next frame
                    if (dirtyTrackingInitialized_) {
                        dirtyTracker_.clearDirtyFlags();
                    }

                    // Copy to back buffer with integer overflow protection
                    if (renderSuccess) {
                        SkPixmap pixmap;
                        if (threadSurface->peekPixels(&pixmap)) {
                            // Use size_t for safe buffer size calculations
                            size_t pixelCount = static_cast<size_t>(localWidth) * static_cast<size_t>(localHeight);
                            size_t byteCount = pixelCount * sizeof(uint32_t);

                            // Validate pixmap has enough data before memcpy
                            if (pixmap.computeByteSize() >= byteCount) {
                                std::lock_guard<std::mutex> lock(bufferMutex);
                                backBuffer.resize(pixelCount);
                                memcpy(backBuffer.data(), pixmap.addr(), byteCount);
                            }
                        }
                    }
                }
            }

            auto renderEnd = Clock::now();
            lastRenderTimeMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();

            // Update cached active workers for main thread display
            if (parallelRenderer) {
                cachedActiveWorkers = parallelRenderer->activeWorkers.load();
            }

            // Check for timeout AFTER render
            if (lastRenderTimeMs > RENDER_TIMEOUT_MS) {
                renderTimedOut = true;
                timeoutCount++;
                droppedFrames++;
                if (debugRenderLoop) {
                    std::cerr << "[RENDER_DEBUG] POST-render timeout! total=" << lastRenderTimeMs
                              << "ms > " << RENDER_TIMEOUT_MS << "ms" << std::endl;
                }
            }

            // Swap buffers if render succeeded
            if (renderSuccess && !renderTimedOut) {
                // Increment render counter NOW (only for frames actually delivered)
                if (usedPartialRender) {
                    partialRenderCount_++;
                } else {
                    fullRenderCount_++;
                }

                std::lock_guard<std::mutex> lock(bufferMutex);
                std::swap(frontBuffer, backBuffer);
                frameReady = true;
                if (debugRenderLoop && renderAttempts <= 5) {
                    std::cerr << "[RENDER_DEBUG] Frame delivered! (attempt #" << renderAttempts
                              << ") " << (usedPartialRender ? "PARTIAL" : "FULL") << std::endl;
                }
            } else if (debugRenderLoop && renderAttempts <= 5) {
                std::cerr << "[RENDER_DEBUG] Frame NOT delivered: renderSuccess=" << renderSuccess
                          << ", renderTimedOut=" << renderTimedOut << std::endl;
            }

            // Request pre-buffered frames for upcoming animation (render thread can safely do this)
            // This enables the PreBuffer mode to actually pre-render frames ahead of time
            // Skip if mode change is in progress to avoid race condition with main thread
            if (parallelRenderer && !parallelRenderer->modeChanging.load() &&
                parallelRenderer->mode == ParallelMode::PreBuffer) {
                size_t totalFrames = totalAnimationFrames.load();
                if (totalFrames > 1) {
                    parallelRenderer->requestFramesAhead(localFrameIndex, totalFrames);
                }
            }

            renderInProgress = false;
        }

        // Log summary when thread exits
        if (debugRenderLoop) {
            std::cerr << "[RENDER_DEBUG] Thread exit: " << loopIterations << " loop iterations, "
                      << renderAttempts << " render attempts" << std::endl;
        }
    }
};

// ============================================================================
// Animation Parsing Functions - Now using shared SVGAnimationController
// These wrapper functions maintain backward compatibility with existing code
// while delegating to the shared implementation.
// ============================================================================

// Global SVGAnimationController instance for parsing
// The controller handles all SMIL parsing and preprocessing
static svgplayer::SVGAnimationController g_animController;

// Pre-process SVG to inject IDs into <use> elements that contain <animate> but lack IDs
// Returns the modified SVG content and populates the provided map with synthetic IDs
// NOTE: This wrapper uses the shared controller's preprocessSVG method
std::string preprocessSVGForAnimation(const std::string& content, std::map<size_t, std::string>& syntheticIds) {
    // DEBUG: Check signal before loadFromContent
    static bool debugSignals = (std::getenv("RENDER_DEBUG") != nullptr);
    if (debugSignals) {
        std::cerr << "[PREPROCESS_DEBUG] Before loadFromContent: g_shutdownRequested="
                  << g_shutdownRequested.load() << ", content size=" << content.size() << std::endl;
    }

    // Use the shared controller to load and preprocess the content
    // The controller handles <symbol> to <g> conversion and synthetic ID injection
    g_animController.loadFromContent(content);

    // DEBUG: Check signal after loadFromContent
    if (debugSignals) {
        std::cerr << "[PREPROCESS_DEBUG] After loadFromContent: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // Note: synthetic IDs are now managed internally by the controller
    // The map parameter is kept for API compatibility but may not be populated
    // in the same way as before (controller stores them internally)

    return g_animController.getProcessedContent();
}

// Extract SMIL animations from SVG content string (after preprocessing)
// NOTE: This wrapper uses the shared controller's parsed animations
std::vector<SMILAnimation> extractAnimationsFromContent(const std::string& content) {
    // Load content into controller (also preprocesses it)
    g_animController.loadFromContent(content);

    // Return the parsed animations from the controller
    return g_animController.getAnimations();
}

// Original interface - reads file and extracts animations
// NOTE: This wrapper uses the shared controller's loadFromFile method
std::vector<SMILAnimation> extractAnimations(const std::string& svgPath) {
    // Use the shared controller to load and parse the file
    if (!g_animController.loadFromFile(svgPath)) {
        std::cerr << "Cannot open file for animation parsing: " << svgPath << std::endl;
        return {};
    }

    return g_animController.getAnimations();
}

// Get the preprocessed SVG content from the controller
// Call this after extractAnimations() or extractAnimationsFromContent()
const std::string& getProcessedSVGContent() { return g_animController.getProcessedContent(); }

// Rolling average calculator
class RollingAverage {
   public:
    RollingAverage(size_t windowSize = 120) : maxSize_(windowSize) {}

    void add(double value) {
        values_.push_back(value);
        if (values_.size() > maxSize_) {
            values_.pop_front();
        }
    }

    double average() const {
        if (values_.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values_) sum += v;
        return sum / values_.size();
    }

    double min() const {
        if (values_.empty()) return 0.0;
        double m = values_[0];
        for (double v : values_)
            if (v < m) m = v;
        return m;
    }

    double max() const {
        if (values_.empty()) return 0.0;
        double m = values_[0];
        for (double v : values_)
            if (v > m) m = v;
        return m;
    }

    double last() const {
        if (values_.empty()) return 0.0;
        return values_.back();
    }

    size_t count() const { return values_.size(); }

    void reset() { values_.clear(); }

   private:
    std::deque<double> values_;
    size_t maxSize_;
};

// Save screenshot as PPM (Portable Pixmap) - uncompressed format
// PPM P6 format: binary RGB data, no compression, maximum compatibility
// Input: ARGB8888 pixel buffer (32-bit per pixel)
// Output: PPM file with 24-bit RGB (8 bits per channel)
bool saveScreenshotPPM(const std::vector<uint32_t>& pixels, int width, int height, const std::string& filename) {
    // Integer overflow protection: validate dimensions before calculating buffer size
    // Maximum reasonable screenshot size: 32768x32768 (1 gigapixel)
    constexpr int MAX_SCREENSHOT_DIM = 32768;
    if (width <= 0 || height <= 0 || width > MAX_SCREENSHOT_DIM || height > MAX_SCREENSHOT_DIM) {
        std::cerr << "Invalid screenshot dimensions: " << width << "x" << height << std::endl;
        return false;
    }

    // Use size_t to avoid integer overflow in buffer size calculation
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t rgbBufferSize = pixelCount * 3;

    // Sanity check: ensure input buffer has expected size
    if (pixels.size() < pixelCount) {
        std::cerr << "Pixel buffer too small: " << pixels.size() << " < " << pixelCount << std::endl;
        return false;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for screenshot: " << filename << std::endl;
        return false;
    }

    // PPM P6 header: magic number, width, height, max color value
    file << "P6\n" << width << " " << height << "\n255\n";

    // Convert BGRA to RGB24 and write raw bytes
    // ThreadedRenderer uses kBGRA_8888_SkColorType for consistent cross-platform behavior
    // BGRA in memory: [B, G, R, A] → uint32_t on little-endian: 0xAARRGGBB
    std::vector<uint8_t> rgb(rgbBufferSize);
    for (size_t i = 0; i < pixelCount; ++i) {
        uint32_t pixel = pixels[i];
        rgb[i * 3 + 0] = (pixel >> 16) & 0xFF;  // R (byte 2 in BGRA)
        rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;   // G (byte 1 in BGRA)
        rgb[i * 3 + 2] = pixel & 0xFF;          // B (byte 0 in BGRA)
    }

    file.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());

    // Verify write succeeded before closing
    if (!file.good()) {
        std::cerr << "Failed to write screenshot data to: " << filename << std::endl;
        file.close();
        return false;
    }

    file.close();
    return true;
}

// Generate timestamped screenshot filename with resolution
std::string generateScreenshotFilename(int width, int height) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    // Use localtime_r for thread safety (POSIX standard)
    struct tm timeinfo;
    localtime_r(&time, &timeinfo);

    std::ostringstream ss;
    ss << "screenshot_" << std::put_time(&timeinfo, "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
       << std::setw(3) << ms.count() << "_" << width << "x" << height << ".ppm";
    return ss.str();
}

// SVG loading error codes
enum class SVGLoadError {
    Success = 0,
    FileSize,
    FileOpen,
    Validation,
    Parse
};

// Load SVG file and update all state variables
// Returns error code indicating success or specific failure type
// Does NOT stop/restart renderers - caller must handle that
// Caller uses error code to decide error handling strategy
SVGLoadError loadSVGFile(const std::string& path,
                         const char*& inputPath,
                         std::string& rawSvgContent,
                         std::vector<SMILAnimation>& animations,
                         sk_sp<SkSVGDOM>& svgDom,
                         int& svgWidth,
                         int& svgHeight,
                         float& aspectRatio,
                         double& preBufferTotalDuration,
                         size_t& preBufferTotalFrames,
                         std::string& currentFilePath) {
    // Validate file exists and size
    size_t fileSize = getFileSize(path.c_str());
    if (fileSize == 0 || fileSize > MAX_SVG_FILE_SIZE) {
        std::cerr << "Error: Invalid file size for: " << path << std::endl;
        return SVGLoadError::FileSize;
    }

    // Read file content
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << path << std::endl;
        return SVGLoadError::FileOpen;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    if (!buffer || buffer.fail()) {
        std::cerr << "Error: Failed to read file content: " << path << std::endl;
        return SVGLoadError::FileOpen;
    }
    std::string originalContent = buffer.str();
    file.close();

    // Validate SVG content structure
    if (!validateSVGContent(originalContent)) {
        std::cerr << "Error: Invalid SVG file: " << path << std::endl;
        return SVGLoadError::Validation;
    }

    // Preprocess SVG and extract animations
    std::map<size_t, std::string> syntheticIds;
    std::string processedContent = preprocessSVGForAnimation(originalContent, syntheticIds);
    std::vector<SMILAnimation> newAnimations = extractAnimationsFromContent(processedContent);

    // Parse with Skia
    sk_sp<SkData> svgData = SkData::MakeWithCopy(processedContent.data(), processedContent.size());
    std::unique_ptr<SkMemoryStream> svgStream = SkMemoryStream::Make(svgData);
    sk_sp<SkSVGDOM> newSvgDom = makeSVGDOMWithFontSupport(*svgStream);

    if (!newSvgDom) {
        std::cerr << "Error: Failed to parse SVG with Skia: " << path << std::endl;
        return SVGLoadError::Parse;
    }

    // Get SVG dimensions
    SkSVGSVG* root = newSvgDom->getRoot();
    if (!root) {
        std::cerr << "Error: SVG has no root element: " << path << std::endl;
        return SVGLoadError::Parse;
    }

    int newSvgWidth = 800;
    int newSvgHeight = 600;

    const auto& viewBox = root->getViewBox();
    // Check if viewBox is populated (Windows uses std::optional, macOS/Linux use SkTLazy)
#if defined(PLATFORM_WINDOWS)
    const bool hasViewBox = viewBox.has_value();
#else
    const bool hasViewBox = viewBox.isValid();
#endif
    if (hasViewBox) {
        newSvgWidth = static_cast<int>(viewBox->width());
        newSvgHeight = static_cast<int>(viewBox->height());
    } else {
        SkSize defaultSize = SkSize::Make(800, 600);
        SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));
        newSvgWidth = (svgSize.width() > 0) ? static_cast<int>(svgSize.width()) : 800;
        newSvgHeight = (svgSize.height() > 0) ? static_cast<int>(svgSize.height()) : 600;
    }

    // Calculate animation timing
    double newMaxDuration = 1.0;
    size_t newMaxFrames = 1;
    for (const auto& anim : newAnimations) {
        if (anim.duration > newMaxDuration) newMaxDuration = anim.duration;
        if (anim.getFrameCount() > newMaxFrames) newMaxFrames = anim.getFrameCount();
    }

    // Success - update all state variables
    currentFilePath = path;
    inputPath = currentFilePath.c_str();
    rawSvgContent = processedContent;
    animations = std::move(newAnimations);
    svgDom = newSvgDom;
    svgWidth = newSvgWidth;
    svgHeight = newSvgHeight;
    aspectRatio = static_cast<float>(svgWidth) / svgHeight;
    preBufferTotalDuration = newMaxDuration;
    preBufferTotalFrames = newMaxFrames;

    return SVGLoadError::Success;
}

int main(int argc, char* argv[]) {
    // Install signal handlers for graceful shutdown (Ctrl+C, kill)
    installSignalHandlers();

    // DEBUG: Check g_shutdownRequested immediately after signal handler install
    bool debugSignals = (std::getenv("RENDER_DEBUG") != nullptr);
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] After installSignalHandlers: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // Check for --json flag early to suppress startup banner
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) {
            g_jsonOutput = true;
            break;
        }
    }

    // Print startup banner (suppress in JSON benchmark mode for clean output)
    if (!g_jsonOutput) {
        std::cerr << SVGPlayerVersion::getStartupBanner() << std::endl;
    }

    // Parse command-line arguments
    const char* inputPath = nullptr;
    bool startFullscreen = true;  // Default to fullscreen for best viewing experience
    bool startMaximized = false;  // Start maximized (zoom) instead of normal windowed
    int startPosX = SDL_WINDOWPOS_CENTERED;  // Window X position (-1 = centered)
    int startPosY = SDL_WINDOWPOS_CENTERED;  // Window Y position (-1 = centered)
    int startWidth = 0;   // 0 = use SVG dimensions
    int startHeight = 0;  // 0 = use SVG dimensions
    bool remoteControlEnabled = false;  // Enable remote control TCP server
    int remoteControlPort = 9999;  // Default remote control port
#ifdef __APPLE__
    bool useMetalBackend = false;  // Use Metal GPU backend (Ganesh)
    bool useGraphiteBackend = false;  // Use Graphite GPU backend (next-gen)
#endif
    int benchmarkDuration = 0;  // Benchmark mode: run for N seconds then exit (0 = disabled)
    std::string screenshotPath;  // Auto-save screenshot after first frame (for benchmarks)
    // Note: jsonOutput is now global g_jsonOutput for thread access
    bool sequentialMode = false;  // Sequential frame mode: render 0,1,2,3... ignoring SMIL timing
    bool isImageSequence = false;  // Playing from folder of individual SVG files
    std::vector<std::string> sequenceFiles;  // List of SVG files for image sequence mode
    std::vector<std::string> sequenceSvgContents;  // Pre-loaded SVG contents for image sequence mode

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            // Show full version info and exit
            std::cerr << SVGPlayerVersion::getVersionBanner() << std::endl;
            std::cerr << "Build: " << SVG_PLAYER_BUILD_INFO << std::endl;
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            // Show extensive help and exit
            printHelp(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            startFullscreen = true;
        } else if (strcmp(argv[i], "--windowed") == 0 || strcmp(argv[i], "-w") == 0) {
            startFullscreen = false;  // Override default fullscreen
        } else if (strcmp(argv[i], "--maximize") == 0 || strcmp(argv[i], "-m") == 0) {
            startMaximized = true;
            startFullscreen = false;  // Maximize implies windowed mode
        } else if (strncmp(argv[i], "--pos=", 6) == 0) {
            // Parse position as X,Y (e.g., --pos=100,200)
            int x, y;
            if (sscanf(argv[i] + 6, "%d,%d", &x, &y) == 2) {
                startPosX = x;
                startPosY = y;
            } else {
                std::cerr << "Invalid position format: " << argv[i] << " (use --pos=X,Y)" << std::endl;
                return 1;
            }
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            // Parse size as WxH (e.g., --size=800x600)
            int w, h;
            if (sscanf(argv[i] + 7, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                startWidth = w;
                startHeight = h;
            } else {
                std::cerr << "Invalid size format: " << argv[i] << " (use --size=WxH)" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--remote-control") == 0) {
            // Enable remote control with default port
            remoteControlEnabled = true;
        } else if (strncmp(argv[i], "--remote-control=", 17) == 0) {
            // Enable remote control with custom port
            remoteControlEnabled = true;
            int port;
            if (sscanf(argv[i] + 17, "%d", &port) == 1 && port > 0 && port < 65536) {
                remoteControlPort = port;
            } else {
                std::cerr << "Invalid port format: " << argv[i] << " (use --remote-control=PORT)" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--duration") == 0) {
            // Benchmark duration (next arg is seconds)
            if (i + 1 < argc) {
                benchmarkDuration = atoi(argv[++i]);
                if (benchmarkDuration <= 0) {
                    std::cerr << "Invalid duration: " << argv[i] << " (must be positive integer)" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "--duration requires a value in seconds" << std::endl;
                return 1;
            }
        } else if (strncmp(argv[i], "--duration=", 11) == 0) {
            // Benchmark duration (inline value)
            benchmarkDuration = atoi(argv[i] + 11);
            if (benchmarkDuration <= 0) {
                std::cerr << "Invalid duration: " << (argv[i] + 11) << " (must be positive integer)" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--json") == 0) {
            // JSON output for benchmark results
            g_jsonOutput = true;
#ifdef __APPLE__
        } else if (strcmp(argv[i], "--metal") == 0) {
            // Enable Metal GPU backend (Ganesh)
            useMetalBackend = true;
        } else if (strcmp(argv[i], "--graphite") == 0) {
            // Enable Graphite GPU backend (next-gen Metal)
            useGraphiteBackend = true;
#endif
        } else if (strncmp(argv[i], "--screenshot=", 13) == 0) {
            // Auto-save screenshot after first frame (for benchmarks)
            screenshotPath = argv[i] + 13;
            if (screenshotPath.empty()) {
                std::cerr << "--screenshot requires a file path (e.g., --screenshot=output.ppm)" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--sequential") == 0) {
            // Sequential frame mode: render frames 0,1,2,3... ignoring SMIL timing
            sequentialMode = true;
        } else if (argv[i][0] != '-') {
            // Non-option argument is the input file
            inputPath = argv[i];
        } else {
            // Unknown option
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            std::cerr << "Use --help for usage information." << std::endl;
            return 1;
        }
    }

    // Configure animation controller verbose mode based on JSON output setting
    // This must be done before any SVG loading/parsing to suppress console messages
    g_animController.setVerbose(!g_jsonOutput);

    // Input file/folder is required
    if (!inputPath) {
        std::cerr << "Error: No input file or folder specified.\n" << std::endl;
        printHelp(argv[0]);
        return 1;
    }

    // Initialize font support for SVG text rendering (must be done before any SVG parsing)
    initializeFontSupport();

    // Check if input is a directory (image sequence mode)
    if (isDirectory(inputPath)) {
        isImageSequence = true;
        sequentialMode = true;  // Image sequences always use sequential mode
        sequenceFiles = scanFolderForSVGSequence(inputPath);
        if (sequenceFiles.empty()) {
            std::cerr << "Error: No SVG files found in folder: " << inputPath << std::endl;
            return 1;
        }
        // Use first file for initial loading (for dimensions and window setup)
        inputPath = sequenceFiles[0].c_str();
        if (!g_jsonOutput) {
            std::cerr << "Image sequence mode: " << sequenceFiles.size() << " frames from folder" << std::endl;
            std::cerr << "Sequential rendering mode enabled (ignoring SMIL timing)" << std::endl;
            std::cerr << "Pre-loading all SVG frames..." << std::endl;
        }
        // Pre-load all SVG file contents for image sequence mode
        // This allows fast frame switching without file I/O during playback
        sequenceSvgContents.reserve(sequenceFiles.size());
        for (const auto& filePath : sequenceFiles) {
            std::ifstream file(filePath);
            if (!file) {
                std::cerr << "Error: Cannot read SVG file: " << filePath << std::endl;
                return 1;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            sequenceSvgContents.push_back(buffer.str());
        }
        if (!g_jsonOutput) {
            std::cerr << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;
        }
    } else if (sequentialMode && !g_jsonOutput) {
        // Single FBF file with sequential mode enabled via --sequential flag
        std::cerr << "Sequential rendering mode enabled (ignoring SMIL timing)" << std::endl;
    }

    // Validate input file before loading
    if (!fileExists(inputPath)) {
        std::cerr << "Error: File not found: " << inputPath << std::endl;
        return 1;
    }

    size_t fileSize = getFileSize(inputPath);
    if (fileSize == 0) {
        std::cerr << "Error: File is empty: " << inputPath << std::endl;
        return 1;
    }
    if (fileSize > MAX_SVG_FILE_SIZE) {
        std::cerr << "Error: File too large (" << (fileSize / 1024 / 1024) << " MB). "
                  << "Maximum supported size is " << (MAX_SVG_FILE_SIZE / 1024 / 1024) << " MB." << std::endl;
        return 1;
    }

    // DEBUG: Check before file read
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] Before file read: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // Read the SVG file content
    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot read file: " << inputPath << std::endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string originalContent = buffer.str();
    file.close();

    // DEBUG: Check after file read
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] After file read (" << originalContent.size() << " bytes): g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // Validate SVG content structure
    if (!validateSVGContent(originalContent)) {
        std::cerr << "Error: Invalid SVG file - no <svg> element found: " << inputPath << std::endl;
        return 1;
    }

    // Pre-process SVG to inject IDs into <use> elements that contain <animate> but lack IDs
    // This is necessary for panther_bird.fbf.svg and similar files where <use> has no id
    if (!g_jsonOutput) std::cout << "Parsing SMIL animations..." << std::endl;
    std::map<size_t, std::string> syntheticIds;
    std::string processedContent = preprocessSVGForAnimation(originalContent, syntheticIds);

    // DEBUG: Check after preprocessing
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] After preprocessSVGForAnimation: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // Extract animations from the preprocessed content
    std::vector<SMILAnimation> animations = extractAnimationsFromContent(processedContent);

    // DEBUG: Check after animation extraction
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] After extractAnimationsFromContent: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    if (!g_jsonOutput) {
        if (animations.empty()) {
            std::cout << "No SMIL animations found - will render static SVG" << std::endl;
        } else {
            std::cout << "Found " << animations.size() << " animation(s)" << std::endl;
        }
    }

    // Store raw SVG content for parallel renderer
    std::string rawSvgContent = processedContent;

    // Load SVG with Skia using the preprocessed content (with synthetic IDs injected)
    sk_sp<SkData> svgData = SkData::MakeWithCopy(processedContent.data(), processedContent.size());
    std::unique_ptr<SkMemoryStream> svgStream = SkMemoryStream::Make(svgData);
    if (!svgStream) {
        std::cerr << "Failed to create memory stream for SVG" << std::endl;
        return 1;
    }

    // Use makeSVGDOMWithFontSupport to ensure SVG text elements render properly
    sk_sp<SkSVGDOM> svgDom = makeSVGDOMWithFontSupport(*svgStream);
    if (!svgDom) {
        std::cerr << "Failed to parse SVG: " << inputPath << std::endl;
        return 1;
    }

    SkSVGSVG* root = svgDom->getRoot();
    if (!root) {
        std::cerr << "SVG has no root element" << std::endl;
        return 1;
    }

    // Verify we can find animated elements
    for (const auto& anim : animations) {
        sk_sp<SkSVGNode>* nodePtr = svgDom->findNodeById(anim.targetId.c_str());
        if (!nodePtr || !*nodePtr) {
            std::cerr << "Warning: Cannot find animated element: " << anim.targetId << std::endl;
        } else if (!g_jsonOutput) {
            std::cout << "Found target element: " << anim.targetId << std::endl;
        }
    }

    // Get SVG dimensions - prefer viewBox over intrinsicSize for percentage-based SVGs
    // When SVG has width="100%" height="100%", intrinsicSize returns the context size (wrong)
    // The viewBox defines the actual content dimensions and should be used instead
    int svgWidth = 800;
    int svgHeight = 600;

    const auto& viewBox = root->getViewBox();
    // Check if viewBox is populated (Windows uses std::optional, macOS/Linux use SkTLazy)
#if defined(PLATFORM_WINDOWS)
    const bool hasViewBox2 = viewBox.has_value();
#else
    const bool hasViewBox2 = viewBox.isValid();
#endif
    if (hasViewBox2) {
        // Use viewBox dimensions - this is the actual content coordinate space
        svgWidth = static_cast<int>(viewBox->width());
        svgHeight = static_cast<int>(viewBox->height());
    } else {
        // Fall back to intrinsicSize if no viewBox
        SkSize defaultSize = SkSize::Make(800, 600);
        SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));
        svgWidth = (svgSize.width() > 0) ? static_cast<int>(svgSize.width()) : 800;
        svgHeight = (svgSize.height() > 0) ? static_cast<int>(svgSize.height()) : 600;
    }
    float aspectRatio = static_cast<float>(svgWidth) / svgHeight;

    if (!g_jsonOutput) {
        std::cout << "SVG dimensions: " << svgWidth << "x" << svgHeight << std::endl;
        std::cout << "Aspect ratio: " << aspectRatio << std::endl;
    }

    // Initialize SDL with hints to reduce stutters
    // Force Metal renderer on macOS for better performance
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    // Enable render batching for better throughput
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    // Use Metal's direct-to-display for lower latency
    SDL_SetHint(SDL_HINT_RENDER_METAL_PREFER_LOW_POWER_DEVICE, "0");
    // Use linear (bilinear) filtering for texture scaling - prevents pixelation
    // "0" = nearest, "1" = linear, "2" = best (anisotropic if available)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    // DEBUG: Check signal before SDL_Init
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] Before SDL_Init: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // Let SDL handle SIGINT/SIGTERM and convert them to SDL_QUIT events
    // Our SDL_QUIT handler sets g_shutdownRequested for proper cleanup
    // (SDL_HINT_NO_SIGNAL_HANDLERS is NOT set, so SDL will catch signals)

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // DEBUG: Check g_shutdownRequested after SDL init
    if (debugSignals) {
        std::cerr << "[SIGNAL_DEBUG] After SDL_Init: g_shutdownRequested="
                  << g_shutdownRequested.load() << std::endl;
    }

    // CRITICAL: SDL_Init may overwrite signal handlers or trigger signals during init.
    // Re-install our handlers and clear the shutdown flag if it was set spuriously.
    if (g_shutdownRequested.load()) {
        if (debugSignals) {
            std::cerr << "[SIGNAL_DEBUG] WARNING: SDL_Init triggered shutdown signal! Resetting flag." << std::endl;
        }
        g_shutdownRequested.store(false);  // Reset the spurious signal
    }
    installSignalHandlers();  // Re-install our handlers after SDL

    // Create window at SVG native resolution (scaled to fit reasonable bounds)
    int windowWidth = svgWidth;
    int windowHeight = svgHeight;

    // Ensure minimum window size of 400px (maintain aspect ratio)
    const int MIN_WINDOW_SIZE = 400;
    if (windowWidth < MIN_WINDOW_SIZE && windowHeight < MIN_WINDOW_SIZE) {
        if (windowWidth > windowHeight) {
            windowWidth = MIN_WINDOW_SIZE;
            windowHeight = static_cast<int>(MIN_WINDOW_SIZE / aspectRatio);
        } else {
            windowHeight = MIN_WINDOW_SIZE;
            windowWidth = static_cast<int>(MIN_WINDOW_SIZE * aspectRatio);
        }
    }

    // Limit initial window size to 1200px max dimension
    if (windowWidth > 1200 || windowHeight > 1200) {
        if (windowWidth > windowHeight) {
            windowWidth = 1200;
            windowHeight = static_cast<int>(1200 / aspectRatio);
        } else {
            windowHeight = 1200;
            windowWidth = static_cast<int>(1200 * aspectRatio);
        }
    }

    // Get native display resolution for fullscreen mode (Retina/HiDPI aware)
    SDL_DisplayMode displayMode;
    if (SDL_GetCurrentDisplayMode(0, &displayMode) == 0) {
        if (!g_jsonOutput) {
            std::cout << "Native display: " << displayMode.w << "x" << displayMode.h << " @ " << displayMode.refresh_rate
                      << "Hz" << std::endl;
        }
    }

    // Window creation with optional exclusive fullscreen
    // For fullscreen: use native display resolution
    // For windowed: use SVG-based dimensions or command-line overrides
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    // Add Metal flag for GPU-accelerated rendering (macOS)
#ifdef __APPLE__
    if (useMetalBackend) {
        windowFlags |= SDL_WINDOW_METAL;
    }
#endif

    // Apply size override from command-line if specified
    int createWidth = (startWidth > 0) ? startWidth : windowWidth;
    int createHeight = (startHeight > 0) ? startHeight : windowHeight;

    if (startFullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
        // Use native display resolution for fullscreen
        createWidth = displayMode.w;
        createHeight = displayMode.h;
    }

    // Apply position from command-line (or use centered if not specified)
    int createPosX = startPosX;
    int createPosY = startPosY;

    SDL_Window* window = SDL_CreateWindow("SVG Player (Animated) - Skia", createPosX, createPosY,
                                          createWidth, createHeight, windowFlags);

    // Track fullscreen state (matches command line flag)
    bool isFullscreen = startFullscreen;

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Configure green button to zoom/maximize instead of fullscreen (macOS)
    // Fullscreen should only be triggered by F key, not by clicking green button
    configureWindowForZoom(window);

    // Apply maximize if requested (after window creation and zoom config)
    if (startMaximized && !startFullscreen) {
        toggleWindowMaximize(window);
        if (!g_jsonOutput) std::cout << "Started maximized" << std::endl;
    }

    // VSync state
    bool vsyncEnabled = false;

    // Create renderer (initially without VSync) - skip for Metal mode
    SDL_Renderer* renderer = nullptr;
#ifdef __APPLE__
    if (!useMetalBackend) {
#endif
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
#ifdef __APPLE__
    }
#endif

    // Metal context for GPU-accelerated rendering (macOS only)
#ifdef __APPLE__
    std::unique_ptr<svgplayer::MetalContext> metalContext;
    GrMTLHandle metalDrawable = nullptr;  // Current drawable for Metal rendering (Ganesh)

    // Graphite context for next-gen GPU rendering (macOS only)
    std::unique_ptr<svgplayer::GraphiteContext> graphiteContext;

    // Graphite initialization (with Metal Ganesh fallback)
    if (useGraphiteBackend) {
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[GRAPHITE_DEBUG] Before createGraphiteContext" << std::endl;
        graphiteContext = svgplayer::createGraphiteContext(window);
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[GRAPHITE_DEBUG] After createGraphiteContext" << std::endl;
        if (graphiteContext && graphiteContext->isInitialized()) {
            installSignalHandlers();
            // Sync VSync state with player's default (off for benchmarking)
            graphiteContext->setVSyncEnabled(vsyncEnabled);
            if (!g_jsonOutput) {
                std::cout << "[Graphite] Next-gen GPU backend enabled - "
                          << graphiteContext->getBackendName() << " rendering active" << std::endl;
                std::cout << "[Graphite] VSync: " << (vsyncEnabled ? "ON" : "OFF") << std::endl;
            }
        } else {
            std::cerr << "[Graphite] Failed to initialize Graphite context, falling back to Metal (Ganesh)" << std::endl;
            useGraphiteBackend = false;
            useMetalBackend = true;  // Fallback to Metal Ganesh
        }
    }

    // Metal (Ganesh) initialization
    if (useMetalBackend) {
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] Before createMetalContext: g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;
        metalContext = svgplayer::createMetalContext(window);
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] After createMetalContext: g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;
        if (metalContext) {
            // Re-install signal handlers after Metal context creation
            // Metal/SDL may override them during CAMetalLayer setup
            installSignalHandlers();
            if (!g_jsonOutput) {
                std::cout << "[Metal] GPU backend (Ganesh) enabled - GPU-accelerated rendering active" << std::endl;
            }
        } else {
            std::cerr << "[Metal] Failed to initialize Metal context, falling back to CPU rendering" << std::endl;
            useMetalBackend = false;
        }
    }
#endif

    // Get actual renderer output size (accounts for HiDPI/Retina)
    int rendererW, rendererH;
#ifdef __APPLE__
    if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
        // Graphite mode: use SDL_Metal_GetDrawableSize
        SDL_Metal_GetDrawableSize(window, &rendererW, &rendererH);
    } else if (useMetalBackend && metalContext && metalContext->isInitialized()) {
        // Metal mode: use SDL_Metal_GetDrawableSize
        SDL_Metal_GetDrawableSize(window, &rendererW, &rendererH);
    } else
#endif
    {
        // CPU mode: use renderer output size
        SDL_GetRendererOutputSize(renderer, &rendererW, &rendererH);
    }
    // HiDPI scale = renderer pixels / window logical pixels
    // For fullscreen: use createWidth/Height (native display), for windowed: use windowWidth/Height
    // Prevent division by zero (should never happen, but defensive programming)
    if (createWidth == 0) createWidth = 1;
    float hiDpiScale = static_cast<float>(rendererW) / createWidth;
    if (!g_jsonOutput) {
        std::cout << "HiDPI scale factor: " << std::fixed << std::setprecision(4) << hiDpiScale << std::endl;
    }

    // Query display refresh rate for frame limiter
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode refreshDisplayMode;  // Renamed to avoid shadowing displayMode
    int displayRefreshRate = 60;         // Default fallback
    if (SDL_GetCurrentDisplayMode(displayIndex, &refreshDisplayMode) == 0) {
        displayRefreshRate = refreshDisplayMode.refresh_rate > 0 ? refreshDisplayMode.refresh_rate : 60;
    }
    if (!g_jsonOutput) {
        std::cout << "Display refresh rate: " << displayRefreshRate << " Hz" << std::endl;
    }

    // Setup font for debug overlay (platform-specific font manager)
    sk_sp<SkFontMgr> fontMgr = createPlatformFontMgr();
    sk_sp<SkTypeface> typeface = fontMgr->matchFamilyStyle("Menlo", SkFontStyle::Normal());
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle("Courier", SkFontStyle::Normal());
    }
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    }
    // Final fallback to prevent null pointer when creating SkFont
    if (!typeface) {
        std::cerr << "Warning: No font available for debug overlay, using default" << std::endl;
        typeface = fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
    }

    // Debug font - 10pt base (40% larger than original 7pt), scaled for HiDPI
    SkFont debugFont(typeface, 10 * hiDpiScale);
    debugFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);

    // Paint for debug text background
    SkPaint bgPaint;
    bgPaint.setColor(SkColorSetARGB(160, 0, 0, 0));  // 20% more transparent
    bgPaint.setStyle(SkPaint::kFill_Style);

    // Paint for debug text
    SkPaint textPaint;
    textPaint.setColor(SK_ColorWHITE);
    textPaint.setAntiAlias(true);

    // Paint for highlight values
    SkPaint highlightPaint;
    highlightPaint.setColor(SkColorSetRGB(0, 255, 128));
    highlightPaint.setAntiAlias(true);

    // Paint for animation info
    SkPaint animPaint;
    animPaint.setColor(SkColorSetRGB(255, 128, 255));
    animPaint.setAntiAlias(true);

    // Paint for key hints
    SkPaint keyPaint;
    keyPaint.setColor(SkColorSetRGB(255, 200, 100));
    keyPaint.setAntiAlias(true);

    // Performance tracking - all phases that add up to total frame time
    // Pipeline phases (in order): Event -> Anim -> Fetch -> Overlay -> Copy -> Present
    // Window size of 30 frames = ~0.5 seconds at 60fps, responsive but stable
    RollingAverage eventTimes(30);    // SDL event processing
    RollingAverage animTimes(30);     // Animation state update
    RollingAverage fetchTimes(30);    // Fetching frame from threaded renderer
    RollingAverage overlayTimes(30);  // Drawing debug overlay
    RollingAverage copyTimes(30);     // Copying pixels to SDL texture
    RollingAverage presentTimes(30);  // SDL_RenderPresent
    RollingAverage frameTimes(30);    // Total frame time (should = sum of above)
    RollingAverage renderTimes(30);   // Actual SVG render time (from render thread)
    RollingAverage idleTimes(30);     // Time spent waiting when no frame ready

    // Frame delivery tracking - measures how often render thread delivers new frames
    uint64_t displayCycles = 0;    // Total main loop iterations (display refresh attempts)
    uint64_t framesDelivered = 0;  // Frames actually received from render thread
    uint64_t frameCount = 0;
    bool screenshotSaved = false;  // Auto-screenshot flag for benchmark mode
    auto startTime = Clock::now();
    auto lastFrameTime = Clock::now();
    auto animationStartTime = Clock::now();

    // Animation state - using SteadyClock for SMIL-compliant timing
    bool animationPaused = false;
    double pausedTime = 0;
    size_t lastFrameIndex = 0;
    size_t currentFrameIndex = 0;
    std::string lastFrameValue;

    // Sequential frame mode counter - increments each cycle instead of using wall-clock time
    // When sequentialMode is true, frames are rendered 0,1,2,3... as fast as possible
    size_t sequentialFrameCounter = 0;

    // PreBuffer mode timing parameters (for global frame index calculation)
    // These MUST match what was passed to parallelRenderer.configure()
    size_t preBufferTotalFrames = 1;
    double preBufferTotalDuration = 1.0;

    // Frame skip tracking for synchronization verification
    size_t framesRendered = 0;  // Actual frames we rendered
    size_t framesSkipped = 0;   // Frames skipped due to slow rendering
    size_t lastRenderedAnimFrame = 0;

    // Stress test mode (press 'S' to toggle)
    bool stressTestEnabled = false;

    // Use SteadyClock for animation (monotonic, immune to clock adjustments)
    auto animationStartTimeSteady = SteadyClock::now();

    // Current render dimensions (in actual pixels, not logical points)
    // Use renderer output size directly - this accounts for HiDPI/Retina and fullscreen resolution
    int renderWidth = rendererW;
    int renderHeight = rendererH;

    // Create initial texture (only for CPU rendering mode)
    SDL_Texture* texture = nullptr;
#ifdef __APPLE__
    if (!useMetalBackend) {
#endif
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, renderWidth, renderHeight);
#ifdef __APPLE__
    }
#endif

    // Skia surface - either CPU-backed (Raster) or GPU-backed (Metal)
    sk_sp<SkSurface> surface;

    // Lambda to create/recreate the Skia surface
    // For Metal: creates GPU-accelerated surface from CAMetalLayer
    // For CPU: creates raster surface in system memory
    auto createSurface = [&](int w, int h) -> bool {
#ifdef __APPLE__
        if (useMetalBackend && metalContext && metalContext->isInitialized()) {
            // Metal GPU-backed surface - render directly to GPU texture
            metalContext->updateDrawableSize(w, h);
            surface = metalContext->createSurface(w, h, &metalDrawable);
            if (surface) {
                return true;
            } else {
                std::cerr << "[Metal] Failed to create GPU surface, falling back to CPU" << std::endl;
                useMetalBackend = false;
                // Fall through to CPU path
            }
        }
#endif
        // CPU raster surface - traditional path
        SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(w, h);
        surface = SkSurfaces::Raster(imageInfo);
        return surface != nullptr;
    };

    // For Metal backend, skip initial surface creation - Metal creates fresh surfaces each frame
    // This avoids exhausting the CAMetalLayer drawable pool (typically 2-3 drawables)
#ifdef __APPLE__
    if (!useMetalBackend) {
#endif
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] Before createSurface: g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;
        if (!createSurface(renderWidth, renderHeight)) {
            std::cerr << "Failed to create Skia surface" << std::endl;
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] After createSurface: g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;
#ifdef __APPLE__
    } else {
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] Skipping initial createSurface for Metal (surfaces created per-frame)" << std::endl;
    }
#endif

    bool running = true;
    bool frameLimiterEnabled = false;  // OFF by default for max FPS
    bool showDebugOverlay = true;      // D key toggles debug info overlay
    SDL_Event event;

    // Parallel renderer using Skia's SkTaskGroup for multi-core rendering
    // Supports 2 modes: P-MODE-OFF (no parallelism) and P-MODE-1 (PreBuffer)
    SkiaParallelRenderer parallelRenderer;
    int totalCores = parallelRenderer.totalCores;
    int availableCores = parallelRenderer.getWorkerCount();

    // Calculate animation timing parameters for PreBuffer mode
    // maxFrames: largest frame count across all animations (for buffer indexing)
    // maxDuration: longest animation duration (for time-based frame calculation)
    size_t maxFrames = 1;
    double maxDuration = 1.0;
    if (!animations.empty()) {
        for (const auto& anim : animations) {
            if (anim.values.size() > maxFrames) {
                maxFrames = anim.values.size();
            }
            if (anim.duration > maxDuration) {
                maxDuration = anim.duration;
            }
        }
    }

    // Store timing parameters for PreBuffer frame index calculation in main loop
    // CRITICAL: These MUST match what parallelRenderer.configure() receives
    preBufferTotalFrames = maxFrames;
    preBufferTotalDuration = maxDuration;

    // ThreadedRenderer for CPU mode - not used in Metal mode
    // Metal renders directly on main thread with GPU acceleration
    std::unique_ptr<ThreadedRenderer> threadedRendererPtr;

#ifdef __APPLE__
    if (!useMetalBackend) {
#endif
        // Initialize parallel renderer with SVG data, ALL animations, and timing info
        // PreBuffer mode uses time-based frame calculation for multi-animation sync
        parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                                   animations, maxDuration, maxFrames);

        // Start parallel renderer in PreBuffer mode by default (best for animations)
        parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);

        // Threaded renderer keeps UI responsive by moving all rendering to background thread
        // Main thread ONLY handles events and blits completed frames - NEVER blocks on rendering
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] Before ThreadedRenderer: g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;
        threadedRendererPtr = std::make_unique<ThreadedRenderer>();
        threadedRendererPtr->configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
        threadedRendererPtr->start();
        if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_DEBUG] After ThreadedRenderer.start(): g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;

        // Initialize cached mode state to reflect PreBuffer is ON by default
        threadedRendererPtr->cachedPreBufferMode = true;
        threadedRendererPtr->cachedActiveWorkers = parallelRenderer.activeWorkers.load();

        // Set total animation frames so PreBuffer mode can pre-render ahead
        threadedRendererPtr->setTotalAnimationFrames(maxFrames);

        // Initialize dirty region tracking for partial rendering optimization
        // Extracts element bounds from SVG content and caches them for dirty rect calculation
        threadedRendererPtr->initializeDirtyTracking(animations);
#ifdef __APPLE__
    } else {
        if (!g_jsonOutput) {
            std::cout << "[Metal] GPU-accelerated rendering enabled - ThreadedRenderer disabled" << std::endl;
        }
    }
#endif

    if (!g_jsonOutput) {
        std::cout << "\nCPU cores detected: " << totalCores << std::endl;
        std::cout << "Skia thread pool size: " << availableCores << " (1 reserved for system)" << std::endl;
        std::cout << "PreBuffer mode: ON (default)" << std::endl;
        std::cout << "UI thread: Non-blocking (render thread active)" << std::endl;

        std::cout << "\nControls:" << std::endl;
        std::cout << "  ESC/Q - Quit" << std::endl;
        std::cout << "  SPACE - Pause/Resume animation" << std::endl;
        std::cout << "  D - Toggle debug info overlay" << std::endl;
        std::cout << "  F/G - Toggle fullscreen mode" << std::endl;
        std::cout << "  M - Toggle maximize/restore (zoom)" << std::endl;
        std::cout << "  S - Toggle stress test (50ms delay per frame)" << std::endl;
        std::cout << "  V - Toggle VSync" << std::endl;
        std::cout << "  T - Toggle frame limiter (" << displayRefreshRate << " FPS cap)" << std::endl;
        std::cout << "  P - Toggle parallel mode: Off <-> PreBuffer" << std::endl;
        std::cout << "      Off: Direct single-threaded rendering" << std::endl;
        std::cout << "      PreBuffer: Pre-render animation frames ahead using thread pool" << std::endl;
        std::cout << "  R - Reset statistics" << std::endl;
        std::cout << "  C - Capture screenshot (PPM format, uncompressed)" << std::endl;
        std::cout << "  O - Open new SVG file (hot-reload)" << std::endl;
        std::cout << "  B - Toggle folder browser (click to navigate)" << std::endl;
        std::cout << "  Resize window to change render resolution" << std::endl;
        std::cout << "\nSMIL Sync Guarantee:" << std::endl;
        std::cout << "  Animation timing uses steady_clock (monotonic)" << std::endl;
        std::cout << "  Frame shown = f(current_time), NOT f(frame_count)" << std::endl;
        std::cout << "  If rendering is slow, frames SKIP but sync is PERFECT" << std::endl;
        std::cout << "  Press 'S' to enable stress test and verify sync" << std::endl;
        std::cout << "\nNote: Occasional stutters may be caused by macOS system tasks." << std::endl;
        std::cout << "      Animation sync remains correct even during stutters." << std::endl;
    }

    // Remote control server for programmatic control via TCP/JSON
    std::unique_ptr<svgplayer::RemoteControlServer> remoteServer;
    if (remoteControlEnabled) {
        remoteServer = std::make_unique<svgplayer::RemoteControlServer>(remoteControlPort);

        // Register command handlers - these capture player state by reference
        using namespace svgplayer;

        // Ping - simple health check
        remoteServer->registerHandler(RemoteCommand::Ping, [](const std::string&) {
            return json::success("\"pong\"");
        });

        // Play - resume animation
        remoteServer->registerHandler(RemoteCommand::Play, [&animationPaused, &pausedTime, &animationStartTimeSteady](const std::string&) {
            if (animationPaused) {
                DurationSec pauseDuration(pausedTime);
                animationStartTimeSteady = SteadyClock::now() - std::chrono::duration_cast<SteadyClock::duration>(pauseDuration);
                animationPaused = false;
                std::cout << "Remote: Animation resumed" << std::endl;
            }
            return json::success();
        });

        // Pause - pause animation
        remoteServer->registerHandler(RemoteCommand::Pause, [&animationPaused, &pausedTime, &animationStartTimeSteady](const std::string&) {
            if (!animationPaused) {
                DurationSec elapsed = SteadyClock::now() - animationStartTimeSteady;
                pausedTime = elapsed.count();
                animationPaused = true;
                std::cout << "Remote: Animation paused at " << pausedTime << "s" << std::endl;
            }
            return json::success();
        });

        // Stop - stop and reset to beginning
        remoteServer->registerHandler(RemoteCommand::Stop, [&animationPaused, &pausedTime, &animationStartTimeSteady](const std::string&) {
            animationPaused = true;
            pausedTime = 0;
            animationStartTimeSteady = SteadyClock::now();
            std::cout << "Remote: Animation stopped" << std::endl;
            return json::success();
        });

        // TogglePlay - toggle play/pause
        remoteServer->registerHandler(RemoteCommand::TogglePlay, [&animationPaused, &pausedTime, &animationStartTimeSteady](const std::string&) {
            if (animationPaused) {
                DurationSec pauseDuration(pausedTime);
                animationStartTimeSteady = SteadyClock::now() - std::chrono::duration_cast<SteadyClock::duration>(pauseDuration);
                animationPaused = false;
                std::cout << "Remote: Animation resumed" << std::endl;
            } else {
                DurationSec elapsed = SteadyClock::now() - animationStartTimeSteady;
                pausedTime = elapsed.count();
                animationPaused = true;
                std::cout << "Remote: Animation paused at " << pausedTime << "s" << std::endl;
            }
            return json::success();
        });

        // Seek - seek to specific time
        remoteServer->registerHandler(RemoteCommand::Seek, [&animationPaused, &pausedTime, &animationStartTimeSteady, &maxDuration](const std::string& params) {
            // Parse time from JSON params
            std::string timeStr;
            size_t pos = params.find("\"time\"");
            if (pos != std::string::npos) {
                pos = params.find(':', pos);
                if (pos != std::string::npos) {
                    pos++;
                    while (pos < params.size() && (params[pos] == ' ' || params[pos] == '\t')) pos++;
                    try {
                        double targetTime = std::stod(params.substr(pos));
                        // Clamp to valid range
                        if (targetTime < 0) targetTime = 0;
                        if (targetTime > maxDuration) targetTime = maxDuration;

                        if (animationPaused) {
                            pausedTime = targetTime;
                        } else {
                            DurationSec seekDuration(targetTime);
                            animationStartTimeSteady = SteadyClock::now() - std::chrono::duration_cast<SteadyClock::duration>(seekDuration);
                        }
                        std::cout << "Remote: Seeked to " << targetTime << "s" << std::endl;
                        return json::success();
                    } catch (...) {
                        return json::error("Invalid time value");
                    }
                }
            }
            return json::error("Missing time parameter");
        });

        // Fullscreen - toggle fullscreen mode
        remoteServer->registerHandler(RemoteCommand::Fullscreen, [&isFullscreen, window, renderer](const std::string&) {
            // Clear screen before mode switch
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);

            isFullscreen = !isFullscreen;
            if (isFullscreen) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
            } else {
                SDL_SetWindowFullscreen(window, 0);
            }

            // Clear again after mode switch
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);

            std::cout << "Remote: Fullscreen " << (isFullscreen ? "ON" : "OFF") << std::endl;
            return json::success();
        });

        // Maximize - toggle maximize/restore
        remoteServer->registerHandler(RemoteCommand::Maximize, [window](const std::string&) {
            bool newState = toggleWindowMaximize(window);
            std::cout << "Remote: Window " << (newState ? "MAXIMIZED" : "RESTORED") << std::endl;
            return json::success();
        });

        // SetPosition - set window position
        remoteServer->registerHandler(RemoteCommand::SetPosition, [window](const std::string& params) {
            // Parse x,y from JSON params
            size_t xPos = params.find("\"x\"");
            size_t yPos = params.find("\"y\"");
            if (xPos != std::string::npos && yPos != std::string::npos) {
                int x = 0, y = 0;
                try {
                    size_t colonPos = params.find(':', xPos);
                    if (colonPos != std::string::npos) {
                        x = std::stoi(params.substr(colonPos + 1));
                    }
                    colonPos = params.find(':', yPos);
                    if (colonPos != std::string::npos) {
                        y = std::stoi(params.substr(colonPos + 1));
                    }
                    SDL_SetWindowPosition(window, x, y);
                    std::cout << "Remote: Window position set to " << x << "," << y << std::endl;
                    return json::success();
                } catch (...) {
                    return json::error("Invalid position values");
                }
            }
            return json::error("Missing x or y parameters");
        });

        // SetSize - set window size
        remoteServer->registerHandler(RemoteCommand::SetSize, [window](const std::string& params) {
            // Parse width,height from JSON params
            size_t wPos = params.find("\"width\"");
            size_t hPos = params.find("\"height\"");
            if (wPos != std::string::npos && hPos != std::string::npos) {
                int w = 0, h = 0;
                try {
                    size_t colonPos = params.find(':', wPos);
                    if (colonPos != std::string::npos) {
                        w = std::stoi(params.substr(colonPos + 1));
                    }
                    colonPos = params.find(':', hPos);
                    if (colonPos != std::string::npos) {
                        h = std::stoi(params.substr(colonPos + 1));
                    }
                    if (w > 0 && h > 0) {
                        SDL_SetWindowSize(window, w, h);
                        std::cout << "Remote: Window size set to " << w << "x" << h << std::endl;
                        return json::success();
                    }
                    return json::error("Invalid size values (must be positive)");
                } catch (...) {
                    return json::error("Invalid size values");
                }
            }
            return json::error("Missing width or height parameters");
        });

        // GetState - get current player state
        remoteServer->registerHandler(RemoteCommand::GetState, [&animationPaused, &pausedTime, &animationStartTimeSteady,
                                                                 &isFullscreen, window, &maxFrames, &maxDuration,
                                                                 inputPath, &currentFrameIndex](const std::string&) {
            PlayerState state;
            state.playing = !animationPaused;
            state.paused = animationPaused;

            // Calculate current time
            if (animationPaused) {
                state.currentTime = pausedTime;
            } else {
                DurationSec elapsed = SteadyClock::now() - animationStartTimeSteady;
                state.currentTime = elapsed.count();
            }

            // Check window state
            Uint32 flags = SDL_GetWindowFlags(window);
            state.fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
            state.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;

            // Get window position and size
            SDL_GetWindowPosition(window, &state.windowX, &state.windowY);
            SDL_GetWindowSize(window, &state.windowWidth, &state.windowHeight);

            // Animation info
            state.currentFrame = static_cast<int>(currentFrameIndex);
            state.totalFrames = static_cast<int>(maxFrames);
            state.totalDuration = maxDuration;
            state.playbackSpeed = 1.0;  // TODO: Add playback speed support
            state.loadedFile = inputPath ? inputPath : "";

            return json::state(state);
        });

        // GetStats - get performance statistics
        remoteServer->registerHandler(RemoteCommand::GetStats, [&frameTimes, &renderTimes, &framesDelivered,
                                                                 &displayCycles, &renderWidth, &renderHeight](const std::string&) {
            PlayerStats stats;
            stats.fps = frameTimes.count() > 0 ? 1000.0 / frameTimes.average() : 0.0;
            stats.avgFrameTime = frameTimes.average();
            stats.avgRenderTime = renderTimes.average();
            stats.droppedFrames = static_cast<int>(displayCycles - framesDelivered);
            stats.memoryUsage = static_cast<size_t>(renderWidth) * renderHeight * 4;  // Approximate
            stats.elementsRendered = 0;  // TODO: Track rendered elements
            return json::stats(stats);
        });

        // Screenshot - capture screenshot to file
        remoteServer->registerHandler(RemoteCommand::Screenshot, [&threadedRendererPtr, &renderWidth, &renderHeight](const std::string& params) {
            // Parse path from JSON params
            std::string path;
            size_t pathPos = params.find("\"path\"");
            if (pathPos != std::string::npos) {
                size_t colonPos = params.find(':', pathPos);
                if (colonPos != std::string::npos) {
                    size_t quoteStart = params.find('"', colonPos);
                    if (quoteStart != std::string::npos) {
                        size_t quoteEnd = params.find('"', quoteStart + 1);
                        if (quoteEnd != std::string::npos) {
                            path = params.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }
            }

            // Get current frame for screenshot
            std::vector<uint32_t> screenshotPixels;
            int screenshotWidth, screenshotHeight;
            if (threadedRendererPtr && threadedRendererPtr->getFrameForScreenshot(screenshotPixels, screenshotWidth, screenshotHeight)) {
                if (path.empty()) {
                    // Generate default filename with dimensions
                    path = generateScreenshotFilename(screenshotWidth, screenshotHeight);
                }
                if (saveScreenshotPPM(screenshotPixels, screenshotWidth, screenshotHeight, path)) {
                    std::cout << "Remote: Screenshot saved to " << path << std::endl;
                    return json::success("\"" + path + "\"");
                }
            }
            return json::error("Failed to capture screenshot (threadedRenderer unavailable in Metal mode)");
        });

        // Quit - quit the player
        remoteServer->registerHandler(RemoteCommand::Quit, [&running](const std::string&) {
            running = false;
            std::cout << "Remote: Quit requested" << std::endl;
            return json::success();
        });

        // Start the remote control server
        if (remoteServer->start()) {
            std::cout << "\nRemote Control: Listening on port " << remoteControlPort << std::endl;
            std::cout << "  Use Python controller: python scripts/svg_player_controller.py --port " << remoteControlPort << std::endl;
        } else {
            std::cerr << "Warning: Failed to start remote control server on port " << remoteControlPort << std::endl;
            remoteServer.reset();
        }
    }

    // Benchmark mode: track start time for duration-based exit
    auto benchmarkStartTime = SteadyClock::now();

    if (!g_jsonOutput) {
        std::cout << "\nRendering..." << std::endl;
    }

    // DEBUG: Check loop entry conditions
    static bool debugMainLoop = (std::getenv("RENDER_DEBUG") != nullptr);
    if (debugMainLoop) {
        std::cerr << "[MAIN_DEBUG] About to enter main loop: running=" << running
                  << ", g_shutdownRequested=" << g_shutdownRequested.load() << std::endl;
    }

    // Main event loop - check both running flag and shutdown request (Ctrl+C)
    while (running && !g_shutdownRequested.load()) {
        // Debug: periodic shutdown check for Metal mode
        if (displayCycles % 100 == 0 && displayCycles > 0) {
            bool shutdownVal = g_shutdownRequested.load();
            if (shutdownVal) {
                std::cerr << "[MAIN_DEBUG] Shutdown detected at cycle " << displayCycles << std::endl;
            }
        }
        if (debugMainLoop && displayCycles == 0) {
            std::cerr << "[MAIN_DEBUG] First main loop iteration starting" << std::endl;
        }
        // CRITICAL: Early ESC/Q check - ensures player is ALWAYS responsive to quit keys
        // This runs BEFORE any potentially blocking operations for guaranteed responsiveness
        SDL_PumpEvents();  // Update keyboard state
        const Uint8* keyState = SDL_GetKeyboardState(nullptr);
        if (keyState[SDL_SCANCODE_ESCAPE] || keyState[SDL_SCANCODE_Q]) {
            if (!g_jsonOutput) {
                std::cout << "\n[QUIT] ESC/Q key detected - exiting immediately" << std::endl;
            }
            running = false;
            break;
        }

        // Benchmark mode: exit after specified duration
        if (benchmarkDuration > 0) {
            auto elapsed = std::chrono::duration<double>(SteadyClock::now() - benchmarkStartTime).count();
            if (elapsed >= benchmarkDuration) {
                running = false;
                break;
            }
        }
        auto frameStart = Clock::now();
        displayCycles++;  // Count every main loop iteration (display refresh attempt)

        // Calculate frame time from last frame
        // Just update lastFrameTime for timing reference (actual frame time tracked when presenting)
        lastFrameTime = frameStart;

        // Calculate animation time using SteadyClock (SMIL-compliant monotonic time)
        // This is the KEY to perfect synchronization:
        // - We always query the CURRENT wall-clock time
        // - We calculate which animation frame SHOULD be displayed NOW
        // - If rendering was slow, we skip frames but show the CORRECT frame for this moment
        // - This guarantees audio sync even if frame rate drops to 1 FPS
        double animTime = 0;
        if (!animationPaused) {
            auto nowSteady = SteadyClock::now();
            DurationSec elapsed = nowSteady - animationStartTimeSteady;
            animTime = elapsed.count();
        } else {
            animTime = pausedTime;
        }

        // Handle events (measure time to detect system stalls)
        // skipStatsThisFrame: set to true when disruptive events occur (reset, mode change, etc.)
        // This prevents expensive one-shot operations from polluting the timing statistics
        bool skipStatsThisFrame = false;
        auto eventStart = Clock::now();
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    // Signal-like shutdown - set the flag so cleanup code runs
                    g_shutdownRequested.store(true);
                    if (!g_jsonOutput) {
                        std::cerr << "[SDL_QUIT] Shutdown requested via event" << std::endl;
                    }
                    break;

                case SDL_KEYDOWN:
                    // Filter out key repeats for toggle keys to prevent rapid on/off cycling
                    if (event.key.repeat) {
                        SDL_Keycode sym = event.key.keysym.sym;
                        // Allow repeats only for non-toggle keys (navigation, quit)
                        if (sym != SDLK_ESCAPE && sym != SDLK_q && sym != SDLK_SPACE &&
                            sym != SDLK_LEFT && sym != SDLK_RIGHT && sym != SDLK_UP && sym != SDLK_DOWN) {
                            break;  // Skip repeat events for toggle keys
                        }
                    }
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        if (g_browserMode) {
                            // Exit browser mode - proper cleanup matching 'B' key toggle
                            g_folderBrowser.stopThumbnailLoader();
                            stopAsyncBrowserDomParse();  // Stop any pending DOM parse
                            g_folderBrowser.cancelScan();
                            g_browserAsyncScanning = false;
                            g_browserMode = false;
                            g_browserSvgDom = nullptr;
                            clearBrowserAnimations();
                            std::cout << "Browser closed" << std::endl;
                        } else {
                            running = false;
                        }
                    } else if (event.key.keysym.sym == SDLK_q) {
                        running = false;
                    } else if (event.key.keysym.sym == SDLK_LEFT && g_browserMode) {
                        // Previous page in browser mode (non-blocking)
                        g_folderBrowser.prevPage();
                        g_folderBrowser.markDirty();  // Render loop handles async parse
                    } else if (event.key.keysym.sym == SDLK_RIGHT && g_browserMode) {
                        // Next page in browser mode (non-blocking)
                        g_folderBrowser.nextPage();
                        g_folderBrowser.markDirty();  // Render loop handles async parse
                    } else if (event.key.keysym.sym == SDLK_SPACE) {
                        if (animationPaused) {
                            // Resume: adjust start time to account for paused duration
                            // Use SteadyClock for animation timing
                            DurationSec pauseDuration(pausedTime);
                            animationStartTimeSteady =
                                SteadyClock::now() - std::chrono::duration_cast<SteadyClock::duration>(pauseDuration);
                            animationPaused = false;
                            std::cout << "Animation resumed" << std::endl;
                        } else {
                            // Pause: save current time using SteadyClock
                            DurationSec elapsed = SteadyClock::now() - animationStartTimeSteady;
                            pausedTime = elapsed.count();
                            animationPaused = true;
                            std::cout << "Animation paused at " << pausedTime << "s" << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_s) {
                        // Toggle stress test (artificial delay to prove sync works)
                        stressTestEnabled = !stressTestEnabled;
                        framesSkipped = 0;
                        framesRendered = 0;
                        if (!g_jsonOutput) {
                            std::cout << "Stress test: " << (stressTestEnabled ? "ON (50ms delay)" : "OFF") << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_r) {
                        eventTimes.reset();
                        animTimes.reset();
                        fetchTimes.reset();
                        overlayTimes.reset();
                        copyTimes.reset();
                        presentTimes.reset();
                        frameTimes.reset();
                        renderTimes.reset();
                        idleTimes.reset();
                        frameCount = 0;
                        displayCycles = 0;
                        framesDelivered = 0;
                        startTime = Clock::now();
                        animationStartTime = Clock::now();
                        animationStartTimeSteady = SteadyClock::now();
                        pausedTime = 0;
                        framesSkipped = 0;
                        framesRendered = 0;
                        lastRenderedAnimFrame = 0;
                        skipStatsThisFrame = true;  // Don't pollute fresh stats with reset operation time
                        if (!g_jsonOutput) {
                            std::cout << "Statistics reset" << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_v) {
                        // Toggle VSync by recreating renderer (CPU mode only)
                        // Metal mode: VSync is controlled by CAMetalLayer.displaySyncEnabled
                        vsyncEnabled = !vsyncEnabled;

#ifdef __APPLE__
                        if (!useMetalBackend) {
#endif
                            SDL_DestroyTexture(texture);
                            SDL_DestroyRenderer(renderer);

                            // Set VSync hint BEFORE creating renderer (critical for Metal on macOS)
                            // This hint must be set before renderer creation to take effect
                            SDL_SetHint(SDL_HINT_RENDER_VSYNC, vsyncEnabled ? "1" : "0");

                            Uint32 flags = SDL_RENDERER_ACCELERATED;
                            if (vsyncEnabled) {
                                flags |= SDL_RENDERER_PRESENTVSYNC;
                            }

                            renderer = SDL_CreateRenderer(window, -1, flags);
                            if (!renderer) {
                                std::cerr << "Failed to recreate renderer!" << std::endl;
                                running = false;
                                break;
                            }

                            // Recreate texture
                            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                        renderWidth, renderHeight);
#ifdef __APPLE__
                        } else if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                            // Graphite mode: Use GraphiteContext VSync control
                            graphiteContext->setVSyncEnabled(vsyncEnabled);
                        } else {
                            // Metal mode: Use MetalContext VSync control
                            if (metalContext && metalContext->isInitialized()) {
                                metalContext->setVSyncEnabled(vsyncEnabled);
                            }
                        }
#endif

                        // Reset ALL stats after VSync change (critical for accurate FPS/hit rate)
                        eventTimes.reset();
                        animTimes.reset();
                        fetchTimes.reset();
                        overlayTimes.reset();
                        copyTimes.reset();
                        presentTimes.reset();
                        frameTimes.reset();
                        renderTimes.reset();
                        idleTimes.reset();
                        frameCount = 0;
                        displayCycles = 0;
                        framesDelivered = 0;
                        startTime = Clock::now();
                        skipStatsThisFrame = true;

                        if (!g_jsonOutput) {
                            std::cout << "VSync: " << (vsyncEnabled ? "ON" : "OFF") << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_t) {
                        // Toggle frame limiter (T for Timing limiter)
                        frameLimiterEnabled = !frameLimiterEnabled;
                        // Reset ALL stats (critical for accurate FPS/hit rate)
                        eventTimes.reset();
                        animTimes.reset();
                        fetchTimes.reset();
                        overlayTimes.reset();
                        copyTimes.reset();
                        presentTimes.reset();
                        frameTimes.reset();
                        renderTimes.reset();
                        idleTimes.reset();
                        frameCount = 0;
                        displayCycles = 0;
                        framesDelivered = 0;
                        startTime = Clock::now();
                        skipStatsThisFrame = true;
                        if (!g_jsonOutput) {
                            std::cout << "Frame limiter: "
                                      << (frameLimiterEnabled ? "ON (" + std::to_string(displayRefreshRate) + " FPS cap)"
                                                              : "OFF")
                                      << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_p) {
                        // Toggle parallel mode: Off <-> PreBuffer (NON-BLOCKING!)
                        // Request is queued for render thread - main thread never blocks
                        // Note: Mode toggle not available in Metal mode (GPU always active)
                        if (threadedRendererPtr) {
                            threadedRendererPtr->requestModeChange();
                        }

                        // Reset ALL stats (critical for accurate FPS/hit rate)
                        eventTimes.reset();
                        animTimes.reset();
                        fetchTimes.reset();
                        overlayTimes.reset();
                        copyTimes.reset();
                        presentTimes.reset();
                        frameTimes.reset();
                        renderTimes.reset();
                        idleTimes.reset();
                        frameCount = 0;
                        displayCycles = 0;
                        framesDelivered = 0;
                        startTime = Clock::now();
                        skipStatsThisFrame = true;
                    } else if (event.key.keysym.sym == SDLK_f || event.key.keysym.sym == SDLK_g) {
                        // Toggle fullscreen mode (F or G for Fullscreen - exclusive, takes over display)
                        // Clear screen to black BEFORE mode switch to prevent ghosting artifacts
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderClear(renderer);
                        SDL_RenderPresent(renderer);

                        isFullscreen = !isFullscreen;
                        if (isFullscreen) {
                            // Use SDL_WINDOW_FULLSCREEN for exclusive fullscreen (no compositor, direct display)
                            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                        } else {
                            // Back to windowed mode
                            SDL_SetWindowFullscreen(window, 0);
                        }

                        // Clear again AFTER mode switch to ensure clean slate
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderClear(renderer);
                        SDL_RenderPresent(renderer);

                        // Force resize detection on next frame
                        skipStatsThisFrame = true;
                        if (!g_jsonOutput) {
                            std::cout << "Fullscreen: " << (isFullscreen ? "ON (exclusive)" : "OFF") << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_m) {
                        // Toggle maximize/zoom (M for Maximize)
                        // Only works in windowed mode, not in fullscreen
                        if (!isFullscreen) {
                            bool nowMaximized = toggleWindowMaximize(window);
                            if (!g_jsonOutput) {
                                std::cout << "Window: " << (nowMaximized ? "MAXIMIZED" : "RESTORED") << std::endl;
                            }
                            skipStatsThisFrame = true;
                        } else if (!g_jsonOutput) {
                            std::cout << "Exit fullscreen first (press F)" << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_d) {
                        // Toggle debug overlay
                        showDebugOverlay = !showDebugOverlay;
                        if (!g_jsonOutput) {
                            std::cout << "Debug overlay: " << (showDebugOverlay ? "ON" : "OFF") << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_c) {
                        // Capture screenshot - exact rendered frame at current resolution
                        std::vector<uint32_t> screenshotPixels;
                        int screenshotWidth, screenshotHeight;
                        bool screenshotSuccess = false;

                        if (g_browserMode) {
                            // Browser mode: capture directly from the Skia surface
                            // The browser renders to 'surface' via canvas, not through threadedRenderer
                            SkPixmap pixmap;
                            if (surface && surface->peekPixels(&pixmap)) {
                                screenshotWidth = pixmap.width();
                                screenshotHeight = pixmap.height();
                                screenshotPixels.resize(screenshotWidth * screenshotHeight);
                                memcpy(screenshotPixels.data(), pixmap.addr(),
                                       screenshotWidth * screenshotHeight * sizeof(uint32_t));
                                screenshotSuccess = true;
                            }
#ifdef __APPLE__
                        } else if (useMetalBackend) {
                            // Metal mode: read pixels from GPU surface
                            // Need to render a frame first to get current state
                            if (metalContext && metalContext->isInitialized() && surface) {
                                // CRITICAL: Flush GPU work before reading pixels
                                // readPixels performs a GPU-to-CPU transfer which requires
                                // all pending GPU commands to complete first
                                metalContext->flush();

                                // Metal surfaces require readPixels() instead of peekPixels()
                                // because the texture is on GPU memory
                                screenshotWidth = surface->width();
                                screenshotHeight = surface->height();
                                SkImageInfo info = SkImageInfo::Make(screenshotWidth, screenshotHeight,
                                                                      kBGRA_8888_SkColorType, kPremul_SkAlphaType);
                                screenshotPixels.resize(screenshotWidth * screenshotHeight);
                                if (surface->readPixels(info, screenshotPixels.data(),
                                                        screenshotWidth * sizeof(uint32_t), 0, 0)) {
                                    screenshotSuccess = true;
                                } else {
                                    std::cerr << "[Metal] Failed to read pixels from GPU surface" << std::endl;
                                }
                            } else {
                                std::cerr << "[Metal] Screenshot failed: no active surface" << std::endl;
                            }
#endif
                        } else {
                            // Animation mode (CPU): capture from threaded renderer
                            if (threadedRendererPtr) {
                                screenshotSuccess = threadedRendererPtr->getFrameForScreenshot(
                                    screenshotPixels, screenshotWidth, screenshotHeight);
                            }
                        }

                        if (screenshotSuccess) {
                            std::string filename = generateScreenshotFilename(screenshotWidth, screenshotHeight);
                            if (saveScreenshotPPM(screenshotPixels, screenshotWidth, screenshotHeight, filename)) {
                                if (!g_jsonOutput) {
                                    std::cout << "Screenshot saved: " << filename << std::endl;
                                }
                            }
                        } else {
                            if (!g_jsonOutput) {
                                std::cerr << "Screenshot failed: no frame available" << std::endl;
                            }
                        }
                        skipStatsThisFrame = true;  // File I/O can be slow, don't pollute stats
                    } else if (event.key.keysym.sym == SDLK_o) {
                        // Open file dialog to load a new SVG file (hot-reload)
                        // Static storage for the path string (inputPath is const char*)
                        static std::string currentFilePath;
                        std::string newPath = openSVGFileDialog("Open SVG File", "");
                        if (!newPath.empty() && fileExists(newPath.c_str())) {
                            if (!g_jsonOutput) {
                                std::cout << "\n=== Loading new SVG: " << newPath << " ===" << std::endl;
                            }

                            // Stop renderers to safely release SVG resources
                            if (threadedRendererPtr) {
                                threadedRendererPtr->stop();
                            }
                            parallelRenderer.stop();

                            // Load new SVG file using unified loading function
                            SVGLoadError error = loadSVGFile(newPath, inputPath, rawSvgContent, animations, svgDom,
                                                            svgWidth, svgHeight, aspectRatio,
                                                            preBufferTotalDuration, preBufferTotalFrames,
                                                            currentFilePath);

                            if (error != SVGLoadError::Success) {
                                // Handle errors based on type
                                if (error == SVGLoadError::Validation || error == SVGLoadError::Parse) {
                                    // Non-fatal validation/parse errors - restart with old content
                                    std::cerr << "SVG validation/parse error, reverting to previous content" << std::endl;
                                    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                    if (threadedRendererPtr) threadedRendererPtr->start();
                                } else {
                                    // I/O errors (FileSize, FileOpen) - restart with old content
                                    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                    if (threadedRendererPtr) threadedRendererPtr->start();
                                }
                            } else {
                                // Success - reset stats and configure renderers
                                g_animController.resetStats();

                                parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                                                          animations, preBufferTotalDuration, preBufferTotalFrames);
                                parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);

                                if (threadedRendererPtr) {
                                    threadedRendererPtr->configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                    threadedRendererPtr->setTotalAnimationFrames(preBufferTotalFrames);
                                    threadedRendererPtr->initializeDirtyTracking(animations);
                                    threadedRendererPtr->start();
                                }

                                // Reset animation timing state
                                animationStartTime = Clock::now();
                                animationStartTimeSteady = SteadyClock::now();
                                pausedTime = 0;
                                lastRenderedAnimFrame = 0;
                                displayCycles = 0;
                                framesDelivered = 0;
                                framesSkipped = 0;
                                framesRendered = 0;
                                animationPaused = false;

                                // Update window title
                                size_t lastSlash = newPath.find_last_of("/\\");
                                std::string filename = (lastSlash != std::string::npos) ? newPath.substr(lastSlash + 1) : newPath;
                                std::string windowTitle = "SVG Player - " + filename;
                                SDL_SetWindowTitle(window, windowTitle.c_str());

                                std::cout << "Loaded: " << newPath << std::endl;
                                std::cout << "  Dimensions: " << svgWidth << "x" << svgHeight << std::endl;
                                std::cout << "  Animations: " << animations.size() << std::endl;
                                std::cout << "  Duration: " << preBufferTotalDuration << "s, Frames: " << preBufferTotalFrames << std::endl;
                            }
                            skipStatsThisFrame = true;
                        } else if (!newPath.empty()) {
                            std::cerr << "File not found: " << newPath << std::endl;
                        }
                        // else: user cancelled dialog, do nothing
                    } else if (event.key.keysym.sym == SDLK_b) {
                        // Toggle folder browser mode
                        g_browserMode = !g_browserMode;
                        if (g_browserMode) {
                            // Configure browser with viewport dimensions
                            float vh = static_cast<float>(renderHeight) / 100.0f;
                            svgplayer::BrowserConfig config;
                            config.containerWidth = renderWidth;
                            config.containerHeight = renderHeight;
                            config.cellMargin = 2.0f * vh;
                            config.labelHeight = 6.0f * vh;
                            config.headerHeight = 5.0f * vh;
                            config.navBarHeight = 4.0f * vh;
                            config.buttonBarHeight = 6.0f * vh;
                            g_folderBrowser.setConfig(config);

                            // Start thumbnail loader thread (background loading)
                            g_folderBrowser.startThumbnailLoader();

                            // Start async directory scan (non-blocking)
                            char cwd[PATH_MAX];
                            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                                g_browserAsyncScanning = true;
                                g_browserScanProgress.store(0.0f);

                                // Progress callback runs in background thread
                                g_folderBrowser.setDirectoryAsync(cwd,
                                    [](float progress, const std::string& message) -> bool {
                                        g_browserScanProgress.store(progress);
                                        {
                                            std::lock_guard<std::mutex> lock(g_browserScanMessageMutex);
                                            g_browserScanMessage = message;
                                        }
                                        // Update FolderBrowser's internal progress for overlay
                                        g_folderBrowser.setProgress(progress);
                                        return true;  // Continue scanning
                                    });

                                std::cout << "Browser loading (async)..." << std::endl;
                            }
                        } else {
                            // Close browser - stop thumbnail loader and cancel any pending scan
                            g_folderBrowser.stopThumbnailLoader();
                            stopAsyncBrowserDomParse();  // Stop any pending DOM parse
                            g_folderBrowser.cancelScan();
                            g_browserAsyncScanning = false;
                            g_browserSvgDom = nullptr;
                            clearBrowserAnimations();
                            std::cout << "Browser closed" << std::endl;
                        }
                        skipStatsThisFrame = true;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    // Debug: trace mouse motion events even when browser is not ready
                    {
                        static int motionDebugCounter = 0;
                        motionDebugCounter = (motionDebugCounter + 1) % 1000000;  // Prevent overflow
                        if (motionDebugCounter % 120 == 0) {  // Every 2 seconds at 60fps
                            std::cout << "MOTION: browserMode=" << g_browserMode
                                      << ", svgDom=" << (g_browserSvgDom ? "OK" : "NULL") << std::endl;
                        }
                    }
                    if (g_browserMode && g_browserSvgDom) {
                        // Update hover state on mouse movement
                        // Get actual window size (not stale initial values)
                        int actualWinW, actualWinH;
                        SDL_GetWindowSize(window, &actualWinW, &actualWinH);
                        float scaleX = static_cast<float>(renderWidth) / actualWinW;
                        float scaleY = static_cast<float>(renderHeight) / actualWinH;
                        float hoverX = event.motion.x * scaleX;
                        float hoverY = event.motion.y * scaleY;

                        // Debug: trace hover coordinates (every 30 events to catch issues)
                        static int hoverDebugCounter = 0;
                        hoverDebugCounter = (hoverDebugCounter + 1) % 1000000;  // Prevent overflow
                        if (hoverDebugCounter % 30 == 0) {
                            std::cout << "Hover: win(" << event.motion.x << "," << event.motion.y
                                      << ") -> render(" << hoverX << "," << hoverY
                                      << ") scale=" << scaleX << "x" << scaleY
                                      << " hoveredIdx=" << g_folderBrowser.getHoveredIndex() << std::endl;
                        }

                        // Hit test to find what's under the cursor
                        const svgplayer::BrowserEntry* hoveredEntry = nullptr;
                        svgplayer::HitTestResult hitResult = g_folderBrowser.hitTest(hoverX, hoverY, &hoveredEntry, nullptr);

                        // Update hover index based on hit test result
                        int newHoveredIndex = -1;
                        if (hitResult == svgplayer::HitTestResult::Entry && hoveredEntry) {
                            newHoveredIndex = hoveredEntry->gridIndex;
                        }

                        // Only regenerate SVG if hover state changed
                        if (newHoveredIndex != g_folderBrowser.getHoveredIndex()) {
                            g_folderBrowser.setHoveredEntry(newHoveredIndex);
                            std::string browserSvg = g_folderBrowser.generateBrowserSVG();
                            sk_sp<SkData> browserData = SkData::MakeWithCopy(browserSvg.data(), browserSvg.size());
                            std::unique_ptr<SkMemoryStream> browserStream = SkMemoryStream::Make(browserData);
                            sk_sp<SkSVGDOM> newDom = makeSVGDOMWithFontSupport(*browserStream);
                            // Only update if parse succeeded (protect against null DOM)
                            if (newDom) {
                                g_browserSvgDom = std::move(newDom);
                            } else {
                                std::cerr << "ERROR: Failed to parse hover SVG!" << std::endl;
                            }
                        }
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (g_browserMode && event.button.button == SDL_BUTTON_LEFT) {
                        // Handle click in browser mode
                        // Convert screen coordinates (considering HiDPI scaling)
                        int mouseX, mouseY;
                        SDL_GetMouseState(&mouseX, &mouseY);
                        // Get actual window size (not stale initial values)
                        int actualWinW, actualWinH;
                        SDL_GetWindowSize(window, &actualWinW, &actualWinH);
                        float scaleX = static_cast<float>(renderWidth) / actualWinW;
                        float scaleY = static_cast<float>(renderHeight) / actualWinH;
                        float clickX = mouseX * scaleX;
                        float clickY = mouseY * scaleY;

                        // Use new hitTest API with HitTestResult enum
                        const svgplayer::BrowserEntry* clickedEntry = nullptr;
                        std::string clickedBreadcrumbPath;
                        svgplayer::HitTestResult hitResult = g_folderBrowser.hitTest(clickX, clickY, &clickedEntry, &clickedBreadcrumbPath);

                        // Detect double-click (for folder navigation)
                        Uint64 currentTime = SDL_GetTicks64();  // No 49-day wraparound with Uint64
                        bool isDoubleClick = false;
                        int currentClickIndex = clickedEntry ? clickedEntry->gridIndex : -1;
                        if (hitResult == svgplayer::HitTestResult::Entry && currentClickIndex >= 0) {
                            if (currentClickIndex == g_browserLastClickIndex &&
                                (currentTime - g_browserLastClickTime) <= DOUBLE_CLICK_THRESHOLD_MS) {
                                isDoubleClick = true;
                            }
                            g_browserLastClickIndex = currentClickIndex;
                            g_browserLastClickTime = currentTime;
                        }

                        // Lambda to mark browser dirty after any state change (non-blocking)
                        // Render loop handles async SVG regeneration and DOM parsing
                        auto refreshBrowserSVG = [&]() {
                            if (g_browserMode) {
                                g_folderBrowser.markDirty();
                            }
                        };

                        // Shared progress callback for all async navigation
                        auto progressCallback = [](float progress, const std::string& message) -> bool {
                            g_browserScanProgress.store(progress);
                            {
                                std::lock_guard<std::mutex> lock(g_browserScanMessageMutex);
                                g_browserScanMessage = message;
                            }
                            g_folderBrowser.setProgress(progress);
                            return true;
                        };

                        // Setup async state before any navigation
                        auto startAsyncNavigation = []() {
                            g_browserAsyncScanning = true;
                            g_browserScanProgress.store(0.0f);
                        };

                        // Lambda for async navigation to a specific path
                        auto navigateAsync = [&](const std::string& path) {
                            startAsyncNavigation();
                            g_folderBrowser.setDirectoryAsync(path, progressCallback);
                        };

                        // Lambda for async back navigation
                        auto goBackAsync = [&]() {
                            if (!g_folderBrowser.canGoBack()) return;
                            startAsyncNavigation();
                            g_folderBrowser.goBackAsync(progressCallback);
                        };

                        // Lambda for async forward navigation
                        auto goForwardAsync = [&]() {
                            if (!g_folderBrowser.canGoForward()) return;
                            startAsyncNavigation();
                            g_folderBrowser.goForwardAsync(progressCallback);
                        };

                        // Lambda for async parent navigation
                        auto goToParentAsync = [&]() {
                            startAsyncNavigation();
                            g_folderBrowser.goToParentAsync(progressCallback);
                        };

                        // Lambda for async folder entry
                        auto enterFolderAsync = [&](const std::string& folderName) {
                            startAsyncNavigation();
                            g_folderBrowser.enterFolderAsync(folderName, progressCallback);
                        };

                        switch (hitResult) {
                            case svgplayer::HitTestResult::CancelButton:
                                // Cancel button closes browser - proper cleanup
                                g_folderBrowser.stopThumbnailLoader();
                                stopAsyncBrowserDomParse();
                                g_folderBrowser.cancelScan();
                                g_browserAsyncScanning = false;
                                g_browserMode = false;
                                g_browserSvgDom = nullptr;
                                clearBrowserAnimations();
                                g_folderBrowser.clearSelection();
                                std::cout << "Browser cancelled" << std::endl;
                                break;

                            case svgplayer::HitTestResult::LoadButton:
                                // Load button loads selected entry (SVG file or frame folder)
                                if (g_folderBrowser.canLoad()) {
                                    std::optional<svgplayer::BrowserEntry> selected = g_folderBrowser.getSelectedEntry();
                                    if (selected.has_value()) {
                                        // Handle different loadable types
                                        if (selected->type == svgplayer::BrowserEntryType::FrameFolder) {
                                            // Load frame sequence folder
                                            std::cout << "\n=== Loading frame sequence (Load button): " << selected->fullPath << " ===" << std::endl;
                                            g_folderBrowser.stopThumbnailLoader();
                                            stopAsyncBrowserDomParse();
                                            g_folderBrowser.cancelScan();
                                            g_browserAsyncScanning = false;
                                            g_browserMode = false;
                                            g_browserSvgDom = nullptr;
                                            clearBrowserAnimations();

                                            // Stop FBF.SVG renderers before switching to image sequence mode
                                            if (threadedRendererPtr) threadedRendererPtr->stop();
                                            parallelRenderer.stop();
                                            rawSvgContent.clear();
                                            animations.clear();
                                            svgDom = nullptr;

                                            // Enable image sequence mode
                                            isImageSequence = true;
                                            sequentialMode = true;

                                            // Scan folder for SVG frames
                                            sequenceFiles = scanFolderForSVGSequence(selected->fullPath.c_str());
                                            if (sequenceFiles.empty()) {
                                                std::cerr << "Error: No SVG files found in folder: " << selected->fullPath << std::endl;
                                                isImageSequence = false;
                                            } else {
                                                // Pre-load all SVG file contents for fast frame switching
                                                sequenceSvgContents.clear();
                                                sequenceSvgContents.reserve(sequenceFiles.size());
                                                for (const auto& filePath : sequenceFiles) {
                                                    std::ifstream file(filePath);
                                                    if (file) {
                                                        std::stringstream buffer;
                                                        buffer << file.rdbuf();
                                                        sequenceSvgContents.push_back(buffer.str());
                                                    }
                                                }
                                                std::cout << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;

                                                // Reset animation timing state for new sequence
                                                animationStartTime = Clock::now();
                                                animationStartTimeSteady = SteadyClock::now();
                                                pausedTime = 0;
                                                lastRenderedAnimFrame = 0;
                                                displayCycles = 0;
                                                framesDelivered = 0;
                                                framesSkipped = 0;
                                                framesRendered = 0;
                                                animationPaused = false;

                                                // Update window title
                                                size_t lastSlash = selected->fullPath.find_last_of("/\\");
                                                std::string folderName = (lastSlash != std::string::npos) ?
                                                    selected->fullPath.substr(lastSlash + 1) : selected->fullPath;
                                                std::string windowTitle = "SVG Player - " + folderName + " (frames)";
                                                SDL_SetWindowTitle(window, windowTitle.c_str());
                                                std::cout << "Loaded frame sequence: " << selected->fullPath << std::endl;
                                            }
                                        } else if (selected->type == svgplayer::BrowserEntryType::SVGFile ||
                                                   selected->type == svgplayer::BrowserEntryType::FBFSVGFile) {
                                            // Load SVG file (static or animated FBF.SVG)
                                            std::cout << "\n=== Loading from browser (Load button): " << selected->fullPath << " ===" << std::endl;
                                            g_folderBrowser.stopThumbnailLoader();
                                            stopAsyncBrowserDomParse();
                                            g_folderBrowser.cancelScan();
                                            g_browserAsyncScanning = false;
                                            g_browserMode = false;
                                            g_browserSvgDom = nullptr;
                                            clearBrowserAnimations();

                                            static std::string currentFilePathLoad;
                                            std::string newPath = selected->fullPath;
                                            if (!newPath.empty() && fileExists(newPath.c_str())) {
                                                // Stop renderers before loading
                                                if (threadedRendererPtr) threadedRendererPtr->stop();
                                                parallelRenderer.stop();

                                                // Load SVG file using unified loading function
                                                SVGLoadError error = loadSVGFile(newPath, inputPath, rawSvgContent, animations, svgDom,
                                                                                svgWidth, svgHeight, aspectRatio,
                                                                                preBufferTotalDuration, preBufferTotalFrames,
                                                                                currentFilePathLoad);

                                                if (error == SVGLoadError::Success) {
                                                    // Success - reset stats and configure renderers
                                                    g_animController.resetStats();

                                                    parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                                                                              animations, preBufferTotalDuration, preBufferTotalFrames);
                                                    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);

                                                    if (threadedRendererPtr) {
                                                        threadedRendererPtr->configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                                        threadedRendererPtr->setTotalAnimationFrames(preBufferTotalFrames);
                                                        threadedRendererPtr->initializeDirtyTracking(animations);
                                                        threadedRendererPtr->start();
                                                    }

                                                    // Reset animation timing state
                                                    animationStartTime = Clock::now();
                                                    animationStartTimeSteady = SteadyClock::now();
                                                    pausedTime = 0;
                                                    lastRenderedAnimFrame = 0;
                                                    displayCycles = 0;
                                                    framesDelivered = 0;
                                                    framesSkipped = 0;
                                                    framesRendered = 0;
                                                    animationPaused = false;

                                                    // Update window title
                                                    size_t lastSlash = newPath.find_last_of("/\\");
                                                    std::string filename = (lastSlash != std::string::npos) ? newPath.substr(lastSlash + 1) : newPath;
                                                    std::string windowTitle = "SVG Player - " + filename;
                                                    SDL_SetWindowTitle(window, windowTitle.c_str());

                                                    std::cout << "Loaded: " << newPath << std::endl;
                                                    std::cout << "  Dimensions: " << svgWidth << "x" << svgHeight << std::endl;
                                                    std::cout << "  Animations: " << animations.size() << std::endl;
                                                } else {
                                                    // Loading failed - restart with old content if available
                                                    if (svgDom) {
                                                        parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                                        if (threadedRendererPtr) threadedRendererPtr->start();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                break;

                            case svgplayer::HitTestResult::BackButton:
                                // Navigate back in history (async)
                                goBackAsync();
                                break;

                            case svgplayer::HitTestResult::ForwardButton:
                                // Navigate forward in history (async)
                                goForwardAsync();
                                break;

                            case svgplayer::HitTestResult::SortButton:
                                // Toggle sort mode
                                g_folderBrowser.toggleSortMode();
                                refreshBrowserSVG();
                                std::cout << "Browser: sort mode = "
                                          << (g_folderBrowser.getSortMode() == svgplayer::BrowserSortMode::Alphabetical ? "A-Z" : "Date")
                                          << std::endl;
                                break;

                            case svgplayer::HitTestResult::PrevPage:
                                // Previous page
                                std::cout << "Browser: prev page clicked (page " << g_folderBrowser.getCurrentPage() << " -> " << (g_folderBrowser.getCurrentPage() - 1) << ")" << std::endl;
                                g_folderBrowser.prevPage();
                                refreshBrowserSVG();
                                break;

                            case svgplayer::HitTestResult::NextPage:
                                // Next page
                                std::cout << "Browser: next page clicked (page " << g_folderBrowser.getCurrentPage() << " -> " << (g_folderBrowser.getCurrentPage() + 1) << ")" << std::endl;
                                g_folderBrowser.nextPage();
                                refreshBrowserSVG();
                                break;

                            case svgplayer::HitTestResult::Breadcrumb:
                                // Navigate to clicked breadcrumb path segment (async)
                                if (!clickedBreadcrumbPath.empty()) {
                                    navigateAsync(clickedBreadcrumbPath);
                                }
                                break;

                            case svgplayer::HitTestResult::Entry:
                                // Handle entry click (with selection and double-click logic)
                                if (clickedEntry) {
                                    // Trigger click feedback flash animation
                                    g_folderBrowser.triggerClickFeedback(clickedEntry->gridIndex);
                                    refreshBrowserSVG();

                                    switch (clickedEntry->type) {
                                        case svgplayer::BrowserEntryType::ParentDir:
                                            // Single click on ".." always navigates up (async)
                                            goToParentAsync();
                                            break;

                                        case svgplayer::BrowserEntryType::Volume:
                                            // Single click on volume navigates to it (async)
                                            navigateAsync(clickedEntry->fullPath);
                                            break;

                                        case svgplayer::BrowserEntryType::Folder:
                                            if (isDoubleClick) {
                                                // Double-click enters folder (async)
                                                enterFolderAsync(clickedEntry->name);
                                            } else {
                                                // Single click selects folder (for viewing path, etc.)
                                                g_folderBrowser.selectEntry(clickedEntry->gridIndex);
                                                refreshBrowserSVG();
                                            }
                                            break;

                                        case svgplayer::BrowserEntryType::FrameFolder:
                                            if (isDoubleClick) {
                                                // Double-click loads the frame sequence folder
                                                std::cout << "\n=== Loading frame sequence from browser: " << clickedEntry->fullPath << " ===" << std::endl;
                                            } else {
                                                // Single click selects the frame folder
                                                g_folderBrowser.selectEntry(clickedEntry->gridIndex);
                                                refreshBrowserSVG();
                                                break;
                                            }
                                            // Load the frame folder as image sequence (reached via double-click fall-through)
                                            {
                                                // Proper browser cleanup before loading
                                                g_folderBrowser.stopThumbnailLoader();
                                                stopAsyncBrowserDomParse();
                                                g_folderBrowser.cancelScan();
                                                g_browserAsyncScanning = false;
                                                g_browserMode = false;
                                                g_browserSvgDom = nullptr;
                                                clearBrowserAnimations();

                                                // Stop FBF.SVG renderers before switching to image sequence mode
                                                if (threadedRendererPtr) threadedRendererPtr->stop();
                                                parallelRenderer.stop();

                                                // Clear FBF.SVG mode state
                                                rawSvgContent.clear();
                                                animations.clear();
                                                svgDom = nullptr;

                                                // Enable image sequence mode
                                                isImageSequence = true;
                                                sequentialMode = true;

                                                // Scan folder for SVG frames
                                                sequenceFiles = scanFolderForSVGSequence(clickedEntry->fullPath.c_str());
                                                if (sequenceFiles.empty()) {
                                                    std::cerr << "Error: No SVG files found in folder: " << clickedEntry->fullPath << std::endl;
                                                    isImageSequence = false;
                                                } else {
                                                    // Pre-load all SVG file contents for fast frame switching
                                                    sequenceSvgContents.clear();
                                                    sequenceSvgContents.reserve(sequenceFiles.size());
                                                    for (const auto& filePath : sequenceFiles) {
                                                        std::ifstream file(filePath);
                                                        if (file) {
                                                            std::stringstream buffer;
                                                            buffer << file.rdbuf();
                                                            sequenceSvgContents.push_back(buffer.str());
                                                        }
                                                    }
                                                    std::cout << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;

                                                    // Reset animation timing state for new sequence
                                                    animationStartTime = Clock::now();
                                                    animationStartTimeSteady = SteadyClock::now();
                                                    pausedTime = 0;
                                                    lastRenderedAnimFrame = 0;
                                                    displayCycles = 0;
                                                    framesDelivered = 0;
                                                    framesSkipped = 0;
                                                    framesRendered = 0;
                                                    animationPaused = false;

                                                    // Update window title
                                                    size_t lastSlash = clickedEntry->fullPath.find_last_of("/\\");
                                                    std::string folderName = (lastSlash != std::string::npos) ?
                                                        clickedEntry->fullPath.substr(lastSlash + 1) : clickedEntry->fullPath;
                                                    std::string windowTitle = "SVG Player - " + folderName + " (frames)";
                                                    SDL_SetWindowTitle(window, windowTitle.c_str());

                                                    std::cout << "Loaded frame sequence: " << clickedEntry->fullPath << std::endl;
                                                }
                                            }
                                            break;

                                        case svgplayer::BrowserEntryType::FBFSVGFile:
                                            // FBFSVGFile behaves same as SVGFile (animated FBF.SVG)
                                            if (isDoubleClick) {
                                                std::cout << "\n=== Loading FBF.SVG from browser: " << clickedEntry->fullPath << " ===" << std::endl;
                                            } else {
                                                g_folderBrowser.selectEntry(clickedEntry->gridIndex);
                                                refreshBrowserSVG();
                                                break;
                                            }
                                            // Fall through to SVGFile loading code
                                            [[fallthrough]];

                                        case svgplayer::BrowserEntryType::SVGFile:
                                            if (isDoubleClick) {
                                                // Double-click loads the SVG file directly
                                                std::cout << "\n=== Loading from browser: " << clickedEntry->fullPath << " ===" << std::endl;
                                                // Fall through to load code below
                                            } else {
                                                // Single click selects the SVG file
                                                g_folderBrowser.selectEntry(clickedEntry->gridIndex);
                                                refreshBrowserSVG();
                                                break;
                                            }
                                            // Load the selected SVG file (reached via double-click fall-through)
                                            {
                                                // Proper browser cleanup before loading
                                                g_folderBrowser.stopThumbnailLoader();
                                                stopAsyncBrowserDomParse();
                                                g_folderBrowser.cancelScan();
                                                g_browserAsyncScanning = false;
                                                g_browserMode = false;
                                                g_browserSvgDom = nullptr;
                                                clearBrowserAnimations();

                                                static std::string currentFilePath;
                                                std::string newPath = clickedEntry->fullPath;
                                                if (!newPath.empty() && fileExists(newPath.c_str())) {
                                                    // Stop renderers before loading
                                                    if (threadedRendererPtr) threadedRendererPtr->stop();
                                                    parallelRenderer.stop();

                                                    // Load SVG file using unified loading function
                                                    SVGLoadError error = loadSVGFile(newPath, inputPath, rawSvgContent, animations, svgDom,
                                                                                    svgWidth, svgHeight, aspectRatio,
                                                                                    preBufferTotalDuration, preBufferTotalFrames,
                                                                                    currentFilePath);

                                                    if (error == SVGLoadError::Success) {
                                                        // Success - reset stats and configure renderers
                                                        g_animController.resetStats();

                                                        parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                                                                                  animations, preBufferTotalDuration, preBufferTotalFrames);
                                                        parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);

                                                        if (threadedRendererPtr) {
                                                            threadedRendererPtr->configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                                            threadedRendererPtr->setTotalAnimationFrames(preBufferTotalFrames);
                                                            threadedRendererPtr->initializeDirtyTracking(animations);
                                                            threadedRendererPtr->start();
                                                        }

                                                        // Reset animation timing state
                                                        animationStartTime = Clock::now();
                                                        animationStartTimeSteady = SteadyClock::now();
                                                        pausedTime = 0;
                                                        lastRenderedAnimFrame = 0;
                                                        displayCycles = 0;
                                                        framesDelivered = 0;
                                                        framesSkipped = 0;
                                                        framesRendered = 0;
                                                        animationPaused = false;

                                                        // Update window title
                                                        size_t lastSlash = newPath.find_last_of("/\\");
                                                        std::string filename = (lastSlash != std::string::npos) ? newPath.substr(lastSlash + 1) : newPath;
                                                        std::string windowTitle = "SVG Player - " + filename;
                                                        SDL_SetWindowTitle(window, windowTitle.c_str());

                                                        std::cout << "Loaded: " << newPath << std::endl;
                                                        std::cout << "  Dimensions: " << svgWidth << "x" << svgHeight << std::endl;
                                                        std::cout << "  Animations: " << animations.size() << std::endl;
                                                    } else {
                                                        // Loading failed - restart with old content if available
                                                        if (svgDom) {
                                                            parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                                            if (threadedRendererPtr) threadedRendererPtr->start();
                                                        }
                                                    }
                                                }
                                            }
                                            break;
                                    }  // end inner switch
                                }  // end if clickedEntry
                                break;

                            case svgplayer::HitTestResult::PlayArrowEntry:
                                // Play arrow clicked on a FrameFolder - play it as image sequence
                                if (clickedEntry && clickedEntry->type == svgplayer::BrowserEntryType::FrameFolder) {
                                    std::cout << "\n=== Playing frame sequence (play arrow): " << clickedEntry->fullPath << " ===" << std::endl;

                                    // Proper browser cleanup before loading
                                    g_folderBrowser.stopThumbnailLoader();
                                    stopAsyncBrowserDomParse();
                                    g_folderBrowser.cancelScan();
                                    g_browserAsyncScanning = false;
                                    g_browserMode = false;
                                    g_browserSvgDom = nullptr;
                                    clearBrowserAnimations();

                                    // Stop FBF.SVG renderers before switching to image sequence mode
                                    if (threadedRendererPtr) threadedRendererPtr->stop();
                                    parallelRenderer.stop();

                                    // Clear FBF.SVG mode state
                                    rawSvgContent.clear();
                                    animations.clear();
                                    svgDom = nullptr;

                                    // Enable image sequence mode
                                    isImageSequence = true;
                                    sequentialMode = true;

                                    // Scan folder for SVG frames
                                    sequenceFiles = scanFolderForSVGSequence(clickedEntry->fullPath.c_str());
                                    if (sequenceFiles.empty()) {
                                        std::cerr << "Error: No SVG files found in folder: " << clickedEntry->fullPath << std::endl;
                                        isImageSequence = false;
                                    } else {
                                        // Pre-load all SVG file contents for fast frame switching
                                        sequenceSvgContents.clear();
                                        sequenceSvgContents.reserve(sequenceFiles.size());
                                        for (const auto& filePath : sequenceFiles) {
                                            std::ifstream file(filePath);
                                            if (file) {
                                                std::stringstream buffer;
                                                buffer << file.rdbuf();
                                                sequenceSvgContents.push_back(buffer.str());
                                            }
                                        }
                                        std::cout << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;

                                        // Reset animation timing state for new sequence
                                        animationStartTime = Clock::now();
                                        animationStartTimeSteady = SteadyClock::now();
                                        pausedTime = 0;
                                        lastRenderedAnimFrame = 0;
                                        displayCycles = 0;
                                        framesDelivered = 0;
                                        framesSkipped = 0;
                                        framesRendered = 0;
                                        animationPaused = false;

                                        // Update window title
                                        size_t lastSlash = clickedEntry->fullPath.find_last_of("/\\");
                                        std::string folderName = (lastSlash != std::string::npos) ?
                                            clickedEntry->fullPath.substr(lastSlash + 1) : clickedEntry->fullPath;
                                        std::string windowTitle = "SVG Player - " + folderName + " (frames)";
                                        SDL_SetWindowTitle(window, windowTitle.c_str());

                                        std::cout << "Loaded frame sequence: " << clickedEntry->fullPath << std::endl;
                                    }
                                }
                                break;

                            case svgplayer::HitTestResult::None:
                                // Clicked on empty space - clear selection
                                g_folderBrowser.clearSelection();
                                refreshBrowserSVG();
                                break;
                        }  // end switch hitResult
                        skipStatsThisFrame = true;
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        // Get actual renderer output size (HiDPI aware)
                        int actualW, actualH;
#ifdef __APPLE__
                        if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                            // Graphite mode: use SDL_Metal_GetDrawableSize for HiDPI
                            SDL_Metal_GetDrawableSize(window, &actualW, &actualH);
                        } else if (useMetalBackend && metalContext && metalContext->isInitialized()) {
                            // Ganesh Metal mode: use SDL_Metal_GetDrawableSize for HiDPI
                            SDL_Metal_GetDrawableSize(window, &actualW, &actualH);
                        } else
#endif
                        {
                            SDL_GetRendererOutputSize(renderer, &actualW, &actualH);
                        }

                        // Use full output size - SVG's preserveAspectRatio handles centering
                        // This ensures debug overlay at (0,0) is truly at top-left of window
                        renderWidth = actualW;
                        renderHeight = actualH;

                        // Update SDL texture for CPU rendering mode only
                        // GPU backends (Graphite, Metal Ganesh) manage their own surfaces
#ifdef __APPLE__
                        if (!useGraphiteBackend && !useMetalBackend) {
#endif
                            SDL_DestroyTexture(texture);
                            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                        renderWidth, renderHeight);
#ifdef __APPLE__
                        }
#endif

                        // Update GPU backend drawable sizes
#ifdef __APPLE__
                        if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                            graphiteContext->updateDrawableSize(renderWidth, renderHeight);
                        } else if (useMetalBackend && metalContext && metalContext->isInitialized()) {
                            metalContext->updateDrawableSize(renderWidth, renderHeight);
                        } else
#endif
                        {
                            createSurface(renderWidth, renderHeight);
                        }

                        // Resize threaded renderer buffers (non-blocking)
                        if (threadedRendererPtr) {
                            threadedRendererPtr->resize(renderWidth, renderHeight);
                        }

                        // Resize parallel renderer - clears pre-buffered frames at old size
                        parallelRenderer.resize(renderWidth, renderHeight);

                        // Update browser if in browser mode (real-time resize)
                        if (g_browserMode) {
                            // Use viewport-height-based sizing (vh units)
                            // Each dimension is a fixed percentage of viewport height
                            // This ensures consistent proportions at any resolution
                            float vh = static_cast<float>(renderHeight) / 100.0f;

                            svgplayer::BrowserConfig config = g_folderBrowser.getConfig();
                            config.containerWidth = renderWidth;
                            config.containerHeight = renderHeight;
                            config.cellMargin = 2.0f * vh;        // 2vh
                            config.labelHeight = 6.0f * vh;       // 6vh - label area
                            config.headerHeight = 5.0f * vh;      // 5vh - breadcrumbs
                            config.navBarHeight = 4.0f * vh;      // 4vh - nav bar
                            config.buttonBarHeight = 6.0f * vh;   // 6vh - buttons
                            g_folderBrowser.setConfig(config);
                            // Mark dirty - render loop will regenerate and async parse
                            g_folderBrowser.markDirty();
                        }
                    }
                    break;
            }
        }
        auto eventEnd = Clock::now();
        DurationMs eventTime = eventEnd - eventStart;

        if (!running) break;

        // === UPDATE ANIMATIONS (SMIL-compliant time-based) ===
        // The animation frame is determined SOLELY by the current time
        // This guarantees perfect sync even if rendering is slow
        auto animStart = Clock::now();

        // IMAGE SEQUENCE MODE: Calculate frame index before animations loop
        // Image sequences don't have SMIL animations, so we handle them separately
        if (isImageSequence && !sequenceSvgContents.empty()) {
            // Image sequence mode: use sequential counter-based frame index
            size_t totalFrames = sequenceSvgContents.size();
            currentFrameIndex = sequentialFrameCounter % totalFrames;
            sequentialFrameCounter++;
            // Update frame tracking for stats
            if (currentFrameIndex != lastRenderedAnimFrame) {
                lastRenderedAnimFrame = currentFrameIndex;
                framesRendered++;
            }
        }

        for (const auto& anim : animations) {
            std::string newValue = anim.getCurrentValue(animTime);

            // SMIL-compliant time-based frame calculation (consistent across modes)
            // Both PreBuffer and Direct modes use the same time-based formula
            // In Metal mode, threadedRendererPtr is null - use direct mode calculation
            //
            // SEQUENTIAL MODE: When enabled, ignore wall-clock timing entirely.
            // Instead, advance frames 0,1,2,3... as fast as possible for benchmarking.
            // Skip if image sequence mode (already handled above)
            if (isImageSequence) {
                // Skip SMIL animation processing for image sequences
                continue;
            }
            if (sequentialMode) {
                // Sequential mode: use counter-based frame index (ignores SMIL timing)
                // This renders frames in order as fast as the renderer can go
                size_t totalFrames = preBufferTotalFrames > 0 ? preBufferTotalFrames : anim.values.size();
                currentFrameIndex = sequentialFrameCounter % totalFrames;
                // Increment counter for next cycle (wraps around automatically via modulo)
                sequentialFrameCounter++;
            } else if (threadedRendererPtr && threadedRendererPtr->isPreBufferMode() && preBufferTotalDuration > 0) {
                // PreBuffer mode: calculate GLOBAL frame index from time ratio
                // This MUST match parallelRenderer's frame calculation:
                // framePtr->elapsedTimeSeconds = (frameIndex / totalFrameCount) * totalDuration
                // Inverting: frameIndex = floor((elapsedTime / totalDuration) * totalFrameCount)
                double timeRatio = animTime / preBufferTotalDuration;
                // Wrap around for looping animations (use fmod for better precision)
                timeRatio = std::fmod(timeRatio, 1.0);
                currentFrameIndex = static_cast<size_t>(std::floor(timeRatio * preBufferTotalFrames));
                // Clamp to valid range
                if (currentFrameIndex >= preBufferTotalFrames) {
                    currentFrameIndex = preBufferTotalFrames - 1;
                }
            } else {
                // Direct mode: use same time-based calculation as PreBuffer
                // getCurrentFrameIndex() internally uses time-based calculation consistent with above
                currentFrameIndex = anim.getCurrentFrameIndex(animTime);
            }
            lastFrameValue = newValue;

            // Track frame skips (for sync verification)
            if (currentFrameIndex != lastRenderedAnimFrame) {
                size_t expectedNext = (lastRenderedAnimFrame + 1) % anim.values.size();
                // Only check for skips after we've rendered at least one frame (avoids false positives on first frame)
                if (currentFrameIndex != expectedNext && framesRendered > 0) {
                    // We skipped one or more animation frames
                    size_t skipped = 0;
                    if (currentFrameIndex > lastRenderedAnimFrame) {
                        skipped = currentFrameIndex - lastRenderedAnimFrame - 1;
                    } else {
                        // Wrapped around
                        skipped = (anim.values.size() - lastRenderedAnimFrame - 1) + currentFrameIndex;
                    }
                    framesSkipped += skipped;
                }
                lastRenderedAnimFrame = currentFrameIndex;
                framesRendered++;
            }

            // FREEZE DETECTION: Monitor for stuck animation
            // Warning at 2s, FATAL EXIT at 5s with stacktrace
            static constexpr double FREEZE_WARN_THRESHOLD = 2.0;   // Seconds before warning
            static constexpr double FREEZE_FATAL_THRESHOLD = 5.0;  // Seconds before fatal exit
            static size_t lastMonitoredFrameIndex = SIZE_MAX;
            static auto lastFrameChangeTime = Clock::now();
            static bool freezeWarningLogged = false;

            if (currentFrameIndex != lastMonitoredFrameIndex) {
                // Frame advanced - reset monitoring state
                lastMonitoredFrameIndex = currentFrameIndex;
                lastFrameChangeTime = Clock::now();
                freezeWarningLogged = false;
            } else if (!animationPaused) {
                // Frame hasn't changed - check if stuck for too long
                auto timeSinceChange = std::chrono::duration<double>(Clock::now() - lastFrameChangeTime).count();

                // FATAL FREEZE: Exit with error and stacktrace after 5 seconds
                // Skip freeze detection in Metal mode (no threaded renderer)
                if (timeSinceChange > FREEZE_FATAL_THRESHOLD && threadedRendererPtr) {
                    float pct = (preBufferTotalFrames > 0) ?
                        (static_cast<float>(currentFrameIndex) / preBufferTotalFrames * 100.0f) : 0.0f;
                    std::cerr << "\n[FATAL FREEZE] Animation completely stuck at frame "
                              << currentFrameIndex << "/" << preBufferTotalFrames
                              << " (" << std::fixed << std::setprecision(1) << pct << "%)"
                              << " for " << std::setprecision(1) << timeSinceChange << "s"
                              << " - PreBuffer=" << threadedRendererPtr->isPreBufferMode() << std::endl;
                    std::cerr << "ThreadedRenderer state: running=" << threadedRendererPtr->running.load()
                              << ", timeouts=" << threadedRendererPtr->timeoutCount.load()
                              << ", dropped=" << threadedRendererPtr->droppedFrames.load() << std::endl;
                    printStackTrace("FATAL FREEZE - Animation stuck");
                    std::exit(EXIT_FAILURE);  // Exit with error code
                }

                // Warning at 2 seconds (log once)
                if (timeSinceChange > FREEZE_WARN_THRESHOLD && !freezeWarningLogged) {
                    freezeWarningLogged = true;
                    float pct = (preBufferTotalFrames > 0) ?
                        (static_cast<float>(currentFrameIndex) / preBufferTotalFrames * 100.0f) : 0.0f;
                    std::cerr << "[FREEZE WARNING] Animation stuck at frame " << currentFrameIndex << "/" << preBufferTotalFrames
                              << " (" << std::fixed << std::setprecision(1) << pct << "%)"
                              << " for " << std::setprecision(1) << timeSinceChange << "s"
                              << " - will exit in " << (FREEZE_FATAL_THRESHOLD - timeSinceChange) << "s if not resolved"
                              << std::endl;
                }
            }

            // Update animation state in ThreadedRenderer (non-blocking)
            // Render thread will apply this to its own DOM
            // In Metal mode, animation state is applied directly during rendering
            if (threadedRendererPtr) {
                threadedRendererPtr->setAnimationState(anim.targetId, anim.attributeName, newValue);
            }
        }
        auto animEnd = Clock::now();
        DurationMs animTime_ms = animEnd - animStart;

        // Update frame tracking for dirty region optimization
        // This tracks which animations changed frame for partial rendering
        g_animController.updateFrameTracking(animTime);
        if (threadedRendererPtr) {
            threadedRendererPtr->setFrameChanges(g_animController.getFrameChanges());
        }

        // === STRESS TEST: Artificial delay to prove sync works ===
        if (stressTestEnabled) {
            // Sleep 50ms to simulate heavy load
            // The animation should SKIP frames but stay perfectly synchronized
            SDL_Delay(50);
        }

        // === FETCH FRAME FROM THREADED RENDERER (NON-BLOCKING!) ===
        // Main thread NEVER blocks on rendering - always responsive to input
        auto fetchStart = Clock::now();

        // Get canvas - for Metal mode, surface is created per-frame in the render loop below
        // For CPU mode, surface was created at startup
        SkCanvas* canvas = nullptr;
#ifdef __APPLE__
        if (!useMetalBackend) {
            canvas = surface->getCanvas();
        }
        // Metal mode: canvas is set after creating the per-frame surface below
#else
        canvas = surface->getCanvas();
#endif
        bool gotNewFrame = false;

        // === BROWSER MODE: Render folder browser instead of animation ===
        if (g_browserMode) {
#ifdef __APPLE__
            // Metal mode: Create per-frame surface for browser rendering
            if (useMetalBackend && metalContext && metalContext->isInitialized()) {
                metalDrawable = nullptr;
                surface = metalContext->createSurface(renderWidth, renderHeight, &metalDrawable);
                if (surface && metalDrawable) {
                    canvas = surface->getCanvas();
                } else {
                    // Failed to get Metal drawable, skip this frame
                    if (!g_jsonOutput) {
                        std::cerr << "[Metal Browser] Failed to acquire drawable" << std::endl;
                    }
                }
            }
#endif
            // Check if async scan completed - finalize results on main thread
            if (g_browserAsyncScanning && g_folderBrowser.pollScanComplete()) {
                g_folderBrowser.finalizeScan();
                g_browserAsyncScanning = false;
                g_folderBrowser.markDirty();  // Trigger SVG regeneration

                std::cout << "Browser opened: " << g_folderBrowser.getCurrentDirectory() << std::endl;
                std::cout << "Browser entries: " << g_folderBrowser.getCurrentPageEntries().size() << std::endl;
            }

            // Note: hasNewReadyThumbnails() is consumed inside regenerateBrowserSVGIfNeeded()
            // which returns true if browser SVG was regenerated. The atomic flag is checked
            // and cleared atomically within that function - don't duplicate it here to avoid
            // consuming the atomic flag twice

            // Update click feedback animation (decays each frame)
            if (g_folderBrowser.hasClickFeedback()) {
                g_folderBrowser.updateClickFeedback();
                g_folderBrowser.markDirty();  // Click feedback changed
            }

            // Check if async DOM parse completed - swap in new DOM (NON-BLOCKING!)
            if (trySwapBrowserDom()) {
                // Read animation count under lock
                size_t animCount = 0;
                {
                    std::lock_guard<std::mutex> lock(g_browserAnimMutex);
                    animCount = g_browserAnimations.size();
                }
                std::cout << "Browser SVG parsed (async), entries="
                          << g_folderBrowser.getCurrentPageEntries().size()
                          << ", animations=" << animCount << std::endl;
            }

            // Regenerate browser SVG only when dirty (avoids 60fps regeneration)
            if (g_folderBrowser.regenerateBrowserSVGIfNeeded()) {
                // Start async DOM parse (NON-BLOCKING! main thread stays responsive)
                const std::string& browserSvg = g_folderBrowser.getCachedBrowserSVG();
                std::cout << "Browser SVG regenerated, size=" << browserSvg.size()
                          << ", starting async parse..." << std::endl;
                startAsyncBrowserDomParse(browserSvg);
            }

            // Render current DOM (may be stale if new one is parsing - that's OK!)
            // NULL CHECK: canvas may be nullptr if Metal failed to acquire drawable
            if (g_browserSvgDom && canvas) {
                canvas->clear(SK_ColorBLACK);
                SkSize browserSize = SkSize::Make(renderWidth, renderHeight);
                g_browserSvgDom->setContainerSize(browserSize);

                // Apply animation states to DOM before rendering (LIVE ANIMATED GRID)
                // Each cell's SMIL animations are applied based on elapsed time
                // Copy animation data under short lock, then apply without holding mutex
                std::vector<svgplayer::SMILAnimation> animSnapshot;
                std::chrono::steady_clock::time_point animStartSnapshot;
                {
                    std::lock_guard<std::mutex> lock(g_browserAnimMutex);
                    if (!g_browserAnimations.empty()) {
                        animSnapshot = g_browserAnimations;  // Fast copy of vector metadata
                        animStartSnapshot = g_browserAnimStartTime;
                    }
                }

                // Apply animations without holding mutex (DOM ops can be slow)
                if (!animSnapshot.empty()) {
                    // Skip animation updates when paused - values haven't changed
                    if (!animationPaused) {
                        auto now = std::chrono::steady_clock::now();
                        double elapsedSeconds = std::chrono::duration<double>(now - animStartSnapshot).count();

                        for (const auto& anim : animSnapshot) {
                            if (!anim.targetId.empty() && !anim.attributeName.empty() && !anim.values.empty()) {
                                std::string value = anim.getCurrentValue(elapsedSeconds);
                                sk_sp<SkSVGNode>* nodePtr = g_browserSvgDom->findNodeById(anim.targetId.c_str());
                                if (nodePtr && *nodePtr) {
                                    (*nodePtr)->setAttribute(anim.attributeName.c_str(), value.c_str());
                                }
                            }
                        }
                    }
                }

                g_browserSvgDom->render(canvas);
                gotNewFrame = true;
            } else if ((g_browserAsyncScanning || g_browserDomParsing.load()) && canvas) {
                // No DOM yet but parsing - show loading placeholder with progress bar
                // NULL CHECK: canvas may be nullptr if Metal failed to acquire drawable
                canvas->clear(SkColorSetARGB(255, 26, 26, 46));  // Dark browser bg

                // Get current progress (atomic read from scan thread)
                float progress = g_browserScanProgress.load();
                std::string progressMsg;
                {
                    std::lock_guard<std::mutex> lock(g_browserScanMessageMutex);
                    progressMsg = g_browserScanMessage;
                }

                // Draw centered progress bar
                float barWidth = renderWidth * 0.6f;
                float barHeight = 20.0f;
                float barX = (renderWidth - barWidth) / 2.0f;
                float barY = renderHeight / 2.0f;

                // Progress bar background (dark gray)
                SkPaint bgPaint;
                bgPaint.setColor(SkColorSetARGB(255, 60, 60, 80));
                bgPaint.setStyle(SkPaint::kFill_Style);
                canvas->drawRect(SkRect::MakeXYWH(barX, barY, barWidth, barHeight), bgPaint);

                // Progress bar fill (cyan gradient feel)
                SkPaint fillPaint;
                fillPaint.setColor(SkColorSetARGB(255, 0, 200, 220));
                fillPaint.setStyle(SkPaint::kFill_Style);
                float fillWidth = barWidth * (progress / 100.0f);
                if (fillWidth > 0) {
                    canvas->drawRect(SkRect::MakeXYWH(barX, barY, fillWidth, barHeight), fillPaint);
                }

                // Progress bar border
                SkPaint borderPaint;
                borderPaint.setColor(SkColorSetARGB(255, 100, 100, 120));
                borderPaint.setStyle(SkPaint::kStroke_Style);
                borderPaint.setStrokeWidth(2.0f);
                canvas->drawRect(SkRect::MakeXYWH(barX, barY, barWidth, barHeight), borderPaint);

                // "Loading..." text above progress bar
                SkPaint textPaint;
                textPaint.setColor(SK_ColorWHITE);
                textPaint.setAntiAlias(true);
                SkFont font(nullptr, 24.0f);
                std::string loadingText = "Loading folder...";
                canvas->drawString(loadingText.c_str(), barX, barY - 30.0f, font, textPaint);

                // Percentage text below progress bar
                char percentText[32];
                snprintf(percentText, sizeof(percentText), "%.0f%%", progress);
                canvas->drawString(percentText, barX + barWidth / 2.0f - 20.0f, barY + barHeight + 30.0f, font, textPaint);

                // Status message below percentage
                if (!progressMsg.empty()) {
                    SkFont smallFont(nullptr, 16.0f);
                    canvas->drawString(progressMsg.c_str(), barX, barY + barHeight + 60.0f, smallFont, textPaint);
                }

                // Print progress to console (throttled)
                static int lastPrintedProgress = -1;
                int currentProgress = static_cast<int>(progress);
                if (currentProgress != lastPrintedProgress && currentProgress % 10 == 0) {
                    std::cout << "Progress: " << currentProgress << "%" << std::endl;
                    lastPrintedProgress = currentProgress;
                }

                gotNewFrame = true;
            }
        } else {
            // === NORMAL SVG RENDERING MODE ===
#ifdef __APPLE__
            if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                // === GRAPHITE GPU RENDERING PATH (Next-gen) ===
                // Graphite uses Recorder/Recording model - simpler than Ganesh
                if (std::getenv("RENDER_DEBUG")) {
                    std::cerr << "[GRAPHITE_RENDER_DEBUG] Starting frame render" << std::endl;
                    std::cerr << "[GRAPHITE_RENDER_DEBUG] renderWidth=" << renderWidth << ", renderHeight=" << renderHeight << std::endl;
                }
                surface = graphiteContext->createSurface(renderWidth, renderHeight);

                if (surface) {
                    // DIAGNOSTIC: Verify surface dimensions match render dimensions
                    int surfaceW = surface->width();
                    int surfaceH = surface->height();
                    if (surfaceW != renderWidth || surfaceH != renderHeight) {
                        std::cerr << "[Graphite] CRITICAL: Surface dimensions mismatch!" << std::endl;
                        std::cerr << "[Graphite]   Expected: " << renderWidth << "x" << renderHeight << std::endl;
                        std::cerr << "[Graphite]   Actual:   " << surfaceW << "x" << surfaceH << std::endl;
                        std::cerr << "[Graphite]   This causes 1/4 screen rendering!" << std::endl;
                        // Force exit to alert user of the bug
                        exit(1);
                    }

                    canvas = surface->getCanvas();
                    canvas->clear(SK_ColorBLACK);

                    // Calculate scale to fit SVG in render area (same as Linux player)
                    // This ensures SVG content fills the full HiDPI resolution
                    float scale = std::min(static_cast<float>(renderWidth) / svgWidth,
                                          static_cast<float>(renderHeight) / svgHeight);
                    float offsetX = (renderWidth - svgWidth * scale) / 2.0f;
                    float offsetY = (renderHeight - svgHeight * scale) / 2.0f;

                    if (std::getenv("RENDER_DEBUG")) {
                        std::cerr << "[GRAPHITE_RENDER_DEBUG] Scale=" << scale
                                  << ", offset=(" << offsetX << "," << offsetY << ")"
                                  << ", svgSize=" << svgWidth << "x" << svgHeight << std::endl;
                    }

                    // IMAGE SEQUENCE MODE: Parse and render different SVG files per frame
                    if (isImageSequence && !sequenceSvgContents.empty()) {
                        size_t frameIdx = currentFrameIndex % sequenceSvgContents.size();
                        const std::string& svgContent = sequenceSvgContents[frameIdx];

                        sk_sp<SkData> frameData = SkData::MakeWithCopy(svgContent.data(), svgContent.size());
                        auto frameStream = SkMemoryStream::Make(frameData);
                        if (frameStream) {
                            sk_sp<SkSVGDOM> frameDom = makeSVGDOMWithFontSupport(*frameStream);
                            if (frameDom) {
                                // Apply scale transform to fill HiDPI canvas
                                canvas->save();
                                canvas->translate(offsetX, offsetY);
                                canvas->scale(scale, scale);
                                SkSize containerSize = SkSize::Make(svgWidth, svgHeight);
                                frameDom->setContainerSize(containerSize);
                                frameDom->render(canvas);
                                canvas->restore();
                            }
                        }
                    } else {
                        // FBF.SVG MODE: Apply SMIL animations to single DOM
                        if (svgDom && !animations.empty()) {
                            for (const auto& anim : animations) {
                                if (!anim.targetId.empty() && !anim.attributeName.empty() && !anim.values.empty()) {
                                    std::string value = anim.getCurrentValue(animTime);
                                    sk_sp<SkSVGNode>* nodePtr = svgDom->findNodeById(anim.targetId.c_str());
                                    if (nodePtr && *nodePtr) {
                                        (*nodePtr)->setAttribute(anim.attributeName.c_str(), value.c_str());
                                    }
                                }
                            }
                        }

                        // Render SVG directly to Graphite-backed surface
                        // Apply scale transform to fill HiDPI canvas
                        if (svgDom) {
                            canvas->save();
                            canvas->translate(offsetX, offsetY);
                            canvas->scale(scale, scale);
                            SkSize containerSize = SkSize::Make(svgWidth, svgHeight);
                            svgDom->setContainerSize(containerSize);
                            svgDom->render(canvas);
                            canvas->restore();
                        }
                    }

                    // Submit and present via Graphite
                    graphiteContext->submitFrame();
                    gotNewFrame = true;
                    framesDelivered++;
                    if (std::getenv("RENDER_DEBUG")) std::cerr << "[GRAPHITE_RENDER_DEBUG] Frame complete" << std::endl;
                } else {
                    if (!g_jsonOutput) {
                        std::cerr << "[Graphite] Failed to create surface this frame" << std::endl;
                    }
                }
            } else if (useMetalBackend && metalContext && metalContext->isInitialized()) {
                // === METAL GPU RENDERING PATH (Ganesh) ===
                // Metal requires a fresh drawable each frame (single-use resource)
                // GPU acceleration compensates for single-threaded rendering
                if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Starting frame render" << std::endl;
                metalDrawable = nullptr;  // Clear previous drawable
                surface = metalContext->createSurface(renderWidth, renderHeight, &metalDrawable);

                if (surface && metalDrawable) {
                    if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Got surface and drawable" << std::endl;
                    // Set the global canvas variable so debug overlay and browser mode work
                    canvas = surface->getCanvas();
                    if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Got canvas" << std::endl;
                    canvas->clear(SK_ColorBLACK);
                    if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Cleared canvas" << std::endl;

                    // Calculate scale to fit SVG in render area (same as Linux player)
                    // This ensures SVG content fills the full HiDPI resolution
                    float scale = std::min(static_cast<float>(renderWidth) / svgWidth,
                                          static_cast<float>(renderHeight) / svgHeight);
                    float offsetX = (renderWidth - svgWidth * scale) / 2.0f;
                    float offsetY = (renderHeight - svgHeight * scale) / 2.0f;

                    if (std::getenv("RENDER_DEBUG")) {
                        std::cerr << "[METAL_RENDER_DEBUG] Scale=" << scale
                                  << ", offset=(" << offsetX << "," << offsetY << ")"
                                  << ", svgSize=" << svgWidth << "x" << svgHeight << std::endl;
                    }

                    // IMAGE SEQUENCE MODE: Parse and render different SVG files per frame
                    // For image sequences, each frame is a separate SVG file, not SMIL animation
                    if (isImageSequence && !sequenceSvgContents.empty()) {
                        // Get the current frame's SVG content from pre-loaded data
                        size_t frameIdx = currentFrameIndex % sequenceSvgContents.size();
                        const std::string& svgContent = sequenceSvgContents[frameIdx];

                        // Parse this frame's SVG (re-parse each frame since they are different files)
                        sk_sp<SkData> frameData = SkData::MakeWithCopy(svgContent.data(), svgContent.size());
                        auto frameStream = SkMemoryStream::Make(frameData);
                        if (frameStream) {
                            sk_sp<SkSVGDOM> frameDom = makeSVGDOMWithFontSupport(*frameStream);
                            if (frameDom) {
                                // Apply scale transform to fill HiDPI canvas
                                canvas->save();
                                canvas->translate(offsetX, offsetY);
                                canvas->scale(scale, scale);
                                SkSize containerSize = SkSize::Make(svgWidth, svgHeight);
                                frameDom->setContainerSize(containerSize);
                                frameDom->render(canvas);
                                canvas->restore();
                                if (std::getenv("RENDER_DEBUG")) {
                                    std::cerr << "[METAL_RENDER_DEBUG] Rendered image sequence frame "
                                              << frameIdx << "/" << sequenceSvgContents.size() << std::endl;
                                }
                            }
                        }
                    } else {
                        // FBF.SVG MODE: Apply SMIL animations to single DOM
                        // Apply animation state to SVG DOM before rendering
                        if (svgDom && !animations.empty()) {
                            if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Applying animations" << std::endl;
                            for (const auto& anim : animations) {
                                if (!anim.targetId.empty() && !anim.attributeName.empty() && !anim.values.empty()) {
                                    std::string value = anim.getCurrentValue(animTime);
                                    sk_sp<SkSVGNode>* nodePtr = svgDom->findNodeById(anim.targetId.c_str());
                                    if (nodePtr && *nodePtr) {
                                        (*nodePtr)->setAttribute(anim.attributeName.c_str(), value.c_str());
                                    }
                                }
                            }
                            if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Animations applied" << std::endl;
                        }

                        // Render SVG directly to Metal-backed surface (GPU-accelerated)
                        // Apply scale transform to fill HiDPI canvas
                        if (svgDom) {
                            if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] About to render SVG" << std::endl;
                            canvas->save();
                            canvas->translate(offsetX, offsetY);
                            canvas->scale(scale, scale);
                            SkSize containerSize = SkSize::Make(svgWidth, svgHeight);
                            svgDom->setContainerSize(containerSize);
                            svgDom->render(canvas);
                            canvas->restore();
                            if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] SVG rendered" << std::endl;
                        }
                    }

                    gotNewFrame = true;
                    framesDelivered++;
                    if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_RENDER_DEBUG] Frame complete" << std::endl;
                } else {
                    // Failed to get drawable - Metal may be busy, skip this frame
                    if (!g_jsonOutput) {
                        std::cerr << "[Metal] Failed to acquire drawable this frame"
                                  << " (surface=" << (surface ? "OK" : "NULL")
                                  << ", drawable=" << (metalDrawable ? "OK" : "NULL")
                                  << ", renderSize=" << renderWidth << "x" << renderHeight << ")" << std::endl;
                    }
                }
            } else
#endif
            {
                // === CPU RENDERING PATH ===
                // For image sequence mode: use direct rendering (parse + render each frame)
                // For FBF.SVG mode: use ThreadedRenderer for async pre-buffered rendering
                if (isImageSequence && !sequenceSvgContents.empty()) {
                    // IMAGE SEQUENCE MODE (CPU): Direct rendering of separate SVG files
                    // We need a surface to render to - reuse the existing surface from SDL texture
                    size_t frameIdx = currentFrameIndex % sequenceSvgContents.size();
                    const std::string& svgContent = sequenceSvgContents[frameIdx];

                    // Parse this frame's SVG
                    sk_sp<SkData> frameData = SkData::MakeWithCopy(svgContent.data(), svgContent.size());
                    auto frameStream = SkMemoryStream::Make(frameData);
                    if (frameStream) {
                        sk_sp<SkSVGDOM> frameDom = makeSVGDOMWithFontSupport(*frameStream);
                        if (frameDom) {
                            // Render to the existing surface
                            SkCanvas* surfaceCanvas = surface->getCanvas();
                            if (surfaceCanvas) {
                                surfaceCanvas->clear(SK_ColorBLACK);

                                // Calculate scale to fit SVG in render area while preserving aspect ratio
                                // This matches the GPU rendering paths (Graphite and Ganesh Metal)
                                float scale = std::min(static_cast<float>(renderWidth) / svgWidth,
                                                      static_cast<float>(renderHeight) / svgHeight);
                                float offsetX = (renderWidth - svgWidth * scale) / 2.0f;
                                float offsetY = (renderHeight - svgHeight * scale) / 2.0f;

                                // Apply transform to preserve aspect ratio and center content
                                surfaceCanvas->save();
                                surfaceCanvas->translate(offsetX, offsetY);
                                surfaceCanvas->scale(scale, scale);
                                SkSize containerSize = SkSize::Make(svgWidth, svgHeight);
                                frameDom->setContainerSize(containerSize);
                                frameDom->render(surfaceCanvas);
                                surfaceCanvas->restore();

                                gotNewFrame = true;
                                framesDelivered++;
                            }
                        }
                    }
                } else if (threadedRendererPtr) {
                    // FBF.SVG MODE (CPU): Use ThreadedRenderer for async pre-buffered rendering
                    // Request new frame (render thread will process asynchronously)
                    // Main thread NEVER touches parallelRenderer directly - all through ThreadedRenderer
                    threadedRendererPtr->requestFrame(currentFrameIndex);

                    // Try to get rendered frame from ThreadedRenderer (non-blocking!)
                    const uint32_t* renderedPixels = threadedRendererPtr->getFrontBufferIfReady();
                    if (renderedPixels) {
                        // Got new frame from render thread - copy to surface
                        SkPixmap pixmap;
                        if (surface->peekPixels(&pixmap)) {
                            memcpy(const_cast<void*>(pixmap.addr()), renderedPixels, renderWidth * renderHeight * sizeof(uint32_t));
                            gotNewFrame = true;
                            framesDelivered++;  // Count frames actually received from render thread
                        }
                    }
                    // If no new frame ready, surface keeps last frame (no blocking!)
                }
            }
        }

        auto fetchEnd = Clock::now();
        DurationMs fetchTime = fetchEnd - fetchStart;

        // Track fetch time and actual render time separately
        // Skip when disruptive events occurred (reset, mode change, screenshot, etc.)
        if (!skipStatsThisFrame) {
            fetchTimes.add(fetchTime.count());
            if (gotNewFrame && threadedRendererPtr) {
                // Record actual SVG render time from the render thread
                renderTimes.add(threadedRendererPtr->lastRenderTimeMs.load());
            }
        }

        // === DRAW DEBUG OVERLAY (always update when enabled, independent of frame delivery) ===
        // Debug overlay shows real-time stats and must not freeze when renderer is slow
        auto overlayStart = Clock::now();
        if (showDebugOverlay) {
            // Calculate scale for display in overlay
            float scaleX = static_cast<float>(renderWidth) / svgWidth;
            float scaleY = static_cast<float>(renderHeight) / svgHeight;
            float scale = std::min(scaleX, scaleY);

            auto totalElapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
            double fps = (frameCount > 0) ? frameCount / totalElapsed : 0;
            double instantFps = (frameTimes.last() > 0) ? 1000.0 / frameTimes.last() : 0;

            // Debug overlay layout constants - scaled by DEBUG_OVERLAY_SCALE to match font
            float lineHeight = static_cast<float>(9 * DEBUG_OVERLAY_SCALE) * hiDpiScale;
            float padding = static_cast<float>(2 * DEBUG_OVERLAY_SCALE) * hiDpiScale;
            float labelWidth = static_cast<float>(80 * DEBUG_OVERLAY_SCALE) * hiDpiScale;

            // === PASS 1: Build all debug lines and measure max width ===
            // Line types: 0=normal, 1=highlight, 2=anim, 3=key, 4=gap_small, 5=gap_large, 6=single
            struct DebugLine {
                int type;
                std::string label;
                std::string value;
                std::string key;  // For key lines
            };
            std::vector<DebugLine> lines;
            std::ostringstream oss;

            // Helper to add debug lines - unified interface for all line types
            // type: 0=normal, 1=highlight, 2=anim, 3=key, 4=gap_small, 5=gap_large, 6=single
            auto addDebugLine = [&](int type, const std::string& label = "", const std::string& value = "", const std::string& key = "") {
                lines.push_back({type, label, value, key});
            };
            // Convenience wrappers for common types
            auto addLine = [&](const std::string& label, const std::string& value) {
                addDebugLine(0, label, value);
            };
            auto addHighlight = [&](const std::string& label, const std::string& value) {
                addDebugLine(1, label, value);
            };
            auto addAnim = [&](const std::string& label, const std::string& value) {
                addDebugLine(2, label, value);
            };
            auto addKey = [&](const std::string& key, const std::string& label, const std::string& value) {
                addDebugLine(3, label, value, key);
            };
            auto addSmallGap = [&]() { addDebugLine(4); };
            auto addLargeGap = [&]() { addDebugLine(5); };
            auto addSingle = [&](const std::string& text) { addDebugLine(6, text); };

            // Build all lines - FPS and frame time first
            oss.str("");
            oss << std::fixed << std::setprecision(1) << fps;
            addHighlight("FPS (avg):", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(1) << instantFps;
            addLine("FPS (instant):", oss.str());

            // Frame delivery rate - shows how often Skia worker thread delivers new frames
            // This is the KEY metric: if Skia render is slower than display, hit rate drops
            double hitRate = displayCycles > 0 ? (100.0 * framesDelivered / displayCycles) : 0.0;
            double effectiveFps = totalElapsed > 0 ? (framesDelivered / totalElapsed) : 0.0;
            oss.str("");
            oss << std::fixed << std::setprecision(1) << effectiveFps << " (" << std::setprecision(0) << hitRate
                << "% ready)";
            addHighlight("Skia FPS:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << frameTimes.average() << " ms";
            addLine("Frame time:", oss.str());

            addSmallGap();

            // === PIPELINE TIMING BREAKDOWN ===
            // All phases should add up to total frame time
            double totalAvg = frameTimes.average();
            double eventAvg = eventTimes.average();
            double animAvg = animTimes.average();
            double fetchAvg = fetchTimes.average();
            double overlayAvg = overlayTimes.average();
            double copyAvg = copyTimes.average();
            double presentAvg = presentTimes.average();
            double renderAvg = renderTimes.average();  // Actual SVG render (in render thread)

            // Calculate percentages (avoid div by zero)
            auto pct = [totalAvg](double v) -> double { return totalAvg > 0 ? (v / totalAvg * 100.0) : 0.0; };

            addSingle("--- Pipeline ---");

            oss.str("");
            oss << std::fixed << std::setprecision(2) << eventAvg << " ms (" << std::setprecision(1) << pct(eventAvg)
                << "%)";
            addLine("Event:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << animAvg << " ms (" << std::setprecision(1) << pct(animAvg)
                << "%)";
            addLine("Anim:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << fetchAvg << " ms (" << std::setprecision(1) << pct(fetchAvg)
                << "%)";
            addLine("Fetch:", oss.str());

            // Waiting for Skia worker - idle time when main loop polls but no frame ready
            // Pipeline order: wait for producer → then process (overlay, copy, present)
            double idleAvg = idleTimes.average();
            // hitRate already calculated above for Skia FPS display
            oss.str("");
            oss << std::fixed << std::setprecision(2) << idleAvg << " ms (" << std::setprecision(0) << (100.0 - hitRate)
                << "% idle)";
            addHighlight("Wait Skia:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << overlayAvg << " ms (" << std::setprecision(1)
                << pct(overlayAvg) << "%)";
            addLine("Overlay:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << copyAvg << " ms (" << std::setprecision(1) << pct(copyAvg)
                << "%)";
            addLine("Copy:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << presentAvg << " ms (" << std::setprecision(1)
                << pct(presentAvg) << "%)";
            addLine("Present:", oss.str());

            addSmallGap();

            // Skia worker render time (async, not blocking main thread)
            oss.str("");
            oss << std::fixed << std::setprecision(2) << renderAvg << " ms (min=" << std::setprecision(2)
                << renderTimes.min() << ", max=" << renderTimes.max() << ")";
            addLine("Skia work:", oss.str());

            addSmallGap();

            // Sum of main thread active phases (when frame IS ready)
            double sumPhases = eventAvg + animAvg + fetchAvg + overlayAvg + copyAvg + presentAvg;
            oss.str("");
            oss << std::fixed << std::setprecision(2) << sumPhases << " ms (" << std::setprecision(1) << pct(sumPhases)
                << "%)";
            addLine("Active work:", oss.str());

            addSmallGap();

            oss.str("");
            oss << renderWidth << " x " << renderHeight;
            addLine("Resolution:", oss.str());

            oss.str("");
            oss << svgWidth << " x " << svgHeight;
            addLine("SVG size:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(2) << scale << "x";
            addLine("Scale:", oss.str());

            oss.str("");
            oss << frameCount;
            addLine("Frames:", oss.str());

            // Animation info
            if (!animations.empty()) {
                addLargeGap();

                oss.str("");
                oss << std::fixed << std::setprecision(3) << animTime << "s";
                if (animationPaused) oss << " (PAUSED)";
                addAnim("Anim time:", oss.str());

                oss.str("");
                oss << (currentFrameIndex + 1) << " / " << animations[0].values.size();
                addAnim("Anim frame:", oss.str());

                oss.str("");
                oss << std::fixed << std::setprecision(2) << animations[0].duration << "s";
                addAnim("Anim duration:", oss.str());

                // Animation mode/type (Loop, PingPong, Once, Count)
                oss.str("");
                oss << repeatModeToString(g_animController.getRepeatMode());
                addAnim("Anim mode:", oss.str());

                // Remaining frames and time until animation cycle completes
                size_t totalAnimFrames = animations[0].values.size();
                size_t remainingFrames = (totalAnimFrames > currentFrameIndex)
                    ? (totalAnimFrames - currentFrameIndex - 1) : 0;
                double remainingTime = (remainingFrames * animations[0].duration) / totalAnimFrames;
                oss.str("");
                oss << remainingFrames << " frames (" << std::fixed << std::setprecision(2) << remainingTime << "s)";
                addLine("Remaining:", oss.str());

                oss.str("");
                oss << framesRendered;
                addLine("Frames shown:", oss.str());

                oss.str("");
                oss << framesSkipped;
                if (framesSkipped > 0)
                    addHighlight("Frames skipped:", oss.str());
                else
                    addLine("Frames skipped:", oss.str());

                if (framesRendered + framesSkipped > 0) {
                    // Note: Assumes all animations have same frame count/duration (enforced during load)
                    double skipRate = 100.0 * framesSkipped / (framesRendered + framesSkipped);
                    oss.str("");
                    oss << std::fixed << std::setprecision(1) << skipRate << "%";
                    if (skipRate > 10)
                        addHighlight("Skip rate:", oss.str());
                    else
                        addLine("Skip rate:", oss.str());
                }

                double animFps = animations[0].values.size() / animations[0].duration;
                oss.str("");
                oss << std::fixed << std::setprecision(1) << animFps << " FPS";
                addLine("Anim target:", oss.str());

                addSmallGap();

                // Black screen detection status - shows if content is actually being rendered
                int nonBlackCount = g_lastNonBlackPixelCount.load();
                int consecutiveBlack = g_consecutiveBlackFrames.load();
                bool isBlack = g_blackScreenDetected.load();
                oss.str("");
                if (isBlack) {
                    oss << "BLACK! (x" << consecutiveBlack << ")";
                    addHighlight("Screen:", oss.str());
                } else {
                    // Show approximate fill percentage (sampled 1/100 pixels)
                    int totalSampled = (renderWidth * renderHeight) / 100;
                    double fillPercent = totalSampled > 0 ? (100.0 * nonBlackCount / totalSampled) : 0;
                    oss << "OK (" << std::fixed << std::setprecision(0) << fillPercent << "% filled)";
                    addLine("Screen:", oss.str());
                }
            }

            addLargeGap();

            // Controls
            addKey("[V]", "VSync:", vsyncEnabled ? "ON" : "OFF");
            addKey("[F]",
                   "Limiter:", frameLimiterEnabled ? ("ON (" + std::to_string(displayRefreshRate) + " FPS)") : "OFF");

            // Parallel mode status - use cached mode from ThreadedRenderer to avoid blocking
            // In Metal mode (threadedRendererPtr is null), show "Metal" as the mode
            std::string parallelStatus = threadedRendererPtr ? (threadedRendererPtr->isPreBufferMode() ? "PreBuffer" : "Off") : "Metal";
            addKey("[P]", "Mode:", parallelStatus);

            // Real-time CPU stats from macOS Mach APIs
            CPUStats cpuStats = getProcessCPUStats();
            oss.str("");
            oss << cpuStats.activeThreads << " active / " << cpuStats.totalThreads << " threads";
            addLine("Threads:", oss.str());

            oss.str("");
            oss << std::fixed << std::setprecision(1) << cpuStats.cpuUsagePercent << "%";
            addLine("CPU usage:", oss.str());

            if (!animations.empty()) {
                addKey("[SPACE]", "Animation:", animationPaused ? "PAUSED" : "PLAYING");
                addKey("[S]", "Stress test:", stressTestEnabled ? "ON (50ms delay)" : "OFF");
            }

            addSingle("[R] Reset stats  [D] Toggle overlay  [G] Fullscreen");

            // === Measure max width needed ===
            float maxWidth = 0;
            for (const auto& line : lines) {
                if (line.type == 4 || line.type == 5) continue;  // Skip gaps

                float lineWidth = 0;
                if (line.type == 6) {
                    // Single text line
                    lineWidth = debugFont.measureText(line.label.c_str(), line.label.size(), SkTextEncoding::kUTF8);
                } else if (line.type == 3) {
                    // Key line: [KEY] Label: Value
                    float keyW = debugFont.measureText(line.key.c_str(), line.key.size(), SkTextEncoding::kUTF8);
                    float valW = debugFont.measureText(line.value.c_str(), line.value.size(), SkTextEncoding::kUTF8);
                    lineWidth = keyW + 7 * hiDpiScale + labelWidth + valW;  // Was 5, now 7 (40% larger)
                } else {
                    // Normal line: Label: Value
                    lineWidth = labelWidth +
                                debugFont.measureText(line.value.c_str(), line.value.size(), SkTextEncoding::kUTF8);
                }
                maxWidth = std::max(maxWidth, lineWidth);
            }

            // Calculate box dimensions - tight fit around text
            float boxWidth = maxWidth + padding * 2;
            float boxHeight = padding;
            for (const auto& line : lines) {
                if (line.type == 4)
                    boxHeight += 6 * hiDpiScale;  // Small gap (was 4, now 6)
                else if (line.type == 5)
                    boxHeight += 11 * hiDpiScale;  // Large gap (was 8, now 11)
                else
                    boxHeight += lineHeight;
            }
            boxHeight += padding;

            // === PASS 2: Draw background then all text ===
            // NULL CHECK: canvas may be nullptr if Metal failed to acquire drawable
            if (canvas) {
                canvas->drawRect(SkRect::MakeXYWH(0, 0, boxWidth, boxHeight), bgPaint);

                float y = padding + lineHeight;
                float x = padding;

                for (const auto& line : lines) {
                    if (line.type == 4) {
                        y += 6 * hiDpiScale;  // Small gap (was 4, now 6 - 40% larger)
                    } else if (line.type == 5) {
                        y += 11 * hiDpiScale;  // Large gap (was 8, now 11 - 40% larger)
                    } else if (line.type == 6) {
                        // Single text line
                        canvas->drawString(line.label.c_str(), x, y, debugFont, keyPaint);
                        y += lineHeight;
                    } else if (line.type == 3) {
                        // Key line
                        canvas->drawString(line.key.c_str(), x, y, debugFont, keyPaint);
                        float keyW = debugFont.measureText(line.key.c_str(), line.key.size(), SkTextEncoding::kUTF8);
                        canvas->drawString(line.label.c_str(), x + keyW + 7 * hiDpiScale, y, debugFont,
                                           textPaint);  // Was 5, now 7
                        canvas->drawString(line.value.c_str(), x + labelWidth, y, debugFont, highlightPaint);
                        y += lineHeight;
                    } else {
                        // Normal/highlight/anim line
                        canvas->drawString(line.label.c_str(), x, y, debugFont, textPaint);
                        SkPaint* valuePaint = &textPaint;
                        if (line.type == 1)
                            valuePaint = &highlightPaint;
                        else if (line.type == 2)
                            valuePaint = &animPaint;
                        canvas->drawString(line.value.c_str(), x + labelWidth, y, debugFont, *valuePaint);
                        y += lineHeight;
                    }
                }
            }  // end canvas null check
        }  // end showDebugOverlay
        auto overlayEnd = Clock::now();
        DurationMs overlayTime = overlayEnd - overlayStart;

        // === ONLY PRESENT WHEN WE HAVE NEW CONTENT ===
        // This eliminates flickering by syncing overlay updates with image updates
        // Without this, the loop runs 400+ times/sec but only some have new image data
        DurationMs copyTime{0};
        DurationMs presentTime{0};
        auto presentEnd = Clock::now();  // Default for timing when we skip present

        if (gotNewFrame) {
            frameCount++;

#ifdef __APPLE__
            if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                // === GRAPHITE GPU PRESENTATION PATH ===
                // Frame was already submitted in render loop, just present and track timing
                if (std::getenv("RENDER_DEBUG")) std::cerr << "[GRAPHITE_PRESENT_DEBUG] About to present" << std::endl;
                auto copyStart = Clock::now();

                // Periodic black screen detection (same pattern as Metal)
                if (frameCount % 60 == 0 && surface) {
                    SkImageInfo info = SkImageInfo::Make(renderWidth, renderHeight,
                                                          kBGRA_8888_SkColorType, kPremul_SkAlphaType);
                    std::vector<uint32_t> checkPixels(renderWidth * renderHeight);
                    if (surface->readPixels(info, checkPixels.data(),
                                            renderWidth * sizeof(uint32_t), 0, 0)) {
                        int debugOverlayW = static_cast<int>(300 * hiDpiScale);
                        int debugOverlayH = static_cast<int>(500 * hiDpiScale);
                        int nonBlackPixels = countNonBlackPixels(
                            checkPixels.data(), renderWidth, renderHeight,
                            0, 0, debugOverlayW, debugOverlayH);
                        g_lastNonBlackPixelCount.store(nonBlackPixels);

                        if (nonBlackPixels < 10) {
                            g_blackScreenDetected.store(true);
                            g_consecutiveBlackFrames.fetch_add(60);
                            if (!g_jsonOutput) {
                                std::cerr << "[Graphite WARNING] Black screen detected! Frame #" << frameCount << std::endl;
                            }
                        } else {
                            g_blackScreenDetected.store(false);
                            g_consecutiveBlackFrames.store(0);
                        }
                    }
                }

                // Present the frame via Graphite
                auto presentStart = Clock::now();
                graphiteContext->present();
                presentEnd = Clock::now();
                presentTime = presentEnd - presentStart;
                if (std::getenv("RENDER_DEBUG")) std::cerr << "[GRAPHITE_PRESENT_DEBUG] Present complete" << std::endl;

                auto copyEnd = Clock::now();
                copyTime = copyEnd - copyStart;
                if (!skipStatsThisFrame) {
                    copyTimes.add(copyTime.count());
                    eventTimes.add(eventTime.count());
                    animTimes.add(animTime_ms.count());
                    overlayTimes.add(overlayTime.count());
                    presentTimes.add(presentTime.count());
                }
            } else if (useMetalBackend && metalContext && metalContext->isInitialized() && metalDrawable) {
                // === METAL GPU PRESENTATION PATH (Ganesh) ===
                // No CPU→GPU copy needed - already rendered to GPU texture
                if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_PRESENT_DEBUG] About to present" << std::endl;
                auto copyStart = Clock::now();

                // === PERIODIC BLACK SCREEN DETECTION FOR METAL ===
                // GPU→CPU copies are expensive, so only check every 60 frames (once per second at 60fps)
                // This provides debugging info without impacting performance
                if (frameCount % 60 == 0 && surface) {
                    SkImageInfo info = SkImageInfo::Make(renderWidth, renderHeight,
                                                          kBGRA_8888_SkColorType, kPremul_SkAlphaType);
                    std::vector<uint32_t> checkPixels(renderWidth * renderHeight);
                    if (surface->readPixels(info, checkPixels.data(),
                                            renderWidth * sizeof(uint32_t), 0, 0)) {
                        int debugOverlayW = static_cast<int>(300 * hiDpiScale);
                        int debugOverlayH = static_cast<int>(500 * hiDpiScale);
                        int nonBlackPixels = countNonBlackPixels(
                            checkPixels.data(), renderWidth, renderHeight,
                            0, 0, debugOverlayW, debugOverlayH);
                        g_lastNonBlackPixelCount.store(nonBlackPixels);

                        if (nonBlackPixels < 10) {
                            g_blackScreenDetected.store(true);
                            g_consecutiveBlackFrames.fetch_add(60);  // Assume all 60 frames were black
                            if (!g_jsonOutput) {
                                std::cerr << "[Metal WARNING] Black screen detected! Frame #" << frameCount << std::endl;
                            }
                        } else {
                            g_blackScreenDetected.store(false);
                            g_consecutiveBlackFrames.store(0);
                        }
                    }
                }

                // Flush Skia drawing commands and present Metal drawable
                auto presentStart = Clock::now();
                metalContext->presentDrawable(metalDrawable);
                presentEnd = Clock::now();
                presentTime = presentEnd - presentStart;
                if (std::getenv("RENDER_DEBUG")) std::cerr << "[METAL_PRESENT_DEBUG] Present complete" << std::endl;

                // Note: copyTime is effectively 0 for Metal (no CPU copy)
                auto copyEnd = Clock::now();
                copyTime = copyEnd - copyStart;
                if (!skipStatsThisFrame) {
                    copyTimes.add(copyTime.count());
                }

                // Track all phase times for frames that were presented
                if (!skipStatsThisFrame) {
                    eventTimes.add(eventTime.count());
                    animTimes.add(animTime_ms.count());
                    overlayTimes.add(overlayTime.count());
                    presentTimes.add(presentTime.count());
                }

                // Clear drawable for next frame
                metalDrawable = nullptr;
            } else
#endif
            {
                // === CPU RENDERING + SDL PRESENTATION PATH ===
                auto copyStart = Clock::now();

                SkPixmap pixmap;
                if (surface->peekPixels(&pixmap)) {
                    void* pixels;
                    int pitch;
                    SDL_LockTexture(texture, nullptr, &pixels, &pitch);

                    const uint8_t* src = static_cast<const uint8_t*>(pixmap.addr());
                    uint8_t* dst = static_cast<uint8_t*>(pixels);
                    size_t rowBytes = renderWidth * 4;

                    // Optimize: single memcpy if pitch matches rowBytes (common case)
                    if (pitch == static_cast<int>(pixmap.rowBytes())) {
                        memcpy(dst, src, rowBytes * renderHeight);
                    } else {
                        // Row-by-row copy needed when pitch differs (e.g., aligned stride)
                        for (int row = 0; row < renderHeight; row++) {
                            memcpy(dst + row * pitch, src + row * pixmap.rowBytes(), rowBytes);
                        }
                    }

                    SDL_UnlockTexture(texture);

                    // === BLACK SCREEN DETECTION ===
                    // Check if the rendered frame contains visible content (not just black)
                    // Exclude the debug overlay area (top-left corner) from the check
                    // Debug overlay is approximately 300x400 pixels at HiDPI scale
                    int debugOverlayW = static_cast<int>(300 * hiDpiScale);
                    int debugOverlayH = static_cast<int>(500 * hiDpiScale);
                    int nonBlackPixels = countNonBlackPixels(
                        static_cast<const uint32_t*>(pixmap.addr()),
                        renderWidth, renderHeight,
                        0, 0, debugOverlayW, debugOverlayH  // Exclude debug overlay region
                    );
                    g_lastNonBlackPixelCount.store(nonBlackPixels);

                    // Track consecutive black frames (threshold: <10 non-black pixels sampled)
                    if (nonBlackPixels < 10) {
                        g_blackScreenDetected.store(true);
                        g_consecutiveBlackFrames.fetch_add(1);
                        // Warn on first black frame or every 60 black frames (once per second at 60fps)
                        int blackCount = g_consecutiveBlackFrames.load();
                        if (!g_jsonOutput && (blackCount == 1 || blackCount % 60 == 0)) {
                            std::cerr << "[WARNING] Black screen detected! Frame #" << frameCount
                                      << ", consecutive black frames: " << blackCount << std::endl;
                        }
                    } else {
                        g_blackScreenDetected.store(false);
                        g_consecutiveBlackFrames.store(0);
                    }

                    // Auto-screenshot for benchmark mode (save first frame only)
                    if (!screenshotPath.empty() && !screenshotSaved && frameCount == 1) {
                        std::vector<uint32_t> screenshotPixels(renderWidth * renderHeight);
                        memcpy(screenshotPixels.data(), pixmap.addr(),
                               renderWidth * renderHeight * sizeof(uint32_t));
                        if (saveScreenshotPPM(screenshotPixels, renderWidth, renderHeight, screenshotPath)) {
                            if (!g_jsonOutput) {
                                std::cerr << "Screenshot saved: " << screenshotPath
                                          << " (" << renderWidth << "x" << renderHeight << ")" << std::endl;
                            }
                        }
                        screenshotSaved = true;
                    }
                }

                auto copyEnd = Clock::now();
                copyTime = copyEnd - copyStart;
                if (!skipStatsThisFrame) {
                    copyTimes.add(copyTime.count());
                }

                // Clear and render to screen (pure black for exclusive fullscreen)
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);

                // Render texture at full size - no centering needed
                // SVG's preserveAspectRatio handles centering within the texture
                // This keeps debug overlay at true top-left corner
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);

                // Measure SDL_RenderPresent time separately (often the stutter source)
                auto presentStart = Clock::now();
                SDL_RenderPresent(renderer);
                presentEnd = Clock::now();
                presentTime = presentEnd - presentStart;

                // Track all phase times for frames that were presented
                // Skip when disruptive events occurred (reset, mode change, screenshot, etc.)
                if (!skipStatsThisFrame) {
                    eventTimes.add(eventTime.count());
                    animTimes.add(animTime_ms.count());
                    overlayTimes.add(overlayTime.count());
                    presentTimes.add(presentTime.count());
                }
            }
        } else {
            // No new frame - yield CPU briefly to prevent busy-spinning
            // Increased from 1ms to 5ms to reduce CPU usage when idle
            auto idleStart = Clock::now();
            SDL_Delay(5);
            auto idleEnd = Clock::now();
            DurationMs idleTime = idleEnd - idleStart;
            idleTimes.add(idleTime.count());
        }

        // === UPDATE WINDOW TITLE WITH FPS (every 500ms) ===
        // Always visible even when debug overlay is off
        // Uses instantaneous FPS (rolling average) instead of cumulative to respond immediately to limiter changes
        {
            static auto lastTitleUpdate = Clock::now();
            auto now = Clock::now();
            auto timeSinceUpdate = std::chrono::duration<double>(now - lastTitleUpdate).count();
            if (timeSinceUpdate >= 0.5) {  // Update every 500ms
                // Use rolling average of recent frame times for instantaneous FPS
                // This responds immediately when frame limiter is toggled (unlike cumulative average)
                double avgFrameTime = frameTimes.average();
                double currentFps = avgFrameTime > 0 ? (1000.0 / avgFrameTime) : 0.0;

                // Extract filename from path
                std::string pathStr(inputPath);
                size_t lastSlash = pathStr.find_last_of("/\\");
                std::string filename = (lastSlash != std::string::npos) ? pathStr.substr(lastSlash + 1) : pathStr;

                // Build title: "filename - XX.X FPS - SVG Player"
                std::ostringstream titleStream;
                titleStream << filename << " - " << std::fixed << std::setprecision(1) << currentFps << " FPS - SVG Player";
                SDL_SetWindowTitle(window, titleStream.str().c_str());

                lastTitleUpdate = now;
            }
        }

        // Detect and log stutters (frame time > 30ms) - only when we presented
        // Skip stutter detection when disruptive events occurred (reset, mode change, etc.)
        if (gotNewFrame && !skipStatsThisFrame) {
            DurationMs totalFrameTime = presentEnd - frameStart;
            static int stutterCount = 0;
            static double lastStutterTime = 0;
            if (totalFrameTime.count() > 30.0 && !stressTestEnabled) {
                stutterCount++;
                double stutterAt = std::chrono::duration<double>(presentEnd - startTime).count();
                double sinceLast = stutterAt - lastStutterTime;
                // Identify the culprit phase (using all tracked phases)
                const char* culprit = "unknown";
                double maxPhase = std::max(
                    {eventTime.count(), fetchTime.count(), overlayTime.count(), copyTime.count(), presentTime.count()});
                if (maxPhase == eventTime.count())
                    culprit = "EVENT";
                else if (maxPhase == fetchTime.count())
                    culprit = "FETCH";
                else if (maxPhase == overlayTime.count())
                    culprit = "OVERLAY";
                else if (maxPhase == copyTime.count())
                    culprit = "COPY";
                else if (maxPhase == presentTime.count())
                    culprit = "PRESENT";
                if (!g_jsonOutput) {
                    std::cerr << "STUTTER #" << stutterCount << " at " << std::fixed << std::setprecision(2) << stutterAt
                              << "s (+" << sinceLast << "s) [" << culprit << "]: "
                              << "event=" << eventTime.count() << "ms, "
                              << "fetch=" << fetchTime.count() << "ms, "
                              << "overlay=" << overlayTime.count() << "ms, "
                              << "copy=" << copyTime.count() << "ms, "
                              << "present=" << presentTime.count() << "ms, "
                              << "TOTAL=" << totalFrameTime.count() << "ms" << std::endl;
                }
                lastStutterTime = stutterAt;
            }

            // Track frame times for display (only when we actually presented)
            frameTimes.add(totalFrameTime.count());

            // Soft frame limiter when VSync is OFF (target display refresh rate without blocking)
            // This prevents GPU resource contention that causes periodic stutters
            if (frameLimiterEnabled && !vsyncEnabled && !stressTestEnabled) {
                const double targetFrameTimeMs = 1000.0 / displayRefreshRate;
                double actualFrameTimeMs = totalFrameTime.count();
                if (actualFrameTimeMs < targetFrameTimeMs) {
                    SDL_Delay(static_cast<Uint32>(targetFrameTimeMs - actualFrameTimeMs));
                }
            }
        }
    }

    // Final statistics
    auto totalElapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
    double totalAvg = frameTimes.average();
    auto pctFinal = [totalAvg](double v) -> double { return totalAvg > 0 ? (v / totalAvg * 100.0) : 0.0; };

    // Get dirty region stats for both output modes
    uint64_t partialCount = 0, fullCount = 0;
    double avgSavedRatio = 0.0;
    if (threadedRendererPtr) {
        threadedRendererPtr->getPartialRenderStats(partialCount, fullCount, avgSavedRatio);
    }
    uint64_t totalRenders = partialCount + fullCount;
    double finalHitRate = displayCycles > 0 ? (100.0 * framesDelivered / displayCycles) : 0.0;

    if (g_jsonOutput) {
        // JSON output for benchmark scripts
        double avgFps = totalElapsed > 0 ? (framesDelivered / totalElapsed) : 0;
        double minFps = frameTimes.max() > 0 ? (1000.0 / frameTimes.max()) : 0;
        double maxFps = frameTimes.min() > 0 ? (1000.0 / frameTimes.min()) : 0;

        std::cout << "{";
        std::cout << "\"player\":\"fbfsvg-player\",";
        std::cout << "\"file\":\"" << inputPath << "\",";
        std::cout << "\"duration_seconds\":" << std::fixed << std::setprecision(2) << totalElapsed << ",";
        std::cout << "\"total_frames\":" << framesDelivered << ",";
        std::cout << "\"avg_fps\":" << std::setprecision(2) << avgFps << ",";
        std::cout << "\"avg_frame_time_ms\":" << std::setprecision(3) << frameTimes.average() << ",";
        std::cout << "\"min_fps\":" << std::setprecision(2) << minFps << ",";
        std::cout << "\"max_fps\":" << std::setprecision(2) << maxFps << ",";
        std::cout << "\"partial_renders\":" << partialCount << ",";
        std::cout << "\"full_renders\":" << fullCount;
        std::cout << "}" << std::endl;
    } else {
        // Normal text output
        std::cout << "\n=== Final Statistics ===" << std::endl;
        std::cout << "Display cycles: " << displayCycles << std::endl;
        std::cout << "Frames delivered: " << framesDelivered << std::endl;
        std::cout << "Frame hit rate: " << std::fixed << std::setprecision(1) << finalHitRate << "%" << std::endl;
        std::cout << "Total time: " << std::setprecision(2) << totalElapsed << "s" << std::endl;
        std::cout << "Display FPS: " << std::setprecision(2) << (displayCycles / totalElapsed) << " (main loop rate)"
                  << std::endl;
        std::cout << "Skia FPS: " << std::setprecision(2) << (framesDelivered / totalElapsed)
                  << " (frames from Skia worker)" << std::endl;
        std::cout << "Average frame time: " << std::setprecision(2) << frameTimes.average() << "ms" << std::endl;

        std::cout << "\n--- Pipeline Timing (average) ---" << std::endl;
        std::cout << "Event:      " << std::setprecision(2) << eventTimes.average() << "ms (" << std::setprecision(1)
                  << pctFinal(eventTimes.average()) << "%)" << std::endl;
        std::cout << "Anim:       " << std::setprecision(2) << animTimes.average() << "ms (" << std::setprecision(1)
                  << pctFinal(animTimes.average()) << "%)" << std::endl;
        std::cout << "Fetch:      " << std::setprecision(2) << fetchTimes.average() << "ms (" << std::setprecision(1)
                  << pctFinal(fetchTimes.average()) << "%)" << std::endl;
        std::cout << "Wait Skia:  " << std::setprecision(2) << idleTimes.average() << "ms (" << std::setprecision(1)
                  << (100.0 - finalHitRate) << "% idle)" << std::endl;
        std::cout << "Overlay:    " << std::setprecision(2) << overlayTimes.average() << "ms (" << std::setprecision(1)
                  << pctFinal(overlayTimes.average()) << "%)" << std::endl;
        std::cout << "Copy:       " << std::setprecision(2) << copyTimes.average() << "ms (" << std::setprecision(1)
                  << pctFinal(copyTimes.average()) << "%)" << std::endl;
        std::cout << "Present:    " << std::setprecision(2) << presentTimes.average() << "ms (" << std::setprecision(1)
                  << pctFinal(presentTimes.average()) << "%)" << std::endl;
        std::cout << "Skia work:  " << std::setprecision(2) << renderTimes.average()
                  << "ms (worker, min=" << renderTimes.min() << ", max=" << renderTimes.max() << ")" << std::endl;
        double sumPhases = eventTimes.average() + animTimes.average() + fetchTimes.average() + overlayTimes.average() +
                           copyTimes.average() + presentTimes.average();
        std::cout << "Active:     " << std::setprecision(2) << sumPhases << "ms (" << std::setprecision(1)
                  << pctFinal(sumPhases) << "%)" << std::endl;

        // Dirty region tracking statistics
        std::cout << "\n--- Dirty Region Tracking ---" << std::endl;
        if (totalRenders > 0) {
            double partialPct = (100.0 * partialCount) / totalRenders;
            std::cout << "Partial renders: " << partialCount << " (" << std::setprecision(1) << partialPct << "%)" << std::endl;
            std::cout << "Full renders:    " << fullCount << " (" << std::setprecision(1) << (100.0 - partialPct) << "%)" << std::endl;
            if (partialCount > 0) {
                std::cout << "Avg area saved:  " << std::setprecision(1) << (avgSavedRatio * 100.0) << "% (per partial render)" << std::endl;
                double overallSaved = (partialCount * avgSavedRatio) / totalRenders * 100.0;
                std::cout << "Overall savings: " << std::setprecision(1) << overallSaved << "% render area reduction" << std::endl;
            }
        } else {
            std::cout << "No frames rendered (animation not started)" << std::endl;
        }
    }

    // Stop all background threads BEFORE static objects are destroyed
    // Order matters: DOM parse -> scan -> thumbnail loader -> renderers

    // 1. Stop async DOM parsing thread (may be accessing g_folderBrowser)
    if (!g_jsonOutput) std::cout << "\nStopping browser DOM parse thread..." << std::endl;
    stopAsyncBrowserDomParse();
    if (!g_jsonOutput) std::cout << "DOM parse thread stopped." << std::endl;

    // 2. Cancel folder browser scan thread
    if (!g_jsonOutput) std::cout << "Cancelling browser scan..." << std::endl;
    g_folderBrowser.cancelScan();
    if (!g_jsonOutput) std::cout << "Browser scan cancelled." << std::endl;

    // 3. Stop thumbnail loader thread
    if (!g_jsonOutput) std::cout << "Stopping thumbnail loader..." << std::endl;
    g_folderBrowser.stopThumbnailLoader();
    if (!g_jsonOutput) std::cout << "Thumbnail loader stopped." << std::endl;

    // Stop remote control server first (if running)
    if (remoteServer) {
        if (!g_jsonOutput) std::cout << "Stopping remote control server..." << std::endl;
        remoteServer->stop();
        remoteServer.reset();
        if (!g_jsonOutput) std::cout << "Remote control server stopped." << std::endl;
    }

    // Stop threaded renderer first (must stop before parallel renderer)
    if (threadedRendererPtr) {
        if (!g_jsonOutput) std::cout << "Stopping render thread..." << std::endl;
        threadedRendererPtr->stop();
        if (!g_jsonOutput) std::cout << "Render thread stopped." << std::endl;
    }

    // Stop parallel renderer if running
    if (parallelRenderer.isEnabled()) {
        if (!g_jsonOutput) std::cout << "Stopping parallel render threads..." << std::endl;
        parallelRenderer.stop();
        if (!g_jsonOutput) std::cout << "Parallel renderer stopped." << std::endl;
    }

    // CRITICAL: Destroy GPU contexts BEFORE SDL cleanup
    // These contexts hold SDL resources which require SDL to be active
#ifdef __APPLE__
    // Destroy Graphite context first (if using next-gen backend)
    if (graphiteContext) {
        if (!g_jsonOutput) std::cout << "Destroying Graphite context..." << std::endl;
        graphiteContext.reset();
        if (!g_jsonOutput) std::cout << "Graphite context destroyed." << std::endl;
    }

    // Destroy Metal (Ganesh) context
    if (metalContext) {
        if (!g_jsonOutput) std::cout << "Destroying Metal context..." << std::endl;
        metalContext.reset();
        if (!g_jsonOutput) std::cout << "Metal context destroyed." << std::endl;
    }
#endif

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
