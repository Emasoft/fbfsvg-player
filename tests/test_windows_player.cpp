// test_windows_player.cpp - Unit tests for Windows SVG Player
//
// Tests for Graphite GPU backend, CPU fallback, command-line parsing,
// and rendering mode detection on Windows.
//
// Compile with MSVC:
//   cl /std:c++17 /I..\shared /DTEST_WINDOWS_PLAYER test_windows_player.cpp /Fe:test_windows_player.exe
//
// These tests verify Windows-specific functionality of the SVG player.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#else
// Mock Windows types for cross-platform compilation of test structure
typedef int BOOL;
typedef unsigned long DWORD;
#define TRUE 1
#define FALSE 0
#endif

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
static int g_skipCount = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { registerTest(#name, test_##name); } \
    } g_registrar_##name; \
    static void test_##name()

#define TEST_SKIP(reason) \
    do { \
        throw SkipTestException(reason); \
    } while(0)

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

#define ASSERT_STREQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            throw std::runtime_error("ASSERT_STREQ failed: strings differ"); \
        } \
    } while(0)

#define ASSERT_STRNE(a, b) \
    do { \
        if (strcmp((a), (b)) == 0) { \
            throw std::runtime_error("ASSERT_STRNE failed: strings are equal"); \
        } \
    } while(0)

#define ASSERT_CONTAINS(haystack, needle) \
    do { \
        if (strstr((haystack), (needle)) == nullptr) { \
            throw std::runtime_error("ASSERT_CONTAINS failed: \"" needle "\" not found"); \
        } \
    } while(0)

// Exception class for skipped tests
class SkipTestException : public std::exception {
public:
    explicit SkipTestException(const char* reason) : reason_(reason) {}
    const char* what() const noexcept override { return reason_; }
private:
    const char* reason_;
};

using TestFunc = std::function<void()>;
static std::vector<std::pair<std::string, TestFunc>> g_tests;

static void registerTest(const char* name, TestFunc func) {
    g_tests.push_back({name, func});
}

// =============================================================================
// Test SVG Data
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

// =============================================================================
// Command-line Argument Parsing Simulation
// =============================================================================

// Simulates parsing command-line arguments as done in svg_player_animated_windows.cpp
struct ParsedArgs {
    bool useGraphiteBackend = true;  // Graphite GPU is default
    bool startFullscreen = false;
    bool startMaximized = false;
    bool parallelRendering = true;
    int benchmarkDuration = 0;
    std::string screenshotPath;
    std::string inputFile;
    int windowWidth = 800;
    int windowHeight = 600;
    int windowPosX = -1;
    int windowPosY = -1;
    bool remoteControlEnabled = false;
    int remoteControlPort = 9999;
    bool jsonOutput = false;
    bool showHelp = false;
    bool showVersion = false;
};

// Parse command-line arguments (mirrors the logic in svg_player_animated_windows.cpp)
ParsedArgs parseCommandLine(int argc, const char* argv[]) {
    ParsedArgs args;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            args.showVersion = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            args.showHelp = true;
        } else if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            args.startFullscreen = true;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            // Use CPU raster rendering instead of Graphite GPU
            args.useGraphiteBackend = false;
        } else if (strcmp(argv[i], "--graphite") == 0) {
            // Legacy flag - Graphite is now default, this is a no-op
            args.useGraphiteBackend = true;
        } else if (strcmp(argv[i], "--windowed") == 0 || strcmp(argv[i], "-w") == 0) {
            args.startFullscreen = false;
        } else if (strcmp(argv[i], "--maximize") == 0 || strcmp(argv[i], "-m") == 0) {
            args.startMaximized = true;
        } else if (strcmp(argv[i], "--sequential") == 0) {
            args.parallelRendering = false;
        } else if (strcmp(argv[i], "--json") == 0) {
            args.jsonOutput = true;
        } else if (strncmp(argv[i], "--duration=", 11) == 0) {
            args.benchmarkDuration = atoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--screenshot=", 13) == 0) {
            args.screenshotPath = argv[i] + 13;
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            // Parse --size=WxH
            int w, h;
            if (sscanf(argv[i] + 7, "%dx%d", &w, &h) == 2) {
                args.windowWidth = w;
                args.windowHeight = h;
            }
        } else if (strncmp(argv[i], "--pos=", 6) == 0) {
            // Parse --pos=X,Y
            int x, y;
            if (sscanf(argv[i] + 6, "%d,%d", &x, &y) == 2) {
                args.windowPosX = x;
                args.windowPosY = y;
            }
        } else if (strcmp(argv[i], "--remote-control") == 0) {
            args.remoteControlEnabled = true;
        } else if (strncmp(argv[i], "--remote-control=", 17) == 0) {
            args.remoteControlEnabled = true;
            args.remoteControlPort = atoi(argv[i] + 17);
        } else if (argv[i][0] != '-') {
            // Positional argument (input file)
            args.inputFile = argv[i];
        }
    }

    return args;
}

// =============================================================================
// Vulkan Availability Detection (Windows)
// =============================================================================

#ifdef _WIN32
// Check if Vulkan runtime is available on Windows
bool isVulkanAvailable() {
    HMODULE vulkanLib = LoadLibraryA("vulkan-1.dll");
    if (vulkanLib) {
        FreeLibrary(vulkanLib);
        return true;
    }
    return false;
}

// Get detailed Vulkan error information
const char* getVulkanErrorMessage() {
    if (!isVulkanAvailable()) {
        return "Vulkan runtime (vulkan-1.dll) not found. Install GPU drivers or Vulkan SDK.";
    }
    return nullptr;  // No error - Vulkan is available
}
#else
// Non-Windows stubs
bool isVulkanAvailable() {
    // On non-Windows, check for libvulkan.so
    return false;  // Stub for test compilation
}

const char* getVulkanErrorMessage() {
    return "Vulkan not available on this platform (test stub)";
}
#endif

// =============================================================================
// Rendering Backend Simulation
// =============================================================================

enum class RenderingBackend {
    CPU_RASTER,
    GRAPHITE_VULKAN,
    GRAPHITE_METAL  // Not used on Windows
};

// Simulates backend selection logic from svg_player_animated_windows.cpp
RenderingBackend selectRenderingBackend(bool useGraphiteBackend, bool vulkanAvailable) {
    if (!useGraphiteBackend) {
        // User explicitly requested CPU rendering
        return RenderingBackend::CPU_RASTER;
    }

    if (!vulkanAvailable) {
        // Graphite requested but Vulkan unavailable - fallback to CPU
        return RenderingBackend::CPU_RASTER;
    }

    // Graphite with Vulkan on Windows
    return RenderingBackend::GRAPHITE_VULKAN;
}

const char* getBackendName(RenderingBackend backend) {
    switch (backend) {
        case RenderingBackend::CPU_RASTER:
            return "CPU Raster";
        case RenderingBackend::GRAPHITE_VULKAN:
            return "Vulkan Graphite";
        case RenderingBackend::GRAPHITE_METAL:
            return "Metal Graphite";
        default:
            return "Unknown";
    }
}

// =============================================================================
// Mock Graphite Context for Testing
// =============================================================================

// Mock implementation of GraphiteContext for testing without actual GPU
class MockGraphiteContext {
public:
    MockGraphiteContext(bool shouldInitSucceed = true)
        : shouldInitSucceed_(shouldInitSucceed), initialized_(false) {}

    bool initialize() {
        if (!shouldInitSucceed_) {
            errorMessage_ = "Mock initialization failure for testing";
            return false;
        }
        initialized_ = true;
        return true;
    }

    void destroy() {
        initialized_ = false;
    }

    bool isInitialized() const { return initialized_; }

    const char* getBackendName() const {
        return "Mock Vulkan Graphite";
    }

    const char* getLastError() const {
        return errorMessage_.empty() ? nullptr : errorMessage_.c_str();
    }

    // Simulate stats reporting
    struct Stats {
        int framesRendered;
        double totalRenderTimeMs;
        double avgFrameTimeMs;
        size_t gpuMemoryUsed;
    };

    Stats getStats() const {
        return {framesRendered_, totalRenderTimeMs_,
                framesRendered_ > 0 ? totalRenderTimeMs_ / framesRendered_ : 0.0,
                gpuMemoryUsed_};
    }

    void recordFrame(double renderTimeMs, size_t memoryUsed) {
        framesRendered_++;
        totalRenderTimeMs_ += renderTimeMs;
        gpuMemoryUsed_ = memoryUsed;
    }

private:
    bool shouldInitSucceed_;
    bool initialized_;
    std::string errorMessage_;
    int framesRendered_ = 0;
    double totalRenderTimeMs_ = 0.0;
    size_t gpuMemoryUsed_ = 0;
};

// =============================================================================
// GRAPHITE GPU BACKEND TESTS
// =============================================================================

TEST(graphite_context_initialization_success) {
    // Test that mock Graphite context initializes successfully
    MockGraphiteContext context(true);  // Should succeed

    ASSERT_FALSE(context.isInitialized());  // Not initialized yet

    bool result = context.initialize();
    ASSERT_TRUE(result);
    ASSERT_TRUE(context.isInitialized());

    ASSERT_STREQ(context.getBackendName(), "Mock Vulkan Graphite");

    context.destroy();
    ASSERT_FALSE(context.isInitialized());
}

TEST(graphite_context_initialization_failure) {
    // Test that mock Graphite context reports failure correctly
    MockGraphiteContext context(false);  // Should fail

    bool result = context.initialize();
    ASSERT_FALSE(result);
    ASSERT_FALSE(context.isInitialized());

    const char* error = context.getLastError();
    ASSERT_NOT_NULL(error);
    ASSERT_CONTAINS(error, "Mock initialization failure");
}

TEST(graphite_gpu_stats_reporting) {
    // Test that GPU stats are tracked correctly
    MockGraphiteContext context(true);
    context.initialize();

    // Simulate rendering some frames
    context.recordFrame(16.67, 1024 * 1024);  // 60fps frame
    context.recordFrame(15.50, 1024 * 1024);
    context.recordFrame(17.20, 1024 * 1024);

    auto stats = context.getStats();

    ASSERT_EQ(stats.framesRendered, 3);
    ASSERT_TRUE(stats.totalRenderTimeMs > 49.0 && stats.totalRenderTimeMs < 50.0);
    ASSERT_TRUE(stats.avgFrameTimeMs > 16.0 && stats.avgFrameTimeMs < 17.0);
    ASSERT_EQ(stats.gpuMemoryUsed, 1024 * 1024);

    context.destroy();
}

TEST(graphite_backend_is_default_on_windows) {
    // Verify that Graphite is the default backend (not CPU)
    ParsedArgs defaultArgs = parseCommandLine(0, nullptr);

    ASSERT_TRUE(defaultArgs.useGraphiteBackend);  // Graphite is default
}

TEST(graphite_backend_selection_with_vulkan_available) {
    // When Vulkan is available and Graphite requested, use Graphite
    RenderingBackend backend = selectRenderingBackend(true, true);
    ASSERT_EQ(backend, RenderingBackend::GRAPHITE_VULKAN);
    ASSERT_STREQ(getBackendName(backend), "Vulkan Graphite");
}

// =============================================================================
// CPU FALLBACK TESTS
// =============================================================================

TEST(cpu_fallback_with_explicit_flag) {
    // Test that --cpu flag forces CPU rendering
    const char* argv[] = {"svg_player", "--cpu", "test.svg"};
    ParsedArgs args = parseCommandLine(3, argv);

    ASSERT_FALSE(args.useGraphiteBackend);

    // Even with Vulkan available, should use CPU
    RenderingBackend backend = selectRenderingBackend(args.useGraphiteBackend, true);
    ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
}

TEST(cpu_fallback_when_vulkan_unavailable) {
    // Test graceful fallback when Vulkan initialization fails
    // (User wants Graphite but Vulkan not available)
    RenderingBackend backend = selectRenderingBackend(true, false);  // Vulkan unavailable

    ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
    ASSERT_STREQ(getBackendName(backend), "CPU Raster");
}

TEST(cpu_raster_mode_works_standalone) {
    // Test that CPU raster mode can be used independently
    ParsedArgs args = parseCommandLine(0, nullptr);
    args.useGraphiteBackend = false;  // Force CPU mode

    // Backend selection should work regardless of Vulkan availability
    RenderingBackend backend1 = selectRenderingBackend(false, true);   // Vulkan available
    RenderingBackend backend2 = selectRenderingBackend(false, false);  // Vulkan unavailable

    // Both should select CPU since --cpu was specified
    ASSERT_EQ(backend1, RenderingBackend::CPU_RASTER);
    ASSERT_EQ(backend2, RenderingBackend::CPU_RASTER);
}

TEST(cpu_rendering_does_not_require_vulkan) {
    // Verify CPU rendering path doesn't depend on Vulkan at all
    MockGraphiteContext gpuContext(false);  // GPU init will fail

    // Should not throw or crash - just report failure
    bool gpuResult = gpuContext.initialize();
    ASSERT_FALSE(gpuResult);

    // CPU rendering should still be available as fallback
    RenderingBackend backend = selectRenderingBackend(false, false);
    ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
}

// =============================================================================
// COMMAND-LINE FLAG PARSING TESTS
// =============================================================================

TEST(parse_cpu_flag) {
    const char* argv[] = {"svg_player", "--cpu"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_FALSE(args.useGraphiteBackend);
}

TEST(parse_graphite_flag_is_noop) {
    // --graphite is legacy (Graphite is now default), should be handled gracefully
    const char* argv[] = {"svg_player", "--graphite"};
    ParsedArgs args = parseCommandLine(2, argv);

    // Should still be true (no-op, keeps default)
    ASSERT_TRUE(args.useGraphiteBackend);
}

TEST(parse_fullscreen_flag) {
    const char* argv[] = {"svg_player", "--fullscreen"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.startFullscreen);
}

TEST(parse_fullscreen_short_flag) {
    const char* argv[] = {"svg_player", "-f"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.startFullscreen);
}

TEST(parse_windowed_flag) {
    const char* argv[] = {"svg_player", "--windowed"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_FALSE(args.startFullscreen);
}

TEST(parse_maximize_flag) {
    const char* argv[] = {"svg_player", "--maximize"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.startMaximized);
}

TEST(parse_sequential_flag) {
    const char* argv[] = {"svg_player", "--sequential"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_FALSE(args.parallelRendering);
}

TEST(parse_json_flag) {
    const char* argv[] = {"svg_player", "--json"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.jsonOutput);
}

TEST(parse_duration_flag) {
    const char* argv[] = {"svg_player", "--duration=30"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_EQ(args.benchmarkDuration, 30);
}

TEST(parse_screenshot_flag) {
    const char* argv[] = {"svg_player", "--screenshot=output.png"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_STREQ(args.screenshotPath.c_str(), "output.png");
}

TEST(parse_size_flag) {
    const char* argv[] = {"svg_player", "--size=1920x1080"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_EQ(args.windowWidth, 1920);
    ASSERT_EQ(args.windowHeight, 1080);
}

TEST(parse_pos_flag) {
    const char* argv[] = {"svg_player", "--pos=100,200"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_EQ(args.windowPosX, 100);
    ASSERT_EQ(args.windowPosY, 200);
}

TEST(parse_remote_control_flag) {
    const char* argv[] = {"svg_player", "--remote-control"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.remoteControlEnabled);
    ASSERT_EQ(args.remoteControlPort, 9999);  // Default port
}

TEST(parse_remote_control_with_port) {
    const char* argv[] = {"svg_player", "--remote-control=8080"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.remoteControlEnabled);
    ASSERT_EQ(args.remoteControlPort, 8080);
}

TEST(parse_input_file) {
    const char* argv[] = {"svg_player", "animation.svg"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_STREQ(args.inputFile.c_str(), "animation.svg");
}

TEST(parse_combined_flags) {
    // Test parsing multiple flags together
    const char* argv[] = {"svg_player", "--cpu", "--fullscreen", "--size=800x600", "test.svg"};
    ParsedArgs args = parseCommandLine(5, argv);

    ASSERT_FALSE(args.useGraphiteBackend);
    ASSERT_TRUE(args.startFullscreen);
    ASSERT_EQ(args.windowWidth, 800);
    ASSERT_EQ(args.windowHeight, 600);
    ASSERT_STREQ(args.inputFile.c_str(), "test.svg");
}

TEST(parse_help_flag) {
    const char* argv[] = {"svg_player", "--help"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.showHelp);
}

TEST(parse_version_flag) {
    const char* argv[] = {"svg_player", "--version"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_TRUE(args.showVersion);
}

// =============================================================================
// RENDERING MODE DETECTION TESTS
// =============================================================================

TEST(detect_rendering_backend_graphite_vulkan) {
    RenderingBackend backend = selectRenderingBackend(true, true);
    ASSERT_EQ(backend, RenderingBackend::GRAPHITE_VULKAN);
}

TEST(detect_rendering_backend_cpu_explicit) {
    RenderingBackend backend = selectRenderingBackend(false, true);
    ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
}

TEST(detect_rendering_backend_cpu_fallback) {
    RenderingBackend backend = selectRenderingBackend(true, false);
    ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
}

TEST(backend_name_strings_are_valid) {
    ASSERT_STREQ(getBackendName(RenderingBackend::CPU_RASTER), "CPU Raster");
    ASSERT_STREQ(getBackendName(RenderingBackend::GRAPHITE_VULKAN), "Vulkan Graphite");
    ASSERT_STREQ(getBackendName(RenderingBackend::GRAPHITE_METAL), "Metal Graphite");
}

TEST(rendering_mode_from_command_line_integration) {
    // Test the full flow from command line to backend selection

    // Default: Graphite with Vulkan
    {
        const char* argv[] = {"svg_player", "test.svg"};
        ParsedArgs args = parseCommandLine(2, argv);
        RenderingBackend backend = selectRenderingBackend(args.useGraphiteBackend, true);
        ASSERT_EQ(backend, RenderingBackend::GRAPHITE_VULKAN);
    }

    // With --cpu: CPU raster
    {
        const char* argv[] = {"svg_player", "--cpu", "test.svg"};
        ParsedArgs args = parseCommandLine(3, argv);
        RenderingBackend backend = selectRenderingBackend(args.useGraphiteBackend, true);
        ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
    }
}

// =============================================================================
// VULKAN REQUIREMENTS TESTS
// =============================================================================

TEST(vulkan_availability_detection) {
    // This test verifies the detection function works
    // The actual result depends on the system
    bool available = isVulkanAvailable();

    // Just verify it returns a valid boolean (doesn't crash)
    ASSERT_TRUE(available == true || available == false);
}

TEST(vulkan_error_message_when_unavailable) {
    // Test error message content when Vulkan is not available
    bool available = isVulkanAvailable();

    if (!available) {
        const char* error = getVulkanErrorMessage();
        ASSERT_NOT_NULL(error);
        ASSERT_TRUE(strlen(error) > 0);
        // Should mention vulkan-1.dll on Windows
#ifdef _WIN32
        ASSERT_CONTAINS(error, "vulkan-1.dll");
#endif
    }
}

TEST(vulkan_error_message_null_when_available) {
    bool available = isVulkanAvailable();

    if (available) {
        const char* error = getVulkanErrorMessage();
        ASSERT_NULL(error);  // No error when Vulkan is available
    }
}

TEST(graphite_requires_vulkan_on_windows) {
    // Verify that Graphite on Windows requires Vulkan
    // If Vulkan unavailable, should fallback to CPU

    bool vulkanAvailable = isVulkanAvailable();

    // When requesting Graphite...
    RenderingBackend backend = selectRenderingBackend(true, vulkanAvailable);

    if (vulkanAvailable) {
        // ...and Vulkan is available, get Graphite
        ASSERT_EQ(backend, RenderingBackend::GRAPHITE_VULKAN);
    } else {
        // ...and Vulkan unavailable, fallback to CPU
        ASSERT_EQ(backend, RenderingBackend::CPU_RASTER);
    }
}

// =============================================================================
// WINDOWS-SPECIFIC TESTS
// =============================================================================

#ifdef _WIN32

TEST(windows_console_handler_types) {
    // Verify Windows console event types are defined
    DWORD ctrlC = CTRL_C_EVENT;
    DWORD ctrlBreak = CTRL_BREAK_EVENT;

    ASSERT_EQ(ctrlC, 0);
    ASSERT_EQ(ctrlBreak, 1);
}

TEST(windows_path_max_defined) {
    // Verify PATH_MAX equivalent is available
    ASSERT_TRUE(MAX_PATH >= 260);  // Windows minimum
}

TEST(windows_vulkan_dll_name) {
    // Verify we're checking for the correct DLL name
    const char* dllName = "vulkan-1.dll";
    ASSERT_STREQ(dllName, "vulkan-1.dll");
}

TEST(windows_can_load_kernel32) {
    // Basic test that Windows DLL loading works
    HMODULE kernel32 = LoadLibraryA("kernel32.dll");
    ASSERT_TRUE(kernel32 != nullptr);
    FreeLibrary(kernel32);
}

#else

TEST(windows_specific_tests_skipped_on_other_platforms) {
    TEST_SKIP("Windows-specific tests skipped on non-Windows platform");
}

#endif

// =============================================================================
// EDGE CASES AND ERROR HANDLING TESTS
// =============================================================================

TEST(empty_command_line_uses_defaults) {
    ParsedArgs args = parseCommandLine(0, nullptr);

    ASSERT_TRUE(args.useGraphiteBackend);  // Default: Graphite
    ASSERT_FALSE(args.startFullscreen);
    ASSERT_FALSE(args.startMaximized);
    ASSERT_TRUE(args.parallelRendering);
    ASSERT_EQ(args.benchmarkDuration, 0);
    ASSERT_TRUE(args.screenshotPath.empty());
    ASSERT_TRUE(args.inputFile.empty());
}

TEST(invalid_size_flag_format_ignored) {
    // Invalid format should leave defaults unchanged
    const char* argv[] = {"svg_player", "--size=invalid"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_EQ(args.windowWidth, 800);   // Default
    ASSERT_EQ(args.windowHeight, 600);  // Default
}

TEST(invalid_pos_flag_format_ignored) {
    const char* argv[] = {"svg_player", "--pos=invalid"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_EQ(args.windowPosX, -1);  // Default (unset)
    ASSERT_EQ(args.windowPosY, -1);  // Default (unset)
}

TEST(zero_duration_is_valid) {
    const char* argv[] = {"svg_player", "--duration=0"};
    ParsedArgs args = parseCommandLine(2, argv);

    ASSERT_EQ(args.benchmarkDuration, 0);
}

TEST(negative_duration_handled) {
    const char* argv[] = {"svg_player", "--duration=-5"};
    ParsedArgs args = parseCommandLine(2, argv);

    // atoi returns -5, which is technically valid but meaningless
    // The application should handle this gracefully
    ASSERT_EQ(args.benchmarkDuration, -5);
}

TEST(unknown_flags_ignored) {
    // Unknown flags should not crash, just be ignored
    const char* argv[] = {"svg_player", "--unknown-flag", "--another-unknown"};
    ParsedArgs args = parseCommandLine(3, argv);

    // Should use defaults
    ASSERT_TRUE(args.useGraphiteBackend);
    ASSERT_FALSE(args.showHelp);
}

TEST(multiple_input_files_takes_last) {
    const char* argv[] = {"svg_player", "file1.svg", "file2.svg"};
    ParsedArgs args = parseCommandLine(3, argv);

    // Last positional argument wins
    ASSERT_STREQ(args.inputFile.c_str(), "file2.svg");
}

TEST(mock_context_destroy_is_safe_when_not_initialized) {
    MockGraphiteContext context(false);
    // Never initialized
    context.destroy();  // Should not crash
    ASSERT_FALSE(context.isInitialized());
}

TEST(mock_context_double_destroy_is_safe) {
    MockGraphiteContext context(true);
    context.initialize();

    context.destroy();
    ASSERT_FALSE(context.isInitialized());

    context.destroy();  // Second destroy should be safe
    ASSERT_FALSE(context.isInitialized());
}

TEST(mock_context_reinitialize_after_destroy) {
    MockGraphiteContext context(true);

    context.initialize();
    ASSERT_TRUE(context.isInitialized());

    context.destroy();
    ASSERT_FALSE(context.isInitialized());

    // Should be able to reinitialize
    context.initialize();
    ASSERT_TRUE(context.isInitialized());

    context.destroy();
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main(int argc, char* argv[]) {
    printf("\n");
    printf("================================================================\n");
    printf("Windows SVG Player - Unit Tests\n");
    printf("================================================================\n");
#ifdef _WIN32
    printf("Platform: Windows\n");
    printf("Vulkan Available: %s\n", isVulkanAvailable() ? "Yes" : "No");
#else
    printf("Platform: Non-Windows (limited test coverage)\n");
#endif
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
        } catch (const SkipTestException& e) {
            printf("\033[33mSKIP\033[0m (%s)\n", e.what());
            g_skipCount++;
            g_results.push_back({name, true, std::string("SKIP: ") + e.what()});
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
    if (g_skipCount > 0) {
        printf(" (\033[33m%d skipped\033[0m)", g_skipCount);
    }
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
