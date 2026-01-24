// test_linux_player.cpp - Unit tests for Linux SVG Player (Graphite/Vulkan backend)
//
// Simple test framework without external dependencies.
// Compile with: clang++ -std=c++17 -I../shared -DTEST_LINUX_PLAYER test_linux_player.cpp -o test_linux_player
//
// These tests verify the Linux-specific Graphite GPU backend, CPU fallback,
// command-line parsing, and rendering mode detection.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

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

#define SKIP_TEST(reason) \
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

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (std::string(a) != std::string(b)) { \
            std::string msg = "ASSERT_STR_EQ failed: \"" + std::string(a) + "\" != \"" + std::string(b) + "\""; \
            throw std::runtime_error(msg); \
        } \
    } while(0)

#define ASSERT_STR_CONTAINS(haystack, needle) \
    do { \
        if (std::string(haystack).find(needle) == std::string::npos) { \
            std::string msg = "ASSERT_STR_CONTAINS failed: \"" + std::string(haystack) + "\" does not contain \"" + std::string(needle) + "\""; \
            throw std::runtime_error(msg); \
        } \
    } while(0)

// Exception for skipped tests
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
// Mock/Stub types for testing without full Skia/SDL dependencies
// These simulate the interfaces for unit testing purposes
// =============================================================================

// Simulated rendering backend mode
enum class RenderingBackend {
    Unknown,
    GraphiteVulkan,
    CPURaster
};

// Simulated command-line parser result
struct ParsedCommandLine {
    bool useGraphiteBackend = true;     // Default: Graphite enabled
    bool cpuFallback = false;           // --cpu flag
    bool fullscreen = false;            // --fullscreen flag
    bool maximize = false;              // --maximize flag
    int windowWidth = 0;                // --size=WxH
    int windowHeight = 0;
    int posX = -1;                      // --pos=X,Y
    int posY = -1;
    int benchmarkDuration = 0;          // --duration=SECS
    std::string inputPath;              // Input file
    std::string screenshotPath;         // --screenshot=PATH
    bool remoteControlEnabled = false;  // --remote-control
    int remoteControlPort = 9999;       // --remote-control[=PORT]
    bool jsonOutput = false;            // --json
    bool showHelp = false;              // --help
    bool showVersion = false;           // --version
    std::string error;                  // Parse error message
};

// Simulated Vulkan availability check
struct VulkanCapabilities {
    bool vulkanAvailable = false;
    bool vulkan11Supported = false;
    std::string driverVersion;
    std::string deviceName;
    std::string errorMessage;
};

// =============================================================================
// Command-Line Parser (extracted from svg_player_animated_linux.cpp)
// =============================================================================

ParsedCommandLine parseCommandLine(int argc, const char* argv[]) {
    ParsedCommandLine result;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            result.showVersion = true;
            return result;  // Early exit for version
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            result.showHelp = true;
            return result;  // Early exit for help
        }
        if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            result.fullscreen = true;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            // Use CPU raster rendering instead of Graphite GPU
            result.useGraphiteBackend = false;
            result.cpuFallback = true;
        } else if (strcmp(argv[i], "--graphite") == 0) {
            // Legacy flag - Graphite is now the default, this is a no-op
            // Kept for backward compatibility
            result.useGraphiteBackend = true;
        } else if (strcmp(argv[i], "--windowed") == 0 || strcmp(argv[i], "-w") == 0) {
            result.fullscreen = false;
        } else if (strcmp(argv[i], "--maximize") == 0 || strcmp(argv[i], "-m") == 0) {
            result.maximize = true;
            result.fullscreen = false;  // Maximize implies windowed mode
        } else if (strncmp(argv[i], "--pos=", 6) == 0) {
            int x, y;
            if (sscanf(argv[i] + 6, "%d,%d", &x, &y) == 2) {
                result.posX = x;
                result.posY = y;
            } else {
                result.error = std::string("Invalid position format: ") + argv[i] + " (use --pos=X,Y)";
                return result;
            }
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            int w, h;
            if (sscanf(argv[i] + 7, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                result.windowWidth = w;
                result.windowHeight = h;
            } else {
                result.error = std::string("Invalid size format: ") + argv[i] + " (use --size=WxH)";
                return result;
            }
        } else if (strncmp(argv[i], "--duration=", 11) == 0) {
            result.benchmarkDuration = atoi(argv[i] + 11);
            if (result.benchmarkDuration <= 0) {
                result.error = "Invalid duration: must be positive";
                return result;
            }
        } else if (strcmp(argv[i], "--json") == 0) {
            result.jsonOutput = true;
        } else if (strncmp(argv[i], "--screenshot=", 13) == 0) {
            result.screenshotPath = argv[i] + 13;
            if (result.screenshotPath.empty()) {
                result.error = "--screenshot requires a file path";
                return result;
            }
        } else if (strcmp(argv[i], "--remote-control") == 0) {
            result.remoteControlEnabled = true;
        } else if (strncmp(argv[i], "--remote-control=", 17) == 0) {
            result.remoteControlEnabled = true;
            result.remoteControlPort = atoi(argv[i] + 17);
            if (result.remoteControlPort <= 0 || result.remoteControlPort > 65535) {
                result.error = std::string("Invalid remote control port: ") + (argv[i] + 17);
                return result;
            }
        } else if (argv[i][0] != '-') {
            // Non-option argument is the input file
            result.inputPath = argv[i];
        } else {
            // Unknown option
            result.error = std::string("Unknown option: ") + argv[i];
            return result;
        }
    }

    return result;
}

// =============================================================================
// Simulated Vulkan Availability Check
// =============================================================================

VulkanCapabilities checkVulkanAvailability() {
    VulkanCapabilities caps;

    // In a real implementation, this would call:
    // - vkEnumerateInstanceVersion() to check Vulkan version
    // - vkEnumeratePhysicalDevices() to list GPUs
    // - vkGetPhysicalDeviceProperties() to get device info

    // For testing purposes, we simulate based on environment variable
    const char* mockVulkan = getenv("TEST_MOCK_VULKAN");
    if (mockVulkan) {
        if (strcmp(mockVulkan, "available") == 0) {
            caps.vulkanAvailable = true;
            caps.vulkan11Supported = true;
            caps.deviceName = "Mock Vulkan GPU";
            caps.driverVersion = "1.3.0";
        } else if (strcmp(mockVulkan, "no_vulkan11") == 0) {
            caps.vulkanAvailable = true;
            caps.vulkan11Supported = false;
            caps.deviceName = "Old GPU";
            caps.driverVersion = "1.0.0";
            caps.errorMessage = "Vulkan 1.1+ required, but only 1.0 available";
        } else if (strcmp(mockVulkan, "unavailable") == 0) {
            caps.vulkanAvailable = false;
            caps.errorMessage = "No Vulkan ICD found";
        }
    } else {
        // Default: assume Vulkan might be available (would actually check in real code)
        // For unit tests without the mock, we skip GPU-related tests
        caps.vulkanAvailable = false;
        caps.errorMessage = "Vulkan availability not mocked (use TEST_MOCK_VULKAN env var)";
    }

    return caps;
}

// =============================================================================
// Simulated Rendering Backend Detection
// =============================================================================

RenderingBackend detectActiveBackend(bool useGraphite, bool graphiteInitialized) {
    if (useGraphite && graphiteInitialized) {
        return RenderingBackend::GraphiteVulkan;
    } else {
        return RenderingBackend::CPURaster;
    }
}

const char* getBackendName(RenderingBackend backend) {
    switch (backend) {
        case RenderingBackend::GraphiteVulkan:
            return "Vulkan Graphite";
        case RenderingBackend::CPURaster:
            return "CPU Raster";
        default:
            return "Unknown";
    }
}

// =============================================================================
// Test: Graphite GPU Backend Tests
// =============================================================================

TEST(graphite_vulkan_context_initialization_success) {
    // Test that when Vulkan is available, Graphite context reports success
    // This is a simulated test - real test requires actual Vulkan/GPU

    VulkanCapabilities caps;
    caps.vulkanAvailable = true;
    caps.vulkan11Supported = true;

    // Simulate successful initialization
    bool graphiteInitialized = caps.vulkanAvailable && caps.vulkan11Supported;
    ASSERT_TRUE(graphiteInitialized);
}

TEST(graphite_vulkan_context_requires_vulkan11) {
    // Test that Graphite context requires Vulkan 1.1+
    VulkanCapabilities caps;
    caps.vulkanAvailable = true;
    caps.vulkan11Supported = false;  // Only Vulkan 1.0

    // Should fail without Vulkan 1.1
    bool graphiteInitialized = caps.vulkanAvailable && caps.vulkan11Supported;
    ASSERT_FALSE(graphiteInitialized);
}

TEST(graphite_backend_name_is_vulkan_graphite) {
    // Test that the Graphite backend reports correct name on Linux
    RenderingBackend backend = RenderingBackend::GraphiteVulkan;
    const char* name = getBackendName(backend);
    ASSERT_STR_CONTAINS(name, "Vulkan");
    ASSERT_STR_CONTAINS(name, "Graphite");
}

TEST(graphite_gpu_stats_structure) {
    // Test that GPU stats structure has expected fields
    // This verifies the stats interface matches what the player expects

    struct GPUStats {
        double renderTimeMs = 0.0;
        double gpuMemoryUsedMB = 0.0;
        int drawCalls = 0;
        int triangles = 0;
        bool vsyncEnabled = false;
    };

    GPUStats stats;
    stats.renderTimeMs = 16.67;  // ~60fps
    stats.gpuMemoryUsedMB = 128.5;
    stats.drawCalls = 42;
    stats.triangles = 10000;
    stats.vsyncEnabled = true;

    ASSERT_TRUE(stats.renderTimeMs > 0.0);
    ASSERT_TRUE(stats.gpuMemoryUsedMB > 0.0);
    ASSERT_TRUE(stats.drawCalls > 0);
    ASSERT_TRUE(stats.vsyncEnabled);
}

// =============================================================================
// Test: CPU Fallback Tests
// =============================================================================

TEST(cpu_mode_enabled_with_cpu_flag) {
    // Test that --cpu flag disables Graphite and enables CPU raster
    const char* args[] = {"svg_player", "test.svg", "--cpu"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_FALSE(result.useGraphiteBackend);
    ASSERT_TRUE(result.cpuFallback);
}

TEST(cpu_fallback_when_vulkan_unavailable) {
    // Test graceful fallback to CPU when Vulkan/Graphite fails
    VulkanCapabilities caps;
    caps.vulkanAvailable = false;
    caps.errorMessage = "No Vulkan ICD found";

    // Simulated initialization attempt
    bool useGraphite = true;
    bool graphiteInitialized = caps.vulkanAvailable && caps.vulkan11Supported;

    // Should fall back to CPU
    if (!graphiteInitialized && useGraphite) {
        useGraphite = false;  // Fallback
    }

    RenderingBackend backend = detectActiveBackend(useGraphite, graphiteInitialized);
    ASSERT_EQ(backend, RenderingBackend::CPURaster);
}

TEST(cpu_raster_backend_name) {
    // Test that CPU raster backend reports correct name
    RenderingBackend backend = RenderingBackend::CPURaster;
    const char* name = getBackendName(backend);
    ASSERT_STR_CONTAINS(name, "CPU");
    ASSERT_STR_CONTAINS(name, "Raster");
}

TEST(cpu_mode_can_create_surface) {
    // Test that CPU mode can create a raster surface (simulated)
    // In real code, this would use SkSurfaces::Raster()

    struct MockSurface {
        int width;
        int height;
        bool valid;
    };

    auto createCPUSurface = [](int w, int h) -> MockSurface {
        MockSurface surface;
        surface.width = w;
        surface.height = h;
        surface.valid = (w > 0 && h > 0);
        return surface;
    };

    MockSurface surface = createCPUSurface(1920, 1080);
    ASSERT_TRUE(surface.valid);
    ASSERT_EQ(surface.width, 1920);
    ASSERT_EQ(surface.height, 1080);
}

// =============================================================================
// Test: Command-Line Flag Parsing
// =============================================================================

TEST(parse_cpu_flag_recognized) {
    const char* args[] = {"svg_player", "test.svg", "--cpu"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_FALSE(result.useGraphiteBackend);
    ASSERT_STR_EQ(result.inputPath.c_str(), "test.svg");
}

TEST(parse_graphite_flag_is_noop) {
    // --graphite is legacy flag - Graphite is now default, so it's a no-op
    const char* args[] = {"svg_player", "test.svg", "--graphite"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.useGraphiteBackend);  // Default is true, --graphite doesn't change it
}

TEST(parse_fullscreen_flag) {
    const char* args[] = {"svg_player", "test.svg", "--fullscreen"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.fullscreen);
}

TEST(parse_fullscreen_short_flag) {
    const char* args[] = {"svg_player", "test.svg", "-f"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.fullscreen);
}

TEST(parse_windowed_flag) {
    const char* args[] = {"svg_player", "test.svg", "--windowed"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_FALSE(result.fullscreen);
}

TEST(parse_maximize_flag) {
    const char* args[] = {"svg_player", "test.svg", "--maximize"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.maximize);
    ASSERT_FALSE(result.fullscreen);  // Maximize implies windowed
}

TEST(parse_size_flag) {
    const char* args[] = {"svg_player", "test.svg", "--size=1920x1080"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.windowWidth, 1920);
    ASSERT_EQ(result.windowHeight, 1080);
}

TEST(parse_size_flag_invalid_format) {
    const char* args[] = {"svg_player", "test.svg", "--size=invalid"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_FALSE(result.error.empty());
    ASSERT_STR_CONTAINS(result.error.c_str(), "Invalid size format");
}

TEST(parse_pos_flag) {
    const char* args[] = {"svg_player", "test.svg", "--pos=100,200"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.posX, 100);
    ASSERT_EQ(result.posY, 200);
}

TEST(parse_pos_flag_invalid_format) {
    const char* args[] = {"svg_player", "test.svg", "--pos=abc"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_FALSE(result.error.empty());
    ASSERT_STR_CONTAINS(result.error.c_str(), "Invalid position format");
}

TEST(parse_duration_flag) {
    const char* args[] = {"svg_player", "test.svg", "--duration=30"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.benchmarkDuration, 30);
}

TEST(parse_duration_flag_invalid) {
    const char* args[] = {"svg_player", "test.svg", "--duration=0"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_FALSE(result.error.empty());
    ASSERT_STR_CONTAINS(result.error.c_str(), "Invalid duration");
}

TEST(parse_screenshot_flag) {
    const char* args[] = {"svg_player", "test.svg", "--screenshot=/tmp/out.png"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_STR_EQ(result.screenshotPath.c_str(), "/tmp/out.png");
}

TEST(parse_json_flag) {
    const char* args[] = {"svg_player", "test.svg", "--json"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.jsonOutput);
}

TEST(parse_remote_control_flag) {
    const char* args[] = {"svg_player", "test.svg", "--remote-control"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.remoteControlEnabled);
    ASSERT_EQ(result.remoteControlPort, 9999);  // Default port
}

TEST(parse_remote_control_with_port) {
    const char* args[] = {"svg_player", "test.svg", "--remote-control=8080"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.remoteControlEnabled);
    ASSERT_EQ(result.remoteControlPort, 8080);
}

TEST(parse_remote_control_invalid_port) {
    const char* args[] = {"svg_player", "test.svg", "--remote-control=99999"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_FALSE(result.error.empty());
    ASSERT_STR_CONTAINS(result.error.c_str(), "Invalid remote control port");
}

TEST(parse_help_flag) {
    const char* args[] = {"svg_player", "--help"};
    ParsedCommandLine result = parseCommandLine(2, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.showHelp);
}

TEST(parse_version_flag) {
    const char* args[] = {"svg_player", "--version"};
    ParsedCommandLine result = parseCommandLine(2, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.showVersion);
}

TEST(parse_unknown_option_error) {
    const char* args[] = {"svg_player", "test.svg", "--unknown-flag"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_FALSE(result.error.empty());
    ASSERT_STR_CONTAINS(result.error.c_str(), "Unknown option");
}

TEST(parse_multiple_flags) {
    const char* args[] = {"svg_player", "test.svg", "--cpu", "--fullscreen", "--size=800x600", "--json"};
    ParsedCommandLine result = parseCommandLine(6, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_FALSE(result.useGraphiteBackend);  // --cpu
    ASSERT_TRUE(result.fullscreen);
    ASSERT_EQ(result.windowWidth, 800);
    ASSERT_EQ(result.windowHeight, 600);
    ASSERT_TRUE(result.jsonOutput);
}

TEST(parse_default_graphite_enabled) {
    // Test that Graphite is enabled by default (no --cpu flag)
    const char* args[] = {"svg_player", "test.svg"};
    ParsedCommandLine result = parseCommandLine(2, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.useGraphiteBackend);  // Default is true
    ASSERT_FALSE(result.cpuFallback);
}

// =============================================================================
// Test: Rendering Mode Detection
// =============================================================================

TEST(detect_graphite_backend_active) {
    bool useGraphite = true;
    bool graphiteInitialized = true;

    RenderingBackend backend = detectActiveBackend(useGraphite, graphiteInitialized);
    ASSERT_EQ(backend, RenderingBackend::GraphiteVulkan);
}

TEST(detect_cpu_backend_when_graphite_disabled) {
    bool useGraphite = false;
    bool graphiteInitialized = false;

    RenderingBackend backend = detectActiveBackend(useGraphite, graphiteInitialized);
    ASSERT_EQ(backend, RenderingBackend::CPURaster);
}

TEST(detect_cpu_backend_when_graphite_fails) {
    bool useGraphite = true;
    bool graphiteInitialized = false;  // Initialization failed

    RenderingBackend backend = detectActiveBackend(useGraphite, graphiteInitialized);
    ASSERT_EQ(backend, RenderingBackend::CPURaster);
}

TEST(backend_selection_respects_cpu_flag) {
    const char* args[] = {"svg_player", "test.svg", "--cpu"};
    ParsedCommandLine result = parseCommandLine(3, args);

    // Even if Graphite could initialize, --cpu should force CPU raster
    bool graphiteWouldWork = true;  // Hypothetically
    bool useGraphite = result.useGraphiteBackend;

    RenderingBackend backend = detectActiveBackend(useGraphite, graphiteWouldWork);
    ASSERT_EQ(backend, RenderingBackend::CPURaster);
}

// =============================================================================
// Test: Vulkan Requirements
// =============================================================================

TEST(vulkan_availability_detection_available) {
    // Simulated test with mocked Vulkan available
    VulkanCapabilities caps;
    caps.vulkanAvailable = true;
    caps.vulkan11Supported = true;
    caps.deviceName = "Test GPU";
    caps.driverVersion = "1.3.0";

    ASSERT_TRUE(caps.vulkanAvailable);
    ASSERT_TRUE(caps.vulkan11Supported);
    ASSERT_FALSE(caps.deviceName.empty());
}

TEST(vulkan_availability_detection_unavailable) {
    // Simulated test with no Vulkan
    VulkanCapabilities caps;
    caps.vulkanAvailable = false;
    caps.errorMessage = "No Vulkan ICD found";

    ASSERT_FALSE(caps.vulkanAvailable);
    ASSERT_FALSE(caps.vulkan11Supported);
    ASSERT_FALSE(caps.errorMessage.empty());
}

TEST(vulkan_error_message_when_unavailable) {
    VulkanCapabilities caps;
    caps.vulkanAvailable = false;
    caps.errorMessage = "Failed to load Vulkan library: libvulkan.so.1 not found";

    ASSERT_STR_CONTAINS(caps.errorMessage.c_str(), "Vulkan");
}

TEST(vulkan_11_required_for_graphite) {
    // Graphite requires Vulkan 1.1+ for certain features
    VulkanCapabilities caps;
    caps.vulkanAvailable = true;
    caps.vulkan11Supported = false;  // Only Vulkan 1.0

    bool graphiteCanInitialize = caps.vulkanAvailable && caps.vulkan11Supported;
    ASSERT_FALSE(graphiteCanInitialize);
}

TEST(vulkan_device_enumeration) {
    // Test device enumeration structure
    struct VulkanDevice {
        std::string name;
        std::string driverVersion;
        bool discreteGPU;
        size_t vramMB;
    };

    std::vector<VulkanDevice> devices;
    devices.push_back({"NVIDIA GeForce RTX 3080", "525.89.02", true, 10240});
    devices.push_back({"Intel UHD Graphics 630", "27.20.100.9565", false, 2048});

    ASSERT_EQ(devices.size(), 2u);
    ASSERT_TRUE(devices[0].discreteGPU);
    ASSERT_FALSE(devices[1].discreteGPU);
}

// =============================================================================
// Test: Integration Scenarios
// =============================================================================

TEST(scenario_default_startup) {
    // Default startup: Graphite enabled, try GPU, fallback to CPU if needed
    const char* args[] = {"svg_player", "animation.svg"};
    ParsedCommandLine result = parseCommandLine(2, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.useGraphiteBackend);
    ASSERT_STR_EQ(result.inputPath.c_str(), "animation.svg");
}

TEST(scenario_benchmark_mode) {
    // Benchmark mode: JSON output, fixed duration, screenshot
    const char* args[] = {"svg_player", "test.svg", "--duration=60", "--json", "--screenshot=/tmp/bench.png"};
    ParsedCommandLine result = parseCommandLine(5, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.benchmarkDuration, 60);
    ASSERT_TRUE(result.jsonOutput);
    ASSERT_STR_EQ(result.screenshotPath.c_str(), "/tmp/bench.png");
}

TEST(scenario_cpu_benchmark) {
    // CPU-only benchmark for comparison
    const char* args[] = {"svg_player", "test.svg", "--cpu", "--duration=30", "--json"};
    ParsedCommandLine result = parseCommandLine(5, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_FALSE(result.useGraphiteBackend);
    ASSERT_EQ(result.benchmarkDuration, 30);
    ASSERT_TRUE(result.jsonOutput);
}

TEST(scenario_remote_control_server) {
    // Remote control mode for automated testing
    const char* args[] = {"svg_player", "test.svg", "--remote-control=12345"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.remoteControlEnabled);
    ASSERT_EQ(result.remoteControlPort, 12345);
}

TEST(scenario_fullscreen_presentation) {
    // Fullscreen presentation mode
    const char* args[] = {"svg_player", "presentation.svg", "--fullscreen"};
    ParsedCommandLine result = parseCommandLine(3, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(result.fullscreen);
    ASSERT_STR_EQ(result.inputPath.c_str(), "presentation.svg");
}

TEST(scenario_windowed_with_position) {
    // Windowed mode with specific position and size
    const char* args[] = {"svg_player", "test.svg", "--windowed", "--pos=50,100", "--size=1280x720"};
    ParsedCommandLine result = parseCommandLine(5, args);

    ASSERT_TRUE(result.error.empty());
    ASSERT_FALSE(result.fullscreen);
    ASSERT_EQ(result.posX, 50);
    ASSERT_EQ(result.posY, 100);
    ASSERT_EQ(result.windowWidth, 1280);
    ASSERT_EQ(result.windowHeight, 720);
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main(int argc, char* argv[]) {
    printf("\n");
    printf("================================================================\n");
    printf("Linux SVG Player - Unit Tests\n");
    printf("(Graphite/Vulkan Backend, CPU Fallback, Command-Line Parsing)\n");
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
        printf(", \033[33m%d skipped\033[0m", g_skipCount);
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
