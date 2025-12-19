// svg_player.cpp - Cross-platform SVG Player implementation
//
// This is a stub implementation. The actual rendering logic will be
// implemented using Skia when the cross-platform core is complete.

#define SVG_PLAYER_BUILDING_DLL
#include "svg_player.h"

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

// Version string
static const char* VERSION_STRING = "1.0.0";

// Internal player structure
struct SVGPlayer {
    // Loading state
    bool loaded;
    std::string svgData;
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

    // Error handling
    std::string lastError;

    // Element subscriptions
    std::unordered_set<std::string> subscribedElements;
    std::string lastHitElement;

    // Timing
    std::chrono::high_resolution_clock::time_point lastRenderStart;

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
        memset(&stats, 0, sizeof(stats));
    }
};

// ============================================================================
// Lifecycle Functions
// ============================================================================

SVG_PLAYER_API SVGPlayerHandle SVGPlayer_Create(void) {
    try {
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

    player->svgData = std::move(buffer);
    player->filePath = filepath;

    // TODO: Parse SVG using Skia's SkSVGDOM
    // For now, set placeholder values
    player->loaded = true;
    player->width = 800;
    player->height = 600;
    player->viewBoxX = 0;
    player->viewBoxY = 0;
    player->viewBoxWidth = 800;
    player->viewBoxHeight = 600;
    player->duration = 5.0;  // Placeholder: 5 seconds
    player->totalFrames = (int)(player->duration * player->frameRate);
    player->currentTime = 0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->completedLoops = 0;

    player->lastError.clear();
    return true;
}

SVG_PLAYER_API bool SVGPlayer_LoadSVGData(SVGPlayerHandle player, const void* data, size_t length) {
    if (!player || !data || length == 0) {
        if (player) player->lastError = "Invalid arguments";
        return false;
    }

    player->svgData.assign(static_cast<const char*>(data), length);
    player->filePath.clear();

    // TODO: Parse SVG using Skia's SkSVGDOM
    // For now, set placeholder values
    player->loaded = true;
    player->width = 800;
    player->height = 600;
    player->viewBoxX = 0;
    player->viewBoxY = 0;
    player->viewBoxWidth = 800;
    player->viewBoxHeight = 600;
    player->duration = 5.0;
    player->totalFrames = (int)(player->duration * player->frameRate);
    player->currentTime = 0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->completedLoops = 0;

    player->lastError.clear();
    return true;
}

SVG_PLAYER_API void SVGPlayer_Unload(SVGPlayerHandle player) {
    if (!player) return;

    player->svgData.clear();
    player->filePath.clear();
    player->loaded = false;
    player->width = 0;
    player->height = 0;
    player->duration = 0;
    player->currentTime = 0;
    player->totalFrames = 0;
    player->playbackState = SVGPlaybackState_Stopped;
    player->completedLoops = 0;
    player->subscribedElements.clear();
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

    auto renderStart = std::chrono::high_resolution_clock::now();

    // TODO: Implement actual Skia rendering
    // For now, fill with a gradient pattern to verify the buffer is being written
    uint8_t* buffer = static_cast<uint8_t*>(pixelBuffer);
    float progress = SVGPlayer_GetProgress(player);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;

            // Create a simple animated gradient
            float fx = (float)x / width;
            float fy = (float)y / height;

            // Animated color based on progress
            uint8_t r = (uint8_t)(128 + 127 * sin(progress * 6.28 + fx * 3.14));
            uint8_t g = (uint8_t)(128 + 127 * sin(progress * 6.28 + fy * 3.14));
            uint8_t b = (uint8_t)(128 + 127 * sin(progress * 6.28 + (fx + fy) * 1.57));

            buffer[idx + 0] = r;  // R
            buffer[idx + 1] = g;  // G
            buffer[idx + 2] = b;  // B
            buffer[idx + 3] = 255; // A
        }
    }

    auto renderEnd = std::chrono::high_resolution_clock::now();
    player->stats.renderTimeMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
    player->stats.elementsRendered = 1; // Placeholder

    // Calculate FPS
    static double lastRenderTime = 0;
    double currentRenderTime = std::chrono::duration<double>(renderEnd.time_since_epoch()).count();
    if (lastRenderTime > 0) {
        double delta = currentRenderTime - lastRenderTime;
        if (delta > 0) {
            player->stats.fps = 1.0 / delta;
        }
    }
    lastRenderTime = currentRenderTime;

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
