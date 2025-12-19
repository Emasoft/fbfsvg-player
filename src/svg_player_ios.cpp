// svg_player_ios.cpp - iOS SVG Player implementation
// This file provides the implementation for the iOS SVG rendering API.
// It uses Skia for rendering and CoreText for fonts.
//
// Note: This is a library meant to be linked into iOS apps.
// It does NOT include SDL2 - iOS uses UIKit for windowing.

#include "svg_player_ios.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkColorSpace.h"
#include "include/ports/SkFontMgr_mac_ct.h"  // iOS uses CoreText like macOS
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skshaper/utils/FactoryHelpers.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <vector>
#include <mutex>

// Shared animation controller for cross-platform SMIL parsing and playback
#include "shared/SVGAnimationController.h"

// Use shared types from the animation controller
using svgplayer::SMILAnimation;
using svgplayer::AnimationState;

// iOS/macOS APIs for CPU monitoring (shared with macOS)
#include <mach/mach.h>
#include <mach/thread_info.h>
#include <mach/task.h>
#include <mach/mach_time.h>

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
    // CoreText font manager for iOS (same as macOS)
    g_fontMgr = SkFontMgr_New_CoreText(nullptr);
    // Use the best available text shaper (CoreText on iOS)
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

// Internal SVG Player implementation
struct SVGPlayer {
    // SVG DOM and resources
    sk_sp<SkSVGDOM> svgDom;
    sk_sp<SkFontMgr> fontMgr;
    std::string svgContent;
    std::vector<SMILAnimation> animations;

    // Shared animation controller for parsing
    svgplayer::SVGAnimationController animController;

    // Animation state
    double animationTime = 0.0;
    double animationDuration = 0.0;
    bool looping = true;
    SVGPlaybackState playbackState = SVGPlaybackState_Stopped;

    // Rendering state
    int svgWidth = 0;
    int svgHeight = 0;

    // Statistics
    SVGRenderStats stats = {};
    std::chrono::time_point<SteadyClock> lastFrameTime;
    int frameCount = 0;
    double fpsAccumulator = 0.0;

    // Error handling
    std::string lastError;

    // Thread safety
    std::mutex renderMutex;

    SVGPlayer() {
        fontMgr = SkFontMgr_New_CoreText(nullptr);
        lastFrameTime = SteadyClock::now();
    }
};

// Forward declarations for internal functions
static bool parseSMILAnimations(SVGPlayer* player, const std::string& svgContent);
static bool updateSVGForAnimation(SVGPlayer* player, double time);

// Parse SMIL animations from SVG content using shared animation controller
static bool parseSMILAnimations(SVGPlayer* player, const std::string& svgContent) {
    player->animations.clear();
    player->animationDuration = 0.0;

    // Use the shared animation controller to parse
    if (!player->animController.loadFromContent(svgContent)) {
        return false;
    }

    // Get the preprocessed content (with <symbol> to <g> conversion and synthetic IDs)
    player->svgContent = player->animController.getProcessedContent();

    // Copy animations from controller
    player->animations = player->animController.getAnimations();

    // Set duration from controller
    player->animationDuration = player->animController.getDuration();

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

// Public API Implementation

SVGPlayerHandle SVGPlayer_Create(void) {
    try {
        return new SVGPlayer();
    } catch (...) {
        return nullptr;
    }
}

void SVGPlayer_Destroy(SVGPlayerHandle player) {
    if (player) {
        delete player;
    }
}

bool SVGPlayer_LoadSVG(SVGPlayerHandle player, const char* filepath) {
    if (!player || !filepath) {
        if (player) player->lastError = "Invalid parameters";
        return false;
    }

    std::lock_guard<std::mutex> lock(player->renderMutex);

    // Read file content
    std::ifstream file(filepath);
    if (!file.is_open()) {
        player->lastError = "Failed to open file: ";
        player->lastError += filepath;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    player->svgContent = buffer.str();

    return SVGPlayer_LoadSVGData(player, player->svgContent.c_str(), player->svgContent.size());
}

bool SVGPlayer_LoadSVGData(SVGPlayerHandle player, const void* data, size_t length) {
    if (!player || !data || length == 0) {
        if (player) player->lastError = "Invalid parameters";
        return false;
    }

    std::lock_guard<std::mutex> lock(player->renderMutex);

    // Store SVG content
    player->svgContent = std::string(static_cast<const char*>(data), length);

    // Parse SVG with font support for proper text rendering in <text> elements
    auto stream = SkMemoryStream::MakeDirect(player->svgContent.c_str(), player->svgContent.size());
    player->svgDom = makeSVGDOMWithFontSupport(*stream);

    if (!player->svgDom) {
        player->lastError = "Failed to parse SVG";
        return false;
    }

    // Get SVG dimensions
    const SkSVGSVG* root = player->svgDom->getRoot();
    if (root) {
        SkSize containerSize = SkSize::Make(1920, 1080); // Default container
        player->svgDom->setContainerSize(containerSize);

        // Try to get intrinsic size
        auto intrinsic = root->intrinsicSize(SkSVGLengthContext(containerSize));
        player->svgWidth = static_cast<int>(intrinsic.width());
        player->svgHeight = static_cast<int>(intrinsic.height());

        if (player->svgWidth <= 0 || player->svgHeight <= 0) {
            player->svgWidth = 1920;
            player->svgHeight = 1080;
        }
    }

    // Parse SMIL animations
    parseSMILAnimations(player, player->svgContent);

    // Reset animation state
    player->animationTime = 0.0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->stats = {};

    return true;
}

bool SVGPlayer_GetSize(SVGPlayerHandle player, int* width, int* height) {
    if (!player || !player->svgDom) {
        return false;
    }

    if (width) *width = player->svgWidth;
    if (height) *height = player->svgHeight;
    return true;
}

void SVGPlayer_SetPlaybackState(SVGPlayerHandle player, SVGPlaybackState state) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->renderMutex);
    player->playbackState = state;

    if (state == SVGPlaybackState_Playing) {
        player->lastFrameTime = SteadyClock::now();
    }
}

SVGPlaybackState SVGPlayer_GetPlaybackState(SVGPlayerHandle player) {
    if (!player) return SVGPlaybackState_Stopped;
    return player->playbackState;
}

void SVGPlayer_Update(SVGPlayerHandle player, double deltaTime) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->renderMutex);

    if (player->playbackState != SVGPlaybackState_Playing) {
        return;
    }

    player->animationTime += deltaTime;

    // Handle looping
    if (player->looping && player->animationDuration > 0) {
        player->animationTime = fmod(player->animationTime, player->animationDuration);
    } else if (!player->looping && player->animationTime >= player->animationDuration) {
        player->animationTime = player->animationDuration;
        player->playbackState = SVGPlaybackState_Stopped;
    }

    // Update animation
    updateSVGForAnimation(player, player->animationTime);
}

void SVGPlayer_SeekTo(SVGPlayerHandle player, double timeSeconds) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->renderMutex);

    player->animationTime = timeSeconds;
    if (player->animationDuration > 0) {
        if (player->looping) {
            player->animationTime = fmod(player->animationTime, player->animationDuration);
        } else {
            player->animationTime = std::min(player->animationTime, player->animationDuration);
        }
    }

    updateSVGForAnimation(player, player->animationTime);
}

bool SVGPlayer_Render(SVGPlayerHandle player, void* pixelBuffer, int width, int height, float scale) {
    if (!player || !pixelBuffer || width <= 0 || height <= 0) {
        if (player) player->lastError = "Invalid render parameters";
        return false;
    }

    std::lock_guard<std::mutex> lock(player->renderMutex);

    if (!player->svgDom) {
        player->lastError = "No SVG loaded";
        return false;
    }

    auto startTime = SteadyClock::now();

    // Create Skia surface wrapping the pixel buffer
    SkImageInfo imageInfo = SkImageInfo::Make(
        width, height,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType,
        SkColorSpace::MakeSRGB()
    );

    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(imageInfo, pixelBuffer, width * 4);
    if (!surface) {
        player->lastError = "Failed to create render surface";
        return false;
    }

    SkCanvas* canvas = surface->getCanvas();

    // Clear canvas
    canvas->clear(SK_ColorWHITE);

    // Apply HiDPI scaling
    if (scale != 1.0f) {
        canvas->scale(scale, scale);
    }

    // Calculate scaling to fit SVG in the canvas
    int renderWidth = static_cast<int>(width / scale);
    int renderHeight = static_cast<int>(height / scale);

    float scaleX = static_cast<float>(renderWidth) / player->svgWidth;
    float scaleY = static_cast<float>(renderHeight) / player->svgHeight;
    float fitScale = std::min(scaleX, scaleY);

    // Center the SVG
    float offsetX = (renderWidth - player->svgWidth * fitScale) / 2.0f;
    float offsetY = (renderHeight - player->svgHeight * fitScale) / 2.0f;

    canvas->translate(offsetX, offsetY);
    canvas->scale(fitScale, fitScale);

    // Set container size and render
    player->svgDom->setContainerSize(SkSize::Make(player->svgWidth, player->svgHeight));
    player->svgDom->render(canvas);

    // Update statistics
    auto endTime = SteadyClock::now();
    player->stats.renderTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    player->stats.animationTimeMs = player->animationTime * 1000.0;

    // Calculate FPS
    player->frameCount++;
    double elapsed = std::chrono::duration<double>(endTime - player->lastFrameTime).count();
    player->fpsAccumulator += elapsed;

    if (player->fpsAccumulator >= 1.0) {
        player->stats.fps = player->frameCount / player->fpsAccumulator;
        player->frameCount = 0;
        player->fpsAccumulator = 0.0;
    }

    player->lastFrameTime = endTime;

    // Update frame info
    if (!player->animations.empty()) {
        player->stats.currentFrame = static_cast<int>(player->animations[0].getCurrentFrameIndex(player->animationTime));
        player->stats.totalFrames = static_cast<int>(player->animations[0].values.size());
    }

    return true;
}

SVGRenderStats SVGPlayer_GetStats(SVGPlayerHandle player) {
    if (!player) {
        return {};
    }
    return player->stats;
}

double SVGPlayer_GetDuration(SVGPlayerHandle player) {
    if (!player) return 0.0;
    return player->animationDuration;
}

bool SVGPlayer_IsLooping(SVGPlayerHandle player) {
    if (!player) return false;
    return player->looping;
}

void SVGPlayer_SetLooping(SVGPlayerHandle player, bool looping) {
    if (!player) return;
    player->looping = looping;
}

const char* SVGPlayer_GetLastError(SVGPlayerHandle player) {
    if (!player) return "Invalid player handle";
    return player->lastError.c_str();
}
