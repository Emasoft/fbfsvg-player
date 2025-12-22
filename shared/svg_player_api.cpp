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
    player->lastError = message;

    if (player->errorCallback) {
        player->errorCallback(player->errorUserData, code, message.c_str());
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
        containerSize = SkSize::Make(player->viewBox.width(), player->viewBox.height());
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

    // Initialize animation controller with SVG content
    bool hasAnimations = player->controller->loadFromContent(player->originalSvgData);

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
    }

    // Invoke loop callback if needed
    if (stateChanged && player->loopCallback) {
        player->loopCallback(player->loopUserData, player->completedLoops);
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

    // Calculate scaling to fit SVG in viewport while preserving aspect ratio
    float svgW = static_cast<float>(player->svgWidth);
    float svgH = static_cast<float>(player->svgHeight);
    float viewW = static_cast<float>(width) / scale;
    float viewH = static_cast<float>(height) / scale;

    if (svgW > 0 && svgH > 0) {
        float scaleX = viewW / svgW;
        float scaleY = viewH / svgH;
        float fitScale = std::min(scaleX, scaleY);

        // Center the SVG
        float offsetX = (viewW - svgW * fitScale) / 2.0f;
        float offsetY = (viewH - svgH * fitScale) / 2.0f;

        canvas->translate(offsetX, offsetY);
        canvas->scale(fitScale, fitScale);
    }

    // Render SVG
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

bool SVGPlayer_ViewToSVG(SVGPlayerRef player, float viewX, float viewY, int viewWidth, int viewHeight, float* svgX,
                         float* svgY) {
    if (!player || !svgX || !svgY) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom || player->svgWidth <= 0 || player->svgHeight <= 0) {
        return false;
    }

    // Calculate the scale and offset used during rendering
    float svgW = static_cast<float>(player->svgWidth);
    float svgH = static_cast<float>(player->svgHeight);
    float viewW = static_cast<float>(viewWidth);
    float viewH = static_cast<float>(viewHeight);

    float scaleX = viewW / svgW;
    float scaleY = viewH / svgH;
    float fitScale = std::min(scaleX, scaleY);

    float offsetX = (viewW - svgW * fitScale) / 2.0f;
    float offsetY = (viewH - svgH * fitScale) / 2.0f;

    // Invert the transformation
    *svgX = (viewX - offsetX) / fitScale;
    *svgY = (viewY - offsetY) / fitScale;

    return true;
}

bool SVGPlayer_SVGToView(SVGPlayerRef player, float svgX, float svgY, int viewWidth, int viewHeight, float* viewX,
                         float* viewY) {
    if (!player || !viewX || !viewY) return false;

    std::lock_guard<std::mutex> lock(player->mutex);

    if (!player->svgDom || player->svgWidth <= 0 || player->svgHeight <= 0) {
        return false;
    }

    // Calculate the scale and offset used during rendering
    float svgW = static_cast<float>(player->svgWidth);
    float svgH = static_cast<float>(player->svgHeight);
    float viewW = static_cast<float>(viewWidth);
    float viewH = static_cast<float>(viewHeight);

    float scaleX = viewW / svgW;
    float scaleY = viewH / svgH;
    float fitScale = std::min(scaleX, scaleY);

    float offsetX = (viewW - svgW * fitScale) / 2.0f;
    float offsetY = (viewH - svgH * fitScale) / 2.0f;

    // Apply the transformation
    *viewX = svgX * fitScale + offsetX;
    *viewY = svgY * fitScale + offsetY;

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

    // Convert view coordinates to SVG coordinates
    float svgX, svgY;
    if (!SVGPlayer_ViewToSVG(player, viewX, viewY, viewWidth, viewHeight, &svgX, &svgY)) {
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

    // Convert view coordinates to SVG coordinates
    float svgX, svgY;
    if (!SVGPlayer_ViewToSVG(player, viewX, viewY, viewWidth, viewHeight, &svgX, &svgY)) {
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
    if (player->controller && callback) {
        player->controller->setStateChangeCallback([player](svgplayer::PlaybackState state) {
            if (player->stateChangeCallback) {
                player->stateChangeCallback(player->stateChangeUserData, fromControllerState(state));
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
    if (player->controller && callback) {
        player->controller->setLoopCallback([player](int loopCount) {
            player->completedLoops = loopCount;
            if (player->loopCallback) {
                player->loopCallback(player->loopUserData, loopCount);
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
    if (player->controller && callback) {
        player->controller->setEndCallback([player]() {
            if (player->endCallback) {
                player->endCallback(player->endUserData);
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

    if (totalFrames <= 0) return 0.0;

    frame = clamp(frame, 0, totalFrames - 1);
    return (static_cast<double>(frame) / (totalFrames - 1)) * duration;
}
