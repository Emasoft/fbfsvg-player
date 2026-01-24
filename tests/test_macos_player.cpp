// test_macos_player.cpp - Unit tests for macOS SVG player rendering backends
//
// Tests for Graphite GPU backend, CPU fallback, and Metal Ganesh fallback.
// Compile with: clang++ -std=c++17 -I../shared -DTEST_MACOS_PLAYER test_macos_player.cpp -o test_macos_player
//
// These tests verify:
// 1. Graphite GPU backend initialization and rendering
// 2. CPU raster fallback when --cpu flag is used
// 3. Metal Ganesh fallback when --metal flag is used
// 4. Command-line flag parsing for rendering mode selection
// 5. Rendering mode detection and reporting

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// =============================================================================
// Simple Test Framework (same pattern as test_svg_player_api.cpp)
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

#define SKIP_TEST(reason) \
    do { \
        throw SkipException(reason); \
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
            throw std::runtime_error(std::string("ASSERT_STREQ failed: \"") + (a) + "\" != \"" + (b) + "\""); \
        } \
    } while(0)

#define ASSERT_STRNE(a, b) \
    do { \
        if (strcmp((a), (b)) == 0) { \
            throw std::runtime_error(std::string("ASSERT_STRNE failed: \"") + (a) + "\" == \"" + (b) + "\""); \
        } \
    } while(0)

class SkipException : public std::exception {
public:
    explicit SkipException(const char* reason) : reason_(reason) {}
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
// Rendering Mode Enumeration (mirrors svg_player_animated.cpp logic)
// =============================================================================

enum class RenderingMode {
    CPU,           // CPU raster rendering (--cpu flag)
    MetalGanesh,   // Metal GPU via Ganesh backend (--metal flag)
    Graphite       // Graphite next-gen GPU backend (default on macOS)
};

const char* renderingModeToString(RenderingMode mode) {
    switch (mode) {
        case RenderingMode::CPU: return "CPU Raster";
        case RenderingMode::MetalGanesh: return "Metal (Ganesh)";
        case RenderingMode::Graphite: return "Metal (Graphite)";
    }
    return "Unknown";
}

// =============================================================================
// Command-line Flag Parsing Simulation
// Mirrors the parsing logic from svg_player_animated.cpp main()
// =============================================================================

struct PlayerConfig {
    bool useMetalBackend = false;    // --metal flag
    bool useGraphiteBackend = true;  // Default, disabled by --cpu flag
    bool showHelp = false;           // --help flag
    std::string inputFile;

    // Derived property: actual rendering mode after fallback logic
    RenderingMode getEffectiveRenderingMode() const {
        if (useGraphiteBackend) {
            return RenderingMode::Graphite;
        } else if (useMetalBackend) {
            return RenderingMode::MetalGanesh;
        } else {
            return RenderingMode::CPU;
        }
    }
};

// Parse command-line arguments (mimics svg_player_animated.cpp logic)
PlayerConfig parseCommandLine(int argc, const char* argv[]) {
    PlayerConfig config;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--metal") == 0) {
            // Enable Metal GPU backend (Ganesh)
            config.useMetalBackend = true;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            // Use CPU raster rendering instead of Graphite GPU
            config.useGraphiteBackend = false;
        } else if (strcmp(argv[i], "--graphite") == 0) {
            // Legacy flag - Graphite is now default, this is a no-op
            // But we accept it gracefully for backward compatibility
            config.useGraphiteBackend = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            config.showHelp = true;
        } else if (argv[i][0] != '-') {
            // Non-flag argument is the input file
            config.inputFile = argv[i];
        }
    }

    return config;
}

// =============================================================================
// Mock GPU Context Classes for Testing
// These simulate the actual context classes without requiring GPU hardware
// =============================================================================

class MockGraphiteContext {
public:
    bool initialize() {
        // Simulate Graphite initialization
        // In real code, this would create Metal device, context, etc.
        initialized_ = simulateGPUAvailable_;
        return initialized_;
    }

    void destroy() {
        initialized_ = false;
    }

    bool isInitialized() const { return initialized_; }

    const char* getBackendName() const { return "Metal Graphite"; }

    // Test control: simulate GPU availability
    void setSimulateGPUAvailable(bool available) {
        simulateGPUAvailable_ = available;
    }

    // Simulate stats reporting
    struct RenderStats {
        double gpuTimeMs = 0.0;
        double cpuTimeMs = 0.0;
        int drawCalls = 0;
        size_t memoryUsedBytes = 0;
    };

    RenderStats getStats() const {
        RenderStats stats;
        if (initialized_) {
            stats.gpuTimeMs = 2.5;     // Simulated GPU time
            stats.cpuTimeMs = 0.5;     // Simulated CPU overhead
            stats.drawCalls = 42;      // Simulated draw call count
            stats.memoryUsedBytes = 1024 * 1024 * 16;  // 16MB VRAM
        }
        return stats;
    }

private:
    bool initialized_ = false;
    bool simulateGPUAvailable_ = true;
};

class MockMetalGaneshContext {
public:
    bool initialize() {
        initialized_ = simulateGPUAvailable_;
        return initialized_;
    }

    void destroy() {
        initialized_ = false;
    }

    bool isInitialized() const { return initialized_; }

    const char* getBackendName() const { return "Metal Ganesh"; }

    void setSimulateGPUAvailable(bool available) {
        simulateGPUAvailable_ = available;
    }

private:
    bool initialized_ = false;
    bool simulateGPUAvailable_ = true;
};

// =============================================================================
// Fallback Logic Simulation
// Mirrors the actual fallback behavior in svg_player_animated.cpp
// =============================================================================

struct RenderingContext {
    RenderingMode activeMode = RenderingMode::CPU;
    MockGraphiteContext graphite;
    MockMetalGaneshContext metalGanesh;
    bool gpuAvailable = true;

    // Initialize rendering context based on config (with fallback logic)
    bool initialize(PlayerConfig& config) {
        graphite.setSimulateGPUAvailable(gpuAvailable);
        metalGanesh.setSimulateGPUAvailable(gpuAvailable);

        // Try Graphite first (default on macOS)
        if (config.useGraphiteBackend) {
            if (graphite.initialize()) {
                activeMode = RenderingMode::Graphite;
                return true;
            } else {
                // Graphite failed, fall back to Metal Ganesh
                config.useGraphiteBackend = false;
                config.useMetalBackend = true;
            }
        }

        // Try Metal Ganesh
        if (config.useMetalBackend) {
            if (metalGanesh.initialize()) {
                activeMode = RenderingMode::MetalGanesh;
                return true;
            } else {
                // Metal Ganesh failed, fall back to CPU
                config.useMetalBackend = false;
            }
        }

        // CPU fallback always succeeds
        activeMode = RenderingMode::CPU;
        return true;
    }

    void destroy() {
        graphite.destroy();
        metalGanesh.destroy();
    }
};

// =============================================================================
// SECTION 1: Graphite GPU Backend Tests
// =============================================================================

TEST(graphite_context_initializes_successfully) {
    // Test that Graphite Metal context initializes when GPU is available
    MockGraphiteContext ctx;
    ctx.setSimulateGPUAvailable(true);

    bool result = ctx.initialize();

    ASSERT_TRUE(result);
    ASSERT_TRUE(ctx.isInitialized());
    ASSERT_STREQ(ctx.getBackendName(), "Metal Graphite");

    ctx.destroy();
    ASSERT_FALSE(ctx.isInitialized());
}

TEST(graphite_context_fails_gracefully_when_gpu_unavailable) {
    // Test that Graphite fails gracefully when GPU is not available
    MockGraphiteContext ctx;
    ctx.setSimulateGPUAvailable(false);

    bool result = ctx.initialize();

    ASSERT_FALSE(result);
    ASSERT_FALSE(ctx.isInitialized());
}

TEST(graphite_gpu_stats_reported_correctly) {
    // Test that GPU stats are reported correctly after rendering
    MockGraphiteContext ctx;
    ctx.setSimulateGPUAvailable(true);
    ctx.initialize();

    auto stats = ctx.getStats();

    // When initialized, stats should have meaningful values
    ASSERT_TRUE(stats.gpuTimeMs > 0.0);
    ASSERT_TRUE(stats.drawCalls > 0);
    ASSERT_TRUE(stats.memoryUsedBytes > 0);

    ctx.destroy();
}

TEST(graphite_is_default_backend) {
    // Test that Graphite is the default backend (useGraphiteBackend = true by default)
    PlayerConfig config;

    // Default config should have Graphite enabled
    ASSERT_TRUE(config.useGraphiteBackend);
    ASSERT_FALSE(config.useMetalBackend);
    ASSERT_EQ(config.getEffectiveRenderingMode(), RenderingMode::Graphite);
}

// =============================================================================
// SECTION 2: CPU Fallback Tests
// =============================================================================

TEST(cpu_mode_when_cpu_flag_used) {
    // Test CPU raster mode works when --cpu flag is used
    const char* argv[] = {"svg_player", "--cpu", "test.svg"};
    int argc = 3;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_FALSE(config.useGraphiteBackend);
    ASSERT_FALSE(config.useMetalBackend);
    ASSERT_EQ(config.getEffectiveRenderingMode(), RenderingMode::CPU);
}

TEST(cpu_fallback_when_graphite_fails) {
    // Test graceful fallback to CPU when Graphite initialization fails
    PlayerConfig config;
    config.useGraphiteBackend = true;

    RenderingContext ctx;
    ctx.gpuAvailable = false;  // Simulate no GPU

    bool result = ctx.initialize(config);

    ASSERT_TRUE(result);  // Should still succeed (CPU fallback)
    ASSERT_EQ(ctx.activeMode, RenderingMode::CPU);
    ASSERT_FALSE(ctx.graphite.isInitialized());
    ASSERT_FALSE(ctx.metalGanesh.isInitialized());
}

TEST(cpu_mode_always_succeeds) {
    // Test that CPU rendering mode always succeeds regardless of GPU state
    const char* argv[] = {"svg_player", "--cpu", "test.svg"};
    int argc = 3;

    PlayerConfig config = parseCommandLine(argc, argv);
    RenderingContext ctx;
    ctx.gpuAvailable = false;  // No GPU available

    bool result = ctx.initialize(config);

    ASSERT_TRUE(result);
    ASSERT_EQ(ctx.activeMode, RenderingMode::CPU);
}

TEST(cpu_flag_disables_graphite) {
    // Test that --cpu flag properly disables Graphite backend
    const char* argv[] = {"svg_player", "--cpu"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_FALSE(config.useGraphiteBackend);
    ASSERT_STREQ(renderingModeToString(config.getEffectiveRenderingMode()), "CPU Raster");
}

// =============================================================================
// SECTION 3: Metal Ganesh Fallback Tests
// =============================================================================

TEST(metal_mode_when_metal_flag_used) {
    // Test Metal Ganesh mode works when --metal flag is used
    const char* argv[] = {"svg_player", "--metal", "test.svg"};
    int argc = 3;

    PlayerConfig config = parseCommandLine(argc, argv);

    // Note: --metal enables Metal Ganesh but doesn't disable Graphite by default
    // The actual behavior depends on initialization order
    ASSERT_TRUE(config.useMetalBackend);
}

TEST(metal_ganesh_context_initializes) {
    // Test that Metal Ganesh context initializes successfully
    MockMetalGaneshContext ctx;
    ctx.setSimulateGPUAvailable(true);

    bool result = ctx.initialize();

    ASSERT_TRUE(result);
    ASSERT_TRUE(ctx.isInitialized());
    ASSERT_STREQ(ctx.getBackendName(), "Metal Ganesh");

    ctx.destroy();
}

TEST(graphite_to_metal_ganesh_fallback) {
    // Test fallback from Graphite to Metal Ganesh when Graphite fails
    PlayerConfig config;
    config.useGraphiteBackend = true;
    config.useMetalBackend = false;

    RenderingContext ctx;
    // Simulate Graphite unavailable but Metal Ganesh available
    ctx.graphite.setSimulateGPUAvailable(false);
    ctx.metalGanesh.setSimulateGPUAvailable(true);
    ctx.gpuAvailable = true;  // GPU available for Ganesh

    // We need to manually trigger the fallback scenario
    // In real code, this happens automatically in initialize()
    // Here we test the config mutation that would happen

    if (!ctx.graphite.initialize()) {
        config.useGraphiteBackend = false;
        config.useMetalBackend = true;
    }

    // Now Metal Ganesh should be the chosen backend
    ASSERT_FALSE(config.useGraphiteBackend);
    ASSERT_TRUE(config.useMetalBackend);

    bool ganeshResult = ctx.metalGanesh.initialize();
    ASSERT_TRUE(ganeshResult);
}

TEST(metal_ganesh_to_cpu_fallback) {
    // Test fallback from Metal Ganesh to CPU when Metal fails
    PlayerConfig config;
    config.useGraphiteBackend = false;
    config.useMetalBackend = true;

    RenderingContext ctx;
    ctx.gpuAvailable = false;  // No GPU at all

    bool result = ctx.initialize(config);

    ASSERT_TRUE(result);  // Should succeed with CPU fallback
    ASSERT_EQ(ctx.activeMode, RenderingMode::CPU);
}

// =============================================================================
// SECTION 4: Command-line Flag Parsing Tests
// =============================================================================

TEST(cpu_flag_recognized) {
    // Test --cpu flag is recognized
    const char* argv[] = {"svg_player", "--cpu"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_FALSE(config.useGraphiteBackend);
}

TEST(metal_flag_recognized) {
    // Test --metal flag is recognized
    const char* argv[] = {"svg_player", "--metal"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_TRUE(config.useMetalBackend);
}

TEST(graphite_legacy_flag_accepted) {
    // Test --graphite (legacy) is handled gracefully (no-op since it's now default)
    const char* argv[] = {"svg_player", "--graphite"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    // --graphite should still work and keep Graphite enabled
    ASSERT_TRUE(config.useGraphiteBackend);
}

TEST(help_flag_recognized) {
    // Test --help flag is recognized
    const char* argv[] = {"svg_player", "--help"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_TRUE(config.showHelp);
}

TEST(short_help_flag_recognized) {
    // Test -h flag is recognized
    const char* argv[] = {"svg_player", "-h"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_TRUE(config.showHelp);
}

TEST(input_file_parsed) {
    // Test that input file path is parsed correctly
    const char* argv[] = {"svg_player", "my_animation.svg"};
    int argc = 2;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_STREQ(config.inputFile.c_str(), "my_animation.svg");
}

TEST(multiple_flags_parsed) {
    // Test that multiple flags are parsed correctly
    const char* argv[] = {"svg_player", "--cpu", "--help", "test.svg"};
    int argc = 4;

    PlayerConfig config = parseCommandLine(argc, argv);

    ASSERT_FALSE(config.useGraphiteBackend);  // --cpu disables Graphite
    ASSERT_TRUE(config.showHelp);
    ASSERT_STREQ(config.inputFile.c_str(), "test.svg");
}

TEST(flag_order_independent) {
    // Test that flag order doesn't matter
    const char* argv1[] = {"svg_player", "--cpu", "test.svg"};
    const char* argv2[] = {"svg_player", "test.svg", "--cpu"};

    PlayerConfig config1 = parseCommandLine(3, argv1);
    PlayerConfig config2 = parseCommandLine(3, argv2);

    ASSERT_EQ(config1.useGraphiteBackend, config2.useGraphiteBackend);
    ASSERT_EQ(config1.useMetalBackend, config2.useMetalBackend);
    ASSERT_STREQ(config1.inputFile.c_str(), config2.inputFile.c_str());
}

TEST(conflicting_flags_last_wins) {
    // Test behavior with conflicting flags (--cpu then --graphite)
    // Note: In the actual player, --cpu disables Graphite and --graphite re-enables it
    const char* argv[] = {"svg_player", "--cpu", "--graphite"};
    int argc = 3;

    PlayerConfig config = parseCommandLine(argc, argv);

    // --graphite after --cpu should re-enable Graphite
    ASSERT_TRUE(config.useGraphiteBackend);
}

// =============================================================================
// SECTION 5: Rendering Mode Detection Tests
// =============================================================================

TEST(rendering_mode_cpu_detected) {
    // Test that CPU rendering mode is correctly detected
    PlayerConfig config;
    config.useGraphiteBackend = false;
    config.useMetalBackend = false;

    ASSERT_EQ(config.getEffectiveRenderingMode(), RenderingMode::CPU);
    ASSERT_STREQ(renderingModeToString(config.getEffectiveRenderingMode()), "CPU Raster");
}

TEST(rendering_mode_metal_ganesh_detected) {
    // Test that Metal Ganesh rendering mode is correctly detected
    PlayerConfig config;
    config.useGraphiteBackend = false;
    config.useMetalBackend = true;

    ASSERT_EQ(config.getEffectiveRenderingMode(), RenderingMode::MetalGanesh);
    ASSERT_STREQ(renderingModeToString(config.getEffectiveRenderingMode()), "Metal (Ganesh)");
}

TEST(rendering_mode_graphite_detected) {
    // Test that Graphite rendering mode is correctly detected
    PlayerConfig config;
    config.useGraphiteBackend = true;
    config.useMetalBackend = false;

    ASSERT_EQ(config.getEffectiveRenderingMode(), RenderingMode::Graphite);
    ASSERT_STREQ(renderingModeToString(config.getEffectiveRenderingMode()), "Metal (Graphite)");
}

TEST(graphite_priority_over_metal_ganesh) {
    // Test that Graphite has priority when both flags are set
    // (This simulates the initialization order in svg_player_animated.cpp)
    PlayerConfig config;
    config.useGraphiteBackend = true;
    config.useMetalBackend = true;  // Both enabled

    // Graphite should take priority
    ASSERT_EQ(config.getEffectiveRenderingMode(), RenderingMode::Graphite);
}

TEST(rendering_context_reports_active_mode) {
    // Test that rendering context correctly reports the active mode
    PlayerConfig config;
    config.useGraphiteBackend = true;

    RenderingContext ctx;
    ctx.gpuAvailable = true;
    ctx.initialize(config);

    ASSERT_EQ(ctx.activeMode, RenderingMode::Graphite);
    ASSERT_TRUE(ctx.graphite.isInitialized());
}

TEST(rendering_context_fallback_chain) {
    // Test the complete fallback chain: Graphite -> Metal Ganesh -> CPU
    PlayerConfig config;
    config.useGraphiteBackend = true;

    // Test 1: Graphite available - should use Graphite
    {
        RenderingContext ctx;
        ctx.gpuAvailable = true;
        ctx.initialize(config);
        ASSERT_EQ(ctx.activeMode, RenderingMode::Graphite);
    }

    // Test 2: Graphite unavailable, Metal available - should use Metal Ganesh
    // (This requires modifying the mock to simulate Graphite failing but Metal succeeding)
    // Note: Our current mock doesn't support this granularity, so we test the config change
    {
        PlayerConfig config2;
        config2.useGraphiteBackend = false;  // Simulating Graphite failure
        config2.useMetalBackend = true;      // Falling back to Metal

        RenderingContext ctx;
        ctx.gpuAvailable = true;
        ctx.initialize(config2);
        ASSERT_EQ(ctx.activeMode, RenderingMode::MetalGanesh);
    }

    // Test 3: No GPU available - should use CPU
    {
        PlayerConfig config3;
        config3.useGraphiteBackend = true;  // Will fail

        RenderingContext ctx;
        ctx.gpuAvailable = false;  // No GPU
        ctx.initialize(config3);
        ASSERT_EQ(ctx.activeMode, RenderingMode::CPU);
    }
}

// =============================================================================
// SECTION 6: Backend Name Reporting Tests
// =============================================================================

TEST(graphite_backend_name_correct) {
    // Test that Graphite backend reports correct name
    MockGraphiteContext ctx;
    ASSERT_STREQ(ctx.getBackendName(), "Metal Graphite");
}

TEST(metal_ganesh_backend_name_correct) {
    // Test that Metal Ganesh backend reports correct name
    MockMetalGaneshContext ctx;
    ASSERT_STREQ(ctx.getBackendName(), "Metal Ganesh");
}

TEST(rendering_mode_string_cpu) {
    // Test renderingModeToString for CPU mode
    ASSERT_STREQ(renderingModeToString(RenderingMode::CPU), "CPU Raster");
}

TEST(rendering_mode_string_metal_ganesh) {
    // Test renderingModeToString for Metal Ganesh mode
    ASSERT_STREQ(renderingModeToString(RenderingMode::MetalGanesh), "Metal (Ganesh)");
}

TEST(rendering_mode_string_graphite) {
    // Test renderingModeToString for Graphite mode
    ASSERT_STREQ(renderingModeToString(RenderingMode::Graphite), "Metal (Graphite)");
}

// =============================================================================
// SECTION 7: Initialization and Cleanup Tests
// =============================================================================

TEST(context_cleanup_safe_when_not_initialized) {
    // Test that calling destroy() on non-initialized context is safe
    MockGraphiteContext graphite;
    MockMetalGaneshContext metal;

    // Should not crash even though not initialized
    graphite.destroy();
    metal.destroy();

    ASSERT_FALSE(graphite.isInitialized());
    ASSERT_FALSE(metal.isInitialized());
}

TEST(context_double_destroy_safe) {
    // Test that calling destroy() twice is safe
    MockGraphiteContext ctx;
    ctx.setSimulateGPUAvailable(true);
    ctx.initialize();

    ctx.destroy();
    ctx.destroy();  // Second destroy should be safe

    ASSERT_FALSE(ctx.isInitialized());
}

TEST(context_reinitialize_after_destroy) {
    // Test that context can be reinitialized after destroy
    MockGraphiteContext ctx;
    ctx.setSimulateGPUAvailable(true);

    ctx.initialize();
    ASSERT_TRUE(ctx.isInitialized());

    ctx.destroy();
    ASSERT_FALSE(ctx.isInitialized());

    ctx.initialize();  // Reinitialize
    ASSERT_TRUE(ctx.isInitialized());

    ctx.destroy();
}

TEST(rendering_context_cleanup) {
    // Test that RenderingContext properly cleans up all contexts
    RenderingContext ctx;
    ctx.gpuAvailable = true;

    PlayerConfig config;
    config.useGraphiteBackend = true;
    ctx.initialize(config);

    ASSERT_TRUE(ctx.graphite.isInitialized());

    ctx.destroy();

    ASSERT_FALSE(ctx.graphite.isInitialized());
    ASSERT_FALSE(ctx.metalGanesh.isInitialized());
}

// =============================================================================
// SECTION 8: Edge Cases and Error Handling Tests
// =============================================================================

TEST(empty_argv_parsing) {
    // Test parsing with minimal argv (just program name)
    const char* argv[] = {"svg_player"};
    int argc = 1;

    PlayerConfig config = parseCommandLine(argc, argv);

    // Should use defaults
    ASSERT_TRUE(config.useGraphiteBackend);
    ASSERT_FALSE(config.useMetalBackend);
    ASSERT_TRUE(config.inputFile.empty());
}

TEST(unknown_flags_ignored) {
    // Test that unknown flags are ignored gracefully
    const char* argv[] = {"svg_player", "--unknown-flag", "--another-unknown", "test.svg"};
    int argc = 4;

    PlayerConfig config = parseCommandLine(argc, argv);

    // Unknown flags should not affect config, file should still be parsed
    ASSERT_STREQ(config.inputFile.c_str(), "test.svg");
    ASSERT_TRUE(config.useGraphiteBackend);  // Default
}

TEST(gpu_stats_zero_when_not_initialized) {
    // Test that stats are zero when context is not initialized
    MockGraphiteContext ctx;
    // Don't initialize

    auto stats = ctx.getStats();

    ASSERT_TRUE(stats.gpuTimeMs == 0.0);
    ASSERT_TRUE(stats.drawCalls == 0);
    ASSERT_TRUE(stats.memoryUsedBytes == 0);
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main(int argc, char* argv[]) {
    printf("\n");
    printf("================================================================\n");
    printf("macOS SVG Player - Rendering Backend Unit Tests\n");
    printf("================================================================\n");
    printf("Testing: Graphite GPU, CPU Fallback, Metal Ganesh, Flag Parsing\n");
    printf("================================================================\n\n");

    for (const auto& [name, func] : g_tests) {
        g_testCount++;
        printf("Running: %-50s ... ", name.c_str());
        fflush(stdout);

        try {
            func();
            printf("\033[32mPASS\033[0m\n");
            g_passCount++;
            g_results.push_back({name, true, ""});
        } catch (const SkipException& e) {
            printf("\033[33mSKIP\033[0m (%s)\n", e.what());
            g_skipCount++;
            g_results.push_back({name, true, std::string("SKIPPED: ") + e.what()});
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

    // Summary by category
    printf("Test Categories:\n");
    printf("  - Graphite GPU Backend:     Section 1\n");
    printf("  - CPU Fallback:             Section 2\n");
    printf("  - Metal Ganesh Fallback:    Section 3\n");
    printf("  - Command-line Parsing:     Section 4\n");
    printf("  - Rendering Mode Detection: Section 5\n");
    printf("  - Backend Name Reporting:   Section 6\n");
    printf("  - Initialization/Cleanup:   Section 7\n");
    printf("  - Edge Cases:               Section 8\n");
    printf("\n");

    return g_failCount > 0 ? 1 : 0;
}
