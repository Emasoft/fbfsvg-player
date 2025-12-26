// svg_player_api.cpp - Unified Cross-Platform SVG Player Implementation
//
// This file implements the unified SVG Player C API by wrapping
// SVGAnimationController and Skia rendering primitives.
//
// Copyright (c) 2024. MIT License.

#include "svg_player_api.h"

#include "SVGAnimationController.h"

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

/// Skia blend mode mapping from SVGLayerBlendMode
static SkBlendMode toSkBlendMode(SVGLayerBlendMode mode) {
    switch (mode) {
        case SVGLayerBlend_Multiply: return SkBlendMode::kMultiply;
        case SVGLayerBlend_Screen: return SkBlendMode::kScreen;
        case SVGLayerBlend_Overlay: return SkBlendMode::kOverlay;
        case SVGLayerBlend_Darken: return SkBlendMode::kDarken;
        case SVGLayerBlend_Lighten: return SkBlendMode::kLighten;
        default: return SkBlendMode::kSrcOver;
    }
}

/// Internal layer structure for multi-SVG compositing (opaque to C API)
struct SVGLayer {
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
    SVGLayerBlendMode blendMode = SVGLayerBlend_Normal;

    // Owner player (for accessing shared resources)
    struct SVGPlayer* owner = nullptr;

    SVGLayer() {
        controller = std::make_unique<svgplayer::SVGAnimationController>();
    }
};

/// Internal player structure (opaque to C API)
struct SVGPlayer {
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
    std::vector<std::unique_ptr<SVGLayer>> layers;

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

    SVGPlayer() {
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
void setError(SVGPlayer* player, int code, const std::string& message) {
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
bool parseSVG(SVGPlayer* player, const char* data, size_t length) {
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
    if (root->getViewBox().isValid()) {
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
void updateSVGForCurrentTime(SVGPlayer* player) {
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

SVGPlayerRef SVGPlayer_Create(void) {
    try {
        return new SVGPlayer();
    } catch (...) {
        return nullptr;
    }
}

void SVGPlayer_Destroy(SVGPlayerRef player) {
    if (player) {
        delete player;
    }
}

const char* SVGPlayer_GetVersion(void) {
    // Return the full version string from version.h (includes prerelease tag)
    return SVG_PLAYER_VERSION_STRING;
}

void SVGPlayer_GetVersionNumbers(int* major, int* minor, int* patch) {
    if (major) *major = SVG_PLAYER_API_VERSION_MAJOR;
    if (minor) *minor = SVG_PLAYER_API_VERSION_MINOR;
    if (patch) *patch = SVG_PLAYER_API_VERSION_PATCH;
}

// =============================================================================
// Section 2: Loading Functions
// =============================================================================

bool SVGPlayer_LoadSVG(SVGPlayerRef player, const char* filepath) {
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

bool SVGPlayer_LoadSVGData(SVGPlayerRef player, const void* data, size_t length) {
    if (!player || !data || length == 0) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return parseSVG(player, static_cast<const char*>(data), length);
}

void SVGPlayer_Unload(SVGPlayerRef player) {
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

bool SVGPlayer_IsLoaded(SVGPlayerRef player) {
    if (!player) return false;
    std::lock_guard<std::mutex> lock(player->mutex);
    return player->svgDom != nullptr;
}

bool SVGPlayer_HasAnimations(SVGPlayerRef player) {
    if (!player) return false;
    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller && player->controller->hasAnimations();
}

// =============================================================================
// Section 3: Size and Dimension Functions
// =============================================================================

bool SVGPlayer_GetSize(SVGPlayerRef player, int* width, int* height) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom) return false;

    if (width) *width = player->svgWidth;
    if (height) *height = player->svgHeight;
    return true;
}

bool SVGPlayer_GetSizeInfo(SVGPlayerRef player, SVGSizeInfo* info) {
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

void SVGPlayer_SetViewport(SVGPlayerRef player, int width, int height) {
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

void SVGPlayer_Play(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    SVGPlaybackState oldState;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        oldState = fromControllerState(player->controller->getPlaybackState());
        player->controller->play();
    }

    // Invoke callback outside lock to avoid deadlock
    SVGPlaybackState newState = SVGPlaybackState_Playing;
    if (player->stateChangeCallback && oldState != newState) {
        player->stateChangeCallback(player->stateChangeUserData, newState);
    }
}

void SVGPlayer_Pause(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    SVGPlaybackState oldState;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        oldState = fromControllerState(player->controller->getPlaybackState());
        player->controller->pause();
    }

    SVGPlaybackState newState = SVGPlaybackState_Paused;
    if (player->stateChangeCallback && oldState != newState) {
        player->stateChangeCallback(player->stateChangeUserData, newState);
    }
}

void SVGPlayer_Stop(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    SVGPlaybackState oldState;
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        oldState = fromControllerState(player->controller->getPlaybackState());
        player->controller->stop();
        player->completedLoops = 0;
        player->playingForward = true;
    }

    SVGPlaybackState newState = SVGPlaybackState_Stopped;
    if (player->stateChangeCallback && oldState != newState) {
        player->stateChangeCallback(player->stateChangeUserData, newState);
    }
}

void SVGPlayer_TogglePlayback(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->togglePlayback();
}

void SVGPlayer_SetPlaybackState(SVGPlayerRef player, SVGPlaybackState state) {
    switch (state) {
        case SVGPlaybackState_Playing:
            SVGPlayer_Play(player);
            break;
        case SVGPlaybackState_Paused:
            SVGPlayer_Pause(player);
            break;
        case SVGPlaybackState_Stopped:
            SVGPlayer_Stop(player);
            break;
    }
}

SVGPlaybackState SVGPlayer_GetPlaybackState(SVGPlayerRef player) {
    if (!player || !player->controller) return SVGPlaybackState_Stopped;

    std::lock_guard<std::mutex> lock(player->mutex);
    return fromControllerState(player->controller->getPlaybackState());
}

bool SVGPlayer_IsPlaying(SVGPlayerRef player) { return SVGPlayer_GetPlaybackState(player) == SVGPlaybackState_Playing; }

bool SVGPlayer_IsPaused(SVGPlayerRef player) { return SVGPlayer_GetPlaybackState(player) == SVGPlaybackState_Paused; }

bool SVGPlayer_IsStopped(SVGPlayerRef player) { return SVGPlayer_GetPlaybackState(player) == SVGPlaybackState_Stopped; }

// =============================================================================
// Section 5: Repeat Mode Functions
// =============================================================================

void SVGPlayer_SetRepeatMode(SVGPlayerRef player, SVGRepeatMode mode) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->setRepeatMode(toControllerRepeatMode(mode));
}

SVGRepeatMode SVGPlayer_GetRepeatMode(SVGPlayerRef player) {
    if (!player || !player->controller) return SVGRepeatMode_None;

    std::lock_guard<std::mutex> lock(player->mutex);
    return fromControllerRepeatMode(player->controller->getRepeatMode());
}

void SVGPlayer_SetRepeatCount(SVGPlayerRef player, int count) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->repeatCount = std::max(1, count);
}

int SVGPlayer_GetRepeatCount(SVGPlayerRef player) {
    if (!player) return 1;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->repeatCount;
}

int SVGPlayer_GetCompletedLoops(SVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->completedLoops;
}

bool SVGPlayer_IsPlayingForward(SVGPlayerRef player) {
    if (!player) return true;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->playingForward;
}

bool SVGPlayer_IsLooping(SVGPlayerRef player) {
    SVGRepeatMode mode = SVGPlayer_GetRepeatMode(player);
    return mode == SVGRepeatMode_Loop || mode == SVGRepeatMode_Reverse;
}

void SVGPlayer_SetLooping(SVGPlayerRef player, bool looping) {
    SVGPlayer_SetRepeatMode(player, looping ? SVGRepeatMode_Loop : SVGRepeatMode_None);
}

// =============================================================================
// Section 6: Playback Rate Functions
// =============================================================================

void SVGPlayer_SetPlaybackRate(SVGPlayerRef player, float rate) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->playbackRate = clamp(rate, -10.0f, 10.0f);

    // Avoid exactly 0 rate (would freeze playback)
    if (std::abs(player->playbackRate) < 0.1f) {
        player->playbackRate = (player->playbackRate >= 0) ? 0.1f : -0.1f;
    }
}

float SVGPlayer_GetPlaybackRate(SVGPlayerRef player) {
    if (!player) return 1.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->playbackRate;
}

// =============================================================================
// Section 7: Timeline Functions
// =============================================================================

bool SVGPlayer_Update(SVGPlayerRef player, double deltaTime) {
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

double SVGPlayer_GetDuration(SVGPlayerRef player) {
    if (!player || !player->controller) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getDuration();
}

double SVGPlayer_GetCurrentTime(SVGPlayerRef player) {
    if (!player || !player->controller) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getCurrentTime();
}

float SVGPlayer_GetProgress(SVGPlayerRef player) {
    if (!player || !player->controller) return 0.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return static_cast<float>(player->controller->getProgress());
}

int SVGPlayer_GetCurrentFrame(SVGPlayerRef player) {
    if (!player || !player->controller) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getCurrentFrame();
}

int SVGPlayer_GetTotalFrames(SVGPlayerRef player) {
    if (!player || !player->controller) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->controller->getTotalFrames();
}

float SVGPlayer_GetFrameRate(SVGPlayerRef player) {
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

void SVGPlayer_SeekTo(SVGPlayerRef player, double timeSeconds) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->seekTo(timeSeconds);
    updateSVGForCurrentTime(player);

    // Clear pre-buffer after seeking
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

void SVGPlayer_SeekToFrame(SVGPlayerRef player, int frame) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->seekToFrame(frame);
    updateSVGForCurrentTime(player);

    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

void SVGPlayer_SeekToProgress(SVGPlayerRef player, float progress) {
    if (!player || !player->controller) return;

    double duration = SVGPlayer_GetDuration(player);
    SVGPlayer_SeekTo(player, duration * clamp(progress, 0.0f, 1.0f));
}

void SVGPlayer_SeekToStart(SVGPlayerRef player) { SVGPlayer_SeekTo(player, 0.0); }

void SVGPlayer_SeekToEnd(SVGPlayerRef player) { SVGPlayer_SeekTo(player, SVGPlayer_GetDuration(player)); }

void SVGPlayer_SeekForwardByTime(SVGPlayerRef player, double seconds) {
    double current = SVGPlayer_GetCurrentTime(player);
    SVGPlayer_SeekTo(player, current + seconds);
}

void SVGPlayer_SeekBackwardByTime(SVGPlayerRef player, double seconds) {
    double current = SVGPlayer_GetCurrentTime(player);
    SVGPlayer_SeekTo(player, current - seconds);
}

// =============================================================================
// Section 9: Frame Stepping Functions
// =============================================================================

void SVGPlayer_StepForward(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->pause();
    player->controller->stepForward();
    updateSVGForCurrentTime(player);
}

void SVGPlayer_StepBackward(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->pause();
    player->controller->stepBackward();
    updateSVGForCurrentTime(player);
}

void SVGPlayer_StepByFrames(SVGPlayerRef player, int frames) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->pause();
    player->controller->stepByFrames(frames);
    updateSVGForCurrentTime(player);
}

// =============================================================================
// Section 10: Scrubbing Functions
// =============================================================================

void SVGPlayer_BeginScrubbing(SVGPlayerRef player) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->stateBeforeScrub = fromControllerState(player->controller->getPlaybackState());
    player->controller->beginScrubbing();
    player->isScrubbing = true;
}

void SVGPlayer_ScrubToProgress(SVGPlayerRef player, float progress) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->scrubToProgress(clamp(progress, 0.0f, 1.0f));
    updateSVGForCurrentTime(player);
}

void SVGPlayer_EndScrubbing(SVGPlayerRef player, bool resume) {
    if (!player || !player->controller) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->controller->endScrubbing(resume);
    player->isScrubbing = false;
}

bool SVGPlayer_IsScrubbing(SVGPlayerRef player) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->isScrubbing;
}

// =============================================================================
// Section 11: Rendering Functions
// =============================================================================

bool SVGPlayer_Render(SVGPlayerRef player, void* pixelBuffer, int width, int height, float scale) {
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

bool SVGPlayer_RenderAtTime(SVGPlayerRef player, void* pixelBuffer, int width, int height, float scale,
                            double timeSeconds) {
    if (!player || !player->controller) return false;

    // Temporarily seek to the specified time
    double savedTime = SVGPlayer_GetCurrentTime(player);

    {
        std::lock_guard<std::mutex> lock(player->mutex);
        player->controller->seekTo(timeSeconds);
        updateSVGForCurrentTime(player);
    }

    // Render
    bool result = SVGPlayer_Render(player, pixelBuffer, width, height, scale);

    // Restore original time
    {
        std::lock_guard<std::mutex> lock(player->mutex);
        player->controller->seekTo(savedTime);
        updateSVGForCurrentTime(player);
    }

    return result;
}

bool SVGPlayer_RenderFrame(SVGPlayerRef player, void* pixelBuffer, int width, int height, float scale, int frame) {
    if (!player || !player->controller) return false;

    // Convert frame to time
    int totalFrames = player->controller->getTotalFrames();
    double duration = player->controller->getDuration();

    if (totalFrames <= 0) {
        frame = 0;
    } else {
        frame = clamp(frame, 0, totalFrames - 1);
    }

    double timeSeconds = (totalFrames > 0) ? (static_cast<double>(frame) / totalFrames) * duration : 0.0;

    return SVGPlayer_RenderAtTime(player, pixelBuffer, width, height, scale, timeSeconds);
}

// =============================================================================
// Section 12: Coordinate Conversion Functions
// =============================================================================

// Internal helper - MUST be called while holding player->mutex
// Prevents deadlock when called from functions that already hold the lock
static bool viewToSVGInternal(SVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight,
                               float* svgX, float* svgY) {
    // Caller must ensure: player != nullptr, svgX != nullptr, svgY != nullptr, mutex held

    if (!player->svgDom || player->svgWidth <= 0 || player->svgHeight <= 0) {
        return false;
    }

    // Use currentViewBox for zoom support - must match SVGPlayer_Render logic
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

bool SVGPlayer_ViewToSVG(SVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight, float* svgX,
                         float* svgY) {
    if (!player || !svgX || !svgY) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return viewToSVGInternal(player, viewX, viewY, viewWidth, viewHeight, svgX, svgY);
}

bool SVGPlayer_SVGToView(SVGPlayerRef player, float svgX, float svgY, int viewWidth, int viewHeight, float* viewX,
                         float* viewY) {
    if (!player || !viewX || !viewY) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom || player->svgWidth <= 0 || player->svgHeight <= 0) {
        return false;
    }

    // Use currentViewBox for zoom support - must match SVGPlayer_Render logic
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

void SVGPlayer_SubscribeToElement(SVGPlayerRef player, const char* objectID) {
    if (!player || !objectID) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->subscribedElements.insert(std::string(objectID));
}

void SVGPlayer_UnsubscribeFromElement(SVGPlayerRef player, const char* objectID) {
    if (!player || !objectID) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->subscribedElements.erase(std::string(objectID));
}

void SVGPlayer_UnsubscribeFromAllElements(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->subscribedElements.clear();
}

const char* SVGPlayer_HitTest(SVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight) {
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

bool SVGPlayer_GetElementBounds(SVGPlayerRef player, const char* objectID, SVGRect* bounds) {
    if (!player || !objectID || !bounds) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Check cache first
    auto it = player->elementBoundsCache.find(objectID);
    if (it != player->elementBoundsCache.end()) {
        *bounds = it->second;
        return true;
    }

    // TODO: Query Skia DOM for element bounds
    // This requires traversing the DOM tree to find the element by ID
    // and computing its bounding box. For now, return false.

    return false;
}

int SVGPlayer_GetElementsAtPoint(SVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight,
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

bool SVGPlayer_ElementExists(SVGPlayerRef player, const char* elementID) {
    if (!player || !elementID) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Check if element ID exists in the original SVG
    // Simple string search for id="elementID"
    std::string searchPattern = std::string("id=\"") + elementID + "\"";
    return player->originalSvgData.find(searchPattern) != std::string::npos;
}

bool SVGPlayer_GetElementProperty(SVGPlayerRef player, const char* elementID, const char* propertyName, char* outValue,
                                  int maxLength) {
    if (!player || !elementID || !propertyName || !outValue || maxLength <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(player->mutex);

    // TODO: Implement proper DOM traversal to get element properties
    // For now, this is a stub that returns false

    outValue[0] = '\0';
    return false;
}

// =============================================================================
// Section 15: Callback Functions
// =============================================================================

void SVGPlayer_SetStateChangeCallback(SVGPlayerRef player, SVGStateChangeCallback callback, void* userData) {
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

void SVGPlayer_SetLoopCallback(SVGPlayerRef player, SVGLoopCallback callback, void* userData) {
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

void SVGPlayer_SetEndCallback(SVGPlayerRef player, SVGEndCallback callback, void* userData) {
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

void SVGPlayer_SetErrorCallback(SVGPlayerRef player, SVGErrorCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->errorCallback = callback;
    player->errorUserData = userData;
}

void SVGPlayer_SetElementTouchCallback(SVGPlayerRef player, SVGElementTouchCallback callback, void* userData) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->elementTouchCallback = callback;
    player->elementTouchUserData = userData;
}

// =============================================================================
// Section 16: Statistics and Diagnostics
// =============================================================================

SVGRenderStats SVGPlayer_GetStats(SVGPlayerRef player) {
    SVGRenderStats emptyStats = {};
    if (!player) return emptyStats;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->stats;
}

void SVGPlayer_ResetStats(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->stats = {};
}

const char* SVGPlayer_GetLastError(SVGPlayerRef player) {
    if (!player) return "";

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->lastError.c_str();
}

void SVGPlayer_ClearError(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->lastError.clear();
}

// =============================================================================
// Section 17: Pre-buffering Functions
// =============================================================================

void SVGPlayer_EnablePreBuffer(SVGPlayerRef player, bool enable) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->preBufferEnabled = enable;

    if (!enable) {
        player->frameBuffer.clear();
        player->bufferedFrameStart = -1;
    }
}

bool SVGPlayer_IsPreBufferEnabled(SVGPlayerRef player) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->preBufferEnabled;
}

void SVGPlayer_SetPreBufferFrames(SVGPlayerRef player, int frameCount) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->preBufferFrameCount = std::max(1, std::min(frameCount, 60));
}

int SVGPlayer_GetBufferedFrames(SVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return static_cast<int>(player->frameBuffer.size());
}

void SVGPlayer_ClearPreBuffer(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->frameBuffer.clear();
    player->bufferedFrameStart = -1;
}

// =============================================================================
// Section 18: Debug Overlay Functions
// =============================================================================

void SVGPlayer_EnableDebugOverlay(SVGPlayerRef player, bool enable) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->debugOverlayEnabled = enable;
}

bool SVGPlayer_IsDebugOverlayEnabled(SVGPlayerRef player) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->debugOverlayEnabled;
}

void SVGPlayer_SetDebugFlags(SVGPlayerRef player, uint32_t flags) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->debugFlags = flags;
}

uint32_t SVGPlayer_GetDebugFlags(SVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->debugFlags;
}

// =============================================================================
// Section 19: Utility Functions
// =============================================================================

const char* SVGPlayer_FormatTime(double timeSeconds, char* outBuffer, int bufferSize) {
    if (!outBuffer || bufferSize <= 0) return "";

    int totalMs = static_cast<int>(timeSeconds * 1000);
    int minutes = totalMs / 60000;
    int seconds = (totalMs % 60000) / 1000;
    int ms = totalMs % 1000;

    std::snprintf(outBuffer, bufferSize, "%02d:%02d.%03d", minutes, seconds, ms);
    return outBuffer;
}

int SVGPlayer_TimeToFrame(SVGPlayerRef player, double timeSeconds) {
    if (!player || !player->controller) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);

    double duration = player->controller->getDuration();
    int totalFrames = player->controller->getTotalFrames();

    if (duration <= 0 || totalFrames <= 0) return 0;

    double progress = clamp(timeSeconds / duration, 0.0, 1.0);
    return static_cast<int>(progress * (totalFrames - 1));
}

double SVGPlayer_FrameToTime(SVGPlayerRef player, int frame) {
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

bool SVGPlayer_GetViewBox(SVGPlayerRef player, float* x, float* y, float* width, float* height) {
    if (!player || !x || !y || !width || !height) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->currentViewBox.isEmpty()) return false;

    *x = player->currentViewBox.x();
    *y = player->currentViewBox.y();
    *width = player->currentViewBox.width();
    *height = player->currentViewBox.height();
    return true;
}

void SVGPlayer_SetViewBox(SVGPlayerRef player, float x, float y, float width, float height) {
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

void SVGPlayer_ResetViewBox(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->currentViewBox = player->originalViewBox;
    player->currentZoom = 1.0f;
}

float SVGPlayer_GetZoom(SVGPlayerRef player) {
    if (!player) return 1.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->currentZoom;
}

void SVGPlayer_SetZoom(SVGPlayerRef player, float zoom, float centerX, float centerY,
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

void SVGPlayer_ZoomIn(SVGPlayerRef player, float factor, int viewWidth, int viewHeight) {
    if (!player) return;
    if (factor <= 0) factor = 1.5f;  // Default zoom in factor

    float currentZoom = SVGPlayer_GetZoom(player);
    float newZoom = currentZoom * factor;

    // Zoom centered on the view
    SVGPlayer_SetZoom(player, newZoom,
                      static_cast<float>(viewWidth) / 2.0f,
                      static_cast<float>(viewHeight) / 2.0f,
                      viewWidth, viewHeight);
}

void SVGPlayer_ZoomOut(SVGPlayerRef player, float factor, int viewWidth, int viewHeight) {
    if (!player) return;
    if (factor <= 0) factor = 1.5f;  // Default zoom out factor

    float currentZoom = SVGPlayer_GetZoom(player);
    float newZoom = currentZoom / factor;

    // Zoom centered on the view
    SVGPlayer_SetZoom(player, newZoom,
                      static_cast<float>(viewWidth) / 2.0f,
                      static_cast<float>(viewHeight) / 2.0f,
                      viewWidth, viewHeight);
}

void SVGPlayer_ZoomToRect(SVGPlayerRef player, float svgX, float svgY, float svgWidth, float svgHeight) {
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

bool SVGPlayer_ZoomToElement(SVGPlayerRef player, const char* elementID, float padding) {
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

void SVGPlayer_Pan(SVGPlayerRef player, float deltaX, float deltaY, int viewWidth, int viewHeight) {
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

float SVGPlayer_GetMinZoom(SVGPlayerRef player) {
    if (!player) return 0.1f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->minZoom;
}

void SVGPlayer_SetMinZoom(SVGPlayerRef player, float minZoom) {
    if (!player) return;
    if (minZoom <= 0) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->minZoom = minZoom;

    // Clamp current zoom if necessary
    if (player->currentZoom < player->minZoom) {
        player->currentZoom = player->minZoom;
    }
}

float SVGPlayer_GetMaxZoom(SVGPlayerRef player) {
    if (!player) return 10.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->maxZoom;
}

void SVGPlayer_SetMaxZoom(SVGPlayerRef player, float maxZoom) {
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
bool parseLayerSVG(SVGLayer* layer, SVGPlayer* player) {
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

    // Duration is automatically parsed from SVG animation elements during loadFromContent()
    // No manual setDuration needed - the controller extracts duration from animations

    return true;
}

}  // namespace

SVGLayerRef SVGPlayer_CreateLayer(SVGPlayerRef player, const char* filepath) {
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
    return SVGPlayer_CreateLayerFromData(player, buffer.data(), buffer.size());
}

SVGLayerRef SVGPlayer_CreateLayerFromData(SVGPlayerRef player, const void* data, size_t length) {
    if (!player || !data || length == 0) return nullptr;

    std::lock_guard<std::mutex> lock(player->mutex);

    auto layer = std::make_unique<SVGLayer>();
    layer->owner = player;
    layer->svgData.assign(static_cast<const char*>(data), length);

    // Assign z-order based on current layer count
    layer->zOrder = static_cast<int>(player->layers.size());

    if (!parseLayerSVG(layer.get(), player)) {
        setError(player, 101, "Failed to parse layer SVG data");
        return nullptr;
    }

    SVGLayer* result = layer.get();
    player->layers.push_back(std::move(layer));

    return result;
}

void SVGPlayer_DestroyLayer(SVGPlayerRef player, SVGLayerRef layer) {
    if (!player || !layer) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Find and remove the layer
    auto it = std::find_if(player->layers.begin(), player->layers.end(),
                           [layer](const std::unique_ptr<SVGLayer>& l) { return l.get() == layer; });

    if (it != player->layers.end()) {
        player->layers.erase(it);
    }
}

int SVGPlayer_GetLayerCount(SVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Count includes primary SVG (if loaded) plus layers
    int count = player->svgDom ? 1 : 0;
    count += static_cast<int>(player->layers.size());
    return count;
}

SVGLayerRef SVGPlayer_GetLayerAtIndex(SVGPlayerRef player, int index) {
    if (!player || index < 0) return nullptr;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Index 0 could refer to "primary" SVG but we don't expose it as a layer
    // So we return from the layers vector directly
    if (index >= static_cast<int>(player->layers.size())) return nullptr;

    return player->layers[index].get();
}

void SVGLayer_SetPosition(SVGLayerRef layer, float x, float y) {
    if (!layer) return;
    layer->posX = x;
    layer->posY = y;
}

void SVGLayer_GetPosition(SVGLayerRef layer, float* x, float* y) {
    if (!layer) return;
    if (x) *x = layer->posX;
    if (y) *y = layer->posY;
}

void SVGLayer_SetOpacity(SVGLayerRef layer, float opacity) {
    if (!layer) return;
    layer->opacity = clamp(opacity, 0.0f, 1.0f);
}

float SVGLayer_GetOpacity(SVGLayerRef layer) {
    if (!layer) return 1.0f;
    return layer->opacity;
}

void SVGLayer_SetZOrder(SVGLayerRef layer, int zOrder) {
    if (!layer) return;
    layer->zOrder = zOrder;
}

int SVGLayer_GetZOrder(SVGLayerRef layer) {
    if (!layer) return 0;
    return layer->zOrder;
}

void SVGLayer_SetVisible(SVGLayerRef layer, bool visible) {
    if (!layer) return;
    layer->visible = visible;
}

bool SVGLayer_IsVisible(SVGLayerRef layer) {
    if (!layer) return false;
    return layer->visible;
}

void SVGLayer_SetScale(SVGLayerRef layer, float scaleX, float scaleY) {
    if (!layer) return;
    layer->scaleX = scaleX;
    layer->scaleY = scaleY;
}

void SVGLayer_GetScale(SVGLayerRef layer, float* scaleX, float* scaleY) {
    if (!layer) return;
    if (scaleX) *scaleX = layer->scaleX;
    if (scaleY) *scaleY = layer->scaleY;
}

void SVGLayer_SetRotation(SVGLayerRef layer, float angleDegrees) {
    if (!layer) return;
    layer->rotation = angleDegrees;
}

float SVGLayer_GetRotation(SVGLayerRef layer) {
    if (!layer) return 0.0f;
    return layer->rotation;
}

void SVGLayer_SetBlendMode(SVGLayerRef layer, SVGLayerBlendMode blendMode) {
    if (!layer) return;
    layer->blendMode = blendMode;
}

SVGLayerBlendMode SVGLayer_GetBlendMode(SVGLayerRef layer) {
    if (!layer) return SVGLayerBlend_Normal;
    return layer->blendMode;
}

bool SVGLayer_GetSize(SVGLayerRef layer, int* width, int* height) {
    if (!layer) return false;
    if (width) *width = layer->width;
    if (height) *height = layer->height;
    return true;
}

double SVGLayer_GetDuration(SVGLayerRef layer) {
    if (!layer || !layer->controller) return 0.0;
    return layer->controller->getDuration();
}

bool SVGLayer_HasAnimations(SVGLayerRef layer) {
    if (!layer) return false;
    return layer->controller && layer->controller->getDuration() > 0.0;
}

void SVGLayer_Play(SVGLayerRef layer) {
    if (!layer || !layer->controller) return;
    layer->controller->play();
}

void SVGLayer_Pause(SVGLayerRef layer) {
    if (!layer || !layer->controller) return;
    layer->controller->pause();
}

void SVGLayer_Stop(SVGLayerRef layer) {
    if (!layer || !layer->controller) return;
    layer->controller->stop();
}

void SVGLayer_SeekTo(SVGLayerRef layer, double timeSeconds) {
    if (!layer || !layer->controller) return;
    layer->controller->seekTo(timeSeconds);
}

bool SVGLayer_Update(SVGLayerRef layer, double deltaTime) {
    if (!layer || !layer->controller) return false;
    return layer->controller->update(deltaTime);
}

bool SVGPlayer_UpdateAllLayers(SVGPlayerRef player, double deltaTime) {
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

void SVGPlayer_PlayAllLayers(SVGPlayerRef player) {
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

void SVGPlayer_PauseAllLayers(SVGPlayerRef player) {
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

void SVGPlayer_StopAllLayers(SVGPlayerRef player) {
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

bool SVGPlayer_RenderComposite(SVGPlayerRef player, void* pixelBuffer, int width, int height, float scale) {
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
        SVGLayer* layer;
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
            if (layer->opacity < 1.0f || layer->blendMode != SVGLayerBlend_Normal) {
                SkRect bounds = SkRect::MakeWH(layer->width, layer->height);
                canvas.saveLayerAlpha(&bounds, static_cast<U8CPU>(layer->opacity * 255));
            }

            if (layer->svgDom) {
                layer->svgDom->setContainerSize(SkSize::Make(layer->width, layer->height));
                layer->svgDom->render(&canvas);
            }

            if (layer->opacity < 1.0f || layer->blendMode != SVGLayerBlend_Normal) {
                canvas.restore();
            }
        }

        canvas.restore();
    }

    // Copy to output buffer
    std::memcpy(pixelBuffer, bitmap.getPixels(), scaledWidth * scaledHeight * 4);

    return true;
}

bool SVGPlayer_RenderCompositeAtTime(SVGPlayerRef player, void* pixelBuffer, int width, int height, float scale,
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
    return SVGPlayer_RenderComposite(player, pixelBuffer, width, height, scale);
}

// =============================================================================
// Section 21: Frame Rate and Timing Control
// =============================================================================

void SVGPlayer_SetTargetFrameRate(SVGPlayerRef player, float fps) {
    if (!player) return;
    if (fps <= 0.0f) return;  // Invalid FPS

    std::lock_guard<std::mutex> lock(player->mutex);
    player->targetFrameRate = fps;
}

float SVGPlayer_GetTargetFrameRate(SVGPlayerRef player) {
    if (!player) return 60.0f;  // Default fallback

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->targetFrameRate;
}

double SVGPlayer_GetIdealFrameInterval(SVGPlayerRef player) {
    if (!player) return 1.0 / 60.0;  // Default 60 FPS interval

    std::lock_guard<std::mutex> lock(player->mutex);
    if (player->targetFrameRate <= 0.0f) return 1.0 / 60.0;
    return 1.0 / static_cast<double>(player->targetFrameRate);
}

void SVGPlayer_BeginFrame(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    // Record frame begin time using high-resolution clock
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    player->frameBeginTimeSeconds = std::chrono::duration<double>(duration).count();
}

void SVGPlayer_EndFrame(SVGPlayerRef player) {
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
        player->frameHistoryIndex = (player->frameHistoryIndex + 1) % SVGPlayer::FRAME_HISTORY_SIZE;
        if (player->frameHistoryCount < SVGPlayer::FRAME_HISTORY_SIZE) {
            player->frameHistoryCount++;
        }

        // Detect dropped frames (if frame took longer than 2x target interval)
        double targetInterval = 1.0 / static_cast<double>(player->targetFrameRate);
        if (player->lastFrameDurationSeconds > targetInterval * 2.0) {
            player->droppedFrameCount++;
        }
    }
}

double SVGPlayer_GetLastFrameDuration(SVGPlayerRef player) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->lastFrameDurationSeconds;
}

double SVGPlayer_GetAverageFrameDuration(SVGPlayerRef player) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (player->frameHistoryCount == 0) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < player->frameHistoryCount; i++) {
        sum += player->frameDurationHistory[i];
    }
    return sum / static_cast<double>(player->frameHistoryCount);
}

float SVGPlayer_GetMeasuredFPS(SVGPlayerRef player) {
    if (!player) return 0.0f;

    double avgDuration = SVGPlayer_GetAverageFrameDuration(player);
    if (avgDuration <= 0.0) return 0.0f;

    return static_cast<float>(1.0 / avgDuration);
}

bool SVGPlayer_ShouldRenderFrame(SVGPlayerRef player, double currentTimeSeconds) {
    if (!player) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    double targetInterval = 1.0 / static_cast<double>(player->targetFrameRate);
    double timeSinceLastRender = currentTimeSeconds - player->lastRenderTimeSeconds;

    // Render if enough time has passed since last render
    // Use 0.9x threshold to avoid accumulating delay
    return timeSinceLastRender >= (targetInterval * 0.9);
}

void SVGPlayer_MarkFrameRendered(SVGPlayerRef player, double renderTimeSeconds) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    player->lastRenderTimeSeconds = renderTimeSeconds;
}

int SVGPlayer_GetDroppedFrameCount(SVGPlayerRef player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->droppedFrameCount;
}

void SVGPlayer_ResetFrameStats(SVGPlayerRef player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);

    player->droppedFrameCount = 0;
    player->frameHistoryIndex = 0;
    player->frameHistoryCount = 0;
    player->lastFrameDurationSeconds = 0.0;
    player->lastRenderTimeSeconds = 0.0;
    player->frameBeginTimeSeconds = 0.0;

    // Clear history array
    for (int i = 0; i < SVGPlayer::FRAME_HISTORY_SIZE; i++) {
        player->frameDurationHistory[i] = 0.0;
    }
}

double SVGPlayer_GetLastRenderTime(SVGPlayerRef player) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return player->lastRenderTimeSeconds;
}

double SVGPlayer_GetTimeSinceLastRender(SVGPlayerRef player, double currentTimeSeconds) {
    if (!player) return 0.0;

    std::lock_guard<std::mutex> lock(player->mutex);
    return currentTimeSeconds - player->lastRenderTimeSeconds;
}
