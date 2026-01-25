// test_api_compile.cpp - Verify API header compiles correctly
//
// This is a lightweight test that only checks if the public API header
// can be compiled without errors. It does NOT require Skia to be built.
//
// Compile with:
//   clang++ -std=c++17 -I../shared -c test_api_compile.cpp -o test_api_compile.o
//
// This test is used in CI to quickly verify API header syntax.

#include "../shared/fbfsvg_player_api.h"
#include <cstdio>

// Verify all public types are defined and accessible
static void verify_types() {
    // Opaque handle type
    FBFSVGPlayerRef player = nullptr;
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
    auto create = &FBFSVGPlayer_Create;
    auto destroy = &FBFSVGPlayer_Destroy;
    auto loadSVG = &FBFSVGPlayer_LoadSVG;
    auto loadSVGData = &FBFSVGPlayer_LoadSVGData;
    auto unload = &FBFSVGPlayer_Unload;
    auto isLoaded = &FBFSVGPlayer_IsLoaded;
    auto getSize = &FBFSVGPlayer_GetSize;
    auto play = &FBFSVGPlayer_Play;
    auto pause = &FBFSVGPlayer_Pause;
    auto stop = &FBFSVGPlayer_Stop;
    auto togglePlayback = &FBFSVGPlayer_TogglePlayback;
    auto getPlaybackState = &FBFSVGPlayer_GetPlaybackState;
    auto setPlaybackState = &FBFSVGPlayer_SetPlaybackState;
    auto getRepeatMode = &FBFSVGPlayer_GetRepeatMode;
    auto setRepeatMode = &FBFSVGPlayer_SetRepeatMode;
    auto setRepeatCount = &FBFSVGPlayer_SetRepeatCount;
    auto getPlaybackRate = &FBFSVGPlayer_GetPlaybackRate;
    auto setPlaybackRate = &FBFSVGPlayer_SetPlaybackRate;
    auto update = &FBFSVGPlayer_Update;
    auto getCurrentTime = &FBFSVGPlayer_GetCurrentTime;
    auto getDuration = &FBFSVGPlayer_GetDuration;
    auto getProgress = &FBFSVGPlayer_GetProgress;
    auto getCurrentFrame = &FBFSVGPlayer_GetCurrentFrame;
    auto getTotalFrames = &FBFSVGPlayer_GetTotalFrames;
    auto getFrameRate = &FBFSVGPlayer_GetFrameRate;
    auto seekTo = &FBFSVGPlayer_SeekTo;
    auto seekToFrame = &FBFSVGPlayer_SeekToFrame;
    auto seekToProgress = &FBFSVGPlayer_SeekToProgress;
    auto stepForward = &FBFSVGPlayer_StepForward;
    auto stepBackward = &FBFSVGPlayer_StepBackward;
    auto stepByFrames = &FBFSVGPlayer_StepByFrames;
    auto beginScrubbing = &FBFSVGPlayer_BeginScrubbing;
    auto scrubToProgress = &FBFSVGPlayer_ScrubToProgress;
    auto endScrubbing = &FBFSVGPlayer_EndScrubbing;
    auto isScrubbing = &FBFSVGPlayer_IsScrubbing;
    auto render = &FBFSVGPlayer_Render;
    auto getStats = &FBFSVGPlayer_GetStats;
    auto resetStats = &FBFSVGPlayer_ResetStats;
    auto getLastError = &FBFSVGPlayer_GetLastError;
    auto formatTime = &FBFSVGPlayer_FormatTime;
    auto getVersion = &FBFSVGPlayer_GetVersion;

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
    static_assert(FBFSVG_PLAYER_API_VERSION_MAJOR >= 0, "Major version must be >= 0");
    static_assert(FBFSVG_PLAYER_API_VERSION_MINOR >= 0, "Minor version must be >= 0");
    static_assert(FBFSVG_PLAYER_API_VERSION_PATCH >= 0, "Patch version must be >= 0");
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
