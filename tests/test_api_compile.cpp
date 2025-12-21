// test_api_compile.cpp - Verify API header compiles correctly
//
// This is a lightweight test that only checks if the public API header
// can be compiled without errors. It does NOT require Skia to be built.
//
// Compile with:
//   clang++ -std=c++17 -I../shared -c test_api_compile.cpp -o test_api_compile.o
//
// This test is used in CI to quickly verify API header syntax.

#include "../shared/svg_player_api.h"
#include <cstdio>

// Verify all public types are defined and accessible
static void verify_types() {
    // Opaque handle type
    SVGPlayerRef player = nullptr;
    (void)player;

    // Playback state enum
    SVGPlaybackState state = SVGPlaybackState_Stopped;
    state = SVGPlaybackState_Playing;
    state = SVGPlaybackState_Paused;
    (void)state;

    // Repeat mode enum
    SVGRepeatMode mode = SVGRepeatMode_None;
    mode = SVGRepeatMode_Loop;
    mode = SVGRepeatMode_Reverse;
    mode = SVGRepeatMode_Count;
    (void)mode;

    // Statistics structure
    SVGRenderStats stats = {};
    stats.renderTimeMs = 0.0;
    stats.updateTimeMs = 0.0;
    stats.animationTimeMs = 0.0;
    stats.currentFrame = 0;
    stats.totalFrames = 0;
    stats.fps = 0.0;
    stats.peakMemoryBytes = 0;
    stats.elementsRendered = 0;
    (void)stats;

    // Callback types
    SVGStateChangeCallback stateCallback = nullptr;
    SVGLoopCallback loopCallback = nullptr;
    SVGEndCallback endCallback = nullptr;
    SVGErrorCallback errorCallback = nullptr;
    (void)stateCallback;
    (void)loopCallback;
    (void)endCallback;
    (void)errorCallback;
}

// Verify all public function declarations exist
// (We don't call them, just verify they're declared)
static void verify_function_declarations() {
    // Get function pointers to verify declarations exist
    auto create = &SVGPlayer_Create;
    auto destroy = &SVGPlayer_Destroy;
    auto loadSVG = &SVGPlayer_LoadSVG;
    auto loadSVGData = &SVGPlayer_LoadSVGData;
    auto unload = &SVGPlayer_Unload;
    auto isLoaded = &SVGPlayer_IsLoaded;
    auto getSize = &SVGPlayer_GetSize;
    auto play = &SVGPlayer_Play;
    auto pause = &SVGPlayer_Pause;
    auto stop = &SVGPlayer_Stop;
    auto togglePlayback = &SVGPlayer_TogglePlayback;
    auto getPlaybackState = &SVGPlayer_GetPlaybackState;
    auto setPlaybackState = &SVGPlayer_SetPlaybackState;
    auto getRepeatMode = &SVGPlayer_GetRepeatMode;
    auto setRepeatMode = &SVGPlayer_SetRepeatMode;
    auto setRepeatCount = &SVGPlayer_SetRepeatCount;
    auto getPlaybackRate = &SVGPlayer_GetPlaybackRate;
    auto setPlaybackRate = &SVGPlayer_SetPlaybackRate;
    auto update = &SVGPlayer_Update;
    auto getCurrentTime = &SVGPlayer_GetCurrentTime;
    auto getDuration = &SVGPlayer_GetDuration;
    auto getProgress = &SVGPlayer_GetProgress;
    auto getCurrentFrame = &SVGPlayer_GetCurrentFrame;
    auto getTotalFrames = &SVGPlayer_GetTotalFrames;
    auto getFrameRate = &SVGPlayer_GetFrameRate;
    auto seekTo = &SVGPlayer_SeekTo;
    auto seekToFrame = &SVGPlayer_SeekToFrame;
    auto seekToProgress = &SVGPlayer_SeekToProgress;
    auto stepForward = &SVGPlayer_StepForward;
    auto stepBackward = &SVGPlayer_StepBackward;
    auto stepByFrames = &SVGPlayer_StepByFrames;
    auto beginScrubbing = &SVGPlayer_BeginScrubbing;
    auto scrubToProgress = &SVGPlayer_ScrubToProgress;
    auto endScrubbing = &SVGPlayer_EndScrubbing;
    auto isScrubbing = &SVGPlayer_IsScrubbing;
    auto render = &SVGPlayer_Render;
    auto getStats = &SVGPlayer_GetStats;
    auto resetStats = &SVGPlayer_ResetStats;
    auto getLastError = &SVGPlayer_GetLastError;
    auto formatTime = &SVGPlayer_FormatTime;
    auto getVersion = &SVGPlayer_GetVersion;

    // Suppress unused variable warnings
    (void)create; (void)destroy; (void)loadSVG; (void)loadSVGData;
    (void)unload; (void)isLoaded; (void)getSize;
    (void)play; (void)pause; (void)stop; (void)togglePlayback;
    (void)getPlaybackState; (void)setPlaybackState;
    (void)getRepeatMode; (void)setRepeatMode; (void)setRepeatCount;
    (void)getPlaybackRate; (void)setPlaybackRate;
    (void)update; (void)getCurrentTime; (void)getDuration;
    (void)getProgress; (void)getCurrentFrame; (void)getTotalFrames;
    (void)getFrameRate; (void)seekTo; (void)seekToFrame;
    (void)seekToProgress; (void)stepForward; (void)stepBackward;
    (void)stepByFrames; (void)beginScrubbing; (void)scrubToProgress;
    (void)endScrubbing; (void)isScrubbing; (void)render;
    (void)getStats; (void)resetStats; (void)getLastError;
    (void)formatTime; (void)getVersion;
}

// Verify API version macros
static void verify_version_macros() {
    static_assert(SVG_PLAYER_API_VERSION_MAJOR >= 0, "Major version must be >= 0");
    static_assert(SVG_PLAYER_API_VERSION_MINOR >= 0, "Minor version must be >= 0");
    static_assert(SVG_PLAYER_API_VERSION_PATCH >= 0, "Patch version must be >= 0");
}

int main() {
    printf("API header compilation test\n");
    printf("---------------------------\n");

    verify_types();
    printf("[PASS] All public types are defined\n");

    verify_function_declarations();
    printf("[PASS] All public functions are declared\n");

    verify_version_macros();
    printf("[PASS] Version macros are valid\n");

    printf("\nAPI header compilation: SUCCESS\n");
    return 0;
}
