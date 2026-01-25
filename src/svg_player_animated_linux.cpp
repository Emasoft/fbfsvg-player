// svg_player_animated_linux.cpp - Real-time SVG renderer with SMIL animation support
// Linux version - Uses X11/EGL and fontconfig
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
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <regex>
#include <dirent.h>

#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "src/core/SkTaskGroup.h"  // Skia's high-level task management

// Shared animation controller for cross-platform SMIL parsing and playback
#include "../shared/SVGAnimationController.h"

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

// Graphite next-gen GPU backend (Vulkan on Linux, default, use --cpu to disable)
#include "graphite_context.h"

// Remote control server for programmatic control via TCP/JSON
#include "remote_control.h"

// Required for getcwd() and PATH_MAX
#include <unistd.h>
#include <limits.h>

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
static bool g_jsonOutput = false;  // --json flag for benchmark JSON output
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

// Signal handler for graceful shutdown (SIGINT, SIGTERM)
void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_shutdownRequested.store(true);
        std::cerr << "\nShutdown requested (signal " << signum << ")..." << std::endl;
    }
}

// Install signal handlers for graceful shutdown
void installSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
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
            svgplayer::SVGAnimationController localController;
            std::string preprocessedSvg = localController.getPreprocessedContent(svgToParse);

            // Debug: Check if any <animate> tags exist in the browser SVG
            size_t animateCount = 0;
            size_t pos = 0;
            while ((pos = preprocessedSvg.find("<animate", pos)) != std::string::npos) {
                animateCount++;
                pos++;
            }
            if (animateCount > 0) {
                std::cout << "DEBUG: Found " << animateCount << " <animate> tags in browser SVG" << std::endl;
            }

            // Parse SVG DOM from PREPROCESSED content (includes synthetic IDs)
            sk_sp<SkData> browserData = SkData::MakeWithCopy(preprocessedSvg.data(), preprocessedSvg.size());
            if (!browserData) {
                std::cerr << "Failed to create SkData for browser SVG" << std::endl;
                g_browserDomParsing.store(false);
                return;  // Early exit from thread - DOM parse failed
            }

            std::unique_ptr<SkMemoryStream> browserStream = SkMemoryStream::Make(browserData);
            if (!browserStream) {
                std::cerr << "Failed to create SkMemoryStream for browser SVG" << std::endl;
                g_browserDomParsing.store(false);
                return;  // Early exit from thread - stream creation failed
            }

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

// Check if async DOM parse completed and swap in new DOM (NON-BLOCKING!)
// Returns true if a new DOM was swapped in
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

// Stop async DOM parsing (blocks until current parse completes)
void stopAsyncBrowserDomParse() {
    if (g_browserDomParseThread.joinable()) {
        // Wait for current parse to complete (can't cancel mid-parse)
        g_browserDomParseThread.join();
    }
    g_browserDomParsing.store(false);
    g_browserDomReady.store(false);
}

// Clear browser animations (call when closing browser)
void clearBrowserAnimations() {
    std::lock_guard<std::mutex> lock(g_browserAnimMutex);
    g_browserAnimations.clear();
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

    // Sort by frame number (unknown numbers at the end, alphabetically)
    std::sort(frameFiles.begin(), frameFiles.end(),
              [](const auto& a, const auto& b) {
                  if (a.first == -1 && b.first == -1) return a.second < b.second;
                  if (a.first == -1) return false;
                  if (b.first == -1) return true;
                  return a.first < b.first;
              });

    std::vector<std::string> result;
    result.reserve(frameFiles.size());
    for (const auto& [num, path] : frameFiles) {
        result.push_back(path);
    }
    return result;
}

// Get file size in bytes
size_t getFileSize(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
    return 0;
}

// Maximum SVG file size (100 MB - reasonable limit to prevent memory issues)
static constexpr size_t MAX_SVG_FILE_SIZE = 100 * 1024 * 1024;

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

// Print extensive help screen
void printHelp(const char* programName) {
    std::cerr << SVGPlayerVersion::getVersionBanner() << "\n\n";
    std::cerr << "USAGE:\n";
    std::cerr << "    " << programName << " <input.svg> [OPTIONS]\n\n";
    std::cerr << "DESCRIPTION:\n";
    std::cerr << "    Real-time SVG renderer with SMIL animation support (Linux).\n";
    std::cerr << "    Plays animated SVG files with discrete frame animations\n";
    std::cerr << "    (xlink:href switching) using OpenGL/EGL rendering.\n\n";
    std::cerr << "OPTIONS:\n";
    std::cerr << "    -h, --help        Show this help message and exit\n";
    std::cerr << "    -v, --version     Show version information and exit\n";
    std::cerr << "    -f, --fullscreen  Start in fullscreen mode\n";
    std::cerr << "    -w, --windowed    Start in windowed mode (default)\n";
    std::cerr << "    -m, --maximize    Start with window maximized\n";
    std::cerr << "    --pos=X,Y         Set initial window position\n";
    std::cerr << "    --size=WxH        Set initial window size (0 = use SVG size)\n";
    std::cerr << "    --cpu             Use CPU raster rendering instead of Graphite GPU\n";
    std::cerr << "    --sequential      Sequential animation mode\n";
    std::cerr << "    --duration=SECS   Benchmark duration in seconds (auto-exit)\n";
    std::cerr << "    --benchmark[=N]   Run N frames then exit (default: 300 frames)\n";
    std::cerr << "    --screenshot=PATH Take screenshot and save to PATH\n";
    std::cerr << "    --json            Output benchmark results as JSON\n";
    std::cerr << "    --remote-control[=PORT]  Enable remote control server (default port: 9999)\n\n";
    std::cerr << "KEYBOARD CONTROLS:\n";
    std::cerr << "    Space         Play/Pause animation\n";
    std::cerr << "    R             Restart animation from beginning\n";
    std::cerr << "    F             Toggle fullscreen mode\n";
    std::cerr << "    Left/Right    Seek backward/forward 1 second\n";
    std::cerr << "    Up/Down       Speed up/slow down playback\n";
    std::cerr << "    L             Toggle loop mode\n";
    std::cerr << "    P             Toggle parallel rendering mode\n";
    std::cerr << "    S             Show/hide statistics overlay\n";
    std::cerr << "    Q, Escape     Quit player\n\n";
    std::cerr << "SUPPORTED FORMATS:\n";
    std::cerr << "    - SVG 1.1 with SMIL animations\n";
    std::cerr << "    - Discrete frame animations via xlink:href\n";
    std::cerr << "    - FBF (Frame-by-Frame) SVG format\n\n";
    std::cerr << "EXAMPLES:\n";
    std::cerr << "    " << programName << " animation.svg\n";
    std::cerr << "    " << programName << " animation.svg --fullscreen\n";
    std::cerr << "    " << programName << " --version\n\n";
    std::cerr << "BUILD INFO:\n";
    std::cerr << "    " << FBFSVG_PLAYER_BUILD_INFO << "\n";
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
    // Platform-specific font manager (FontConfig on Linux, CoreText on macOS/iOS)
    g_fontMgr = createPlatformFontMgr();
    // Use the best available text shaper
    g_shaperFactory = SkShapers::BestAvailable();
}

// Create SVG DOM with proper font support for text rendering
// This must be used instead of SkSVGDOM::MakeFromStream to enable SVG <text> elements
sk_sp<SkSVGDOM> makeSVGDOMWithFontSupport(SkStream& stream) {
    return SkSVGDOM::Builder().setFontManager(g_fontMgr).setTextShapingFactory(g_shaperFactory).make(stream);
}

// SMILAnimation struct is now defined in shared/SVGAnimationController.h

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
    static constexpr size_t MAX_BUFFER_SIZE = 30;
    static constexpr size_t LOOKAHEAD_FRAMES = 10;  // How many frames to pre-render ahead

    // Shared rendering resources (protected by paramsMutex for thread safety)
    std::mutex paramsMutex;  // Protects svgData, renderWidth/Height, svgWidth/Height, animations
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
        // Thread-safe: protect writes to shared configuration
        std::lock_guard<std::mutex> lock(paramsMutex);
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
        // Thread-safe: protect reads and writes to renderWidth/Height
        {
            std::lock_guard<std::mutex> lock(paramsMutex);
            if (width == renderWidth && height == renderHeight) return;
            renderWidth = width;
            renderHeight = height;
        }

        // Clear all pre-buffered frames since they're now the wrong size
        std::lock_guard<std::mutex> lock(bufferMutex);
        frameBuffer.clear();
    }

    void start(const std::string& svgContent, int width, int height, int svgW, int svgH,
               ParallelMode initialMode = ParallelMode::PreBuffer) {
        if (mode != ParallelMode::Off) return;

        // Thread-safe: protect writes to shared configuration
        {
            std::lock_guard<std::mutex> lock(paramsMutex);
            svgData = svgContent;
            renderWidth = width;
            renderHeight = height;
            svgWidth = svgW;
            svgHeight = svgH;
        }
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

        // Clear executor (waits for pending tasks to complete)
        if (executor) {
            executor.reset();
            SkExecutor::SetDefault(nullptr);
        }

        // Clear worker caches (safe now that executor is stopped)
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

        // Request next LOOKAHEAD_FRAMES frames
        for (size_t i = 1; i <= LOOKAHEAD_FRAMES; i++) {
            size_t frameIdx = (currentFrame + i) % totalFrames;
            requestFrame(frameIdx);
        }

        // Clean old frames
        clearOldFrames(currentFrame);
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
            if (frameBuffer.size() >= MAX_BUFFER_SIZE) return;
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
        std::thread::id threadId = std::this_thread::get_id();

        // Thread-safe: copy shared configuration under lock for use in this render
        // This prevents data races when main thread calls configure()/resize()
        std::string localSvgData;
        int localRenderWidth, localRenderHeight, localSvgWidth, localSvgHeight;
        std::vector<SMILAnimation> localAnimations;
        {
            std::lock_guard<std::mutex> lock(paramsMutex);
            localSvgData = svgData;
            localRenderWidth = renderWidth;
            localRenderHeight = renderHeight;
            localSvgWidth = svgWidth;
            localSvgHeight = svgHeight;
            localAnimations = animations;  // Deep copy for thread safety
        }

        // Early exit if no data to render
        if (localSvgData.empty() || localRenderWidth <= 0 || localRenderHeight <= 0) {
            return;
        }

        // Get or create cached DOM and surface for this worker thread
        WorkerCache* cache = nullptr;
        {
            std::lock_guard<std::mutex> lock(workerCacheMutex);
            cache = &workerCaches[threadId];
        }

        // Parse SVG once per worker thread (first call only)
        // Use makeSVGDOMWithFontSupport to ensure SVG text elements render properly
        if (!cache->dom) {
            auto stream = SkMemoryStream::MakeDirect(localSvgData.data(), localSvgData.size());
            cache->dom = makeSVGDOMWithFontSupport(*stream);
            if (!cache->dom) return;
            cache->dom->setContainerSize(SkSize::Make(localSvgWidth, localSvgHeight));
        }

        // Recreate surface if size changed
        if (!cache->surface || cache->surfaceWidth != localRenderWidth || cache->surfaceHeight != localRenderHeight) {
            cache->surface = SkSurfaces::Raster(
                SkImageInfo::Make(localRenderWidth, localRenderHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
            cache->surfaceWidth = localRenderWidth;
            cache->surfaceHeight = localRenderHeight;
            if (!cache->surface) return;
        }

        // Apply ALL animation states for this specific time point
        // Each animation calculates its own frame based on elapsed time, not frame index
        // This correctly handles animations with different durations and frame counts
        for (const auto& anim : localAnimations) {
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
        canvas->clear(SK_ColorWHITE);

        // Apply same transform as main render loop
        // Use effectiveSvg* to handle edge case where svgWidth/Height is 0 (fallback to render dimensions)
        int effectiveSvgW = (localSvgWidth > 0) ? localSvgWidth : localRenderWidth;
        int effectiveSvgH = (localSvgHeight > 0) ? localSvgHeight : localRenderHeight;
        float scaleX = static_cast<float>(localRenderWidth) / effectiveSvgW;
        float scaleY = static_cast<float>(localRenderHeight) / effectiveSvgH;
        float scale = std::min(scaleX, scaleY);
        float offsetX = (localRenderWidth - effectiveSvgW * scale) / 2.0f;
        float offsetY = (localRenderHeight - effectiveSvgH * scale) / 2.0f;

        canvas->save();
        canvas->translate(offsetX, offsetY);
        canvas->scale(scale, scale);
        cache->dom->render(canvas);
        canvas->restore();

        SkPixmap pixmap;
        if (cache->surface->peekPixels(&pixmap)) {
            frame->pixels.resize(localRenderWidth * localRenderHeight);
            memcpy(frame->pixels.data(), pixmap.addr(), localRenderWidth * localRenderHeight * sizeof(uint32_t));
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

    // Called from main thread - safely copy front buffer to destination under lock
    // Uses atomic exchange to avoid race condition where we might count the same frame twice
    // Returns true if frame was ready and copied, false otherwise
    bool copyFrontBufferIfReady(void* dest, size_t destSizeBytes) {
        // Atomically check AND clear frameReady - returns previous value
        bool wasReady = frameReady.exchange(false);
        if (!wasReady) return false;

        // Copy under lock to prevent use-after-free if render thread swaps buffers
        std::lock_guard<std::mutex> lock(bufferMutex);
        size_t srcSize = frontBuffer.size() * sizeof(uint32_t);
        size_t copySize = std::min(destSizeBytes, srcSize);
        if (copySize > 0 && dest != nullptr) {
            memcpy(dest, frontBuffer.data(), copySize);
        }
        return true;
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

        while (running) {
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
                    std::cout << "Parallel mode: " << getParallelModeName(parallelRenderer->mode);
                    if (parallelRenderer->mode != ParallelMode::Off) {
                        std::cout << " (" << cachedActiveWorkers.load() << " threads)";
                    }
                    std::cout << std::endl;
                }
                continue;
            }

            if (!newFrameRequested) continue;
            newFrameRequested = false;

            // Get render parameters and animation states
            std::string localSvgData;
            int localWidth, localHeight, localSvgW, localSvgH;
            size_t localFrameIndex;
            std::vector<AnimState> localAnimStates;
            {
                std::lock_guard<std::mutex> lock(paramsMutex);
                localSvgData = svgData;
                localWidth = renderWidth;
                localHeight = renderHeight;
                localSvgW = svgWidth;
                localSvgH = svgHeight;
                localFrameIndex = currentFrameIndex;
                localAnimStates = animationStates;
            }

            // Integer overflow protection: validate dimensions before buffer calculations
            // Maximum dimension matches saveScreenshotPPM limit (32768x32768 = 1 gigapixel)
            constexpr int MAX_RENDER_DIM = 32768;
            if (localWidth <= 0 || localHeight <= 0 ||
                localWidth > MAX_RENDER_DIM || localHeight > MAX_RENDER_DIM) continue;

            renderInProgress = true;
            renderTimedOut = false;
            auto renderStart = Clock::now();

            // === RENDER WITH TIMEOUT WATCHDOG ===
            bool renderSuccess = false;

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

                // Recreate DOM if needed (or first time)
                // Use makeSVGDOMWithFontSupport to ensure SVG text elements render properly
                if (!threadDom) {
                    auto stream = SkMemoryStream::MakeDirect(localSvgData.data(), localSvgData.size());
                    threadDom = makeSVGDOMWithFontSupport(*stream);
                }

                if (threadSurface && threadDom) {
                    threadDom->setContainerSize(SkSize::Make(localSvgW, localSvgH));

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

                    SkCanvas* canvas = threadSurface->getCanvas();
                    canvas->clear(SK_ColorWHITE);

                    // Calculate transform
                    // Use effectiveSvg* to handle edge case where svgWidth/Height is 0 (fallback to render dimensions)
                    int effectiveSvgW = (localSvgW > 0) ? localSvgW : localWidth;
                    int effectiveSvgH = (localSvgH > 0) ? localSvgH : localHeight;
                    float scaleX = static_cast<float>(localWidth) / effectiveSvgW;
                    float scaleY = static_cast<float>(localHeight) / effectiveSvgH;
                    float scale = std::min(scaleX, scaleY);
                    float offsetX = (localWidth - effectiveSvgW * scale) / 2.0f;
                    float offsetY = (localHeight - effectiveSvgH * scale) / 2.0f;

                    canvas->save();
                    canvas->translate(offsetX, offsetY);
                    canvas->scale(scale, scale);

                    // Check timeout before expensive render
                    auto elapsed =
                        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - renderStart).count();

                    if (elapsed < RENDER_TIMEOUT_MS) {
                        threadDom->render(canvas);
                        renderSuccess = true;
                    } else {
                        renderTimedOut = true;
                        timeoutCount++;
                    }

                    canvas->restore();

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
            }

            // Swap buffers if render succeeded
            if (renderSuccess && !renderTimedOut) {
                std::lock_guard<std::mutex> lock(bufferMutex);
                std::swap(frontBuffer, backBuffer);
                frameReady = true;
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
    }
};

// ============================================================================
// Animation parsing - delegated to shared SVGAnimationController
// ============================================================================

// Global animation controller instance for parsing
static svgplayer::SVGAnimationController g_animController;

// Pre-process SVG to inject IDs and convert symbols (delegates to shared controller)
std::string preprocessSVGForAnimation(const std::string& content, std::map<size_t, std::string>& syntheticIds) {
    // Use the shared controller to preprocess the SVG
    g_animController.loadFromContent(content);
    return g_animController.getProcessedContent();
}

// Extract SMIL animations from SVG content string (delegates to shared controller)
std::vector<SMILAnimation> extractAnimationsFromContent(const std::string& content) {
    g_animController.loadFromContent(content);
    return g_animController.getAnimations();
}

// Original interface - reads file and extracts animations (delegates to shared controller)
std::vector<SMILAnimation> extractAnimations(const std::string& svgPath) {
    if (!g_animController.loadFromFile(svgPath)) {
        std::cerr << "Cannot open file for animation parsing: " << svgPath << std::endl;
        return {};
    }
    return g_animController.getAnimations();
}

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
    file.close();

    return true;
}

// Generate timestamped screenshot filename with resolution
std::string generateScreenshotFilename(int width, int height) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream ss;
    ss << "screenshot_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
       << std::setw(3) << ms.count() << "_" << width << "x" << height << ".ppm";
    return ss.str();
}

// =============================================================================
// SVG File Loading with Error Codes
// =============================================================================

// SVG loading error codes for detailed error handling
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
    if (hasViewBox && viewBox->width() > 0 && viewBox->height() > 0) {
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

    // Print startup banner (always shown on execution)
    std::cerr << SVGPlayerVersion::getStartupBanner() << std::endl;

    // Parse command-line arguments
    const char* inputPath = nullptr;
    bool startFullscreen = false;
    bool startMaximized = false;        // --maximize flag
    int startPosX = SDL_WINDOWPOS_UNDEFINED;   // --pos=X,Y
    int startPosY = SDL_WINDOWPOS_UNDEFINED;   // --pos=X,Y
    int startWidth = 0;                 // --size=WxH (0 = use SVG size)
    int startHeight = 0;                // --size=WxH (0 = use SVG size)
    bool sequentialMode = false;        // --sequential
    bool isImageSequence = false;       // Playing from folder of individual SVG files
    std::vector<std::string> sequenceFiles;  // List of SVG files for image sequence mode
    std::vector<std::string> sequenceSvgContents;  // Pre-loaded SVG contents for image sequence mode
    int benchmarkDuration = 0;          // --duration=SECS
    int benchmarkFrames = 0;            // --benchmark=N (run N frames then exit, default 300)
    std::string screenshotPath;         // --screenshot=PATH
    bool useGraphiteBackend = true;  // Graphite GPU backend (Vulkan on Linux)
    bool remoteControlEnabled = false;  // --remote-control flag
    int remoteControlPort = 9999;       // --remote-control[=PORT] port

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            // Show full version info and exit
            std::cerr << SVGPlayerVersion::getVersionBanner() << std::endl;
            std::cerr << "Build: " << FBFSVG_PLAYER_BUILD_INFO << std::endl;
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            // Show extensive help and exit
            printHelp(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            startFullscreen = true;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            // Use CPU raster rendering instead of Graphite GPU
            useGraphiteBackend = false;
            std::cerr << "CPU raster rendering enabled (Graphite GPU disabled)\n";
        } else if (strcmp(argv[i], "--windowed") == 0 || strcmp(argv[i], "-w") == 0) {
            startFullscreen = false;
        } else if (strcmp(argv[i], "--maximize") == 0 || strcmp(argv[i], "-m") == 0) {
            startMaximized = true;
            startFullscreen = false;  // Maximize implies windowed mode
        } else if (strncmp(argv[i], "--pos=", 6) == 0) {
            int x, y;
            if (sscanf(argv[i] + 6, "%d,%d", &x, &y) == 2) {
                startPosX = x;
                startPosY = y;
            } else {
                std::cerr << "Invalid position format: " << argv[i] << " (use --pos=X,Y)" << std::endl;
                return 1;
            }
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            int w, h;
            if (sscanf(argv[i] + 7, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                startWidth = w;
                startHeight = h;
            } else {
                std::cerr << "Invalid size format: " << argv[i] << " (use --size=WxH)" << std::endl;
                return 1;
            }
        } else if (strncmp(argv[i], "--duration=", 11) == 0) {
            benchmarkDuration = atoi(argv[i] + 11);
            if (benchmarkDuration <= 0) {
                std::cerr << "Invalid duration: must be positive" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--benchmark") == 0) {
            // Default to 300 frames
            benchmarkFrames = 300;
        } else if (strncmp(argv[i], "--benchmark=", 12) == 0) {
            benchmarkFrames = atoi(argv[i] + 12);
            if (benchmarkFrames <= 0) {
                std::cerr << "Invalid benchmark frame count: must be positive" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--json") == 0) {
            g_jsonOutput = true;
        } else if (strncmp(argv[i], "--screenshot=", 13) == 0) {
            screenshotPath = argv[i] + 13;
            if (screenshotPath.empty()) {
                std::cerr << "--screenshot requires a file path" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--sequential") == 0) {
            sequentialMode = true;
        } else if (strcmp(argv[i], "--remote-control") == 0) {
            remoteControlEnabled = true;
        } else if (strncmp(argv[i], "--remote-control=", 17) == 0) {
            remoteControlEnabled = true;
            remoteControlPort = atoi(argv[i] + 17);
            if (remoteControlPort <= 0 || remoteControlPort > 65535) {
                std::cerr << "Invalid remote control port: " << (argv[i] + 17) << std::endl;
                return 1;
            }
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

    // Input file is required
    if (!inputPath) {
        std::cerr << "Error: No input file specified.\n" << std::endl;
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
        if (!g_jsonOutput) {
            std::cerr << "Image sequence mode: Found " << sequenceFiles.size() << " SVG frames" << std::endl;
        }
        // Pre-load all SVG file contents for image sequence mode
        sequenceSvgContents.reserve(sequenceFiles.size());
        for (const auto& filePath : sequenceFiles) {
            std::ifstream seqFile(filePath);
            if (!seqFile) {
                std::cerr << "Error: Cannot read file: " << filePath << std::endl;
                continue;
            }
            std::stringstream seqBuffer;
            seqBuffer << seqFile.rdbuf();
            sequenceSvgContents.push_back(seqBuffer.str());
        }
        if (!g_jsonOutput) {
            std::cerr << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;
        }
        // Use first file for initial SVG dimensions
        inputPath = sequenceFiles[0].c_str();
    }

    // Validate input file before processing
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
        std::cerr << "Error: File too large (" << (fileSize / (1024 * 1024)) << " MB). Maximum: "
                  << (MAX_SVG_FILE_SIZE / (1024 * 1024)) << " MB" << std::endl;
        return 1;
    }

    // Read the SVG file content
    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << inputPath << std::endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string originalContent = buffer.str();
    file.close();

    // Validate SVG content
    if (!validateSVGContent(originalContent)) {
        std::cerr << "Error: File does not appear to be a valid SVG: " << inputPath << std::endl;
        return 1;
    }

    // Pre-process SVG to inject IDs into <use> elements that contain <animate> but lack IDs
    // This is necessary for panther_bird.fbf.svg and similar files where <use> has no id
    std::cout << "Parsing SMIL animations..." << std::endl;
    std::map<size_t, std::string> syntheticIds;
    std::string processedContent = preprocessSVGForAnimation(originalContent, syntheticIds);

    // Extract animations from the preprocessed content
    std::vector<SMILAnimation> animations = extractAnimationsFromContent(processedContent);

    if (animations.empty()) {
        std::cout << "No SMIL animations found - will render static SVG" << std::endl;
    } else {
        std::cout << "Found " << animations.size() << " animation(s)" << std::endl;
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
        } else {
            std::cout << "Found target element: " << anim.targetId << std::endl;
        }
    }

    // Get SVG intrinsic dimensions
    SkSize defaultSize = SkSize::Make(800, 600);
    SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));

    int svgWidth = (svgSize.width() > 0) ? static_cast<int>(svgSize.width()) : 800;
    int svgHeight = (svgSize.height() > 0) ? static_cast<int>(svgSize.height()) : 600;
    float aspectRatio = static_cast<float>(svgWidth) / svgHeight;

    std::cout << "SVG dimensions: " << svgWidth << "x" << svgHeight << std::endl;
    std::cout << "Aspect ratio: " << aspectRatio << std::endl;

    // Initialize SDL with hints to reduce stutters
    // Force OpenGL renderer on Linux for better performance
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    // Enable render batching for better throughput
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    // Use linear (bilinear) filtering for texture scaling - prevents pixelation
    // "0" = nearest, "1" = linear, "2" = best (anisotropic if available)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create window at SVG native resolution (or scaled if too large)
    int windowWidth = svgWidth;
    int windowHeight = svgHeight;

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
        std::cout << "Native display: " << displayMode.w << "x" << displayMode.h << " @ " << displayMode.refresh_rate
                  << "Hz" << std::endl;
    }

    // Window creation with optional exclusive fullscreen
    // For fullscreen: use native display resolution
    // For windowed: use SVG-based dimensions
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int createWidth = windowWidth;
    int createHeight = windowHeight;
    if (startFullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
        // Use native display resolution for fullscreen
        createWidth = displayMode.w;
        createHeight = displayMode.h;
    }
    SDL_Window* window = SDL_CreateWindow("SVG Player (Animated) - Skia", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, createWidth, createHeight, windowFlags);

    // Track fullscreen state (matches command line flag)
    bool isFullscreen = startFullscreen;

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // VSync state
    bool vsyncEnabled = false;

    // Create renderer (initially without VSync)
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Get actual renderer output size (accounts for HiDPI/Retina)
    int rendererW, rendererH;
    SDL_GetRendererOutputSize(renderer, &rendererW, &rendererH);
    // HiDPI scale = renderer pixels / window logical pixels
    // For fullscreen: use createWidth/Height (native display), for windowed: use windowWidth/Height
    float hiDpiScale = static_cast<float>(rendererW) / createWidth;
    std::cout << "HiDPI scale factor: " << std::fixed << std::setprecision(4) << hiDpiScale << std::endl;

    // Query display refresh rate for frame limiter
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode refreshDisplayMode;  // Renamed to avoid shadowing displayMode
    int displayRefreshRate = 60;         // Default fallback
    if (SDL_GetCurrentDisplayMode(displayIndex, &refreshDisplayMode) == 0) {
        displayRefreshRate = refreshDisplayMode.refresh_rate > 0 ? refreshDisplayMode.refresh_rate : 60;
    }
    std::cout << "Display refresh rate: " << displayRefreshRate << " Hz" << std::endl;

    // Setup font for debug overlay (platform-specific font manager)
    sk_sp<SkFontMgr> fontMgr = createPlatformFontMgr();
    // Try common monospace fonts available on Linux
    sk_sp<SkTypeface> typeface = fontMgr->matchFamilyStyle("DejaVu Sans Mono", SkFontStyle::Normal());
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle("Liberation Mono", SkFontStyle::Normal());
    }
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle("Monospace", SkFontStyle::Normal());
    }
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle("Courier", SkFontStyle::Normal());
    }
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
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

    // PreBuffer mode total animation metrics (used by loadSVGFile and parallel renderer)
    size_t preBufferTotalFrames = 1;
    double preBufferTotalDuration = 1.0;

    // Frame skip tracking for synchronization verification
    size_t framesRendered = 0;  // Actual frames we rendered
    size_t framesSkipped = 0;   // Frames skipped due to slow rendering
    size_t lastRenderedAnimFrame = 0;

    // Sequential frame mode counter - increments each cycle instead of using wall-clock time
    // When sequentialMode is true, frames are rendered 0,1,2,3... as fast as possible
    size_t sequentialFrameCounter = 0;

    // Stress test mode (press 'S' to toggle)
    bool stressTestEnabled = false;

    // Use SteadyClock for animation (monotonic, immune to clock adjustments)
    auto animationStartTimeSteady = SteadyClock::now();

    // Current render dimensions (in actual pixels, not logical points)
    // Use renderer output size directly - this accounts for HiDPI/Retina and fullscreen resolution
    int renderWidth = rendererW;
    int renderHeight = rendererH;

    // Create initial texture
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, renderWidth, renderHeight);

    // Skia surface
    sk_sp<SkSurface> surface;

    // Graphite context for next-gen GPU rendering (Vulkan on Linux)
    std::unique_ptr<svgplayer::GraphiteContext> graphiteContext;

    // Initialize Graphite if requested (with CPU fallback)
    if (useGraphiteBackend) {
        graphiteContext = svgplayer::createGraphiteContext(window);
        if (graphiteContext && graphiteContext->isInitialized()) {
            std::cout << "[Graphite] Next-gen GPU backend enabled - "
                      << graphiteContext->getBackendName() << " rendering active" << std::endl;
        } else {
            std::cerr << "[Graphite] Failed to initialize Graphite context (Vulkan), falling back to CPU raster" << std::endl;
            useGraphiteBackend = false;
        }
    }

    // Surface creation lambda (uses Graphite or CPU raster)
    auto createSurface = [&](int w, int h) {
        if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
            // Graphite creates its own surfaces
            surface = graphiteContext->createSurface(w, h);
        } else {
            // CPU raster fallback
            SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(w, h);
            surface = SkSurfaces::Raster(imageInfo);
        }
        return surface != nullptr;
    };

    if (!createSurface(renderWidth, renderHeight)) {
        std::cerr << "Failed to create Skia surface" << std::endl;
        if (graphiteContext) graphiteContext.reset();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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

    // Initialize parallel renderer with SVG data, ALL animations, and timing info
    // PreBuffer mode uses time-based frame calculation for multi-animation sync
    parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                               animations, maxDuration, maxFrames);

    // Start parallel renderer in PreBuffer mode by default (best for animations)
    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);

    // Threaded renderer keeps UI responsive by moving all rendering to background thread
    // Main thread ONLY handles events and blits completed frames - NEVER blocks on rendering
    ThreadedRenderer threadedRenderer;
    threadedRenderer.configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
    threadedRenderer.start();

    // Initialize cached mode state to reflect PreBuffer is ON by default
    threadedRenderer.cachedPreBufferMode = true;
    threadedRenderer.cachedActiveWorkers = parallelRenderer.activeWorkers.load();

    // Set total animation frames so PreBuffer mode can pre-render ahead
    threadedRenderer.setTotalAnimationFrames(maxFrames);

    // Suppress startup messages in JSON benchmark mode
    if (!g_jsonOutput) {
        std::cout << "\nCPU cores detected: " << totalCores << std::endl;
        std::cout << "Skia thread pool size: " << availableCores << " (1 reserved for system)" << std::endl;
        std::cout << "PreBuffer mode: ON (default)" << std::endl;
        std::cout << "UI thread: Non-blocking (render thread active)" << std::endl;

        std::cout << "\nControls:" << std::endl;
        std::cout << "  ESC/Q - Quit" << std::endl;
        std::cout << "  SPACE - Pause/Resume animation" << std::endl;
        std::cout << "  D - Toggle debug info overlay" << std::endl;
        std::cout << "  G - Toggle fullscreen mode" << std::endl;
        std::cout << "  S - Toggle stress test (50ms delay per frame)" << std::endl;
        std::cout << "  V - Toggle VSync" << std::endl;
        std::cout << "  T - Toggle frame limiter (Timing - " << displayRefreshRate << " FPS cap)" << std::endl;
        std::cout << "  P - Toggle parallel mode: Off <-> PreBuffer" << std::endl;
        std::cout << "      Off: Direct single-threaded rendering" << std::endl;
        std::cout << "      PreBuffer: Pre-render animation frames ahead using thread pool" << std::endl;
        std::cout << "  R - Reset statistics" << std::endl;
        std::cout << "  C - Capture screenshot (PPM format, uncompressed)" << std::endl;
        std::cout << "  Resize window to change render resolution" << std::endl;
        std::cout << "\nSMIL Sync Guarantee:" << std::endl;
        std::cout << "  Animation timing uses steady_clock (monotonic)" << std::endl;
        std::cout << "  Frame shown = f(current_time), NOT f(frame_count)" << std::endl;
        std::cout << "  If rendering is slow, frames SKIP but sync is PERFECT" << std::endl;
        std::cout << "  Press 'S' to enable stress test and verify sync" << std::endl;
        std::cout << "\nNote: Occasional stutters may be caused by Linux system tasks." << std::endl;
        std::cout << "      Animation sync remains correct even during stutters." << std::endl;
        std::cout << "\nRendering..." << std::endl;
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

        // Maximize - toggle maximize/restore (Linux uses SDL directly)
        remoteServer->registerHandler(RemoteCommand::Maximize, [window](const std::string&) {
            Uint32 flags = SDL_GetWindowFlags(window);
            bool isMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            if (isMaximized) {
                SDL_RestoreWindow(window);
                std::cout << "Remote: Window RESTORED" << std::endl;
            } else {
                SDL_MaximizeWindow(window);
                std::cout << "Remote: Window MAXIMIZED" << std::endl;
            }
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
        remoteServer->registerHandler(RemoteCommand::Screenshot, [&threadedRenderer, &renderWidth, &renderHeight](const std::string& params) {
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
            if (threadedRenderer.getFrameForScreenshot(screenshotPixels, screenshotWidth, screenshotHeight)) {
                if (path.empty()) {
                    // Generate default filename with dimensions
                    path = generateScreenshotFilename(screenshotWidth, screenshotHeight);
                }
                if (saveScreenshotPPM(screenshotPixels, screenshotWidth, screenshotHeight, path)) {
                    std::cout << "Remote: Screenshot saved to " << path << std::endl;
                    return json::success("\"" + path + "\"");
                }
            }
            return json::error("Failed to capture screenshot");
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

    while (running && !g_shutdownRequested.load()) {
        auto frameStart = Clock::now();
        displayCycles++;  // Count every main loop iteration (display refresh attempt)

        // Benchmark mode: exit after specified duration
        if (benchmarkDuration > 0) {
            auto elapsed = std::chrono::duration<double>(SteadyClock::now() - benchmarkStartTime).count();
            if (elapsed >= benchmarkDuration) {
                running = false;
                break;
            }
        }

        // Benchmark mode: exit after specified number of frames
        if (benchmarkFrames > 0 && static_cast<int>(displayCycles) >= benchmarkFrames) {
            running = false;
            break;
        }

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
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        // Exit browser mode if active, otherwise quit application
                        if (g_browserMode) {
                            g_browserMode = false;
                            g_browserSvgDom = nullptr;
                            clearBrowserAnimations();
                            std::cout << "Browser closed" << std::endl;
                        } else {
                            running = false;
                        }
                    } else if (event.key.keysym.sym == SDLK_q) {
                        // Q always quits, even in browser mode
                        running = false;
                    } else if (event.key.keysym.sym == SDLK_LEFT && g_browserMode) {
                        // Previous page in browser mode
                        g_folderBrowser.prevPage();
                        g_folderBrowser.markDirty();
                    } else if (event.key.keysym.sym == SDLK_RIGHT && g_browserMode) {
                        // Next page in browser mode
                        g_folderBrowser.nextPage();
                        g_folderBrowser.markDirty();
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
                        std::cout << "Stress test: " << (stressTestEnabled ? "ON (50ms delay)" : "OFF") << std::endl;
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
                        sequentialFrameCounter = 0;  // Reset sequential counter on stats reset
                        skipStatsThisFrame = true;  // Don't pollute fresh stats with reset operation time
                        std::cout << "Statistics reset" << std::endl;
                    } else if (event.key.keysym.sym == SDLK_v) {
                        // Toggle VSync by recreating renderer
                        vsyncEnabled = !vsyncEnabled;

                        SDL_DestroyTexture(texture);
                        SDL_DestroyRenderer(renderer);

                        // Set VSync hint BEFORE creating renderer (helps ensure VSync state is respected)
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

                        std::cout << "VSync: " << (vsyncEnabled ? "ON" : "OFF") << std::endl;
                    } else if (event.key.keysym.sym == SDLK_t) {
                        // Toggle frame limiter (T key for Timing limiter)
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
                        std::cout << "Frame limiter: "
                                  << (frameLimiterEnabled ? "ON (" + std::to_string(displayRefreshRate) + " FPS cap)"
                                                          : "OFF")
                                  << std::endl;
                    } else if (event.key.keysym.sym == SDLK_p) {
                        // Toggle parallel mode: Off <-> PreBuffer (NON-BLOCKING!)
                        // Request is queued for render thread - main thread never blocks
                        threadedRenderer.requestModeChange();

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
                        // Toggle fullscreen mode (F or G key - exclusive fullscreen)
                        isFullscreen = !isFullscreen;
                        if (isFullscreen) {
                            // Use SDL_WINDOW_FULLSCREEN for exclusive fullscreen (no compositor, direct display)
                            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                        } else {
                            // Back to windowed mode
                            SDL_SetWindowFullscreen(window, 0);
                        }
                        std::cout << "Fullscreen: " << (isFullscreen ? "ON (exclusive)" : "OFF") << std::endl;
                    } else if (event.key.keysym.sym == SDLK_m) {
                        // Toggle maximize/zoom (M for Maximize)
                        // Only works in windowed mode, not in fullscreen
                        if (!isFullscreen) {
                            bool nowMaximized = toggleWindowMaximize(window);
                            std::cout << "Window: " << (nowMaximized ? "MAXIMIZED" : "RESTORED") << std::endl;
                            skipStatsThisFrame = true;
                        } else {
                            std::cout << "Exit fullscreen first (press F)" << std::endl;
                        }
                    } else if (event.key.keysym.sym == SDLK_d) {
                        // Toggle debug overlay
                        showDebugOverlay = !showDebugOverlay;
                        std::cout << "Debug overlay: " << (showDebugOverlay ? "ON" : "OFF") << std::endl;
                    } else if (event.key.keysym.sym == SDLK_c) {
                        // Capture screenshot - exact rendered frame at current resolution
                        std::vector<uint32_t> screenshotPixels;
                        int screenshotWidth, screenshotHeight;
                        if (threadedRenderer.getFrameForScreenshot(screenshotPixels, screenshotWidth,
                                                                   screenshotHeight)) {
                            std::string filename = generateScreenshotFilename(screenshotWidth, screenshotHeight);
                            if (saveScreenshotPPM(screenshotPixels, screenshotWidth, screenshotHeight, filename)) {
                                std::cout << "Screenshot saved: " << filename << std::endl;
                            }
                        } else {
                            std::cerr << "Screenshot failed: no frame available" << std::endl;
                        }
                        skipStatsThisFrame = true;  // File I/O can be slow, don't pollute stats
                    } else if (event.key.keysym.sym == SDLK_o) {
                        // Open file dialog to load a new SVG file (hot-reload)
                        // Static storage for the path string (inputPath is const char*)
                        static std::string currentFilePath;
                        std::string newPath = openSVGFileDialog("Open SVG File", "");
                        if (!newPath.empty() && fileExists(newPath.c_str())) {
                            std::cout << "\n=== Loading new SVG: " << newPath << " ===" << std::endl;

                            // Stop renderers to safely release SVG resources
                            threadedRenderer.stop();
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
                                    threadedRenderer.start();
                                } else {
                                    // I/O errors (FileSize, FileOpen) - restart with old content
                                    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                    threadedRenderer.start();
                                }
                            } else {
                                // Success - reset stats and configure renderers
                                g_animController.resetStats();

                                parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                                                          animations, preBufferTotalDuration, preBufferTotalFrames);
                                parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);

                                threadedRenderer.configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                threadedRenderer.setTotalAnimationFrames(preBufferTotalFrames);
                                threadedRenderer.start();

                                // Reset animation timing state
                                animationStartTime = Clock::now();
                                animationStartTimeSteady = SteadyClock::now();
                                pausedTime = 0;
                                lastRenderedAnimFrame = 0;
                                sequentialFrameCounter = 0;  // Reset sequential counter on file load
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
                                // Cancel button closes browser without loading
                                g_browserMode = false;
                                g_browserSvgDom = nullptr;
                                clearBrowserAnimations();
                                g_folderBrowser.clearSelection();
                                std::cout << "Browser cancelled" << std::endl;
                                break;

                            case svgplayer::HitTestResult::LoadButton:
                                // Load button loads selected SVG/FBF.SVG (if one is selected)
                                // Note: FrameFolder not supported on Linux yet (no image sequence mode)
                                if (g_folderBrowser.canLoad()) {
                                    std::optional<svgplayer::BrowserEntry> selected = g_folderBrowser.getSelectedEntry();
                                    if (selected.has_value() &&
                                        (selected->type == svgplayer::BrowserEntryType::SVGFile ||
                                         selected->type == svgplayer::BrowserEntryType::FBFSVGFile)) {
                                        std::cout << "\n=== Loading from browser (Load button): " << selected->fullPath << " ===" << std::endl;
                                        g_browserMode = false;
                                        g_browserSvgDom = nullptr;
                                        clearBrowserAnimations();

                                        static std::string currentFilePathLoad;
                                        std::string newPath = selected->fullPath;
                                        if (!newPath.empty() && fileExists(newPath.c_str())) {
                                            // Stop renderers before loading
                                            threadedRenderer.stop();
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

                                                threadedRenderer.configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                                threadedRenderer.setTotalAnimationFrames(preBufferTotalFrames);
                                                threadedRenderer.start();

                                                // Reset animation timing state
                                                animationStartTime = Clock::now();
                                                animationStartTimeSteady = SteadyClock::now();
                                                pausedTime = 0;
                                                lastRenderedAnimFrame = 0;
                                                sequentialFrameCounter = 0;  // Reset sequential counter on file load
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
                                                    threadedRenderer.start();
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
                                                g_browserMode = false;
                                                g_browserSvgDom = nullptr;
                                                clearBrowserAnimations();

                                                static std::string currentFilePath;
                                                std::string newPath = clickedEntry->fullPath;
                                                if (!newPath.empty() && fileExists(newPath.c_str())) {
                                                    // Stop renderers before loading
                                                    threadedRenderer.stop();
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

                                                        threadedRenderer.configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                                        threadedRenderer.setTotalAnimationFrames(preBufferTotalFrames);
                                                        threadedRenderer.start();

                                                        // Reset animation timing state
                                                        animationStartTime = Clock::now();
                                                        animationStartTimeSteady = SteadyClock::now();
                                                        pausedTime = 0;
                                                        lastRenderedAnimFrame = 0;
                                                        sequentialFrameCounter = 0;  // Reset sequential counter on file load
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
                                                            threadedRenderer.start();
                                                        }
                                                    }
                                                }
                                            }
                                            break;

                                        case svgplayer::BrowserEntryType::FrameFolder:
                                            // FrameFolder: folder containing numbered SVG frames
                                            if (isDoubleClick) {
                                                // Double-click: Load frame sequence folder
                                                std::cout << "\n=== Loading frame sequence: " << clickedEntry->fullPath << " ===" << std::endl;

                                                // Exit browser mode
                                                g_browserMode = false;
                                                g_browserSvgDom = nullptr;
                                                clearBrowserAnimations();

                                                // Stop renderers
                                                threadedRenderer.stop();
                                                parallelRenderer.stop();

                                                // Clear previous animation state
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
                                                        std::ifstream seqFile(filePath);
                                                        if (seqFile) {
                                                            std::stringstream seqBuffer;
                                                            seqBuffer << seqFile.rdbuf();
                                                            sequenceSvgContents.push_back(seqBuffer.str());
                                                        }
                                                    }
                                                    std::cout << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;

                                                    // Get dimensions from first frame
                                                    if (!sequenceSvgContents.empty()) {
                                                        const std::string& firstFrameContent = sequenceSvgContents[0];
                                                        sk_sp<SkData> firstData = SkData::MakeWithCopy(firstFrameContent.data(), firstFrameContent.size());
                                                        auto firstStream = SkMemoryStream::Make(firstData);
                                                        if (firstStream) {
                                                            sk_sp<SkSVGDOM> firstDom = makeSVGDOMWithFontSupport(*firstStream);
                                                            if (firstDom && firstDom->getRoot()) {
                                                                SkSVGSVG* root = firstDom->getRoot();
                                                                SkSize defaultSize = SkSize::Make(800, 600);
                                                                SkSize frameSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));
                                                                svgWidth = (frameSize.width() > 0) ? static_cast<int>(frameSize.width()) : 800;
                                                                svgHeight = (frameSize.height() > 0) ? static_cast<int>(frameSize.height()) : 600;
                                                                aspectRatio = static_cast<float>(svgWidth) / svgHeight;
                                                            }
                                                        }
                                                    }

                                                    // Reset animation timing state
                                                    animationStartTime = Clock::now();
                                                    animationStartTimeSteady = SteadyClock::now();
                                                    pausedTime = 0;
                                                    lastRenderedAnimFrame = 0;
                                                    sequentialFrameCounter = 0;
                                                    displayCycles = 0;
                                                    framesDelivered = 0;
                                                    framesSkipped = 0;
                                                    framesRendered = 0;
                                                    animationPaused = false;

                                                    // Update window title
                                                    size_t lastSlash = clickedEntry->fullPath.find_last_of("/\\");
                                                    std::string folderName = (lastSlash != std::string::npos)
                                                        ? clickedEntry->fullPath.substr(lastSlash + 1)
                                                        : clickedEntry->fullPath;
                                                    std::string windowTitle = "SVG Player - " + folderName + " (" +
                                                        std::to_string(sequenceSvgContents.size()) + " frames)";
                                                    SDL_SetWindowTitle(window, windowTitle.c_str());

                                                    std::cout << "Frame sequence loaded: " << clickedEntry->fullPath << std::endl;
                                                    std::cout << "  Frames: " << sequenceSvgContents.size() << std::endl;
                                                    std::cout << "  Dimensions: " << svgWidth << "x" << svgHeight << std::endl;
                                                }
                                            } else {
                                                // Single click selects the folder
                                                g_folderBrowser.selectEntry(clickedEntry->gridIndex);
                                                refreshBrowserSVG();
                                            }
                                            break;

                                        case svgplayer::BrowserEntryType::FBFSVGFile:
                                            // FBFSVGFile: animated SVG with SMIL - same loading as SVGFile
                                            if (isDoubleClick) {
                                                // Double-click loads the FBF.SVG file directly
                                                std::cout << "\n=== Loading FBF.SVG from browser: " << clickedEntry->fullPath << " ===" << std::endl;
                                                // Fall through to load code below
                                            } else {
                                                // Single click selects the FBF.SVG file
                                                g_folderBrowser.selectEntry(clickedEntry->gridIndex);
                                                refreshBrowserSVG();
                                                break;
                                            }
                                            // Load the selected FBF.SVG file (same as SVGFile loading)
                                            {
                                                g_browserMode = false;
                                                g_browserSvgDom = nullptr;
                                                clearBrowserAnimations();

                                                static std::string currentFilePath;
                                                std::string newPath = clickedEntry->fullPath;
                                                if (!newPath.empty() && fileExists(newPath.c_str())) {
                                                    // Stop renderers before loading
                                                    threadedRenderer.stop();
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

                                                        threadedRenderer.configure(&parallelRenderer, rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
                                                        threadedRenderer.setTotalAnimationFrames(preBufferTotalFrames);
                                                        threadedRenderer.start();

                                                        // Reset animation timing state
                                                        animationStartTime = Clock::now();
                                                        animationStartTimeSteady = SteadyClock::now();
                                                        pausedTime = 0;
                                                        lastRenderedAnimFrame = 0;
                                                        sequentialFrameCounter = 0;  // Reset sequential counter on file load
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

                                                        std::cout << "Loaded FBF.SVG: " << newPath << std::endl;
                                                        std::cout << "  Dimensions: " << svgWidth << "x" << svgHeight << std::endl;
                                                        std::cout << "  Animations: " << animations.size() << std::endl;
                                                    } else {
                                                        // Loading failed - restart with old content if available
                                                        if (svgDom) {
                                                            parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                                            threadedRenderer.start();
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
                                    std::cout << "\n=== PlayArrowEntry on FrameFolder: " << clickedEntry->fullPath << " ===" << std::endl;

                                    // Exit browser mode
                                    g_browserMode = false;
                                    g_browserSvgDom = nullptr;
                                    clearBrowserAnimations();

                                    // Stop renderers
                                    threadedRenderer.stop();
                                    parallelRenderer.stop();

                                    // Clear previous animation state
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
                                            std::ifstream seqFile(filePath);
                                            if (seqFile) {
                                                std::stringstream seqBuffer;
                                                seqBuffer << seqFile.rdbuf();
                                                sequenceSvgContents.push_back(seqBuffer.str());
                                            }
                                        }
                                        std::cout << "Pre-loaded " << sequenceSvgContents.size() << " SVG frames into memory" << std::endl;

                                        // Get dimensions from first frame
                                        if (!sequenceSvgContents.empty()) {
                                            const std::string& firstFrameContent = sequenceSvgContents[0];
                                            sk_sp<SkData> firstData = SkData::MakeWithCopy(firstFrameContent.data(), firstFrameContent.size());
                                            auto firstStream = SkMemoryStream::Make(firstData);
                                            if (firstStream) {
                                                sk_sp<SkSVGDOM> firstDom = makeSVGDOMWithFontSupport(*firstStream);
                                                if (firstDom && firstDom->getRoot()) {
                                                    SkSVGSVG* root = firstDom->getRoot();
                                                    SkSize defaultSize = SkSize::Make(800, 600);
                                                    SkSize frameSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));
                                                    svgWidth = (frameSize.width() > 0) ? static_cast<int>(frameSize.width()) : 800;
                                                    svgHeight = (frameSize.height() > 0) ? static_cast<int>(frameSize.height()) : 600;
                                                    aspectRatio = static_cast<float>(svgWidth) / svgHeight;
                                                }
                                            }
                                        }

                                        // Reset animation timing state
                                        animationStartTime = Clock::now();
                                        animationStartTimeSteady = SteadyClock::now();
                                        pausedTime = 0;
                                        lastRenderedAnimFrame = 0;
                                        sequentialFrameCounter = 0;
                                        displayCycles = 0;
                                        framesDelivered = 0;
                                        framesSkipped = 0;
                                        framesRendered = 0;
                                        animationPaused = false;

                                        // Update window title
                                        size_t lastSlash = clickedEntry->fullPath.find_last_of("/\\");
                                        std::string folderName = (lastSlash != std::string::npos)
                                            ? clickedEntry->fullPath.substr(lastSlash + 1)
                                            : clickedEntry->fullPath;
                                        std::string windowTitle = "SVG Player - " + folderName + " (" +
                                            std::to_string(sequenceSvgContents.size()) + " frames)";
                                        SDL_SetWindowTitle(window, windowTitle.c_str());

                                        std::cout << "Frame sequence loaded: " << clickedEntry->fullPath << std::endl;
                                        std::cout << "  Frames: " << sequenceSvgContents.size() << std::endl;
                                        std::cout << "  Dimensions: " << svgWidth << "x" << svgHeight << std::endl;
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
                        SDL_GetRendererOutputSize(renderer, &actualW, &actualH);

                        // Use full output size - letterboxing is handled by rendering code
                        // This matches macOS behavior and avoids double-letterboxing bug
                        // (SVG's preserveAspectRatio handles centering with black bars)
                        renderWidth = actualW;
                        renderHeight = actualH;

                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                    renderWidth, renderHeight);

                        createSurface(renderWidth, renderHeight);

                        // Resize threaded renderer buffers (non-blocking)
                        threadedRenderer.resize(renderWidth, renderHeight);

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

        // === UPDATE ANIMATIONS (SMIL-compliant time-based or sequential) ===
        // The animation frame is determined by current time (normal mode) or counter (sequential mode)
        // Sequential mode renders frames 0,1,2,3... as fast as possible, ignoring SMIL timing
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

            // Skip SMIL animation processing for image sequences
            if (isImageSequence) {
                continue;
            }

            if (sequentialMode) {
                // Sequential mode: use counter-based frame index (ignores SMIL timing)
                // This renders frames in order as fast as the renderer can go
                size_t totalFrames = preBufferTotalFrames > 0 ? preBufferTotalFrames : anim.values.size();
                currentFrameIndex = sequentialFrameCounter % totalFrames;
                // Increment counter for next cycle (wraps around automatically via modulo)
                sequentialFrameCounter++;
            } else {
                // Normal mode: time-based frame index (guarantees perfect sync)
                currentFrameIndex = anim.getCurrentFrameIndex(animTime);
            }
            lastFrameValue = newValue;

            // Track frame skips (for sync verification)
            if (currentFrameIndex != lastRenderedAnimFrame) {
                size_t expectedNext = (lastRenderedAnimFrame + 1) % anim.values.size();
                if (currentFrameIndex != expectedNext && lastRenderedAnimFrame != 0) {
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

            // Update animation state in ThreadedRenderer (non-blocking)
            // Render thread will apply this to its own DOM
            threadedRenderer.setAnimationState(anim.targetId, anim.attributeName, newValue);
        }
        auto animEnd = Clock::now();
        DurationMs animTime_ms = animEnd - animStart;

        // === STRESS TEST: Artificial delay to prove sync works ===
        if (stressTestEnabled) {
            // Sleep 50ms to simulate heavy load
            // The animation should SKIP frames but stay perfectly synchronized
            SDL_Delay(50);
        }

        // === FETCH FRAME FROM THREADED RENDERER (NON-BLOCKING!) ===
        // Main thread NEVER blocks on rendering - always responsive to input
        auto fetchStart = Clock::now();

        // Get canvas for rendering (shared by both modes)
        SkCanvas* canvas = surface->getCanvas();
        bool gotNewFrame = false;

        // === BROWSER MODE: Render folder browser instead of animation ===
        if (g_browserMode) {
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
            if (g_browserSvgDom) {
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
            } else if (g_browserAsyncScanning || g_browserDomParsing.load()) {
                // No DOM yet but parsing - show loading placeholder with progress bar
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
            // === NORMAL ANIMATION MODE ===
            // IMAGE SEQUENCE MODE (CPU): Direct rendering of separate SVG files
            // For image sequences, each frame is a separate SVG file, not SMIL animation
            if (isImageSequence && !sequenceSvgContents.empty()) {
                size_t frameIdx = currentFrameIndex % sequenceSvgContents.size();
                const std::string& svgContent = sequenceSvgContents[frameIdx];

                // Parse this frame's SVG (re-parse each frame since they are different files)
                sk_sp<SkData> frameData = SkData::MakeWithCopy(svgContent.data(), svgContent.size());
                auto frameStream = SkMemoryStream::Make(frameData);
                if (frameStream) {
                    sk_sp<SkSVGDOM> frameDom = makeSVGDOMWithFontSupport(*frameStream);
                    if (frameDom) {
                        SkCanvas* surfaceCanvas = surface->getCanvas();
                        if (surfaceCanvas) {
                            surfaceCanvas->clear(SK_ColorBLACK);

                            // Calculate scaling to fit window while preserving aspect ratio
                            float scaleX = (float)renderWidth / svgWidth;
                            float scaleY = (float)renderHeight / svgHeight;
                            float scale = std::min(scaleX, scaleY);
                            float offsetX = (renderWidth - svgWidth * scale) / 2.0f;
                            float offsetY = (renderHeight - svgHeight * scale) / 2.0f;

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
            } else if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                // === GRAPHITE GPU RENDERING PATH (Next-gen Vulkan) ===
                // Direct GPU rendering bypasses threaded CPU renderer for optimal performance
                surface = graphiteContext->createSurface(renderWidth, renderHeight);
                if (surface) {
                    SkCanvas* canvas = surface->getCanvas();
                    canvas->clear(SK_ColorBLACK);

                    // Calculate transform to fit SVG in render area with aspect ratio
                    // Use effectiveSvg* to handle edge case where svgWidth/Height is 0 (fallback to render dimensions)
                    int effectiveSvgW = (svgWidth > 0) ? svgWidth : renderWidth;
                    int effectiveSvgH = (svgHeight > 0) ? svgHeight : renderHeight;
                    float scaleX = static_cast<float>(renderWidth) / effectiveSvgW;
                    float scaleY = static_cast<float>(renderHeight) / effectiveSvgH;
                    float scale = std::min(scaleX, scaleY);
                    float offsetX = (renderWidth - effectiveSvgW * scale) / 2;
                    float offsetY = (renderHeight - effectiveSvgH * scale) / 2;

                    canvas->save();
                    canvas->translate(offsetX, offsetY);
                    canvas->scale(scale, scale);

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
                    if (svgDom) {
                        SkSize containerSize = SkSize::Make(effectiveSvgW, effectiveSvgH);
                        svgDom->setContainerSize(containerSize);
                        svgDom->render(canvas);
                    }

                    canvas->restore();

                    // Submit to GPU
                    graphiteContext->submitFrame();
                    gotNewFrame = true;
                    framesDelivered++;
                }
            } else {
                // === CPU RASTER PATH (with threaded rendering) ===
                // Request new frame (render thread will process asynchronously)
                // Main thread NEVER touches parallelRenderer directly - all through ThreadedRenderer
                threadedRenderer.requestFrame(currentFrameIndex);

                // Try to get rendered frame from ThreadedRenderer (non-blocking!)
                // Copy directly under lock to prevent use-after-free race condition
                SkPixmap pixmap;
                if (surface->peekPixels(&pixmap)) {
                    size_t bufferSize = renderWidth * renderHeight * sizeof(uint32_t);
                    if (threadedRenderer.copyFrontBufferIfReady(const_cast<void*>(pixmap.addr()), bufferSize)) {
                        gotNewFrame = true;
                        framesDelivered++;  // Count frames actually received from render thread
                    }
                }
                // If no new frame ready, surface keeps last frame (no blocking!)
            }
        }

        auto fetchEnd = Clock::now();
        DurationMs fetchTime = fetchEnd - fetchStart;

        // Track fetch time and actual render time separately
        // Skip when disruptive events occurred (reset, mode change, screenshot, etc.)
        if (!skipStatsThisFrame) {
            fetchTimes.add(fetchTime.count());
            if (gotNewFrame) {
                // Record actual SVG render time from the render thread
                renderTimes.add(threadedRenderer.lastRenderTimeMs.load());
            }
        }

        // === DRAW DEBUG OVERLAY (only when we have a new frame to present) ===
        // Consumer-producer pattern: only do expensive work when Skia delivered a new frame
        auto overlayStart = Clock::now();
        if (gotNewFrame && showDebugOverlay) {
            // Calculate scale for display in overlay
            // Use effectiveSvg* to handle edge case where svgWidth/Height is 0 (fallback to render dimensions)
            int effectiveSvgW = (svgWidth > 0) ? svgWidth : renderWidth;
            int effectiveSvgH = (svgHeight > 0) ? svgHeight : renderHeight;
            float scaleX = static_cast<float>(renderWidth) / effectiveSvgW;
            float scaleY = static_cast<float>(renderHeight) / effectiveSvgH;
            float scale = std::min(scaleX, scaleY);

            auto totalElapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
            double fps = (frameCount > 0) ? frameCount / totalElapsed : 0;
            double instantFps = (frameTimes.last() > 0) ? 1000.0 / frameTimes.last() : 0;

            // Debug overlay layout constants - scaled 40% larger to match font
            float lineHeight = 13 * hiDpiScale;   // Was 9, now 13 (40% larger)
            float padding = 3 * hiDpiScale;       // Was 2, now 3 (40% larger)
            float labelWidth = 112 * hiDpiScale;  // Was 80, now 112 (40% larger)

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

            // Helper to add lines
            auto addLine = [&](const std::string& label, const std::string& value) {
                lines.push_back({0, label, value, ""});
            };
            auto addHighlight = [&](const std::string& label, const std::string& value) {
                lines.push_back({1, label, value, ""});
            };
            auto addAnim = [&](const std::string& label, const std::string& value) {
                lines.push_back({2, label, value, ""});
            };
            auto addKey = [&](const std::string& key, const std::string& label, const std::string& value) {
                lines.push_back({3, label, value, key});
            };
            auto addSmallGap = [&]() { lines.push_back({4, "", "", ""}); };
            auto addLargeGap = [&]() { lines.push_back({5, "", "", ""}); };
            auto addSingle = [&](const std::string& text) { lines.push_back({6, text, "", ""}); };

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

            // Current frame position and remaining frames (for animations)
            if (!animations.empty() && !animations[0].values.empty()) {
                size_t totalAnimFrames = animations[0].values.size();
                size_t currentFrame = currentFrameIndex + 1;  // 1-based for display
                size_t remaining = (currentFrame <= totalAnimFrames) ? (totalAnimFrames - currentFrame) : 0;

                oss.str("");
                oss << currentFrame << " / " << totalAnimFrames;
                addLine("Frame:", oss.str());

                oss.str("");
                oss << remaining;
                addLine("Remaining:", oss.str());
            }

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
            }

            addLargeGap();

            // Controls
            addKey("[V]", "VSync:", vsyncEnabled ? "ON" : "OFF");
            addKey("[F]",
                   "Limiter:", frameLimiterEnabled ? ("ON (" + std::to_string(displayRefreshRate) + " FPS)") : "OFF");

            // Parallel mode status - use cached mode from ThreadedRenderer to avoid blocking
            std::string parallelStatus = threadedRenderer.isPreBufferMode() ? "PreBuffer" : "Off";
            addKey("[P]", "Mode:", parallelStatus);

            // Backend indicator - shows GPU (Graphite/Vulkan) or CPU Raster
            std::string backendStr = "CPU Raster";
            if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                backendStr = std::string("GPU (") + graphiteContext->getBackendName() + ")";
            }
            addLine("Backend:", backendStr);

            // Real-time CPU stats from Linux procfs APIs
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

            // Auto-screenshot for benchmark mode (save first frame only, CPU raster path)
            if (!screenshotPath.empty() && !screenshotSaved && frameCount == 1 && surface) {
                SkPixmap pixmap;
                if (surface->peekPixels(&pixmap)) {
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

            if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
                // === GRAPHITE GPU PRESENTATION PATH ===
                // Direct GPU presentation bypasses SDL texture copy for optimal performance
                auto presentStart = Clock::now();
                graphiteContext->present();
                presentEnd = Clock::now();
                presentTime = presentEnd - presentStart;

                // Skip copy time tracking (no CPU copy in Graphite mode)
                if (!skipStatsThisFrame) {
                    eventTimes.add(eventTime.count());
                    animTimes.add(animTime_ms.count());
                    overlayTimes.add(overlayTime.count());
                    presentTimes.add(presentTime.count());
                }
            } else {
                // === CPU RASTER PRESENTATION PATH (SDL Texture) ===
                auto copyStart = Clock::now();

                SkPixmap pixmap;
                if (surface->peekPixels(&pixmap)) {
                    void* pixels;
                    int pitch;
                    SDL_LockTexture(texture, nullptr, &pixels, &pitch);

                    const uint8_t* src = static_cast<const uint8_t*>(pixmap.addr());
                    uint8_t* dst = static_cast<uint8_t*>(pixels);
                    size_t rowBytes = renderWidth * 4;

                    for (int row = 0; row < renderHeight; row++) {
                        memcpy(dst + row * pitch, src + row * pixmap.rowBytes(), rowBytes);
                    }

                    SDL_UnlockTexture(texture);
                }

                auto copyEnd = Clock::now();
                copyTime = copyEnd - copyStart;
                if (!skipStatsThisFrame) {
                    copyTimes.add(copyTime.count());
                }

                // Clear and render to screen (pure black for exclusive fullscreen)
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);

                // Get actual renderer output size for proper centering
                int outW, outH;
                SDL_GetRendererOutputSize(renderer, &outW, &outH);

                SDL_Rect destRect;
                destRect.w = renderWidth;
                destRect.h = renderHeight;
                destRect.x = (outW - renderWidth) / 2;
                destRect.y = (outH - renderHeight) / 2;

                SDL_RenderCopy(renderer, texture, nullptr, &destRect);

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
            auto idleStart = Clock::now();
            SDL_Delay(1);
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
                std::cerr << "STUTTER #" << stutterCount << " at " << std::fixed << std::setprecision(2) << stutterAt
                          << "s (+" << sinceLast << "s) [" << culprit << "]: "
                          << "event=" << eventTime.count() << "ms, "
                          << "fetch=" << fetchTime.count() << "ms, "
                          << "overlay=" << overlayTime.count() << "ms, "
                          << "copy=" << copyTime.count() << "ms, "
                          << "present=" << presentTime.count() << "ms, "
                          << "TOTAL=" << totalFrameTime.count() << "ms" << std::endl;
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
        std::cout << "\"max_fps\":" << std::setprecision(2) << maxFps;
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
    }

    // Stop threaded renderer first (must stop before parallel renderer)
    if (!g_jsonOutput) {
        std::cout << "\nStopping render thread..." << std::endl;
    }
    threadedRenderer.stop();
    if (!g_jsonOutput) {
        std::cout << "Render thread stopped." << std::endl;
    }

    // Stop parallel renderer if running
    if (parallelRenderer.isEnabled()) {
        if (!g_jsonOutput) {
            std::cout << "Stopping parallel render threads..." << std::endl;
        }
        parallelRenderer.stop();
        if (!g_jsonOutput) {
            std::cout << "Parallel renderer stopped." << std::endl;
        }
    }

    // Destroy Graphite context before SDL cleanup (holds Vulkan resources)
    if (graphiteContext) {
        std::cout << "Destroying Graphite context..." << std::endl;
        graphiteContext.reset();
        std::cout << "Graphite context destroyed." << std::endl;
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
