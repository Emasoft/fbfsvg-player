// test_svg_player_api.cpp - Unit tests for unified SVG Player C API
//
// Simple test framework without external dependencies.
// Compile with: clang++ -std=c++17 -I../shared test_svg_player_api.cpp -o test_api
//
// These tests verify the public C API contract defined in fbfsvg_player_api.h

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

// Include the API header (tests the header compiles correctly)
#include "../shared/fbfsvg_player_api.h"

// =============================================================================
// Simple Test Framework
// =============================================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};

static std::vector<TestResult> g_results;
static int g_testCount = 0;
static int g_passCount = 0;
static int g_failCount = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { registerTest(#name, test_##name); } \
    } g_registrar_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            throw std::runtime_error("ASSERT_TRUE failed: " #expr); \
        } \
    } while(0)

#define ASSERT_FALSE(expr) \
    do { \
        if (expr) { \
            throw std::runtime_error("ASSERT_FALSE failed: " #expr); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            throw std::runtime_error("ASSERT_NE failed: " #a " == " #b); \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != nullptr) { \
            throw std::runtime_error("ASSERT_NULL failed: " #ptr " is not null"); \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == nullptr) { \
            throw std::runtime_error("ASSERT_NOT_NULL failed: " #ptr " is null"); \
        } \
    } while(0)

#define ASSERT_FLOAT_EQ(a, b, epsilon) \
    do { \
        if (std::fabs((a) - (b)) > (epsilon)) { \
            throw std::runtime_error("ASSERT_FLOAT_EQ failed: " #a " != " #b); \
        } \
    } while(0)

using TestFunc = std::function<void()>;
static std::vector<std::pair<std::string, TestFunc>> g_tests;

static void registerTest(const char* name, TestFunc func) {
    g_tests.push_back({name, func});
}

// =============================================================================
// Test SVG Data (minimal valid SVG for testing)
// =============================================================================

static const char* MINIMAL_SVG = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 100 100">
  <rect id="test-rect" x="10" y="10" width="80" height="80" fill="red"/>
</svg>
)";

static const char* ANIMATED_SVG = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 200 200">
  <rect id="animated-rect" x="0" y="50" width="50" height="50" fill="blue">
    <animate attributeName="x" from="0" to="150" dur="2s" repeatCount="indefinite"/>
  </rect>
</svg>
)";

static const char* INVALID_SVG = "This is not valid SVG content at all!";

// =============================================================================
// API Header Compilation Tests
// =============================================================================

TEST(api_header_compiles) {
    // This test passes if the header compiles without errors
    // Verify key types are defined
    FBFSVGPlayerRef player = nullptr;
    SVGPlaybackState state = SVGPlaybackState_Stopped;
    SVGRepeatMode mode = SVGRepeatMode_None;
    SVGRenderStats stats = {};

    (void)player;
    (void)state;
    (void)mode;
    (void)stats;

    ASSERT_TRUE(true);
}

TEST(api_version_defined) {
    ASSERT_TRUE(FBFSVG_PLAYER_API_VERSION_MAJOR >= 1);
    ASSERT_TRUE(FBFSVG_PLAYER_API_VERSION_MINOR >= 0);
    ASSERT_TRUE(FBFSVG_PLAYER_API_VERSION_PATCH >= 0);
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST(create_returns_valid_handle) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    ASSERT_NOT_NULL(player);
    FBFSVGPlayer_Destroy(player);
}

TEST(destroy_null_is_safe) {
    // Destroying NULL should not crash
    FBFSVGPlayer_Destroy(nullptr);
    ASSERT_TRUE(true);
}

TEST(multiple_create_destroy_cycles) {
    for (int i = 0; i < 10; i++) {
        FBFSVGPlayerRef player = FBFSVGPlayer_Create();
        ASSERT_NOT_NULL(player);
        FBFSVGPlayer_Destroy(player);
    }
}

// =============================================================================
// Loading Tests
// =============================================================================

TEST(load_svg_data_valid) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    ASSERT_NOT_NULL(player);

    bool result = FBFSVGPlayer_LoadSVGData(player, MINIMAL_SVG, strlen(MINIMAL_SVG));
    ASSERT_TRUE(result);
    ASSERT_TRUE(FBFSVGPlayer_IsLoaded(player));

    FBFSVGPlayer_Destroy(player);
}

TEST(load_svg_data_invalid) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    ASSERT_NOT_NULL(player);

    bool result = FBFSVGPlayer_LoadSVGData(player, INVALID_SVG, strlen(INVALID_SVG));
    ASSERT_FALSE(result);
    ASSERT_FALSE(FBFSVGPlayer_IsLoaded(player));

    FBFSVGPlayer_Destroy(player);
}

TEST(load_svg_data_null_player) {
    bool result = FBFSVGPlayer_LoadSVGData(nullptr, MINIMAL_SVG, strlen(MINIMAL_SVG));
    ASSERT_FALSE(result);
}

TEST(load_svg_data_null_data) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    ASSERT_NOT_NULL(player);

    bool result = FBFSVGPlayer_LoadSVGData(player, nullptr, 0);
    ASSERT_FALSE(result);

    FBFSVGPlayer_Destroy(player);
}

TEST(unload_clears_state) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, MINIMAL_SVG, strlen(MINIMAL_SVG));
    ASSERT_TRUE(FBFSVGPlayer_IsLoaded(player));

    FBFSVGPlayer_Unload(player);
    ASSERT_FALSE(FBFSVGPlayer_IsLoaded(player));

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Size/Dimensions Tests
// =============================================================================

TEST(get_intrinsic_size_valid) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, MINIMAL_SVG, strlen(MINIMAL_SVG));

    float width = 0, height = 0;
    bool result = FBFSVGPlayer_GetIntrinsicSize(player, &width, &height);

    ASSERT_TRUE(result);
    ASSERT_FLOAT_EQ(width, 100.0f, 0.1f);
    ASSERT_FLOAT_EQ(height, 100.0f, 0.1f);

    FBFSVGPlayer_Destroy(player);
}

TEST(get_intrinsic_size_no_svg_loaded) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();

    float width = 999, height = 999;
    bool result = FBFSVGPlayer_GetIntrinsicSize(player, &width, &height);

    ASSERT_FALSE(result);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Playback Control Tests
// =============================================================================

TEST(initial_state_is_stopped) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    SVGPlaybackState state = FBFSVGPlayer_GetPlaybackState(player);
    ASSERT_EQ(state, SVGPlaybackState_Stopped);

    FBFSVGPlayer_Destroy(player);
}

TEST(play_changes_state) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_Play(player);
    SVGPlaybackState state = FBFSVGPlayer_GetPlaybackState(player);
    ASSERT_EQ(state, SVGPlaybackState_Playing);

    FBFSVGPlayer_Destroy(player);
}

TEST(pause_changes_state) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_Play(player);
    FBFSVGPlayer_Pause(player);
    SVGPlaybackState state = FBFSVGPlayer_GetPlaybackState(player);
    ASSERT_EQ(state, SVGPlaybackState_Paused);

    FBFSVGPlayer_Destroy(player);
}

TEST(stop_resets_to_stopped) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_Play(player);
    FBFSVGPlayer_Stop(player);
    SVGPlaybackState state = FBFSVGPlayer_GetPlaybackState(player);
    ASSERT_EQ(state, SVGPlaybackState_Stopped);

    FBFSVGPlayer_Destroy(player);
}

TEST(toggle_playback_works) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    // Stopped -> Playing
    FBFSVGPlayer_TogglePlayback(player);
    ASSERT_EQ(FBFSVGPlayer_GetPlaybackState(player), SVGPlaybackState_Playing);

    // Playing -> Paused
    FBFSVGPlayer_TogglePlayback(player);
    ASSERT_EQ(FBFSVGPlayer_GetPlaybackState(player), SVGPlaybackState_Paused);

    // Paused -> Playing
    FBFSVGPlayer_TogglePlayback(player);
    ASSERT_EQ(FBFSVGPlayer_GetPlaybackState(player), SVGPlaybackState_Playing);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Timeline Tests
// =============================================================================

TEST(get_duration_animated_svg) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    double duration = FBFSVGPlayer_GetDuration(player);
    // Animated SVG has 2s animation
    ASSERT_TRUE(duration > 0.0);

    FBFSVGPlayer_Destroy(player);
}

TEST(get_current_time_initial_zero) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    double currentTime = FBFSVGPlayer_GetCurrentTime(player);
    ASSERT_FLOAT_EQ(currentTime, 0.0, 0.001);

    FBFSVGPlayer_Destroy(player);
}

TEST(update_advances_time) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));
    FBFSVGPlayer_Play(player);

    FBFSVGPlayer_Update(player, 0.5); // Advance 0.5 seconds
    double currentTime = FBFSVGPlayer_GetCurrentTime(player);
    ASSERT_TRUE(currentTime > 0.0);

    FBFSVGPlayer_Destroy(player);
}

TEST(get_progress_in_range) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));
    FBFSVGPlayer_Play(player);

    FBFSVGPlayer_Update(player, 0.5);
    float progress = FBFSVGPlayer_GetProgress(player);
    ASSERT_TRUE(progress >= 0.0f);
    ASSERT_TRUE(progress <= 1.0f);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Seeking Tests
// =============================================================================

TEST(seek_to_time) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SeekToTime(player, 1.0);
    double currentTime = FBFSVGPlayer_GetCurrentTime(player);
    ASSERT_FLOAT_EQ(currentTime, 1.0, 0.01);

    FBFSVGPlayer_Destroy(player);
}

TEST(seek_to_progress) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SeekToProgress(player, 0.5f);
    float progress = FBFSVGPlayer_GetProgress(player);
    ASSERT_FLOAT_EQ(progress, 0.5f, 0.01f);

    FBFSVGPlayer_Destroy(player);
}

TEST(seek_to_frame) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    int totalFrames = FBFSVGPlayer_GetTotalFrames(player);
    if (totalFrames > 1) {
        FBFSVGPlayer_SeekToFrame(player, totalFrames / 2);
        int currentFrame = FBFSVGPlayer_GetCurrentFrame(player);
        ASSERT_EQ(currentFrame, totalFrames / 2);
    }

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Repeat Mode Tests
// =============================================================================

TEST(default_repeat_mode_is_none) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    SVGRepeatMode mode = FBFSVGPlayer_GetRepeatMode(player);
    ASSERT_EQ(mode, SVGRepeatMode_None);

    FBFSVGPlayer_Destroy(player);
}

TEST(set_repeat_mode_loop) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SetRepeatMode(player, SVGRepeatMode_Loop);
    SVGRepeatMode mode = FBFSVGPlayer_GetRepeatMode(player);
    ASSERT_EQ(mode, SVGRepeatMode_Loop);

    FBFSVGPlayer_Destroy(player);
}

TEST(set_repeat_mode_reverse) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SetRepeatMode(player, SVGRepeatMode_Reverse);
    SVGRepeatMode mode = FBFSVGPlayer_GetRepeatMode(player);
    ASSERT_EQ(mode, SVGRepeatMode_Reverse);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Playback Rate Tests
// =============================================================================

TEST(default_playback_rate_is_one) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    float rate = FBFSVGPlayer_GetPlaybackRate(player);
    ASSERT_FLOAT_EQ(rate, 1.0f, 0.001f);

    FBFSVGPlayer_Destroy(player);
}

TEST(set_playback_rate) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SetPlaybackRate(player, 2.0f);
    float rate = FBFSVGPlayer_GetPlaybackRate(player);
    ASSERT_FLOAT_EQ(rate, 2.0f, 0.001f);

    FBFSVGPlayer_Destroy(player);
}

TEST(playback_rate_clamped_min) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SetPlaybackRate(player, 0.01f); // Below minimum
    float rate = FBFSVGPlayer_GetPlaybackRate(player);
    ASSERT_TRUE(rate >= 0.1f); // Should be clamped to minimum

    FBFSVGPlayer_Destroy(player);
}

TEST(playback_rate_clamped_max) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_SetPlaybackRate(player, 100.0f); // Above maximum
    float rate = FBFSVGPlayer_GetPlaybackRate(player);
    ASSERT_TRUE(rate <= 10.0f); // Should be clamped to maximum

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Frame Stepping Tests
// =============================================================================

TEST(step_forward) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    int initialFrame = FBFSVGPlayer_GetCurrentFrame(player);
    FBFSVGPlayer_StepForward(player);
    int newFrame = FBFSVGPlayer_GetCurrentFrame(player);

    ASSERT_EQ(newFrame, initialFrame + 1);

    FBFSVGPlayer_Destroy(player);
}

TEST(step_backward_at_start_stays_at_zero) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_StepBackward(player);
    int frame = FBFSVGPlayer_GetCurrentFrame(player);
    ASSERT_EQ(frame, 0);

    FBFSVGPlayer_Destroy(player);
}

TEST(step_by_frames) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    FBFSVGPlayer_StepByFrames(player, 5);
    int frame = FBFSVGPlayer_GetCurrentFrame(player);
    ASSERT_EQ(frame, 5);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Rendering Tests
// =============================================================================

TEST(render_to_buffer) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, MINIMAL_SVG, strlen(MINIMAL_SVG));

    const int width = 100;
    const int height = 100;
    std::vector<uint8_t> buffer(width * height * 4, 0);

    bool result = FBFSVGPlayer_Render(player, buffer.data(), width, height, 1.0f);
    ASSERT_TRUE(result);

    // Verify some pixels are non-zero (the red rect should be rendered)
    bool hasContent = false;
    for (size_t i = 0; i < buffer.size(); i += 4) {
        if (buffer[i] > 0 || buffer[i+1] > 0 || buffer[i+2] > 0) {
            hasContent = true;
            break;
        }
    }
    ASSERT_TRUE(hasContent);

    FBFSVGPlayer_Destroy(player);
}

TEST(render_null_buffer_fails) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, MINIMAL_SVG, strlen(MINIMAL_SVG));

    bool result = FBFSVGPlayer_Render(player, nullptr, 100, 100, 1.0f);
    ASSERT_FALSE(result);

    FBFSVGPlayer_Destroy(player);
}

TEST(render_no_svg_loaded_fails) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();

    std::vector<uint8_t> buffer(100 * 100 * 4, 0);
    bool result = FBFSVGPlayer_Render(player, buffer.data(), 100, 100, 1.0f);
    ASSERT_FALSE(result);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Statistics Tests
// =============================================================================

TEST(get_stats_returns_valid_data) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    // Render a frame to populate stats
    std::vector<uint8_t> buffer(200 * 200 * 4, 0);
    FBFSVGPlayer_Render(player, buffer.data(), 200, 200, 1.0f);

    SVGRenderStats stats = FBFSVGPlayer_GetStats(player);
    ASSERT_TRUE(stats.totalFrames > 0);

    FBFSVGPlayer_Destroy(player);
}

TEST(reset_stats) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    FBFSVGPlayer_LoadSVGData(player, ANIMATED_SVG, strlen(ANIMATED_SVG));

    std::vector<uint8_t> buffer(200 * 200 * 4, 0);
    FBFSVGPlayer_Render(player, buffer.data(), 200, 200, 1.0f);

    FBFSVGPlayer_ResetStats(player);
    SVGRenderStats stats = FBFSVGPlayer_GetStats(player);
    ASSERT_FLOAT_EQ(stats.renderTimeMs, 0.0, 0.001);

    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST(get_last_error_null_player) {
    const char* error = FBFSVGPlayer_GetLastError(nullptr);
    ASSERT_NULL(error);
}

TEST(get_last_error_no_error) {
    FBFSVGPlayerRef player = FBFSVGPlayer_Create();
    const char* error = FBFSVGPlayer_GetLastError(player);
    // May return null or empty string when no error
    ASSERT_TRUE(error == nullptr || strlen(error) == 0);
    FBFSVGPlayer_Destroy(player);
}

// =============================================================================
// Utility Tests
// =============================================================================

TEST(format_time_works) {
    char buffer[64];
    FBFSVGPlayer_FormatTime(65.5, buffer, sizeof(buffer));
    // Should be something like "01:05" or "1:05.500"
    ASSERT_TRUE(strlen(buffer) > 0);
}

TEST(get_version_string) {
    const char* version = FBFSVGPlayer_GetVersionString();
    ASSERT_NOT_NULL(version);
    ASSERT_TRUE(strlen(version) > 0);
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main(int argc, char* argv[]) {
    printf("\n");
    printf("================================================================\n");
    printf("SVG Player Unified API - Unit Tests\n");
    printf("================================================================\n\n");

    for (const auto& [name, func] : g_tests) {
        g_testCount++;
        printf("Running: %s ... ", name.c_str());
        fflush(stdout);

        try {
            func();
            printf("\033[32mPASS\033[0m\n");
            g_passCount++;
            g_results.push_back({name, true, ""});
        } catch (const std::exception& e) {
            printf("\033[31mFAIL\033[0m\n");
            printf("  Error: %s\n", e.what());
            g_failCount++;
            g_results.push_back({name, false, e.what()});
        }
    }

    printf("\n");
    printf("================================================================\n");
    printf("Results: %d/%d passed", g_passCount, g_testCount);
    if (g_failCount > 0) {
        printf(" (\033[31m%d failed\033[0m)", g_failCount);
    }
    printf("\n");
    printf("================================================================\n\n");

    if (g_failCount > 0) {
        printf("Failed tests:\n");
        for (const auto& result : g_results) {
            if (!result.passed) {
                printf("  - %s: %s\n", result.name.c_str(), result.message.c_str());
            }
        }
        printf("\n");
    }

    return g_failCount > 0 ? 1 : 0;
}
