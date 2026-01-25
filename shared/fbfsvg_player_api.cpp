// svg_player_api.cpp - Unified Cross-Platform SVG Player Implementation
//
// This file implements the unified SVG Player C API by wrapping
// SVGAnimationController and Skia rendering primitives.
//
// Copyright (c) 2024. MIT License.

#include "fbfsvg_player_api.h"

#include "SVGAnimationController.h"
#include "ElementBoundsExtractor.h"

// Skia headers
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "modules/skresources/include/SkResources.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/svg/include/SkSVGSVG.h"

// Platform-specific font managers
#if defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#define CREATE_FONT_MANAGER() SkFontMgr_New_CoreText(nullptr)
#elif defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#define CREATE_FONT_MANAGER() SkFontMgr_New_DirectWrite()
#else
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#define CREATE_FONT_MANAGER() SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType())
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// =============================================================================
// Internal Types
// =============================================================================

/// Skia blend mode mapping from FBFSVGLayerBlendMode
static SkBlendMode toSkBlendMode(FBFSVGLayerBlendMode mode) {
    switch (mode) {
        case FBFSVGLayerBlend_Multiply: return SkBlendMode::kMultiply;
        case FBFSVGLayerBlend_Screen: return SkBlendMode::kScreen;
        case FBFSVGLayerBlend_Overlay: return SkBlendMode::kOverlay;
        case FBFSVGLayerBlend_Darken: return SkBlendMode::kDarken;
        case FBFSVGLayerBlend_Lighten: return SkBlendMode::kLighten;
        default: return SkBlendMode::kSrcOver;
    }
}

/// Internal layer structure for multi-SVG compositing (opaque to C API)
struct FBFSVGLayer {
    // Layer's own animation controller
    std::unique_ptr<svgplayer::SVGAnimationController> controller;

    // Skia DOM for this layer's SVG
    sk_sp<SkSVGDOM> svgDom;

    // Original SVG data (for re-parsing during animation)
    std::string svgData;

    // SVG intrinsic dimensions
    int width = 0;
    int height = 0;
    SkRect viewBox = SkRect::MakeEmpty();

    // Layer transform properties
    float posX = 0.0f;           // X offset in pixels
    float posY = 0.0f;           // Y offset in pixels
    float scaleX = 1.0f;         // Horizontal scale
    float scaleY = 1.0f;         // Vertical scale
    float rotation = 0.0f;       // Rotation in degrees
    float opacity = 1.0f;        // Opacity (0.0 to 1.0)
    int zOrder = 0;              // Render order (higher = on top)
    bool visible = true;         // Visibility flag
    FBFSVGLayerBlendMode blendMode = FBFSVGLayerBlend_Normal;

    // Owner player (for accessing shared resources)
    struct FBFSVGPlayer* owner = nullptr;

    FBFSVGLayer() {
        controller = std::make_unique<svgplayer::SVGAnimationController>();
    }
};

/// Internal player structure (opaque to C API)
struct FBFSVGPlayer {
    // Core animation controller (shared logic)
    std::unique_ptr<svgplayer::SVGAnimationController> controller;

    // Skia DOM for current SVG
    sk_sp<SkSVGDOM> svgDom;

    // Original SVG data for re-parsing during animation
    std::string originalSvgData;

    // Font manager (platform-specific, created once)
    sk_sp<SkFontMgr> fontMgr;

    // Resource provider for loading referenced assets
    sk_sp<skresources::ResourceProvider> resourceProvider;

    // SVG intrinsic dimensions
    int svgWidth = 0;
    int svgHeight = 0;
    SkRect viewBox = SkRect::MakeEmpty();

    // Zoom and viewBox state
    SkRect originalViewBox = SkRect::MakeEmpty();  // Original viewBox for reset
    SkRect currentViewBox = SkRect::MakeEmpty();   // Current viewBox (modified by zoom/pan)
    float currentZoom = 1.0f;                       // Current zoom level (1.0 = no zoom)
    float minZoom = 0.1f;                           // Minimum allowed zoom level
    float maxZoom = 10.0f;                          // Maximum allowed zoom level

    // Viewport dimensions for rendering
    int viewportWidth = 0;
    int viewportHeight = 0;

    // Playback rate multiplier
    float playbackRate = 1.0f;

    // Repeat count for Count mode
    int repeatCount = 1;
    int completedLoops = 0;

    // Direction for ping-pong mode
    bool playingForward = true;

    // Scrubbing state
    bool isScrubbing = false;
    SVGPlaybackState stateBeforeScrub = SVGPlaybackState_Stopped;

    // Hit testing subscriptions
    std::unordered_set<std::string> subscribedElements;
    std::string lastHitTestResult;  // Storage for HitTest return value

    // Element bounds cache
    std::unordered_map<std::string, SVGRect> elementBoundsCache;

    // Statistics
    SVGRenderStats stats = {};
    std::chrono::high_resolution_clock::time_point lastRenderStart;
    std::chrono::high_resolution_clock::time_point lastUpdateStart;

    // Pre-buffering
    bool preBufferEnabled = false;
    int preBufferFrameCount = 3;
    std::vector<std::vector<uint8_t>> frameBuffer;
    int bufferedFrameStart = -1;

    // Debug overlay
    bool debugOverlayEnabled = false;
    uint32_t debugFlags = SVGDebugFlag_None;

    // Error handling
    std::string lastError;

    // Callbacks
    SVGStateChangeCallback stateChangeCallback = nullptr;
    void* stateChangeUserData = nullptr;

    SVGLoopCallback loopCallback = nullptr;
    void* loopUserData = nullptr;

    SVGEndCallback endCallback = nullptr;
    void* endUserData = nullptr;

    SVGErrorCallback errorCallback = nullptr;
    void* errorUserData = nullptr;

    SVGElementTouchCallback elementTouchCallback = nullptr;
    void* elementTouchUserData = nullptr;

    // Multi-SVG compositing layers
    // Layer 0 is the "primary" SVG loaded via LoadSVG
    // Additional layers are created via CreateLayer
    std::vector<std::unique_ptr<FBFSVGLayer>> layers;

    // Frame rate and timing control
    float targetFrameRate = 60.0f;                    // Target FPS for frame pacing
    double lastRenderTimeSeconds = 0.0;               // Time of last rendered frame
    double frameBeginTimeSeconds = 0.0;               // Time when BeginFrame was called
    double lastFrameDurationSeconds = 0.0;            // Duration of last frame
    int droppedFrameCount = 0;                        // Number of dropped frames
    static constexpr int FRAME_HISTORY_SIZE = 30;     // Rolling average window
    std::array<double, FRAME_HISTORY_SIZE> frameDurationHistory = {}; // Frame duration history
    int frameHistoryIndex = 0;                        // Current index in history
    int frameHistoryCount = 0;                        // Number of valid entries

    // Thread safety mutex
    std::mutex mutex;

    FBFSVGPlayer() {
        // Initialize font manager based on platform
        fontMgr = CREATE_FONT_MANAGER();

        // Create null resource provider (no external resources)
        resourceProvider = skresources::FileResourceProvider::Make(SkString(""));

        // Create animation controller
        controller = std::make_unique<svgplayer::SVGAnimationController>();

        // Clear statistics
        std::memset(&stats, 0, sizeof(stats));
    }
};

// =============================================================================
// Internal Helper Functions
// =============================================================================

namespace {

/// Set error message and optionally invoke callback
void setError(FBFSVGPlayer* player, int code, const std::string& message) {
    if (!player) return;

    // Copy callback data under lock, invoke outside lock to prevent deadlock
    SVGErrorCallback callback = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        player->lastError = message;
        callback = player->errorCallback;
        userData = player->errorUserData;
    }

    if (callback) {
        callback(userData, code, message.c_str());
    }
}

/// Parse SVG from data and create DOM
bool parseSVG(FBFSVGPlayer* player, const char* data, size_t length) {
    if (!player || !data || length == 0) {
        return false;
    }

    // Store original SVG data for animation re-parsing
    player->originalSvgData = std::string(data, length);

    // Create memory stream from data
    auto stream = SkMemoryStream::MakeDirect(data, length);
    if (!stream) {
        setError(player, 1, "Failed to create memory stream for SVG data");
        return false;
    }

    // Create SVG DOM builder
    SkSVGDOM::Builder builder;
    builder.setFontManager(player->fontMgr);
    builder.setResourceProvider(player->resourceProvider);

    // Parse SVG
    player->svgDom = builder.make(*stream);
    if (!player->svgDom) {
        setError(player, 2, "Failed to parse SVG document");
        return false;
    }

    // Get SVG root element
    SkSVGSVG* root = player->svgDom->getRoot();
    if (!root) {
        setError(player, 3, "SVG has no root element");
        player->svgDom = nullptr;
        return false;
    }

    // Extract intrinsic size
    SkSize containerSize = SkSize::Make(800, 600);  // Default if not specified

    // Get viewBox if available
    // Windows Skia uses std::optional<SkRect> (.has_value()), macOS/Linux use SkTLazy<SkRect> (.isValid())
    const auto& viewBox = root->getViewBox();
#if defined(PLATFORM_WINDOWS)
    const bool hasViewBox = viewBox.has_value();
#else
    const bool hasViewBox = viewBox.isValid();
#endif
    if (hasViewBox) {
        player->viewBox = *root->getViewBox();
        // Initialize zoom viewBox state - these track zoom/pan modifications
        player->originalViewBox = player->viewBox;
        player->currentViewBox = player->viewBox;
        player->currentZoom = 1.0f;  // Reset zoom level on new SVG load
        containerSize = SkSize::Make(player->viewBox.width(), player->viewBox.height());
    } else {
        // No viewBox specified - create one from dimensions later
        player->viewBox = SkRect::MakeEmpty();
        player->originalViewBox = SkRect::MakeEmpty();
        player->currentViewBox = SkRect::MakeEmpty();
        player->currentZoom = 1.0f;
    }

    // Try to get explicit width/height
    const SkSVGLength& width = root->getWidth();
    const SkSVGLength& height = root->getHeight();

    if (width.unit() != SkSVGLength::Unit::kPercentage) {
        containerSize.fWidth = width.value();
    }
    if (height.unit() != SkSVGLength::Unit::kPercentage) {
        containerSize.fHeight = height.value();
    }

    player->svgWidth = static_cast<int>(containerSize.width());
    player->svgHeight = static_cast<int>(containerSize.height());
    player->svgDom->setContainerSize(containerSize);

    // If no explicit viewBox was set, create one from the final dimensions
    if (player->originalViewBox.isEmpty() && containerSize.width() > 0 && containerSize.height() > 0) {
        player->viewBox = SkRect::MakeWH(containerSize.width(), containerSize.height());
        player->originalViewBox = player->viewBox;
        player->currentViewBox = player->viewBox;
    }

    // Initialize animation controller with SVG content
    // Duration is automatically parsed from SVG animation elements during loadFromContent()
    player->controller->loadFromContent(player->originalSvgData);

    // Reset playback state
    player->completedLoops = 0;
    player->playingForward = true;
    player->stats = {};
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
    player->elementBoundsCache.clear();

    return true;
}

/// Re-parse SVG with updated animation state
void updateSVGForCurrentTime(FBFSVGPlayer* player) {
    if (!player || !player->controller) return;

    // Get current animated SVG content
    std::string animatedSvg = player->controller->getProcessedContent();
    if (animatedSvg.empty()) {
        animatedSvg = player->originalSvgData;
    }

    // Re-parse with animated content
    auto stream = SkMemoryStream::MakeDirect(animatedSvg.data(), animatedSvg.size());
    if (!stream) return;

    SkSVGDOM::Builder builder;
    builder.setFontManager(player->fontMgr);
    builder.setResourceProvider(player->resourceProvider);

    auto newDom = builder.make(*stream);
    if (newDom) {
        player->svgDom = std::move(newDom);

        // Restore container size
        player->svgDom->setContainerSize(
            SkSize::Make(static_cast<float>(player->svgWidth), static_cast<float>(player->svgHeight)));
    }
}

/// Convert SVGPlaybackState to controller PlaybackState
svgplayer::PlaybackState toControllerState(SVGPlaybackState state) {
    switch (state) {
        case SVGPlaybackState_Playing:
            return svgplayer::PlaybackState::Playing;
        case SVGPlaybackState_Paused:
            return svgplayer::PlaybackState::Paused;
        case SVGPlaybackState_Stopped:
        default:
            return svgplayer::PlaybackState::Stopped;
    }
}

/// Convert controller PlaybackState to SVGPlaybackState
SVGPlaybackState fromControllerState(svgplayer::PlaybackState state) {
    switch (state) {
        case svgplayer::PlaybackState::Playing:
            return SVGPlaybackState_Playing;
        case svgplayer::PlaybackState::Paused:
            return SVGPlaybackState_Paused;
        case svgplayer::PlaybackState::Stopped:
        default:
            return SVGPlaybackState_Stopped;
    }
}

/// Convert SVGRepeatMode to controller RepeatMode
svgplayer::RepeatMode toControllerRepeatMode(SVGRepeatMode mode) {
    switch (mode) {
        case SVGRepeatMode_Loop:
            return svgplayer::RepeatMode::Loop;
        case SVGRepeatMode_Reverse:
            return svgplayer::RepeatMode::Reverse;
        case SVGRepeatMode_Count:
            return svgplayer::RepeatMode::Count;
        case SVGRepeatMode_None:
        default:
            return svgplayer::RepeatMode::None;
    }
}

/// Convert controller RepeatMode to SVGRepeatMode
SVGRepeatMode fromControllerRepeatMode(svgplayer::RepeatMode mode) {
    switch (mode) {
        case svgplayer::RepeatMode::Loop:
            return SVGRepeatMode_Loop;
        case svgplayer::RepeatMode::Reverse:
            return SVGRepeatMode_Reverse;
        case svgplayer::RepeatMode::Count:
            return SVGRepeatMode_Count;
        case svgplayer::RepeatMode::None:
        default:
            return SVGRepeatMode_None;
    }
}

/// Clamp value to range
template <typename T>
T clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}

}  // anonymous namespace

// =============================================================================
// Section 1: Lifecycle Functions
// =============================================================================

FBFSVGPlayerRef FBFSVGPlayer_Create(void) {
    try {
        return new FBFSVGPlayer();
    } catch (...) {
        return nullptr;
    }
}

void FBFSVGPlayer_Destroy(FBFSVGPlayerRef player) {
    if (player) {
        // Clear all controller callbacks to prevent use-after-free
        if (player->controller) {
            player->controller->setStateChangeCallback(nullptr);
            player->controller->setLoopCallback(nullptr);
            player->controller->setEndCallback(nullptr);
        }
        delete player;
    }
}

const char* FBFSVGPlayer_GetVersion(void) {
    // Return the full version string from version.h (includes prerelease tag)
    return FBFSVG_PLAYER_VERSION_STRING;
}

void FBFSVGPlayer_GetVersionNumbers(int* major, int* minor, int* patch) {
    if (major) *major = FBFSVG_PLAYER_API_VERSION_MAJOR;
    if (minor) *minor = FBFSVG_PLAYER_API_VERSION_MINOR;
    if (patch) *patch = FBFSVG_PLAYER_API_VERSION_PATCH;
}

// =============================================================================
// Section 2: Loading Functions
// =============================================================================

bool FBFSVGPlayer_LoadSVG(FBFSVGPlayerRef player, const char* filepath) {
    if (!player || !filepath) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Read file contents
    sk_sp<SkData> data = SkData::MakeFromFileName(filepath);
    if (!data || data->size() == 0) {
        setError(player, 10, std::string("Failed to read SVG file: ") + filepath);
        return false;
    }

    // Parse SVG
    return parseSVG(player, static_cast<const char*>(data->data()), data->size());
}

bool FBFSVGPlayer_LoadSVGData(FBFSVGPlayerRef player, const void* data, size_t length) {
    if (!player || !data || length == 0) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return parseSVG(player, static_cast<const char*>(data), length);
}

void FBFSVGPlayer_Unload(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->svgDom = nullptr;
    player->originalSvgData.clear();
    player->svgWidth = 0;
    player->svgHeight = 0;
    player->viewBox = SkRect::MakeEmpty();

    if (player->controller) {
        player->controller->stop();
    }

    player->subscribedElements.clear();
    player->elementBoundsCache.clear();
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
    player->stats = {};
}

bool FBFSVGPlayer_IsLoaded(FBFSVGPlayerRef player) {
    if (!player) return false;
    std::lock_guard<std::mutex> lock(player->mutex);
    return player->svgDom != nullptr;
}

bool FBFSVGPlayer_HasAnimations(FBFSVGPlayerRef player) {
    if (!player) return false;
    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller && player->controller->hasAnimations();
}

// =============================================================================
// Section 3: Size and Dimension Functions
// =============================================================================

bool FBFSVGPlayer_GetSize(FBFSVGPlayerRef player, int* width, int* height) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom) return false;

    if (width) *width = player->svgWidth;
    if (height) *height = player->svgHeight;
    return true;
}

bool FBFSVGPlayer_GetSizeInfo(FBFSVGPlayerRef player, SVGSizeInfo* info) {
    if (!player || !info) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom) return false;

    info->width = player->svgWidth;
    info->height = player->svgHeight;
    info->viewBoxX = player->viewBox.x();
    info->viewBoxY = player->viewBox.y();
    info->viewBoxWidth = player->viewBox.width();
    info->viewBoxHeight = player->viewBox.height();

    return true;
}

void FBFSVGPlayer_SetViewport(FBFSVGPlayerRef player, int width, int height) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->viewportWidth = width;
    player->viewportHeight = height;

    // Clear pre-buffer when viewport changes
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

// =============================================================================
// Section 4: Playback Control Functions
// =============================================================================

void FBFSVGPlayer_Play(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    SVGPlaybackState oldState;
    SVGStateChangeCallback callback = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        oldState = fromControllerState(player->controller->getPlaybackState());
        player->controller->play();
        callback = player->stateChangeCallback;
        userData = player->stateChangeUserData;
    }

    // Invoke callback outside lock to avoid deadlock
    SVGPlaybackState newState = SVGPlaybackState_Playing;
    if (callback && oldState != newState) {
        callback(userData, newState);
    }
}

void FBFSVGPlayer_Pause(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    SVGPlaybackState oldState;
    SVGStateChangeCallback callback = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        oldState = fromControllerState(player->controller->getPlaybackState());
        player->controller->pause();
        callback = player->stateChangeCallback;
        userData = player->stateChangeUserData;
    }

    SVGPlaybackState newState = SVGPlaybackState_Paused;
    if (callback && oldState != newState) {
        callback(userData, newState);
    }
}

void FBFSVGPlayer_Stop(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    SVGPlaybackState oldState;
    SVGStateChangeCallback callback = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        oldState = fromControllerState(player->controller->getPlaybackState());
        player->controller->stop();
        player->completedLoops = 0;
        player->playingForward = true;
        callback = player->stateChangeCallback;
        userData = player->stateChangeUserData;
    }

    SVGPlaybackState newState = SVGPlaybackState_Stopped;
    if (callback && oldState != newState) {
        callback(userData, newState);
    }
}

void FBFSVGPlayer_TogglePlayback(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->togglePlayback();
}

void FBFSVGPlayer_SetPlaybackState(FBFSVGPlayerRef player, SVGPlaybackState state) {
    switch (state) {
        case SVGPlaybackState_Playing:
            FBFSVGPlayer_Play(player);
            break;
        case SVGPlaybackState_Paused:
            FBFSVGPlayer_Pause(player);
            break;
        case SVGPlaybackState_Stopped:
            FBFSVGPlayer_Stop(player);
            break;
    }
}

SVGPlaybackState FBFSVGPlayer_GetPlaybackState(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return SVGPlaybackState_Stopped;

    std::lock_guard<std::mutex> lock(player->mutex);
    return fromControllerState(player->controller->getPlaybackState());
}

bool FBFSVGPlayer_IsPlaying(FBFSVGPlayerRef player) { return FBFSVGPlayer_GetPlaybackState(player) == SVGPlaybackState_Playing; }

bool FBFSVGPlayer_IsPaused(FBFSVGPlayerRef player) { return FBFSVGPlayer_GetPlaybackState(player) == SVGPlaybackState_Paused; }

bool FBFSVGPlayer_IsStopped(FBFSVGPlayerRef player) { return FBFSVGPlayer_GetPlaybackState(player) == SVGPlaybackState_Stopped; }

// =============================================================================
// Section 5: Repeat Mode Functions
// =============================================================================

void FBFSVGPlayer_SetRepeatMode(FBFSVGPlayerRef player, SVGRepeatMode mode) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->setRepeatMode(toControllerRepeatMode(mode));
}

SVGRepeatMode FBFSVGPlayer_GetRepeatMode(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return SVGRepeatMode_None;

    std::lock_guard<std::mutex> lock(player->mutex);
    return fromControllerRepeatMode(player->controller->getRepeatMode());
}

void FBFSVGPlayer_SetRepeatCount(FBFSVGPlayerRef player, int count) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->repeatCount = std::max(1, count);
}

int FBFSVGPlayer_GetRepeatCount(FBFSVGPlayerRef player) {
    if (!player) return 1;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->repeatCount;
}

int FBFSVGPlayer_GetCompletedLoops(FBFSVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->completedLoops;
}

bool FBFSVGPlayer_IsPlayingForward(FBFSVGPlayerRef player) {
    if (!player) return true;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->playingForward;
}

bool FBFSVGPlayer_IsLooping(FBFSVGPlayerRef player) {
    SVGRepeatMode mode = FBFSVGPlayer_GetRepeatMode(player);
    return mode == SVGRepeatMode_Loop || mode == SVGRepeatMode_Reverse;
}

void FBFSVGPlayer_SetLooping(FBFSVGPlayerRef player, bool looping) {
    FBFSVGPlayer_SetRepeatMode(player, looping ? SVGRepeatMode_Loop : SVGRepeatMode_None);
}

// =============================================================================
// Section 6: Playback Rate Functions
// =============================================================================

void FBFSVGPlayer_SetPlaybackRate(FBFSVGPlayerRef player, float rate) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->playbackRate = clamp(rate, -10.0f, 10.0f);

    // Avoid exactly 0 rate (would freeze playback)
    if (std::abs(player->playbackRate) < 0.1f) {
        player->playbackRate = (player->playbackRate >= 0) ? 0.1f : -0.1f;
    }
}

float FBFSVGPlayer_GetPlaybackRate(FBFSVGPlayerRef player) {
    if (!player) return 1.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->playbackRate;
}

// =============================================================================
// Section 7: Timeline Functions
// =============================================================================

bool FBFSVGPlayer_Update(FBFSVGPlayerRef player, double deltaTime) {
    if (!player || !player->controller) return false;

    auto updateStart = std::chrono::high_resolution_clock::now();
    bool stateChanged = false;
    SVGLoopCallback loopCallback = nullptr;
    void* loopUserData = nullptr;
    int completedLoops = 0;

    {
        std::lock_guard<std::mutex> lock(player->mutex);

        // Apply playback rate
        double adjustedDelta = deltaTime * static_cast<double>(player->playbackRate);

        // Handle negative rate (reverse playback)
        if (adjustedDelta < 0) {
            player->playingForward = false;
        } else {
            player->playingForward = true;
        }

        // Store pre-update time
        double oldTime = player->controller->getCurrentTime();

        // Update animation controller
        stateChanged = player->controller->update(std::abs(adjustedDelta));

        // Handle loop completion in Count mode
        if (player->controller->getRepeatMode() == svgplayer::RepeatMode::Count) {
            double newTime = player->controller->getCurrentTime();
            double duration = player->controller->getDuration();

            // Detect loop transition
            if (oldTime > duration * 0.9 && newTime < duration * 0.1) {
                player->completedLoops++;

                // Check if we've reached the repeat count limit
                if (player->completedLoops >= player->repeatCount) {
                    player->controller->stop();
                }
            }
        }

        // Update SVG DOM for current animation state
        if (stateChanged) {
            updateSVGForCurrentTime(player);
        }

        // Update statistics
        auto updateEnd = std::chrono::high_resolution_clock::now();
        player->stats.updateTimeMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
        player->stats.currentFrame = player->controller->getCurrentFrame();
        player->stats.totalFrames = player->controller->getTotalFrames();
        player->stats.animationTimeMs = player->controller->getCurrentTime() * 1000.0;

        // Copy callback data under lock for safe invocation outside lock
        loopCallback = player->loopCallback;
        loopUserData = player->loopUserData;
        completedLoops = player->completedLoops;
    }

    // Invoke loop callback outside lock to prevent deadlock
    if (stateChanged && loopCallback) {
        loopCallback(loopUserData, completedLoops);
    }

    return stateChanged;
}

double FBFSVGPlayer_GetDuration(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getDuration();
}

double FBFSVGPlayer_GetCurrentTime(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getCurrentTime();
}

float FBFSVGPlayer_GetProgress(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return 0.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return static_cast<float>(player->controller->getProgress());
}

int FBFSVGPlayer_GetCurrentFrame(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getCurrentFrame();
}

int FBFSVGPlayer_GetTotalFrames(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getTotalFrames();
}

float FBFSVGPlayer_GetFrameRate(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return 30.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    double duration = player->controller->getDuration();
    int frames = player->controller->getTotalFrames();

    if (duration > 0 && frames > 0) {
        return static_cast<float>(frames / duration);
    }
    return 30.0f;  // Default FPS
}

// =============================================================================
// Section 8: Seeking Functions
// =============================================================================

void FBFSVGPlayer_SeekTo(FBFSVGPlayerRef player, double timeSeconds) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->seekTo(timeSeconds);
    updateSVGForCurrentTime(player);

    // Clear pre-buffer after seeking
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

void FBFSVGPlayer_SeekToFrame(FBFSVGPlayerRef player, int frame) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->seekToFrame(frame);
    updateSVGForCurrentTime(player);

    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

void FBFSVGPlayer_SeekToProgress(FBFSVGPlayerRef player, float progress) {
    if (!player || !player->controller) return;

    double duration = FBFSVGPlayer_GetDuration(player);
    FBFSVGPlayer_SeekTo(player, duration * clamp(progress, 0.0f, 1.0f));
}

void FBFSVGPlayer_SeekToStart(FBFSVGPlayerRef player) { FBFSVGPlayer_SeekTo(player, 0.0); }

void FBFSVGPlayer_SeekToEnd(FBFSVGPlayerRef player) { FBFSVGPlayer_SeekTo(player, FBFSVGPlayer_GetDuration(player)); }

void FBFSVGPlayer_SeekForwardByTime(FBFSVGPlayerRef player, double seconds) {
    double current = FBFSVGPlayer_GetCurrentTime(player);
    FBFSVGPlayer_SeekTo(player, current + seconds);
}

void FBFSVGPlayer_SeekBackwardByTime(FBFSVGPlayerRef player, double seconds) {
    double current = FBFSVGPlayer_GetCurrentTime(player);
    FBFSVGPlayer_SeekTo(player, current - seconds);
}

// =============================================================================
// Section 9: Frame Stepping Functions
// =============================================================================

void FBFSVGPlayer_StepForward(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->pause();
    player->controller->stepForward();
    updateSVGForCurrentTime(player);
}

void FBFSVGPlayer_StepBackward(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->pause();
    player->controller->stepBackward();
    updateSVGForCurrentTime(player);
}

void FBFSVGPlayer_StepByFrames(FBFSVGPlayerRef player, int frames) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->pause();
    player->controller->stepByFrames(frames);
    updateSVGForCurrentTime(player);
}

// =============================================================================
// Section 10: Scrubbing Functions
// =============================================================================

void FBFSVGPlayer_BeginScrubbing(FBFSVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->stateBeforeScrub = fromControllerState(player->controller->getPlaybackState());
    player->controller->beginScrubbing();
    player->isScrubbing = true;
}

void FBFSVGPlayer_ScrubToProgress(FBFSVGPlayerRef player, float progress) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->scrubToProgress(clamp(progress, 0.0f, 1.0f));
    updateSVGForCurrentTime(player);
}

void FBFSVGPlayer_EndScrubbing(FBFSVGPlayerRef player, bool resume) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->endScrubbing(resume);
    player->isScrubbing = false;
}

bool FBFSVGPlayer_IsScrubbing(FBFSVGPlayerRef player) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->isScrubbing;
}

// =============================================================================
// Section 11: Rendering Functions
// =============================================================================

bool FBFSVGPlayer_Render(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale) {
    if (!player || !pixelBuffer || width <= 0 || height <= 0) return false;

    auto renderStart = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom) {
        setError(player, 20, "No SVG loaded for rendering");
        return false;
    }

    // Create image info for RGBA pixels
    SkImageInfo imageInfo =
        SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB());

    // Calculate stride (bytes per row)
    size_t rowBytes = static_cast<size_t>(width) * 4;

    // Create surface that wraps the caller's pixel buffer
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(imageInfo, pixelBuffer, rowBytes);

    if (!surface) {
        setError(player, 21, "Failed to create rendering surface");
        return false;
    }

    SkCanvas* canvas = surface->getCanvas();

    // Clear to transparent
    canvas->clear(SK_ColorTRANSPARENT);

    // Apply HiDPI scale
    canvas->scale(scale, scale);

    // Use currentViewBox for zoom support - smaller viewBox = zoomed in
    // The currentViewBox defines which portion of the SVG to display
    SkRect activeViewBox = player->currentViewBox;
    if (activeViewBox.isEmpty()) {
        // Fallback to full SVG dimensions if no viewBox set
        activeViewBox = SkRect::MakeWH(static_cast<float>(player->svgWidth),
                                        static_cast<float>(player->svgHeight));
    }

    float viewBoxW = activeViewBox.width();
    float viewBoxH = activeViewBox.height();
    float viewW = static_cast<float>(width) / scale;
    float viewH = static_cast<float>(height) / scale;

    if (viewBoxW > 0 && viewBoxH > 0) {
        // Calculate scale to fit the viewBox portion in the viewport
        float scaleX = viewW / viewBoxW;
        float scaleY = viewH / viewBoxH;
        float fitScale = std::min(scaleX, scaleY);

        // Center the viewBox content in the viewport
        float offsetX = (viewW - viewBoxW * fitScale) / 2.0f;
        float offsetY = (viewH - viewBoxH * fitScale) / 2.0f;

        canvas->translate(offsetX, offsetY);
        canvas->scale(fitScale, fitScale);

        // Translate to show the correct portion of the SVG based on viewBox origin
        // This shifts the SVG so the viewBox origin appears at canvas origin
        canvas->translate(-activeViewBox.x(), -activeViewBox.y());
    }

    // Render SVG - the canvas transform will show only the viewBox portion
    player->svgDom->render(canvas);

    // Render debug overlay if enabled
    if (player->debugOverlayEnabled && player->debugFlags != SVGDebugFlag_None) {
        // TODO: Implement debug overlay rendering
        // This would draw FPS, frame info, timing, memory usage, etc.
    }

    // Note: No flush needed for raster surfaces backed by WrapPixels
    // The pixels are written directly to the buffer during render()

    // Update statistics
    auto renderEnd = std::chrono::high_resolution_clock::now();
    player->stats.renderTimeMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
    player->stats.elementsRendered++;
    player->stats.fps = (player->stats.renderTimeMs > 0) ? 1000.0 / player->stats.renderTimeMs : 0.0;

    return true;
}

bool FBFSVGPlayer_RenderAtTime(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale,
                            double timeSeconds) {
    if (!player || !player->controller) return false;

    // Temporarily seek to the specified time
    double savedTime = FBFSVGPlayer_GetCurrentTime(player);

    {
        std::lock_guard<std::mutex> lock(player->mutex);
        player->controller->seekTo(timeSeconds);
        updateSVGForCurrentTime(player);
    }

    // Render
    bool result = FBFSVGPlayer_Render(player, pixelBuffer, width, height, scale);

    // Restore original time
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        player->controller->seekTo(savedTime);
        updateSVGForCurrentTime(player);
    }

    return result;
}

bool FBFSVGPlayer_RenderFrame(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale, int frame) {
    if (!player || !player->controller) return false;

    // Convert frame to time
    double timeSeconds;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        int totalFrames = player->controller->getTotalFrames();
        double duration = player->controller->getDuration();

        if (totalFrames <= 0) {
            frame = 0;
        } else {
            frame = clamp(frame, 0, totalFrames - 1);
        }

        // Fix: Use totalFrames-1 to avoid overflow (frame N-1 should map to duration, not beyond)
        if (totalFrames <= 1) {
            timeSeconds = 0.0;
        } else {
            timeSeconds = (static_cast<double>(frame) / (totalFrames - 1)) * duration;
        }
    }

    return FBFSVGPlayer_RenderAtTime(player, pixelBuffer, width, height, scale, timeSeconds);
}

// =============================================================================
// Section 12: Coordinate Conversion Functions
// =============================================================================

// Internal helper - MUST be called while holding player->mutex
// Prevents deadlock when called from functions that already hold the lock
static bool viewToSVGInternal(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight,
                               float* svgX, float* svgY) {
    // Caller must ensure: player != nullptr, svgX != nullptr, svgY != nullptr, mutex held

    if (!player->svgDom || player->svgWidth <= 0 || player->svgHeight <= 0) {
        return false;
    }

    // Use currentViewBox for zoom support - must match FBFSVGPlayer_Render logic
    SkRect activeViewBox = player->currentViewBox;
    if (activeViewBox.isEmpty()) {
        activeViewBox = SkRect::MakeWH(static_cast<float>(player->svgWidth),
                                        static_cast<float>(player->svgHeight));
    }

    float viewBoxW = activeViewBox.width();
    float viewBoxH = activeViewBox.height();
    float viewW = static_cast<float>(viewWidth);
    float viewH = static_cast<float>(viewHeight);

    if (viewBoxW <= 0 || viewBoxH <= 0) return false;

    float scaleX = viewW / viewBoxW;
    float scaleY = viewH / viewBoxH;
    float fitScale = std::min(scaleX, scaleY);

    float offsetX = (viewW - viewBoxW * fitScale) / 2.0f;
    float offsetY = (viewH - viewBoxH * fitScale) / 2.0f;

    // Invert the transformation: view -> viewBox local -> SVG global
    float localX = (viewX - offsetX) / fitScale;
    float localY = (viewY - offsetY) / fitScale;

    // Add viewBox origin to get SVG global coordinates
    *svgX = localX + activeViewBox.x();
    *svgY = localY + activeViewBox.y();

    return true;
}

bool FBFSVGPlayer_ViewToSVG(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight, float* svgX,
                         float* svgY) {
    if (!player || !svgX || !svgY) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return viewToSVGInternal(player, viewX, viewY, viewWidth, viewHeight, svgX, svgY);
}

bool FBFSVGPlayer_SVGToView(FBFSVGPlayerRef player, float svgX, float svgY, int viewWidth, int viewHeight, float* viewX,
                         float* viewY) {
    if (!player || !viewX || !viewY) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom || player->svgWidth <= 0 || player->svgHeight <= 0) {
        return false;
    }

    // Use currentViewBox for zoom support - must match FBFSVGPlayer_Render logic
    SkRect activeViewBox = player->currentViewBox;
    if (activeViewBox.isEmpty()) {
        activeViewBox = SkRect::MakeWH(static_cast<float>(player->svgWidth),
                                        static_cast<float>(player->svgHeight));
    }

    float viewBoxW = activeViewBox.width();
    float viewBoxH = activeViewBox.height();
    float viewW = static_cast<float>(viewWidth);
    float viewH = static_cast<float>(viewHeight);

    if (viewBoxW <= 0 || viewBoxH <= 0) return false;

    float scaleX = viewW / viewBoxW;
    float scaleY = viewH / viewBoxH;
    float fitScale = std::min(scaleX, scaleY);

    float offsetX = (viewW - viewBoxW * fitScale) / 2.0f;
    float offsetY = (viewH - viewBoxH * fitScale) / 2.0f;

    // Transform: SVG global -> viewBox local -> view coordinates
    float localX = svgX - activeViewBox.x();
    float localY = svgY - activeViewBox.y();

    *viewX = localX * fitScale + offsetX;
    *viewY = localY * fitScale + offsetY;

    return true;
}

// =============================================================================
// Section 13: Hit Testing Functions
// =============================================================================

void FBFSVGPlayer_SubscribeToElement(FBFSVGPlayerRef player, const char* objectID) {
    if (!player || !objectID) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->subscribedElements.insert(std::string(objectID));
}

void FBFSVGPlayer_UnsubscribeFromElement(FBFSVGPlayerRef player, const char* objectID) {
    if (!player || !objectID) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->subscribedElements.erase(std::string(objectID));
}

void FBFSVGPlayer_UnsubscribeFromAllElements(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->subscribedElements.clear();
}

const char* FBFSVGPlayer_HitTest(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight) {
    if (!player) return nullptr;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Convert view coordinates to SVG coordinates using internal helper
    // (avoids deadlock - we already hold the mutex)
    float svgX, svgY;
    if (!viewToSVGInternal(player, viewX, viewY, viewWidth, viewHeight, &svgX, &svgY)) {
        return nullptr;
    }

    // Check subscribed elements for hit
    for (const auto& elementId : player->subscribedElements) {
        auto it = player->elementBoundsCache.find(elementId);
        if (it != player->elementBoundsCache.end()) {
            const SVGRect& bounds = it->second;
            if (svgX >= bounds.x && svgX <= bounds.x + bounds.width && svgY >= bounds.y &&
                svgY <= bounds.y + bounds.height) {
                player->lastHitTestResult = elementId;
                return player->lastHitTestResult.c_str();
            }
        }
    }

    return nullptr;
}

bool FBFSVGPlayer_GetElementBounds(FBFSVGPlayerRef player, const char* objectID, SVGRect* bounds) {
    if (!player || !objectID || !bounds) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Check cache first - avoids re-parsing SVG for repeated queries
    auto it = player->elementBoundsCache.find(objectID);
    if (it != player->elementBoundsCache.end()) {
        *bounds = it->second;
        return true;
    }

    // No SVG loaded - cannot extract bounds
    if (player->originalSvgData.empty()) {
        return false;
    }

    // Use ElementBoundsExtractor to parse SVG and find element bounds
    // This parses the SVG string to find the element by ID and extract its position/size
    svgplayer::DirtyRect dirtyBounds;
    bool found = svgplayer::ElementBoundsExtractor::extractBoundsForId(
        player->originalSvgData,
        std::string(objectID),
        dirtyBounds
    );

    if (!found) {
        return false;
    }

    // Convert DirtyRect to SVGRect and cache the result
    SVGRect result;
    result.x = dirtyBounds.x;
    result.y = dirtyBounds.y;
    result.width = dirtyBounds.width;
    result.height = dirtyBounds.height;

    // Cache for future lookups - bounds are static for FBF.SVG files
    player->elementBoundsCache[objectID] = result;

    *bounds = result;
    return true;
}

int FBFSVGPlayer_GetElementsAtPoint(FBFSVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight,
                                 const char** outElements, int maxElements) {
    if (!player || !outElements || maxElements <= 0) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Convert view coordinates to SVG coordinates using internal helper
    // (avoids deadlock - we already hold the mutex)
    float svgX, svgY;
    if (!viewToSVGInternal(player, viewX, viewY, viewWidth, viewHeight, &svgX, &svgY)) {
        return 0;
    }

    // Check all subscribed elements for hits
    int count = 0;
    for (const auto& elementId : player->subscribedElements) {
        if (count >= maxElements) break;

        auto it = player->elementBoundsCache.find(elementId);
        if (it != player->elementBoundsCache.end()) {
            const SVGRect& bounds = it->second;
            if (svgX >= bounds.x && svgX <= bounds.x + bounds.width && svgY >= bounds.y &&
                svgY <= bounds.y + bounds.height) {
                outElements[count++] = elementId.c_str();
            }
        }
    }

    return count;
}

// =============================================================================
// Section 14: Element Information Functions
// =============================================================================

bool FBFSVGPlayer_ElementExists(FBFSVGPlayerRef player, const char* elementID) {
    if (!player || !elementID) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Check if element ID exists in the original SVG
    // Simple string search for id="elementID"
    std::string searchPattern = std::string("id=\"") + elementID + "\"";
    return player->originalSvgData.find(searchPattern) != std::string::npos;
}

/// Helper: Convert SkSVGPaint to string (hex color, "none", or IRI reference)
static std::string svgPaintToString(const SkSVGPaint& paint) {
    switch (paint.type()) {
        case SkSVGPaint::Type::kNone:
            return "none";
        case SkSVGPaint::Type::kColor: {
            // Extract color from SkSVGColor
            const SkSVGColor& color = paint.color();
            if (color.type() == SkSVGColor::Type::kCurrentColor) {
                return "currentColor";
            }
            // Convert SkColor (ARGB) to hex string #RRGGBB or #AARRGGBB
            SkColor c = color.color();
            uint8_t a = SkColorGetA(c);
            uint8_t r = SkColorGetR(c);
            uint8_t g = SkColorGetG(c);
            uint8_t b = SkColorGetB(c);
            char buf[16];
            if (a == 255) {
                snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
            } else {
                snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", a, r, g, b);
            }
            return std::string(buf);
        }
        case SkSVGPaint::Type::kIRI: {
            // Return the IRI reference (e.g., "url(#gradientId)")
            const SkSVGIRI& iri = paint.iri();
            return std::string("url(#") + iri.iri().c_str() + ")";
        }
    }
    return "";
}

/// Helper: Convert SkSVGLength to string (value + unit suffix)
static std::string svgLengthToString(const SkSVGLength& length) {
    char buf[64];
    const char* unitStr = "";
    switch (length.unit()) {
        case SkSVGLength::Unit::kNumber:    unitStr = ""; break;
        case SkSVGLength::Unit::kPercentage: unitStr = "%"; break;
        case SkSVGLength::Unit::kEMS:       unitStr = "em"; break;
        case SkSVGLength::Unit::kEXS:       unitStr = "ex"; break;
        case SkSVGLength::Unit::kPX:        unitStr = "px"; break;
        case SkSVGLength::Unit::kCM:        unitStr = "cm"; break;
        case SkSVGLength::Unit::kMM:        unitStr = "mm"; break;
        case SkSVGLength::Unit::kIN:        unitStr = "in"; break;
        case SkSVGLength::Unit::kPT:        unitStr = "pt"; break;
        case SkSVGLength::Unit::kPC:        unitStr = "pc"; break;
        default:                            unitStr = ""; break;
    }
    snprintf(buf, sizeof(buf), "%.4g%s", length.value(), unitStr);
    return std::string(buf);
}

/// Helper: Convert SkSVGVisibility to string
static std::string svgVisibilityToString(const SkSVGVisibility& vis) {
    switch (vis.type()) {
        case SkSVGVisibility::Type::kVisible:  return "visible";
        case SkSVGVisibility::Type::kHidden:   return "hidden";
        case SkSVGVisibility::Type::kCollapse: return "collapse";
        case SkSVGVisibility::Type::kInherit:  return "inherit";
    }
    return "";
}

/// Helper: Convert SkSVGLineCap to string
static std::string svgLineCapToString(SkSVGLineCap cap) {
    switch (cap) {
        case SkSVGLineCap::kButt:   return "butt";
        case SkSVGLineCap::kRound:  return "round";
        case SkSVGLineCap::kSquare: return "square";
    }
    return "";
}

/// Helper: Convert SkSVGLineJoin to string
static std::string svgLineJoinToString(const SkSVGLineJoin& join) {
    switch (join.type()) {
        case SkSVGLineJoin::Type::kMiter:   return "miter";
        case SkSVGLineJoin::Type::kRound:   return "round";
        case SkSVGLineJoin::Type::kBevel:   return "bevel";
        case SkSVGLineJoin::Type::kInherit: return "inherit";
    }
    return "";
}

/// Helper: Convert SkSVGFillRule to string
static std::string svgFillRuleToString(const SkSVGFillRule& rule) {
    switch (rule.type()) {
        case SkSVGFillRule::Type::kNonZero: return "nonzero";
        case SkSVGFillRule::Type::kEvenOdd: return "evenodd";
        case SkSVGFillRule::Type::kInherit: return "inherit";
    }
    return "";
}

/// Helper: Convert SkSVGDisplay to string
static std::string svgDisplayToString(SkSVGDisplay display) {
    switch (display) {
        case SkSVGDisplay::kInline: return "inline";
        case SkSVGDisplay::kNone:   return "none";
    }
    return "";
}

/// Helper: Copy string to output buffer with bounds checking
static bool copyToOutBuffer(const std::string& value, char* outValue, int maxLength) {
    if (value.empty()) {
        outValue[0] = '\0';
        return false;
    }
    size_t copyLen = std::min(value.size(), static_cast<size_t>(maxLength - 1));
    std::memcpy(outValue, value.c_str(), copyLen);
    outValue[copyLen] = '\0';
    return true;
}

bool FBFSVGPlayer_GetElementProperty(FBFSVGPlayerRef player, const char* elementID, const char* propertyName, char* outValue,
                                  int maxLength) {
    if (!player || !elementID || !propertyName || !outValue || maxLength <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(player->mutex);

    // Ensure SVG DOM is loaded
    if (!player->svgDom) {
        outValue[0] = '\0';
        return false;
    }

    // Find the element by ID in the Skia SVG DOM
    sk_sp<SkSVGNode>* nodePtr = player->svgDom->findNodeById(elementID);
    if (!nodePtr || !*nodePtr) {
        outValue[0] = '\0';
        return false;
    }

    SkSVGNode* node = nodePtr->get();
    std::string propNameStr(propertyName);
    std::string result;

    // Map property name to SkSVGNode getter and convert to string
    // Presentation attributes (inherited)
    if (propNameStr == "fill") {
        const auto& prop = node->getFill();
        if (prop.isValue()) {
            result = svgPaintToString(*prop);
        }
    } else if (propNameStr == "stroke") {
        const auto& prop = node->getStroke();
        if (prop.isValue()) {
            result = svgPaintToString(*prop);
        }
    } else if (propNameStr == "fill-opacity" || propNameStr == "fillOpacity") {
        const auto& prop = node->getFillOpacity();
        if (prop.isValue()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4g", *prop);
            result = buf;
        }
    } else if (propNameStr == "stroke-opacity" || propNameStr == "strokeOpacity") {
        const auto& prop = node->getStrokeOpacity();
        if (prop.isValue()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4g", *prop);
            result = buf;
        }
    } else if (propNameStr == "opacity") {
        const auto& prop = node->getOpacity();
        if (prop.isValue()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4g", *prop);
            result = buf;
        }
    } else if (propNameStr == "stroke-width" || propNameStr == "strokeWidth") {
        const auto& prop = node->getStrokeWidth();
        if (prop.isValue()) {
            result = svgLengthToString(*prop);
        }
    } else if (propNameStr == "stroke-linecap" || propNameStr == "strokeLinecap") {
        const auto& prop = node->getStrokeLineCap();
        if (prop.isValue()) {
            result = svgLineCapToString(*prop);
        }
    } else if (propNameStr == "stroke-linejoin" || propNameStr == "strokeLinejoin") {
        const auto& prop = node->getStrokeLineJoin();
        if (prop.isValue()) {
            result = svgLineJoinToString(*prop);
        }
    } else if (propNameStr == "stroke-miterlimit" || propNameStr == "strokeMiterlimit") {
        const auto& prop = node->getStrokeMiterLimit();
        if (prop.isValue()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4g", *prop);
            result = buf;
        }
    } else if (propNameStr == "visibility") {
        const auto& prop = node->getVisibility();
        if (prop.isValue()) {
            result = svgVisibilityToString(*prop);
        }
    } else if (propNameStr == "display") {
        const auto& prop = node->getDisplay();
        if (prop.isValue()) {
            result = svgDisplayToString(*prop);
        }
    } else if (propNameStr == "fill-rule" || propNameStr == "fillRule") {
        const auto& prop = node->getFillRule();
        if (prop.isValue()) {
            result = svgFillRuleToString(*prop);
        }
    } else if (propNameStr == "clip-rule" || propNameStr == "clipRule") {
        const auto& prop = node->getClipRule();
        if (prop.isValue()) {
            result = svgFillRuleToString(*prop);
        }
    } else if (propNameStr == "color") {
        const auto& prop = node->getColor();
        if (prop.isValue()) {
            // SkSVGColorType is SkColor (uint32_t ARGB)
            SkColor c = *prop;
            char buf[16];
            uint8_t a = SkColorGetA(c);
            uint8_t r = SkColorGetR(c);
            uint8_t g = SkColorGetG(c);
            uint8_t b = SkColorGetB(c);
            if (a == 255) {
                snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
            } else {
                snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", a, r, g, b);
            }
            result = buf;
        }
    } else if (propNameStr == "font-family" || propNameStr == "fontFamily") {
        const auto& prop = node->getFontFamily();
        if (prop.isValue() && prop->type() == SkSVGFontFamily::Type::kFamily) {
            result = prop->family().c_str();
        }
    } else if (propNameStr == "font-size" || propNameStr == "fontSize") {
        const auto& prop = node->getFontSize();
        if (prop.isValue() && prop->type() == SkSVGFontSize::Type::kLength) {
            result = svgLengthToString(prop->size());
        }
    } else if (propNameStr == "font-style" || propNameStr == "fontStyle") {
        const auto& prop = node->getFontStyle();
        if (prop.isValue()) {
            switch (prop->type()) {
                case SkSVGFontStyle::Type::kNormal:  result = "normal"; break;
                case SkSVGFontStyle::Type::kItalic:  result = "italic"; break;
                case SkSVGFontStyle::Type::kOblique: result = "oblique"; break;
                case SkSVGFontStyle::Type::kInherit: result = "inherit"; break;
            }
        }
    } else if (propNameStr == "font-weight" || propNameStr == "fontWeight") {
        const auto& prop = node->getFontWeight();
        if (prop.isValue()) {
            switch (prop->type()) {
                case SkSVGFontWeight::Type::k100:     result = "100"; break;
                case SkSVGFontWeight::Type::k200:     result = "200"; break;
                case SkSVGFontWeight::Type::k300:     result = "300"; break;
                case SkSVGFontWeight::Type::k400:     result = "400"; break;
                case SkSVGFontWeight::Type::k500:     result = "500"; break;
                case SkSVGFontWeight::Type::k600:     result = "600"; break;
                case SkSVGFontWeight::Type::k700:     result = "700"; break;
                case SkSVGFontWeight::Type::k800:     result = "800"; break;
                case SkSVGFontWeight::Type::k900:     result = "900"; break;
                case SkSVGFontWeight::Type::kNormal:  result = "normal"; break;
                case SkSVGFontWeight::Type::kBold:    result = "bold"; break;
                case SkSVGFontWeight::Type::kBolder:  result = "bolder"; break;
                case SkSVGFontWeight::Type::kLighter: result = "lighter"; break;
                case SkSVGFontWeight::Type::kInherit: result = "inherit"; break;
            }
        }
    } else if (propNameStr == "text-anchor" || propNameStr == "textAnchor") {
        const auto& prop = node->getTextAnchor();
        if (prop.isValue()) {
            switch (prop->type()) {
                case SkSVGTextAnchor::Type::kStart:   result = "start"; break;
                case SkSVGTextAnchor::Type::kMiddle:  result = "middle"; break;
                case SkSVGTextAnchor::Type::kEnd:     result = "end"; break;
                case SkSVGTextAnchor::Type::kInherit: result = "inherit"; break;
            }
        }
    } else {
        // Unknown property name - return false with empty string
        outValue[0] = '\0';
        return false;
    }

    return copyToOutBuffer(result, outValue, maxLength);
}

// =============================================================================
// Section 15: Callback Functions
// =============================================================================

void FBFSVGPlayer_SetStateChangeCallback(FBFSVGPlayerRef player, SVGStateChangeCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->stateChangeCallback = callback;
    player->stateChangeUserData = userData;

    // Also set up controller callback
    // Lambda captures player pointer and accesses callback fields under mutex lock
    // to prevent race conditions when callback is invoked on a different thread
    if (player->controller && callback) {
        player->controller->setStateChangeCallback([player](svgplayer::PlaybackState state) {
            // Copy callback/userData under lock, then invoke outside lock (avoids deadlock)
            SVGStateChangeCallback cb = nullptr;
            void* userData = nullptr;
            {
                std::lock_guard<std::mutex> lock(player->mutex);
                cb = player->stateChangeCallback;
                userData = player->stateChangeUserData;
            }
            if (cb) {
                cb(userData, fromControllerState(state));
            }
        });
    }
}

void FBFSVGPlayer_SetLoopCallback(FBFSVGPlayerRef player, SVGLoopCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->loopCallback = callback;
    player->loopUserData = userData;

    // Set up controller callback
    // Lambda captures player pointer and accesses callback fields under mutex lock
    // to prevent race conditions when callback is invoked on a different thread
    if (player->controller && callback) {
        player->controller->setLoopCallback([player](int loopCount) {
            // Copy callback/userData under lock, then invoke outside lock (avoids deadlock)
            SVGLoopCallback cb = nullptr;
            void* userData = nullptr;
            {
                std::lock_guard<std::mutex> lock(player->mutex);
                player->completedLoops = loopCount;
                cb = player->loopCallback;
                userData = player->loopUserData;
            }
            if (cb) {
                cb(userData, loopCount);
            }
        });
    }
}

void FBFSVGPlayer_SetEndCallback(FBFSVGPlayerRef player, SVGEndCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->endCallback = callback;
    player->endUserData = userData;

    // Set up controller callback
    // Lambda captures player pointer and accesses callback fields under mutex lock
    // to prevent race conditions when callback is invoked on a different thread
    if (player->controller && callback) {
        player->controller->setEndCallback([player]() {
            // Copy callback/userData under lock, then invoke outside lock (avoids deadlock)
            SVGEndCallback cb = nullptr;
            void* userData = nullptr;
            {
                std::lock_guard<std::mutex> lock(player->mutex);
                cb = player->endCallback;
                userData = player->endUserData;
            }
            if (cb) {
                cb(userData);
            }
        });
    }
}

void FBFSVGPlayer_SetErrorCallback(FBFSVGPlayerRef player, SVGErrorCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->errorCallback = callback;
    player->errorUserData = userData;
}

void FBFSVGPlayer_SetElementTouchCallback(FBFSVGPlayerRef player, SVGElementTouchCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->elementTouchCallback = callback;
    player->elementTouchUserData = userData;
}

// =============================================================================
// Section 16: Statistics and Diagnostics
// =============================================================================

SVGRenderStats FBFSVGPlayer_GetStats(FBFSVGPlayerRef player) {
    SVGRenderStats emptyStats = {};
    if (!player) return emptyStats;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->stats;
}

void FBFSVGPlayer_ResetStats(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->stats = {};
}

const char* FBFSVGPlayer_GetLastError(FBFSVGPlayerRef player) {
    if (!player) return "";

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->lastError.c_str();
}

void FBFSVGPlayer_ClearError(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->lastError.clear();
}

// =============================================================================
// Section 17: Pre-buffering Functions
// =============================================================================

void FBFSVGPlayer_EnablePreBuffer(FBFSVGPlayerRef player, bool enable) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->preBufferEnabled = enable;

    if (!enable) {
        player->frameBuffer.clear();
        player->bufferedFrameStart = -1;
    }
}

bool FBFSVGPlayer_IsPreBufferEnabled(FBFSVGPlayerRef player) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->preBufferEnabled;
}

void FBFSVGPlayer_SetPreBufferFrames(FBFSVGPlayerRef player, int frameCount) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->preBufferFrameCount = std::max(1, std::min(frameCount, 60));
}

int FBFSVGPlayer_GetBufferedFrames(FBFSVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return static_cast<int>(player->frameBuffer.size());
}

void FBFSVGPlayer_ClearPreBuffer(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

// =============================================================================
// Section 18: Debug Overlay Functions
// =============================================================================

void FBFSVGPlayer_EnableDebugOverlay(FBFSVGPlayerRef player, bool enable) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->debugOverlayEnabled = enable;
}

bool FBFSVGPlayer_IsDebugOverlayEnabled(FBFSVGPlayerRef player) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->debugOverlayEnabled;
}

void FBFSVGPlayer_SetDebugFlags(FBFSVGPlayerRef player, uint32_t flags) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->debugFlags = flags;
}

uint32_t FBFSVGPlayer_GetDebugFlags(FBFSVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->debugFlags;
}

// =============================================================================
// Section 19: Utility Functions
// =============================================================================

const char* FBFSVGPlayer_FormatTime(double timeSeconds, char* outBuffer, int bufferSize) {
    if (!outBuffer || bufferSize <= 0) return "";

    int totalMs = static_cast<int>(timeSeconds * 1000);
    int minutes = totalMs / 60000;
    int seconds = (totalMs % 60000) / 1000;
    int ms = totalMs % 1000;

    std::snprintf(outBuffer, bufferSize, "%02d:%02d.%03d", minutes, seconds, ms);
    return outBuffer;
}

int FBFSVGPlayer_TimeToFrame(FBFSVGPlayerRef player, double timeSeconds) {
    if (!player || !player->controller) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);

    double duration = player->controller->getDuration();
    int totalFrames = player->controller->getTotalFrames();

    if (duration <= 0 || totalFrames <= 0) return 0;

    double progress = clamp(timeSeconds / duration, 0.0, 1.0);
    return static_cast<int>(progress * (totalFrames - 1));
}

double FBFSVGPlayer_FrameToTime(FBFSVGPlayerRef player, int frame) {
    if (!player || !player->controller) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);

    double duration = player->controller->getDuration();
    int totalFrames = player->controller->getTotalFrames();

    // Handle edge cases: no frames or single frame (avoids division by zero)
    if (totalFrames <= 0) return 0.0;
    if (totalFrames == 1) return 0.0;  // Single frame is always at time 0

    frame = clamp(frame, 0, totalFrames - 1);
    return (static_cast<double>(frame) / (totalFrames - 1)) * duration;
}

// =============================================================================
// Section 20: Zoom and ViewBox Functions
// =============================================================================

bool FBFSVGPlayer_GetViewBox(FBFSVGPlayerRef player, float* x, float* y, float* width, float* height) {
    if (!player || !x || !y || !width || !height) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->currentViewBox.isEmpty()) return false;

    *x = player->currentViewBox.x();
    *y = player->currentViewBox.y();
    *width = player->currentViewBox.width();
    *height = player->currentViewBox.height();
    return true;
}

void FBFSVGPlayer_SetViewBox(FBFSVGPlayerRef player, float x, float y, float width, float height) {
    if (!player) return;
    if (width <= 0 || height <= 0) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->currentViewBox = SkRect::MakeXYWH(x, y, width, height);

    // Calculate zoom level based on viewBox change
    if (!player->originalViewBox.isEmpty()) {
        float originalArea = player->originalViewBox.width() * player->originalViewBox.height();
        float currentArea = width * height;
        if (currentArea > 0) {
            // Zoom = sqrt(original area / current area) - smaller viewBox = zoomed in
            player->currentZoom = std::sqrt(originalArea / currentArea);
            player->currentZoom = clamp(player->currentZoom, player->minZoom, player->maxZoom);
        }
    }
}

void FBFSVGPlayer_ResetViewBox(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->currentViewBox = player->originalViewBox;
    player->currentZoom = 1.0f;
}

float FBFSVGPlayer_GetZoom(FBFSVGPlayerRef player) {
    if (!player) return 1.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->currentZoom;
}

void FBFSVGPlayer_SetZoom(FBFSVGPlayerRef player, float zoom, float centerX, float centerY,
                       int viewWidth, int viewHeight) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->originalViewBox.isEmpty()) return;

    // Clamp zoom to allowed range
    zoom = clamp(zoom, player->minZoom, player->maxZoom);

    // Calculate new viewBox dimensions (smaller viewBox = zoomed in)
    float newWidth = player->originalViewBox.width() / zoom;
    float newHeight = player->originalViewBox.height() / zoom;

    // Convert center point from view coordinates to SVG coordinates
    float svgCenterX, svgCenterY;
    float svgW = static_cast<float>(player->svgWidth);
    float svgH = static_cast<float>(player->svgHeight);
    float viewW = static_cast<float>(viewWidth);
    float viewH = static_cast<float>(viewHeight);

    if (viewW > 0 && viewH > 0 && svgW > 0 && svgH > 0) {
        float scaleX = viewW / svgW;
        float scaleY = viewH / svgH;
        float fitScale = std::min(scaleX, scaleY);

        float offsetX = (viewW - svgW * fitScale) / 2.0f;
        float offsetY = (viewH - svgH * fitScale) / 2.0f;

        svgCenterX = (centerX - offsetX) / fitScale;
        svgCenterY = (centerY - offsetY) / fitScale;
    } else {
        // Default to original viewBox center
        svgCenterX = player->originalViewBox.centerX();
        svgCenterY = player->originalViewBox.centerY();
    }

    // Calculate new viewBox position centered on the zoom point
    float newX = svgCenterX - newWidth / 2.0f;
    float newY = svgCenterY - newHeight / 2.0f;

    // Clamp to original viewBox bounds
    newX = clamp(newX, player->originalViewBox.x(),
                 player->originalViewBox.right() - newWidth);
    newY = clamp(newY, player->originalViewBox.y(),
                 player->originalViewBox.bottom() - newHeight);

    player->currentViewBox = SkRect::MakeXYWH(newX, newY, newWidth, newHeight);
    player->currentZoom = zoom;
}

void FBFSVGPlayer_ZoomIn(FBFSVGPlayerRef player, float factor, int viewWidth, int viewHeight) {
    if (!player) return;
    if (factor <= 0) factor = 1.5f;  // Default zoom in factor

    float currentZoom = FBFSVGPlayer_GetZoom(player);
    float newZoom = currentZoom * factor;

    // Zoom centered on the view
    FBFSVGPlayer_SetZoom(player, newZoom,
                      static_cast<float>(viewWidth) / 2.0f,
                      static_cast<float>(viewHeight) / 2.0f,
                      viewWidth, viewHeight);
}

void FBFSVGPlayer_ZoomOut(FBFSVGPlayerRef player, float factor, int viewWidth, int viewHeight) {
    if (!player) return;
    if (factor <= 0) factor = 1.5f;  // Default zoom out factor

    float currentZoom = FBFSVGPlayer_GetZoom(player);
    float newZoom = currentZoom / factor;

    // Zoom centered on the view
    FBFSVGPlayer_SetZoom(player, newZoom,
                      static_cast<float>(viewWidth) / 2.0f,
                      static_cast<float>(viewHeight) / 2.0f,
                      viewWidth, viewHeight);
}

void FBFSVGPlayer_ZoomToRect(FBFSVGPlayerRef player, float svgX, float svgY, float svgWidth, float svgHeight) {
    if (!player) return;
    if (svgWidth <= 0 || svgHeight <= 0) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->originalViewBox.isEmpty()) return;

    // Set the viewBox directly to the specified rectangle
    player->currentViewBox = SkRect::MakeXYWH(svgX, svgY, svgWidth, svgHeight);

    // Calculate resulting zoom level
    float originalArea = player->originalViewBox.width() * player->originalViewBox.height();
    float newArea = svgWidth * svgHeight;
    if (newArea > 0) {
        player->currentZoom = std::sqrt(originalArea / newArea);
        player->currentZoom = clamp(player->currentZoom, player->minZoom, player->maxZoom);
    }
}

bool FBFSVGPlayer_ZoomToElement(FBFSVGPlayerRef player, const char* elementID, float padding) {
    if (!player || !elementID) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Look up element bounds in cache
    auto it = player->elementBoundsCache.find(std::string(elementID));
    if (it == player->elementBoundsCache.end()) {
        // Element not found or not cached
        return false;
    }

    const SVGRect& bounds = it->second;

    // Apply padding
    float paddedX = bounds.x - padding;
    float paddedY = bounds.y - padding;
    float paddedWidth = bounds.width + 2 * padding;
    float paddedHeight = bounds.height + 2 * padding;

    // Clamp to original viewBox
    paddedX = std::max(paddedX, player->originalViewBox.x());
    paddedY = std::max(paddedY, player->originalViewBox.y());
    if (paddedX + paddedWidth > player->originalViewBox.right()) {
        paddedWidth = player->originalViewBox.right() - paddedX;
    }
    if (paddedY + paddedHeight > player->originalViewBox.bottom()) {
        paddedHeight = player->originalViewBox.bottom() - paddedY;
    }

    player->currentViewBox = SkRect::MakeXYWH(paddedX, paddedY, paddedWidth, paddedHeight);

    // Calculate resulting zoom level
    float originalArea = player->originalViewBox.width() * player->originalViewBox.height();
    float newArea = paddedWidth * paddedHeight;
    if (newArea > 0) {
        player->currentZoom = std::sqrt(originalArea / newArea);
        player->currentZoom = clamp(player->currentZoom, player->minZoom, player->maxZoom);
    }

    return true;
}

void FBFSVGPlayer_Pan(FBFSVGPlayerRef player, float deltaX, float deltaY, int viewWidth, int viewHeight) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->currentViewBox.isEmpty() || player->originalViewBox.isEmpty()) return;

    // Convert view delta to SVG delta
    float svgW = static_cast<float>(player->svgWidth);
    float svgH = static_cast<float>(player->svgHeight);
    float viewW = static_cast<float>(viewWidth);
    float viewH = static_cast<float>(viewHeight);

    if (viewW <= 0 || viewH <= 0 || svgW <= 0 || svgH <= 0) return;

    float scaleX = viewW / svgW;
    float scaleY = viewH / svgH;
    float fitScale = std::min(scaleX, scaleY);

    // Convert delta from view coordinates to SVG coordinates
    // Negate because panning moves the viewBox opposite to the gesture direction
    float svgDeltaX = -deltaX / fitScale / player->currentZoom;
    float svgDeltaY = -deltaY / fitScale / player->currentZoom;

    // Calculate new viewBox position
    float newX = player->currentViewBox.x() + svgDeltaX;
    float newY = player->currentViewBox.y() + svgDeltaY;

    // Clamp to original viewBox bounds
    newX = clamp(newX, player->originalViewBox.x(),
                 player->originalViewBox.right() - player->currentViewBox.width());
    newY = clamp(newY, player->originalViewBox.y(),
                 player->originalViewBox.bottom() - player->currentViewBox.height());

    player->currentViewBox = SkRect::MakeXYWH(newX, newY,
                                               player->currentViewBox.width(),
                                               player->currentViewBox.height());
}

float FBFSVGPlayer_GetMinZoom(FBFSVGPlayerRef player) {
    if (!player) return 0.1f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->minZoom;
}

void FBFSVGPlayer_SetMinZoom(FBFSVGPlayerRef player, float minZoom) {
    if (!player) return;
    if (minZoom <= 0) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->minZoom = minZoom;

    // Clamp current zoom if necessary
    if (player->currentZoom < player->minZoom) {
        player->currentZoom = player->minZoom;
    }
}

float FBFSVGPlayer_GetMaxZoom(FBFSVGPlayerRef player) {
    if (!player) return 10.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->maxZoom;
}

void FBFSVGPlayer_SetMaxZoom(FBFSVGPlayerRef player, float maxZoom) {
    if (!player) return;
    if (maxZoom <= 0) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->maxZoom = maxZoom;

    // Clamp current zoom if necessary
    if (player->currentZoom > player->maxZoom) {
        player->currentZoom = player->maxZoom;
    }
}

// =============================================================================
// Section 20: Multi-SVG Compositing Implementation
// =============================================================================

namespace {

/// Parse SVG data and initialize layer
bool parseLayerSVG(FBFSVGLayer* layer, FBFSVGPlayer* player) {
    if (!layer || !player || layer->svgData.empty()) return false;

    auto data = SkData::MakeWithoutCopy(layer->svgData.data(), layer->svgData.size());
    auto stream = SkMemoryStream::Make(data);

    SkSVGDOM::Builder builder;
    builder.setFontManager(player->fontMgr);
    builder.setResourceProvider(player->resourceProvider);

    layer->svgDom = builder.make(*stream);
    if (!layer->svgDom) return false;

    // Get SVG root
    auto* svgRoot = layer->svgDom->getRoot();
    if (!svgRoot) return false;

    // Get intrinsic size
    auto intrinsicSize = layer->svgDom->containerSize();
    layer->width = static_cast<int>(intrinsicSize.width());
    layer->height = static_cast<int>(intrinsicSize.height());

    // Store viewBox
    layer->viewBox = SkRect::MakeWH(intrinsicSize.width(), intrinsicSize.height());

    // Initialize animation controller with SVG content
    // Duration is automatically parsed from SVG animation elements during loadFromContent()
    layer->controller->loadFromContent(layer->svgData);

    return true;
}

}  // namespace

FBFSVGLayerRef FBFSVGPlayer_CreateLayer(FBFSVGPlayerRef player, const char* filepath) {
    if (!player || !filepath) return nullptr;

    // Read file with RAII - unique_ptr automatically closes on exception or return
    auto fileCloser = [](FILE* f) { if (f) fclose(f); };
    std::unique_ptr<FILE, decltype(fileCloser)> file(fopen(filepath, "rb"), fileCloser);

    if (!file) {
        setError(player, 100, std::string("Failed to open file: ") + filepath);
        return nullptr;
    }

    fseek(file.get(), 0, SEEK_END);
    long size = ftell(file.get());
    fseek(file.get(), 0, SEEK_SET);

    if (size <= 0) {
        setError(player, 100, std::string("Invalid file size: ") + filepath);
        return nullptr;
    }

    std::vector<char> buffer(size);
    size_t read = fread(buffer.data(), 1, size, file.get());

    if (read != static_cast<size_t>(size)) {
        setError(player, 100, std::string("Failed to read file: ") + filepath);
        return nullptr;
    }

    // file is automatically closed when unique_ptr goes out of scope
    return FBFSVGPlayer_CreateLayerFromData(player, buffer.data(), buffer.size());
}

FBFSVGLayerRef FBFSVGPlayer_CreateLayerFromData(FBFSVGPlayerRef player, const void* data, size_t length) {
    if (!player || !data || length == 0) return nullptr;

    std::lock_guard<std::mutex> lock(player->mutex);

    auto layer = std::make_unique<FBFSVGLayer>();
    layer->owner = player;
    layer->svgData.assign(static_cast<const char*>(data), length);

    // Assign z-order based on current layer count
    layer->zOrder = static_cast<int>(player->layers.size());

    if (!parseLayerSVG(layer.get(), player)) {
        setError(player, 101, "Failed to parse layer SVG data");
        return nullptr;
    }

    FBFSVGLayer* result = layer.get();
    player->layers.push_back(std::move(layer));

    return result;
}

void FBFSVGPlayer_DestroyLayer(FBFSVGPlayerRef player, FBFSVGLayerRef layer) {
    if (!player || !layer) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Find and remove the layer
    auto it = std::find_if(player->layers.begin(), player->layers.end(),
                           [layer](const std::unique_ptr<FBFSVGLayer>& l) { return l.get() == layer; });

    if (it != player->layers.end()) {
        player->layers.erase(it);
    }
}

int FBFSVGPlayer_GetLayerCount(FBFSVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Count includes primary SVG (if loaded) plus layers
    int count = player->svgDom ? 1 : 0;
    count += static_cast<int>(player->layers.size());
    return count;
}

FBFSVGLayerRef FBFSVGPlayer_GetLayerAtIndex(FBFSVGPlayerRef player, int index) {
    if (!player || index < 0) return nullptr;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Index 0 could refer to "primary" SVG but we don't expose it as a layer
    // So we return from the layers vector directly
    if (index >= static_cast<int>(player->layers.size())) return nullptr;

    return player->layers[index].get();
}

void FBFSVGLayer_SetPosition(FBFSVGLayerRef layer, float x, float y) {
    if (!layer) return;
    layer->posX = x;
    layer->posY = y;
}

void FBFSVGLayer_GetPosition(FBFSVGLayerRef layer, float* x, float* y) {
    if (!layer) return;
    if (x) *x = layer->posX;
    if (y) *y = layer->posY;
}

void FBFSVGLayer_SetOpacity(FBFSVGLayerRef layer, float opacity) {
    if (!layer) return;
    layer->opacity = clamp(opacity, 0.0f, 1.0f);
}

float FBFSVGLayer_GetOpacity(FBFSVGLayerRef layer) {
    if (!layer) return 1.0f;
    return layer->opacity;
}

void FBFSVGLayer_SetZOrder(FBFSVGLayerRef layer, int zOrder) {
    if (!layer) return;
    layer->zOrder = zOrder;
}

int FBFSVGLayer_GetZOrder(FBFSVGLayerRef layer) {
    if (!layer) return 0;
    return layer->zOrder;
}

void FBFSVGLayer_SetVisible(FBFSVGLayerRef layer, bool visible) {
    if (!layer) return;
    layer->visible = visible;
}

bool FBFSVGLayer_IsVisible(FBFSVGLayerRef layer) {
    if (!layer) return false;
    return layer->visible;
}

void FBFSVGLayer_SetScale(FBFSVGLayerRef layer, float scaleX, float scaleY) {
    if (!layer) return;
    layer->scaleX = scaleX;
    layer->scaleY = scaleY;
}

void FBFSVGLayer_GetScale(FBFSVGLayerRef layer, float* scaleX, float* scaleY) {
    if (!layer) return;
    if (scaleX) *scaleX = layer->scaleX;
    if (scaleY) *scaleY = layer->scaleY;
}

void FBFSVGLayer_SetRotation(FBFSVGLayerRef layer, float angleDegrees) {
    if (!layer) return;
    layer->rotation = angleDegrees;
}

float FBFSVGLayer_GetRotation(FBFSVGLayerRef layer) {
    if (!layer) return 0.0f;
    return layer->rotation;
}

void FBFSVGLayer_SetBlendMode(FBFSVGLayerRef layer, FBFSVGLayerBlendMode blendMode) {
    if (!layer) return;
    layer->blendMode = blendMode;
}

FBFSVGLayerBlendMode FBFSVGLayer_GetBlendMode(FBFSVGLayerRef layer) {
    if (!layer) return FBFSVGLayerBlend_Normal;
    return layer->blendMode;
}

bool FBFSVGLayer_GetSize(FBFSVGLayerRef layer, int* width, int* height) {
    if (!layer) return false;
    if (width) *width = layer->width;
    if (height) *height = layer->height;
    return true;
}

double FBFSVGLayer_GetDuration(FBFSVGLayerRef layer) {
    if (!layer || !layer->controller) return 0.0;
    return layer->controller->getDuration();
}

bool FBFSVGLayer_HasAnimations(FBFSVGLayerRef layer) {
    if (!layer) return false;
    return layer->controller && layer->controller->getDuration() > 0.0;
}

void FBFSVGLayer_Play(FBFSVGLayerRef layer) {
    if (!layer || !layer->controller) return;
    layer->controller->play();
}

void FBFSVGLayer_Pause(FBFSVGLayerRef layer) {
    if (!layer || !layer->controller) return;
    layer->controller->pause();
}

void FBFSVGLayer_Stop(FBFSVGLayerRef layer) {
    if (!layer || !layer->controller) return;
    layer->controller->stop();
}

void FBFSVGLayer_SeekTo(FBFSVGLayerRef layer, double timeSeconds) {
    if (!layer || !layer->controller) return;
    layer->controller->seekTo(timeSeconds);
}

bool FBFSVGLayer_Update(FBFSVGLayerRef layer, double deltaTime) {
    if (!layer || !layer->controller) return false;
    return layer->controller->update(deltaTime);
}

bool FBFSVGPlayer_UpdateAllLayers(FBFSVGPlayerRef player, double deltaTime) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    bool needsRender = false;

    // Update primary SVG
    if (player->controller) {
        needsRender |= player->controller->update(deltaTime);
    }

    // Update all layers
    for (auto& layer : player->layers) {
        if (layer && layer->controller) {
            needsRender |= layer->controller->update(deltaTime);
        }
    }

    return needsRender;
}

void FBFSVGPlayer_PlayAllLayers(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Play primary SVG
    if (player->controller) {
        player->controller->play();
    }

    // Play all layers
    for (auto& layer : player->layers) {
        if (layer && layer->controller) {
            layer->controller->play();
        }
    }
}

void FBFSVGPlayer_PauseAllLayers(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Pause primary SVG
    if (player->controller) {
        player->controller->pause();
    }

    // Pause all layers
    for (auto& layer : player->layers) {
        if (layer && layer->controller) {
            layer->controller->pause();
        }
    }
}

void FBFSVGPlayer_StopAllLayers(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Stop primary SVG
    if (player->controller) {
        player->controller->stop();
    }

    // Stop all layers
    for (auto& layer : player->layers) {
        if (layer && layer->controller) {
            layer->controller->stop();
        }
    }
}

bool FBFSVGPlayer_RenderComposite(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale) {
    if (!player || !pixelBuffer || width <= 0 || height <= 0) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Create Skia surface for rendering
    int scaledWidth = static_cast<int>(width * scale);
    int scaledHeight = static_cast<int>(height * scale);

    SkImageInfo info = SkImageInfo::Make(scaledWidth, scaledHeight, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    SkBitmap bitmap;
    bitmap.allocPixels(info);
    bitmap.eraseColor(SK_ColorTRANSPARENT);

    SkCanvas canvas(bitmap);
    canvas.scale(scale, scale);

    // Collect all renderable items with z-order
    struct RenderItem {
        int zOrder;
        bool isPrimary;
        FBFSVGLayer* layer;
    };
    std::vector<RenderItem> items;

    // Add primary SVG as z-order 0
    if (player->svgDom) {
        items.push_back({0, true, nullptr});
    }

    // Add all visible layers
    for (auto& layer : player->layers) {
        if (layer && layer->visible) {
            items.push_back({layer->zOrder, false, layer.get()});
        }
    }

    // Sort by z-order (lowest first)
    std::sort(items.begin(), items.end(), [](const RenderItem& a, const RenderItem& b) { return a.zOrder < b.zOrder; });

    // Render each item
    for (const auto& item : items) {
        canvas.save();

        if (item.isPrimary) {
            // Render primary SVG using current viewBox (for zoom)
            player->svgDom->setContainerSize(SkSize::Make(width, height));
            player->svgDom->render(&canvas);
        } else if (item.layer) {
            // Apply layer transform
            auto* layer = item.layer;

            // Translate to position
            canvas.translate(layer->posX, layer->posY);

            // Apply rotation around center
            if (layer->rotation != 0.0f) {
                float cx = layer->width * layer->scaleX / 2.0f;
                float cy = layer->height * layer->scaleY / 2.0f;
                canvas.translate(cx, cy);
                canvas.rotate(layer->rotation);
                canvas.translate(-cx, -cy);
            }

            // Apply scale
            canvas.scale(layer->scaleX, layer->scaleY);

            // Set opacity and blend mode via paint
            SkPaint paint;
            paint.setAlpha(static_cast<U8CPU>(layer->opacity * 255));
            paint.setBlendMode(toSkBlendMode(layer->blendMode));

            // Render layer with saveLayer for opacity/blend
            if (layer->opacity < 1.0f || layer->blendMode != FBFSVGLayerBlend_Normal) {
                SkRect bounds = SkRect::MakeWH(layer->width, layer->height);
                canvas.saveLayerAlpha(&bounds, static_cast<U8CPU>(layer->opacity * 255));
            }

            if (layer->svgDom) {
                layer->svgDom->setContainerSize(SkSize::Make(layer->width, layer->height));
                layer->svgDom->render(&canvas);
            }

            if (layer->opacity < 1.0f || layer->blendMode != FBFSVGLayerBlend_Normal) {
                canvas.restore();
            }
        }

        canvas.restore();
    }

    // Copy to output buffer
    std::memcpy(pixelBuffer, bitmap.getPixels(), scaledWidth * scaledHeight * 4);

    return true;
}

bool FBFSVGPlayer_RenderCompositeAtTime(FBFSVGPlayerRef player, void* pixelBuffer, int width, int height, float scale,
                                      double timeSeconds) {
    if (!player) return false;

    // First seek all layers to the specified time
    {
        std::lock_guard<std::mutex> lock(player->mutex);

        if (player->controller) {
            player->controller->seekTo(timeSeconds);
        }

        for (auto& layer : player->layers) {
            if (layer && layer->controller) {
                layer->controller->seekTo(timeSeconds);
            }
        }
    }

    // Then render (RenderComposite does its own locking)
    return FBFSVGPlayer_RenderComposite(player, pixelBuffer, width, height, scale);
}

// =============================================================================
// Section 21: Frame Rate and Timing Control
// =============================================================================

void FBFSVGPlayer_SetTargetFrameRate(FBFSVGPlayerRef player, float fps) {
    if (!player) return;
    if (fps <= 0.0f) return;  // Invalid FPS

    std::lock_guard<std::mutex> lock(player->mutex);
    player->targetFrameRate = fps;
}

float FBFSVGPlayer_GetTargetFrameRate(FBFSVGPlayerRef player) {
    if (!player) return 60.0f;  // Default fallback

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->targetFrameRate;
}

double FBFSVGPlayer_GetIdealFrameInterval(FBFSVGPlayerRef player) {
    if (!player) return 1.0 / 60.0;  // Default 60 FPS interval

    std::lock_guard<std::mutex> lock(player->mutex);
    if (player->targetFrameRate <= 0.0f) return 1.0 / 60.0;
    return 1.0 / static_cast<double>(player->targetFrameRate);
}

void FBFSVGPlayer_BeginFrame(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Record frame begin time using high-resolution clock
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    player->frameBeginTimeSeconds = std::chrono::duration<double>(duration).count();
}

void FBFSVGPlayer_EndFrame(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Calculate frame duration
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double currentTime = std::chrono::duration<double>(duration).count();

    if (player->frameBeginTimeSeconds > 0.0) {
        player->lastFrameDurationSeconds = currentTime - player->frameBeginTimeSeconds;

        // Add to rolling history for average calculation
        player->frameDurationHistory[player->frameHistoryIndex] = player->lastFrameDurationSeconds;
        player->frameHistoryIndex = (player->frameHistoryIndex + 1) % FBFSVGPlayer::FRAME_HISTORY_SIZE;
        if (player->frameHistoryCount < FBFSVGPlayer::FRAME_HISTORY_SIZE) {
            player->frameHistoryCount++;
        }

        // Detect dropped frames (if frame took longer than 2x target interval)
        double targetInterval = 1.0 / static_cast<double>(player->targetFrameRate);
        if (player->lastFrameDurationSeconds > targetInterval * 2.0) {
            player->droppedFrameCount++;
        }
    }
}

double FBFSVGPlayer_GetLastFrameDuration(FBFSVGPlayerRef player) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->lastFrameDurationSeconds;
}

double FBFSVGPlayer_GetAverageFrameDuration(FBFSVGPlayerRef player) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->frameHistoryCount == 0) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < player->frameHistoryCount; i++) {
        sum += player->frameDurationHistory[i];
    }
    return sum / static_cast<double>(player->frameHistoryCount);
}

float FBFSVGPlayer_GetMeasuredFPS(FBFSVGPlayerRef player) {
    if (!player) return 0.0f;

    double avgDuration = FBFSVGPlayer_GetAverageFrameDuration(player);
    if (avgDuration <= 0.0) return 0.0f;

    return static_cast<float>(1.0 / avgDuration);
}

bool FBFSVGPlayer_ShouldRenderFrame(FBFSVGPlayerRef player, double currentTimeSeconds) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    double targetInterval = 1.0 / static_cast<double>(player->targetFrameRate);
    double timeSinceLastRender = currentTimeSeconds - player->lastRenderTimeSeconds;

    // Render if enough time has passed since last render
    // Use 0.9x threshold to avoid accumulating delay
    return timeSinceLastRender >= (targetInterval * 0.9);
}

void FBFSVGPlayer_MarkFrameRendered(FBFSVGPlayerRef player, double renderTimeSeconds) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->lastRenderTimeSeconds = renderTimeSeconds;
}

int FBFSVGPlayer_GetDroppedFrameCount(FBFSVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->droppedFrameCount;
}

void FBFSVGPlayer_ResetFrameStats(FBFSVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->droppedFrameCount = 0;
    player->frameHistoryIndex = 0;
    player->frameHistoryCount = 0;
    player->lastFrameDurationSeconds = 0.0;
    player->lastRenderTimeSeconds = 0.0;
    player->frameBeginTimeSeconds = 0.0;

    // Clear history array
    for (int i = 0; i < FBFSVGPlayer::FRAME_HISTORY_SIZE; i++) {
        player->frameDurationHistory[i] = 0.0;
    }
}

double FBFSVGPlayer_GetLastRenderTime(FBFSVGPlayerRef player) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->lastRenderTimeSeconds;
}

double FBFSVGPlayer_GetTimeSinceLastRender(FBFSVGPlayerRef player, double currentTimeSeconds) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return currentTimeSeconds - player->lastRenderTimeSeconds;
}
