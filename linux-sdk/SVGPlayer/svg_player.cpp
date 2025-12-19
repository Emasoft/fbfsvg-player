// svg_player.cpp - Cross-platform SVG Player implementation for Linux
//
// This implementation uses Skia for SVG rendering with FontConfig/FreeType
// for font support on Linux systems.

#define SVG_PLAYER_BUILDING_DLL
#include "svg_player.h"

// Standard library includes
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>

// Shared animation controller for cross-platform SMIL parsing and playback
#include "shared/SVGAnimationController.h"

// Use shared types from the animation controller
using svgplayer::SMILAnimation;
using svgplayer::AnimationState;

// Skia core includes
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"

// Skia SVG module includes
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGNode.h"

// Skia text shaping includes
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skshaper/utils/FactoryHelpers.h"

// Linux-specific font support: FontConfig + FreeType
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"

// Version string
static const char* VERSION_STRING = "1.0.0";

// Use steady_clock for animation timing (monotonic, immune to system clock changes)
using SteadyClock = std::chrono::steady_clock;
using DurationMs = std::chrono::duration<double, std::milli>;
using DurationSec = std::chrono::duration<double>;

// Global font manager and text shaping factory for SVG text rendering
// These must be set up before any SVG DOM is created to ensure text elements render properly
static sk_sp<SkFontMgr> g_fontMgr;
static sk_sp<SkShapers::Factory> g_shaperFactory;
static bool g_fontSupportInitialized = false;

// Initialize font support for SVG text rendering (called automatically)
static void ensureFontSupportInitialized() {
    if (g_fontSupportInitialized) return;
    // FontConfig font manager with FreeType scanner for Linux
    g_fontMgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    // Use the best available text shaper
    g_shaperFactory = SkShapers::BestAvailable();
    g_fontSupportInitialized = true;
}

// Create SVG DOM with proper font support for text rendering
// This must be used instead of SkSVGDOM::MakeFromStream to enable SVG <text> elements
static sk_sp<SkSVGDOM> makeSVGDOMWithFontSupport(SkStream& stream) {
    ensureFontSupportInitialized();
    return SkSVGDOM::Builder()
        .setFontManager(g_fontMgr)
        .setTextShapingFactory(g_shaperFactory)
        .make(stream);
}

// SMILAnimation struct is now defined in shared/SVGAnimationController.h

// Internal player structure with Skia SVG DOM
struct SVGPlayer {
    // Skia SVG DOM and resources
    sk_sp<SkSVGDOM> svgDom;
    std::string svgContent;
    std::vector<SMILAnimation> animations;

    // Shared animation controller for parsing
    svgplayer::SVGAnimationController animController;

    // Loading state
    bool loaded;
    std::string filePath;

    // Size info
    int width;
    int height;
    float viewBoxX;
    float viewBoxY;
    float viewBoxWidth;
    float viewBoxHeight;

    // Playback state
    SVGPlaybackState playbackState;
    SVGRepeatMode repeatMode;
    int repeatCount;
    int completedLoops;
    float playbackRate;
    bool playingForward;

    // Timeline state
    double duration;
    double currentTime;
    int totalFrames;
    float frameRate;

    // Statistics
    SVGRenderStats stats;
    std::chrono::time_point<SteadyClock> lastFrameTime;
    int frameCount;
    double fpsAccumulator;

    // Error handling
    std::string lastError;

    // Element subscriptions
    std::unordered_set<std::string> subscribedElements;
    std::string lastHitElement;

    // Thread safety
    std::mutex renderMutex;

    SVGPlayer() {
        loaded = false;
        width = 0;
        height = 0;
        viewBoxX = 0;
        viewBoxY = 0;
        viewBoxWidth = 0;
        viewBoxHeight = 0;
        playbackState = SVGPlaybackState_Stopped;
        repeatMode = SVGRepeatMode_Loop;
        repeatCount = 1;
        completedLoops = 0;
        playbackRate = 1.0f;
        playingForward = true;
        duration = 0;
        currentTime = 0;
        totalFrames = 0;
        frameRate = 60.0f;
        frameCount = 0;
        fpsAccumulator = 0.0;
        memset(&stats, 0, sizeof(stats));
        lastFrameTime = SteadyClock::now();
    }
};

// Forward declarations for internal functions
static bool parseSMILAnimations(SVGPlayer* player, const std::string& svgContent);
static bool updateSVGForAnimation(SVGPlayer* player, double time);

// ============================================================================
// SMIL Animation Parsing
// ============================================================================

// Parse SMIL animations from SVG content using shared animation controller
static bool parseSMILAnimations(SVGPlayer* player, const std::string& svgContent) {
    player->animations.clear();
    player->duration = 0.0;

    // Use the shared animation controller to parse
    if (!player->animController.loadFromContent(svgContent)) {
        return false;
    }

    // Get the preprocessed content (with <symbol> to <g> conversion and synthetic IDs)
    player->svgContent = player->animController.getProcessedContent();

    // Copy animations from controller
    player->animations = player->animController.getAnimations();

    // Set duration from controller
    player->duration = player->animController.getDuration();
    player->totalFrames = player->animController.getTotalFrames();
    player->frameRate = player->animController.getFrameRate();

    return !player->animations.empty();
}

// Update SVG DOM for current animation time
static bool updateSVGForAnimation(SVGPlayer* player, double time) {
    if (!player->svgDom || player->animations.empty()) {
        return false;
    }

    // For each animation, update the SVG content
    std::string currentContent = player->svgContent;

    for (const auto& anim : player->animations) {
        if (anim.attributeName == "xlink:href" && !anim.targetId.empty()) {
            std::string currentValue = anim.getCurrentValue(time);
            if (!currentValue.empty()) {
                // Find the use element with matching id and update its xlink:href
                std::string searchPattern = "id=\"" + anim.targetId + "\"";
                size_t usePos = currentContent.find(searchPattern);
                if (usePos != std::string::npos) {
                    // Find the xlink:href attribute in this element
                    size_t elemStart = currentContent.rfind("<", usePos);
                    size_t elemEnd = currentContent.find(">", usePos);
                    if (elemStart != std::string::npos && elemEnd != std::string::npos) {
                        std::string elemTag = currentContent.substr(elemStart, elemEnd - elemStart + 1);

                        // Update xlink:href
                        size_t hrefPos = elemTag.find("xlink:href=\"");
                        if (hrefPos != std::string::npos) {
                            size_t hrefStart = hrefPos + 12;
                            size_t hrefEnd = elemTag.find("\"", hrefStart);
                            if (hrefEnd != std::string::npos) {
                                std::string newElemTag = elemTag.substr(0, hrefStart) + currentValue + elemTag.substr(hrefEnd);
                                currentContent = currentContent.substr(0, elemStart) + newElemTag + currentContent.substr(elemEnd + 1);
                            }
                        }
                    }
                }
            }
        }
    }

    // Re-parse SVG if content changed (with font support for text rendering)
    if (currentContent != player->svgContent) {
        auto stream = SkMemoryStream::MakeDirect(currentContent.c_str(), currentContent.size());
        player->svgDom = makeSVGDOMWithFontSupport(*stream);
        return player->svgDom != nullptr;
    }

    return true;
}

// ============================================================================
// Lifecycle Functions
// ============================================================================

SVG_PLAYER_API SVGPlayerHandle SVGPlayer_Create(void) {
    try {
        // Ensure font support is initialized on first player creation
        ensureFontSupportInitialized();
        return new SVGPlayer();
    } catch (...) {
        return nullptr;
    }
}

SVG_PLAYER_API void SVGPlayer_Destroy(SVGPlayerHandle player) {
    if (player) {
        delete player;
    }
}

SVG_PLAYER_API const char* SVGPlayer_GetVersion(void) {
    return VERSION_STRING;
}

// ============================================================================
// Loading Functions
// ============================================================================

SVG_PLAYER_API bool SVGPlayer_LoadSVG(SVGPlayerHandle player, const char* filepath) {
    if (!player || !filepath) {
        if (player) player->lastError = "Invalid arguments";
        return false;
    }

    std::lock_guard<std::mutex> lock(player->renderMutex);

    // Read file into memory
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        player->lastError = "Failed to open file: ";
        player->lastError += filepath;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string buffer(size, '\0');
    if (!file.read(&buffer[0], size)) {
        player->lastError = "Failed to read file: ";
        player->lastError += filepath;
        return false;
    }

    player->svgContent = std::move(buffer);
    player->filePath = filepath;

    // Parse SVG using Skia's SkSVGDOM with font support
    auto stream = SkMemoryStream::MakeDirect(player->svgContent.c_str(), player->svgContent.size());
    player->svgDom = makeSVGDOMWithFontSupport(*stream);

    if (!player->svgDom) {
        player->lastError = "Failed to parse SVG";
        return false;
    }

    // Get SVG dimensions from root element
    const SkSVGSVG* root = player->svgDom->getRoot();
    if (root) {
        SkSize containerSize = SkSize::Make(1920, 1080); // Default container
        player->svgDom->setContainerSize(containerSize);

        // Try to get intrinsic size
        auto intrinsic = root->intrinsicSize(SkSVGLengthContext(containerSize));
        player->width = static_cast<int>(intrinsic.width());
        player->height = static_cast<int>(intrinsic.height());

        if (player->width <= 0 || player->height <= 0) {
            player->width = 1920;
            player->height = 1080;
        }

        // Set viewBox values
        player->viewBoxX = 0;
        player->viewBoxY = 0;
        player->viewBoxWidth = static_cast<float>(player->width);
        player->viewBoxHeight = static_cast<float>(player->height);
    }

    // Parse SMIL animations
    parseSMILAnimations(player, player->svgContent);

    // Set default duration if no animations found
    if (player->duration <= 0) {
        player->duration = 1.0;
    }

    player->totalFrames = static_cast<int>(player->duration * player->frameRate);
    player->currentTime = 0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->completedLoops = 0;
    player->loaded = true;
    player->lastError.clear();

    return true;
}

SVG_PLAYER_API bool SVGPlayer_LoadSVGData(SVGPlayerHandle player, const void* data, size_t length) {
    if (!player || !data || length == 0) {
        if (player) player->lastError = "Invalid arguments";
        return false;
    }

    std::lock_guard<std::mutex> lock(player->renderMutex);

    // Store SVG content
    player->svgContent.assign(static_cast<const char*>(data), length);
    player->filePath.clear();

    // Parse SVG using Skia's SkSVGDOM with font support
    auto stream = SkMemoryStream::MakeDirect(player->svgContent.c_str(), player->svgContent.size());
    player->svgDom = makeSVGDOMWithFontSupport(*stream);

    if (!player->svgDom) {
        player->lastError = "Failed to parse SVG";
        return false;
    }

    // Get SVG dimensions from root element
    const SkSVGSVG* root = player->svgDom->getRoot();
    if (root) {
        SkSize containerSize = SkSize::Make(1920, 1080); // Default container
        player->svgDom->setContainerSize(containerSize);

        // Try to get intrinsic size
        auto intrinsic = root->intrinsicSize(SkSVGLengthContext(containerSize));
        player->width = static_cast<int>(intrinsic.width());
        player->height = static_cast<int>(intrinsic.height());

        if (player->width <= 0 || player->height <= 0) {
            player->width = 1920;
            player->height = 1080;
        }

        // Set viewBox values
        player->viewBoxX = 0;
        player->viewBoxY = 0;
        player->viewBoxWidth = static_cast<float>(player->width);
        player->viewBoxHeight = static_cast<float>(player->height);
    }

    // Parse SMIL animations
    parseSMILAnimations(player, player->svgContent);

    // Set default duration if no animations found
    if (player->duration <= 0) {
        player->duration = 1.0;
    }

    player->totalFrames = static_cast<int>(player->duration * player->frameRate);
    player->currentTime = 0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->completedLoops = 0;
    player->loaded = true;
    player->lastError.clear();

    return true;
}

SVG_PLAYER_API void SVGPlayer_Unload(SVGPlayerHandle player) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->renderMutex);

    // Release Skia SVG DOM
    player->svgDom.reset();
    player->svgContent.clear();
    player->animations.clear();
    player->filePath.clear();
    player->loaded = false;
    player->width = 0;
    player->height = 0;
    player->viewBoxX = 0;
    player->viewBoxY = 0;
    player->viewBoxWidth = 0;
    player->viewBoxHeight = 0;
    player->duration = 0;
    player->currentTime = 0;
    player->totalFrames = 0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->completedLoops = 0;
    player->playingForward = true;
    player->subscribedElements.clear();
    player->lastHitElement.clear();
    player->lastError.clear();
}

SVG_PLAYER_API bool SVGPlayer_IsLoaded(SVGPlayerHandle player) {
    return player && player->loaded;
}

// ============================================================================
// Size and Dimension Functions
// ============================================================================

SVG_PLAYER_API bool SVGPlayer_GetSize(SVGPlayerHandle player, int* width, int* height) {
    if (!player || !player->loaded) return false;

    if (width) *width = player->width;
    if (height) *height = player->height;
    return true;
}

SVG_PLAYER_API bool SVGPlayer_GetSizeInfo(SVGPlayerHandle player, SVGSizeInfo* info) {
    if (!player || !player->loaded || !info) return false;

    info->width = player->width;
    info->height = player->height;
    info->viewBoxX = player->viewBoxX;
    info->viewBoxY = player->viewBoxY;
    info->viewBoxWidth = player->viewBoxWidth;
    info->viewBoxHeight = player->viewBoxHeight;
    return true;
}

// ============================================================================
// Playback Control Functions
// ============================================================================

SVG_PLAYER_API void SVGPlayer_Play(SVGPlayerHandle player) {
    if (!player || !player->loaded) return;
    player->playbackState = SVGPlaybackState_Playing;
}

SVG_PLAYER_API void SVGPlayer_Pause(SVGPlayerHandle player) {
    if (!player) return;
    player->playbackState = SVGPlaybackState_Paused;
}

SVG_PLAYER_API void SVGPlayer_Stop(SVGPlayerHandle player) {
    if (!player) return;
    player->playbackState = SVGPlaybackState_Stopped;
    player->currentTime = 0;
    player->completedLoops = 0;
    player->playingForward = true;
}

SVG_PLAYER_API void SVGPlayer_TogglePlayback(SVGPlayerHandle player) {
    if (!player) return;

    if (player->playbackState == SVGPlaybackState_Playing) {
        player->playbackState = SVGPlaybackState_Paused;
    } else {
        player->playbackState = SVGPlaybackState_Playing;
    }
}

SVG_PLAYER_API void SVGPlayer_SetPlaybackState(SVGPlayerHandle player, SVGPlaybackState state) {
    if (!player) return;
    player->playbackState = state;
    if (state == SVGPlaybackState_Stopped) {
        player->currentTime = 0;
        player->completedLoops = 0;
    }
}

SVG_PLAYER_API SVGPlaybackState SVGPlayer_GetPlaybackState(SVGPlayerHandle player) {
    if (!player) return SVGPlaybackState_Stopped;
    return player->playbackState;
}

// ============================================================================
// Repeat Mode Functions
// ============================================================================

SVG_PLAYER_API void SVGPlayer_SetRepeatMode(SVGPlayerHandle player, SVGRepeatMode mode) {
    if (!player) return;
    player->repeatMode = mode;
}

SVG_PLAYER_API SVGRepeatMode SVGPlayer_GetRepeatMode(SVGPlayerHandle player) {
    if (!player) return SVGRepeatMode_None;
    return player->repeatMode;
}

SVG_PLAYER_API void SVGPlayer_SetRepeatCount(SVGPlayerHandle player, int count) {
    if (!player) return;
    player->repeatCount = std::max(1, count);
}

SVG_PLAYER_API int SVGPlayer_GetRepeatCount(SVGPlayerHandle player) {
    if (!player) return 1;
    return player->repeatCount;
}

SVG_PLAYER_API int SVGPlayer_GetCompletedLoops(SVGPlayerHandle player) {
    if (!player) return 0;
    return player->completedLoops;
}

SVG_PLAYER_API bool SVGPlayer_IsLooping(SVGPlayerHandle player) {
    if (!player) return false;
    return player->repeatMode == SVGRepeatMode_Loop;
}

SVG_PLAYER_API void SVGPlayer_SetLooping(SVGPlayerHandle player, bool looping) {
    if (!player) return;
    player->repeatMode = looping ? SVGRepeatMode_Loop : SVGRepeatMode_None;
}

// ============================================================================
// Playback Rate Functions
// ============================================================================

SVG_PLAYER_API void SVGPlayer_SetPlaybackRate(SVGPlayerHandle player, float rate) {
    if (!player) return;
    player->playbackRate = std::max(0.1f, std::min(10.0f, rate));
}

SVG_PLAYER_API float SVGPlayer_GetPlaybackRate(SVGPlayerHandle player) {
    if (!player) return 1.0f;
    return player->playbackRate;
}

// ============================================================================
// Timeline Functions
// ============================================================================

SVG_PLAYER_API void SVGPlayer_Update(SVGPlayerHandle player, double deltaTime) {
    if (!player || !player->loaded) return;
    if (player->playbackState != SVGPlaybackState_Playing) return;
    if (player->duration <= 0) return;

    auto updateStart = std::chrono::high_resolution_clock::now();

    double adjustedDelta = deltaTime * player->playbackRate;

    if (player->repeatMode == SVGRepeatMode_Reverse) {
        // Ping-pong mode
        if (player->playingForward) {
            player->currentTime += adjustedDelta;
            if (player->currentTime >= player->duration) {
                player->currentTime = player->duration;
                player->playingForward = false;
            }
        } else {
            player->currentTime -= adjustedDelta;
            if (player->currentTime <= 0) {
                player->currentTime = 0;
                player->playingForward = true;
                player->completedLoops++;
            }
        }
    } else {
        player->currentTime += adjustedDelta;

        if (player->currentTime >= player->duration) {
            player->completedLoops++;

            switch (player->repeatMode) {
                case SVGRepeatMode_None:
                    player->currentTime = player->duration;
                    player->playbackState = SVGPlaybackState_Stopped;
                    break;

                case SVGRepeatMode_Loop:
                    player->currentTime = fmod(player->currentTime, player->duration);
                    break;

                case SVGRepeatMode_Count:
                    if (player->completedLoops >= player->repeatCount) {
                        player->currentTime = player->duration;
                        player->playbackState = SVGPlaybackState_Stopped;
                    } else {
                        player->currentTime = fmod(player->currentTime, player->duration);
                    }
                    break;

                default:
                    break;
            }
        }
    }

    // Update statistics
    auto updateEnd = std::chrono::high_resolution_clock::now();
    player->stats.updateTimeMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
    player->stats.animationTimeMs = player->currentTime * 1000.0;
    player->stats.currentFrame = (int)(player->currentTime * player->frameRate);
    player->stats.totalFrames = player->totalFrames;
}

SVG_PLAYER_API void SVGPlayer_SeekTo(SVGPlayerHandle player, double timeSeconds) {
    if (!player || !player->loaded) return;
    player->currentTime = std::max(0.0, std::min(player->duration, timeSeconds));
}

SVG_PLAYER_API void SVGPlayer_SeekToFrame(SVGPlayerHandle player, int frame) {
    if (!player || !player->loaded || player->totalFrames <= 0) return;
    frame = std::max(0, std::min(player->totalFrames - 1, frame));
    player->currentTime = (double)frame / player->frameRate;
}

SVG_PLAYER_API void SVGPlayer_SeekToProgress(SVGPlayerHandle player, float progress) {
    if (!player || !player->loaded) return;
    progress = std::max(0.0f, std::min(1.0f, progress));
    player->currentTime = progress * player->duration;
}

SVG_PLAYER_API double SVGPlayer_GetDuration(SVGPlayerHandle player) {
    if (!player) return 0;
    return player->duration;
}

SVG_PLAYER_API double SVGPlayer_GetCurrentTime(SVGPlayerHandle player) {
    if (!player) return 0;
    return player->currentTime;
}

SVG_PLAYER_API float SVGPlayer_GetProgress(SVGPlayerHandle player) {
    if (!player || player->duration <= 0) return 0;
    return (float)(player->currentTime / player->duration);
}

SVG_PLAYER_API int SVGPlayer_GetCurrentFrame(SVGPlayerHandle player) {
    if (!player) return 0;
    return (int)(player->currentTime * player->frameRate);
}

SVG_PLAYER_API int SVGPlayer_GetTotalFrames(SVGPlayerHandle player) {
    if (!player) return 0;
    return player->totalFrames;
}

SVG_PLAYER_API float SVGPlayer_GetFrameRate(SVGPlayerHandle player) {
    if (!player) return 60.0f;
    return player->frameRate;
}

// ============================================================================
// Frame Stepping Functions
// ============================================================================

SVG_PLAYER_API void SVGPlayer_StepForward(SVGPlayerHandle player) {
    SVGPlayer_StepByFrames(player, 1);
}

SVG_PLAYER_API void SVGPlayer_StepBackward(SVGPlayerHandle player) {
    SVGPlayer_StepByFrames(player, -1);
}

SVG_PLAYER_API void SVGPlayer_StepByFrames(SVGPlayerHandle player, int frames) {
    if (!player || !player->loaded) return;

    player->playbackState = SVGPlaybackState_Paused;

    int currentFrame = SVGPlayer_GetCurrentFrame(player);
    int newFrame = currentFrame + frames;
    newFrame = std::max(0, std::min(player->totalFrames - 1, newFrame));
    player->currentTime = (double)newFrame / player->frameRate;
}

// ============================================================================
// Rendering Functions
// ============================================================================

SVG_PLAYER_API bool SVGPlayer_Render(SVGPlayerHandle player,
                                      void* pixelBuffer,
                                      int width,
                                      int height,
                                      float scale) {
    if (!player || !player->loaded || !pixelBuffer) {
        if (player) player->lastError = "Invalid render arguments";
        return false;
    }

    if (!player->svgDom) {
        player->lastError = "SVG DOM not loaded";
        return false;
    }

    std::lock_guard<std::mutex> lock(player->renderMutex);

    auto renderStart = std::chrono::high_resolution_clock::now();

    // Update SVG DOM for current animation frame if animations exist
    if (!player->animations.empty()) {
        updateSVGForAnimation(player, player->currentTime);
    }

    // Create SkImageInfo for the pixel buffer (RGBA_8888 format)
    SkImageInfo imageInfo = SkImageInfo::Make(
        width, height,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType
    );

    // Calculate row bytes (stride) for the pixel buffer
    size_t rowBytes = static_cast<size_t>(width) * 4;

    // Create a raster surface that wraps the pixel buffer directly
    // This allows rendering directly to the caller's buffer without copying
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
        imageInfo,
        pixelBuffer,
        rowBytes
    );

    if (!surface) {
        player->lastError = "Failed to create render surface";
        return false;
    }

    SkCanvas* canvas = surface->getCanvas();
    if (!canvas) {
        player->lastError = "Failed to get canvas from surface";
        return false;
    }

    // Clear canvas with transparent white background
    canvas->clear(SK_ColorWHITE);

    // Apply scale transform if needed
    if (scale != 1.0f) {
        canvas->scale(scale, scale);
    }

    // Set the container size for proper SVG scaling
    SkSize containerSize = SkSize::Make(
        static_cast<SkScalar>(width) / scale,
        static_cast<SkScalar>(height) / scale
    );
    player->svgDom->setContainerSize(containerSize);

    // Render the SVG DOM to the canvas
    player->svgDom->render(canvas);

    // Note: For raster surfaces, rendering is synchronous - no flush needed
    // The pixel buffer is immediately updated after render() returns

    auto renderEnd = std::chrono::high_resolution_clock::now();
    player->stats.renderTimeMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();

    // Count rendered elements (estimate from SVG DOM)
    const SkSVGSVG* root = player->svgDom->getRoot();
    player->stats.elementsRendered = root ? 1 : 0;

    // Calculate FPS from frame timing
    auto now = SteadyClock::now();
    double deltaMs = std::chrono::duration<double, std::milli>(now - player->lastFrameTime).count();
    if (deltaMs > 0) {
        player->stats.fps = 1000.0 / deltaMs;
    }
    player->lastFrameTime = now;
    player->frameCount++;

    return true;
}

SVG_PLAYER_API bool SVGPlayer_RenderAtTime(SVGPlayerHandle player,
                                            void* pixelBuffer,
                                            int width,
                                            int height,
                                            float scale,
                                            double timeSeconds) {
    if (!player) return false;

    double savedTime = player->currentTime;
    SVGPlayer_SeekTo(player, timeSeconds);
    bool result = SVGPlayer_Render(player, pixelBuffer, width, height, scale);
    player->currentTime = savedTime;
    return result;
}

// ============================================================================
// Statistics and Diagnostics
// ============================================================================

SVG_PLAYER_API SVGRenderStats SVGPlayer_GetStats(SVGPlayerHandle player) {
    if (!player) {
        SVGRenderStats empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return player->stats;
}

SVG_PLAYER_API const char* SVGPlayer_GetLastError(SVGPlayerHandle player) {
    if (!player) return "";
    return player->lastError.c_str();
}

SVG_PLAYER_API void SVGPlayer_ClearError(SVGPlayerHandle player) {
    if (player) player->lastError.clear();
}

// ============================================================================
// Coordinate Conversion Functions
// ============================================================================

SVG_PLAYER_API bool SVGPlayer_ViewToSVG(SVGPlayerHandle player,
                                         float viewX, float viewY,
                                         int viewWidth, int viewHeight,
                                         float* svgX, float* svgY) {
    if (!player || !player->loaded || viewWidth <= 0 || viewHeight <= 0) return false;

    // TODO: Account for actual aspect ratio fitting and viewport
    float scaleX = player->viewBoxWidth / viewWidth;
    float scaleY = player->viewBoxHeight / viewHeight;

    if (svgX) *svgX = player->viewBoxX + viewX * scaleX;
    if (svgY) *svgY = player->viewBoxY + viewY * scaleY;

    return true;
}

SVG_PLAYER_API bool SVGPlayer_SVGToView(SVGPlayerHandle player,
                                         float svgX, float svgY,
                                         int viewWidth, int viewHeight,
                                         float* outViewX, float* outViewY) {
    if (!player || !player->loaded || viewWidth <= 0 || viewHeight <= 0) return false;

    // TODO: Account for actual aspect ratio fitting and viewport
    float scaleX = viewWidth / player->viewBoxWidth;
    float scaleY = viewHeight / player->viewBoxHeight;

    if (outViewX) *outViewX = (svgX - player->viewBoxX) * scaleX;
    if (outViewY) *outViewY = (svgY - player->viewBoxY) * scaleY;

    return true;
}

// ============================================================================
// Element Touch/Hit Testing Functions
// ============================================================================

SVG_PLAYER_API void SVGPlayer_SubscribeToElement(SVGPlayerHandle player, const char* objectID) {
    if (!player || !objectID) return;
    player->subscribedElements.insert(objectID);

    // TODO: Register with Skia SVG DOM for hit testing
}

SVG_PLAYER_API void SVGPlayer_UnsubscribeFromElement(SVGPlayerHandle player, const char* objectID) {
    if (!player || !objectID) return;
    player->subscribedElements.erase(objectID);
}

SVG_PLAYER_API void SVGPlayer_UnsubscribeFromAllElements(SVGPlayerHandle player) {
    if (!player) return;
    player->subscribedElements.clear();
}

SVG_PLAYER_API const char* SVGPlayer_HitTest(SVGPlayerHandle player,
                                              float viewX, float viewY,
                                              int viewWidth, int viewHeight) {
    if (!player || !player->loaded) return nullptr;

    // TODO: Implement actual hit testing using Skia SVG DOM
    // For now, return nullptr (no hit)
    player->lastHitElement.clear();
    return nullptr;
}

SVG_PLAYER_API bool SVGPlayer_GetElementBounds(SVGPlayerHandle player,
                                                const char* objectID,
                                                float* x, float* y,
                                                float* width, float* height) {
    if (!player || !player->loaded || !objectID) return false;

    // TODO: Implement using Skia SkSVGNode::objectBoundingBox()
    // For now, return failure
    return false;
}
