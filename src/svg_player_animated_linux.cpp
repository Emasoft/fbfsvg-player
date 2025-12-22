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

// =============================================================================
// Global shutdown flag for graceful termination
// =============================================================================
static std::atomic<bool> g_shutdownRequested{false};

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
// File validation helpers
// =============================================================================

// Check if file exists and is readable
bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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
    std::cerr << "    -f, --fullscreen  Start in fullscreen mode\n\n";
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
        std::vector<uint32_t> pixels;
        int width;
        int height;
        std::atomic<bool> ready{false};
    };
    std::mutex bufferMutex;
    std::map<size_t, std::shared_ptr<RenderedFrame>> frameBuffer;
    static constexpr size_t MAX_BUFFER_SIZE = 30;
    static constexpr size_t LOOKAHEAD_FRAMES = 10;  // How many frames to pre-render ahead

    // Shared rendering resources
    std::string svgData;
    int renderWidth{0};
    int renderHeight{0};
    int svgWidth{0};
    int svgHeight{0};

    // Animation info for pre-buffered frames
    std::string animTargetId;
    std::string animAttributeName;
    std::vector<std::string> animValues;

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
                   const std::string& targetId = "", const std::string& attrName = "",
                   const std::vector<std::string>& values = {}) {
        svgData = svgContent;
        renderWidth = width;
        renderHeight = height;
        svgWidth = svgW;
        svgHeight = svgH;
        animTargetId = targetId;
        animAttributeName = attrName;
        animValues = values;
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

        // Get or create cached DOM and surface for this worker thread
        WorkerCache* cache = nullptr;
        {
            std::lock_guard<std::mutex> lock(workerCacheMutex);
            cache = &workerCaches[threadId];
        }

        // Parse SVG once per worker thread (first call only)
        // Use makeSVGDOMWithFontSupport to ensure SVG text elements render properly
        if (!cache->dom) {
            auto stream = SkMemoryStream::MakeDirect(svgData.data(), svgData.size());
            cache->dom = makeSVGDOMWithFontSupport(*stream);
            if (!cache->dom) return;
            cache->dom->setContainerSize(SkSize::Make(svgWidth, svgHeight));
        }

        // Recreate surface if size changed
        if (!cache->surface || cache->surfaceWidth != renderWidth || cache->surfaceHeight != renderHeight) {
            cache->surface = SkSurfaces::Raster(
                SkImageInfo::Make(renderWidth, renderHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
            cache->surfaceWidth = renderWidth;
            cache->surfaceHeight = renderHeight;
            if (!cache->surface) return;
        }

        // Apply animation state for this specific frame index
        if (!animTargetId.empty() && !animAttributeName.empty() && !animValues.empty()) {
            size_t valueIndex = frame->frameIndex % animValues.size();
            sk_sp<SkSVGNode>* nodePtr = cache->dom->findNodeById(animTargetId.c_str());
            if (nodePtr && *nodePtr) {
                (*nodePtr)->setAttribute(animAttributeName.c_str(), animValues[valueIndex].c_str());
            }
        }

        SkCanvas* canvas = cache->surface->getCanvas();
        canvas->clear(SK_ColorWHITE);

        // Apply same transform as main render loop
        float scaleX = static_cast<float>(renderWidth) / svgWidth;
        float scaleY = static_cast<float>(renderHeight) / svgHeight;
        float scale = std::min(scaleX, scaleY);
        float offsetX = (renderWidth - svgWidth * scale) / 2.0f;
        float offsetY = (renderHeight - svgHeight * scale) / 2.0f;

        canvas->save();
        canvas->translate(offsetX, offsetY);
        canvas->scale(scale, scale);
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

    // Animation state (for applying to render thread's DOM)
    std::string animTargetId;
    std::string animAttributeName;
    std::string animCurrentValue;

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

    // Called from main thread - update animation state (non-blocking!)
    void setAnimationState(const std::string& targetId, const std::string& attrName, const std::string& value) {
        std::lock_guard<std::mutex> lock(paramsMutex);
        animTargetId = targetId;
        animAttributeName = attrName;
        animCurrentValue = value;
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

            // Get render parameters and animation state
            std::string localSvgData;
            int localWidth, localHeight, localSvgW, localSvgH;
            size_t localFrameIndex;
            std::string localAnimTargetId, localAnimAttr, localAnimValue;
            {
                std::lock_guard<std::mutex> lock(paramsMutex);
                localSvgData = svgData;
                localWidth = renderWidth;
                localHeight = renderHeight;
                localSvgW = svgWidth;
                localSvgH = svgHeight;
                localFrameIndex = currentFrameIndex;
                localAnimTargetId = animTargetId;
                localAnimAttr = animAttributeName;
                localAnimValue = animCurrentValue;
            }

            if (localWidth <= 0 || localHeight <= 0) continue;

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

                    // Apply animation state to render thread's DOM (sync with main thread)
                    if (!localAnimTargetId.empty() && !localAnimAttr.empty()) {
                        sk_sp<SkSVGNode>* nodePtr = threadDom->findNodeById(localAnimTargetId.c_str());
                        if (nodePtr && *nodePtr) {
                            (*nodePtr)->setAttribute(localAnimAttr.c_str(), localAnimValue.c_str());
                        }
                    }

                    SkCanvas* canvas = threadSurface->getCanvas();
                    canvas->clear(SK_ColorWHITE);

                    // Calculate transform
                    float scaleX = static_cast<float>(localWidth) / localSvgW;
                    float scaleY = static_cast<float>(localHeight) / localSvgH;
                    float scale = std::min(scaleX, scaleY);
                    float offsetX = (localWidth - localSvgW * scale) / 2.0f;
                    float offsetY = (localHeight - localSvgH * scale) / 2.0f;

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

                    // Copy to back buffer
                    if (renderSuccess) {
                        SkPixmap pixmap;
                        if (threadSurface->peekPixels(&pixmap)) {
                            std::lock_guard<std::mutex> lock(bufferMutex);
                            backBuffer.resize(localWidth * localHeight);
                            memcpy(backBuffer.data(), pixmap.addr(), localWidth * localHeight * sizeof(uint32_t));
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
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for screenshot: " << filename << std::endl;
        return false;
    }

    // PPM P6 header: magic number, width, height, max color value
    file << "P6\n" << width << " " << height << "\n255\n";

    // Convert ARGB8888 to RGB24 and write raw bytes
    // ARGB8888 layout: [A7-A0][R7-R0][G7-G0][B7-B0] = 32 bits per pixel
    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        uint32_t pixel = pixels[i];
        rgb[i * 3 + 0] = (pixel >> 16) & 0xFF;  // R
        rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;   // G
        rgb[i * 3 + 2] = pixel & 0xFF;          // B
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

int main(int argc, char* argv[]) {
    // Install signal handlers for graceful shutdown (Ctrl+C, kill)
    installSignalHandlers();

    // Print startup banner (always shown on execution)
    std::cerr << SVGPlayerVersion::getStartupBanner() << std::endl;

    // Parse command-line arguments
    const char* inputPath = nullptr;
    bool startFullscreen = false;

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

    // Initialize font support for SVG text rendering (must be done before any SVG parsing)
    initializeFontSupport();

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
    // Force Metal renderer on macOS for better performance
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    // Enable render batching for better throughput
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    // Use Metal's direct-to-display for lower latency
    SDL_SetHint(SDL_HINT_RENDER_METAL_PREFER_LOW_POWER_DEVICE, "0");
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
    auto startTime = Clock::now();
    auto lastFrameTime = Clock::now();
    auto animationStartTime = Clock::now();

    // Animation state - using SteadyClock for SMIL-compliant timing
    bool animationPaused = false;
    double pausedTime = 0;
    size_t lastFrameIndex = 0;
    size_t currentFrameIndex = 0;
    std::string lastFrameValue;

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

    // Create initial texture
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, renderWidth, renderHeight);

    // Skia surface
    sk_sp<SkSurface> surface;

    auto createSurface = [&](int w, int h) {
        SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(w, h);
        surface = SkSurfaces::Raster(imageInfo);
        return surface != nullptr;
    };

    if (!createSurface(renderWidth, renderHeight)) {
        std::cerr << "Failed to create Skia surface" << std::endl;
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

    // Initialize parallel renderer with SVG data and animation info
    // PreBuffer mode is started by default for best animation performance
    if (!animations.empty()) {
        parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight,
                                   animations[0].targetId, animations[0].attributeName, animations[0].values);
    } else {
        parallelRenderer.configure(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight);
    }

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
    if (!animations.empty()) {
        threadedRenderer.setTotalAnimationFrames(animations[0].values.size());
    }

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
    std::cout << "  F - Toggle frame limiter (" << displayRefreshRate << " FPS cap)" << std::endl;
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
    std::cout << "\nNote: Occasional stutters may be caused by macOS system tasks." << std::endl;
    std::cout << "      Animation sync remains correct even during stutters." << std::endl;
    std::cout << "\nRendering..." << std::endl;

    while (running && !g_shutdownRequested.load()) {
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
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                        running = false;
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
                    } else if (event.key.keysym.sym == SDLK_f) {
                        // Toggle frame limiter
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
                    } else if (event.key.keysym.sym == SDLK_g) {
                        // Toggle fullscreen mode (exclusive fullscreen - takes over display)
                        isFullscreen = !isFullscreen;
                        if (isFullscreen) {
                            // Use SDL_WINDOW_FULLSCREEN for exclusive fullscreen (no compositor, direct display)
                            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                        } else {
                            // Back to windowed mode
                            SDL_SetWindowFullscreen(window, 0);
                        }
                        std::cout << "Fullscreen: " << (isFullscreen ? "ON (exclusive)" : "OFF") << std::endl;
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
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        // Get actual renderer output size (HiDPI aware)
                        int actualW, actualH;
                        SDL_GetRendererOutputSize(renderer, &actualW, &actualH);

                        float windowAspect = static_cast<float>(actualW) / actualH;

                        if (windowAspect > aspectRatio) {
                            renderHeight = actualH;
                            renderWidth = static_cast<int>(actualH * aspectRatio);
                        } else {
                            renderWidth = actualW;
                            renderHeight = static_cast<int>(actualW / aspectRatio);
                        }

                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                    renderWidth, renderHeight);

                        createSurface(renderWidth, renderHeight);

                        // Resize threaded renderer buffers (non-blocking)
                        threadedRenderer.resize(renderWidth, renderHeight);

                        // Resize parallel renderer - clears pre-buffered frames at old size
                        parallelRenderer.resize(renderWidth, renderHeight);
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
        for (const auto& anim : animations) {
            std::string newValue = anim.getCurrentValue(animTime);
            currentFrameIndex = anim.getCurrentFrameIndex(animTime);
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

        // Request new frame (render thread will process asynchronously)
        // Main thread NEVER touches parallelRenderer directly - all through ThreadedRenderer
        threadedRenderer.requestFrame(currentFrameIndex);

        // Try to get rendered frame from ThreadedRenderer (non-blocking!)
        SkCanvas* canvas = surface->getCanvas();
        bool gotNewFrame = false;

        const uint32_t* renderedPixels = threadedRenderer.getFrontBufferIfReady();
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
            float scaleX = static_cast<float>(renderWidth) / svgWidth;
            float scaleY = static_cast<float>(renderHeight) / svgHeight;
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

            // === COPY TO SDL TEXTURE ===
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
        } else {
            // No new frame - yield CPU briefly to prevent busy-spinning
            auto idleStart = Clock::now();
            SDL_Delay(1);
            auto idleEnd = Clock::now();
            DurationMs idleTime = idleEnd - idleStart;
            idleTimes.add(idleTime.count());
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

    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Display cycles: " << displayCycles << std::endl;
    std::cout << "Frames delivered: " << framesDelivered << std::endl;
    double finalHitRate = displayCycles > 0 ? (100.0 * framesDelivered / displayCycles) : 0.0;
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

    // Stop threaded renderer first (must stop before parallel renderer)
    std::cout << "\nStopping render thread..." << std::endl;
    threadedRenderer.stop();
    std::cout << "Render thread stopped." << std::endl;

    // Stop parallel renderer if running
    if (parallelRenderer.isEnabled()) {
        std::cout << "Stopping parallel render threads..." << std::endl;
        parallelRenderer.stop();
        std::cout << "Parallel renderer stopped." << std::endl;
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
