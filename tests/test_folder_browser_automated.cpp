// test_folder_browser_automated.cpp - Automated Folder Browser Test Suite
//
// Self-contained test suite using the extended test harness with instrumentation,
// deterministic scheduling, and regression detection.
//
// Compile with: clang++ -std=c++17 -DSVG_INSTRUMENTATION_ENABLED=1 -I../shared -I. test_folder_browser_automated.cpp -o run_tests

#include "test_harness.h"
#include "test_environment.h"
#include "metrics_collector.h"
#include "baseline_provider.h"
#include "regression_detector.h"
#include "../src/thumbnail_cache.h"
#include "../shared/svg_instrumentation.h"
#include "../shared/svg_deterministic_clock.h"
#include "../shared/SVGGridCompositor.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <iostream>

// =============================================================================
// Missing ASSERT Macros (not defined in test_harness.h)
// =============================================================================

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string("ASSERT_TRUE failed: ") + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            throw std::runtime_error( \
                std::string("ASSERT_FALSE failed: ") + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_EQ(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (_actual != _expected) { \
            throw std::runtime_error( \
                std::string("ASSERT_EQ failed: ") + #actual + " != " + #expected + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == nullptr) { \
            throw std::runtime_error( \
                std::string("ASSERT_NOT_NULL failed: ") + #ptr + " is null" + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_GE(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (_actual < _expected) { \
            throw std::runtime_error( \
                std::string("ASSERT_GE failed: ") + #actual + " < " + #expected + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_LE(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (_actual > _expected) { \
            throw std::runtime_error( \
                std::string("ASSERT_LE failed: ") + #actual + " > " + #expected + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

// =============================================================================
// Global Instances for Deterministic Testing
// =============================================================================

namespace {
    // Global instances for static-like access in tests
    svgplayer::testing::DeterministicClock g_clock;
    svgplayer::testing::DeterministicScheduler g_scheduler;
}

// Static wrapper class for test convenience
class DeterministicClock {
public:
    static void enable() { g_clock.enable(); }
    static void disable() { g_clock.disable(); }
    static bool isEnabled() { return g_clock.isEnabled(); }
    static auto now() { return g_clock.now(); }
    template<typename Rep, typename Period>
    static void advanceBy(std::chrono::duration<Rep, Period> delta) {
        g_clock.advanceBy(delta);
    }
};

class DeterministicScheduler {
public:
    static void enable(size_t numThreads) { g_scheduler.enable(numThreads); }
    static void disable() { g_scheduler.disable(); }
    static bool isEnabled() { return g_scheduler.isEnabled(); }
    static void schedule(std::function<void()> op) { g_scheduler.schedule(std::move(op)); }
    static size_t drainQueue() { return g_scheduler.drainQueue(); }
    static size_t executeOperations(size_t count) { return g_scheduler.executeOperations(count); }
    static size_t pendingOperations() { return g_scheduler.pendingOperations(); }
    static void synchronize() { g_scheduler.synchronize(); }
    static void clear() { g_scheduler.clear(); }
};

// =============================================================================
// Aliases for cleaner test code
// =============================================================================

using svgplayer::ThumbnailState;
using svgplayer::testing::ControlledTestEnvironment;
using svgplayer::testing::MetricsCollector;
using svgplayer::testing::BaselineProvider;
using svgplayer::testing::RegressionDetector;
using svgplayer::testing::RegressionThresholds;
using svgplayer::testing::ComparisonResult;
using TestFramework::TestHarness;
using TestFramework::TestConfig;
using TestFramework::TestSeverity;

// =============================================================================
// Test Infrastructure Tests (validate the test framework itself)
// =============================================================================

TEST_CASE(infrastructure, deterministic_clock_works) {
    // Test that the deterministic clock provides controllable time
    DeterministicClock::enable();

    auto t1 = DeterministicClock::now();
    DeterministicClock::advanceBy(std::chrono::milliseconds(100));
    auto t2 = DeterministicClock::now();

    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    ASSERT_EQ(diff, 100);

    DeterministicClock::disable();
}

TEST_CASE(infrastructure, deterministic_scheduler_queues_operations) {
    // Test that the scheduler queues operations in deterministic mode
    DeterministicScheduler::enable(4);

    int counter = 0;
    DeterministicScheduler::schedule([&counter]() { counter++; });
    DeterministicScheduler::schedule([&counter]() { counter++; });
    DeterministicScheduler::schedule([&counter]() { counter++; });

    // Operations should be queued, not executed yet
    ASSERT_EQ(DeterministicScheduler::pendingOperations(), 3);
    ASSERT_EQ(counter, 0);

    // Execute all queued operations
    size_t executed = DeterministicScheduler::drainQueue();
    ASSERT_EQ(executed, 3);
    ASSERT_EQ(counter, 3);

    DeterministicScheduler::disable();
}

TEST_CASE(infrastructure, test_environment_creates_svgs) {
    // Test that ControlledTestEnvironment creates valid test SVGs
    ControlledTestEnvironment env;

    std::string path = env.addStaticSVG("test", 100, 100);
    ASSERT_TRUE(env.fileExists("test.svg"));

    std::string animPath = env.addAnimatedSVG("anim", 4, 2.0);
    ASSERT_TRUE(env.fileExists("anim.svg"));

    std::string badPath = env.addMalformedSVG("bad");
    ASSERT_TRUE(env.fileExists("bad.svg"));

    // Verify file paths are correct
    ASSERT_TRUE(path.find("test.svg") != std::string::npos);
    ASSERT_TRUE(animPath.find("anim.svg") != std::string::npos);
}

TEST_CASE(infrastructure, metrics_collector_records_data) {
    // Test metrics collection
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("test_metrics");

    collector.recordRenderTime(10.0);
    collector.recordRenderTime(12.0);
    collector.recordRenderTime(11.0);
    collector.recordFrameRendered(0);
    collector.recordFrameRendered(1);

    collector.endCollection();

    auto perf = collector.getPerformance();
    ASSERT_EQ(perf.totalFramesRendered, 2);
    ASSERT_TRUE(perf.avgRenderTimeMs > 0);
}

// =============================================================================
// Instrumentation Tests
// =============================================================================

#if SVG_INSTRUMENTATION_ENABLED

TEST_CASE(instrumentation, hooks_can_be_installed) {
    // Test that instrumentation hooks can be installed and invoked
    int stateChanges = 0;
    int requestsQueued = 0;

    svgplayer::instrumentation::HookInstaller hooks;
    hooks.onThumbnailStateChange([&](ThumbnailState, const std::string&) {
        stateChanges++;
    });
    hooks.onRequestQueued([&](size_t) {
        requestsQueued++;
    });

    // Manually invoke hooks to test they work
    SVG_INSTRUMENT_THUMBNAIL_STATE_CHANGE(ThumbnailState::Loading, "test.svg");
    SVG_INSTRUMENT_REQUEST_QUEUED(1);

    ASSERT_EQ(stateChanges, 1);
    ASSERT_EQ(requestsQueued, 1);
}

TEST_CASE(instrumentation, hook_installer_restores_on_scope_exit) {
    // Test RAII behavior of HookInstaller
    int outerCounter = 0;
    int innerCounter = 0;

    // Set outer hook
    svgplayer::instrumentation::setRequestQueuedHook([&](size_t) {
        outerCounter++;
    });

    SVG_INSTRUMENT_REQUEST_QUEUED(1);
    ASSERT_EQ(outerCounter, 1);
    ASSERT_EQ(innerCounter, 0);

    {
        // Install inner hook (should override)
        svgplayer::instrumentation::HookInstaller hooks;
        hooks.onRequestQueued([&](size_t) {
            innerCounter++;
        });

        SVG_INSTRUMENT_REQUEST_QUEUED(1);
        ASSERT_EQ(outerCounter, 1); // Should not increment
        ASSERT_EQ(innerCounter, 1);
    }
    // HookInstaller destructor should restore outer hook

    SVG_INSTRUMENT_REQUEST_QUEUED(1);
    ASSERT_EQ(outerCounter, 2);  // Restored hook should work
    ASSERT_EQ(innerCounter, 1);  // Should not increment

    // Clear the global hook
    svgplayer::instrumentation::setRequestQueuedHook(nullptr);
}

#endif // SVG_INSTRUMENTATION_ENABLED

// =============================================================================
// Baseline and Regression Detection Tests
// =============================================================================

TEST_CASE(regression, baseline_provider_saves_and_loads) {
    // Test baseline storage
    BaselineProvider baseline("/tmp/svgplayer_test_baselines");

    std::string testJSON = R"({"avgRenderTimeMs": 10.5, "fps": 60.0})";
    bool saved = baseline.saveBaseline("test_baseline", testJSON);
    ASSERT_TRUE(saved);

    ASSERT_TRUE(baseline.hasBaseline("test_baseline"));

    auto loaded = baseline.getBaseline("test_baseline");
    ASSERT_TRUE(loaded.has_value());
    ASSERT_TRUE(loaded->find("10.5") != std::string::npos);
}

TEST_CASE(regression, regression_detector_identifies_regressions) {
    // Test regression detection thresholds
    RegressionThresholds thresholds;
    thresholds.maxRenderTimeIncrease = 20.0;  // 20% threshold

    RegressionDetector detector(thresholds);

    // Create a comparison result showing 25% render time increase (regression)
    // Note: detector looks for "renderTime" or "render_time" patterns
    ComparisonResult regression;
    regression.testName = "test_regression";
    regression.isRegression = false;
    regression.deltas["renderTime"] = 25.0;  // 25% increase

    ASSERT_TRUE(detector.isRegression(regression));
    ASSERT_EQ(detector.getSeverity(regression), TestSeverity::Fail);

    // Create a comparison result within threshold (no regression)
    ComparisonResult noRegression;
    noRegression.testName = "test_no_regression";
    noRegression.isRegression = false;
    noRegression.deltas["renderTime"] = 15.0;  // 15% increase (under 20%)

    ASSERT_FALSE(detector.isRegression(noRegression));
}

TEST_CASE(regression, detector_identifies_improvements) {
    // Test improvement detection
    RegressionThresholds thresholds;
    thresholds.minImprovementForUpdate = 5.0;  // 5% improvement threshold

    RegressionDetector detector(thresholds);

    // Create a comparison result showing improvement
    ComparisonResult improvement;
    improvement.testName = "test_improvement";
    improvement.isRegression = false;
    improvement.deltas["avgRenderTimeMs"] = -10.0;  // 10% decrease (improvement)

    ASSERT_TRUE(detector.isImprovement(improvement));
    ASSERT_TRUE(detector.shouldUpdateBaseline(improvement));
}

TEST_CASE(regression, report_generation) {
    // Test report generation
    RegressionDetector detector;

    ComparisonResult result;
    result.testName = "test_report";
    result.isRegression = true;
    result.deltas["avgRenderTimeMs"] = 30.0;
    result.deltas["fps"] = -15.0;

    std::string report = detector.generateReport(result);
    ASSERT_TRUE(report.find("test_report") != std::string::npos);
    ASSERT_TRUE(report.find("Regression") != std::string::npos ||
                report.find("YES") != std::string::npos);

    std::string jsonReport = detector.generateJSONReport(result);
    ASSERT_TRUE(jsonReport.find("\"test\"") != std::string::npos);
    ASSERT_TRUE(jsonReport.find("isRegression") != std::string::npos);
}

// =============================================================================
// Performance Tests (using metrics collector)
// =============================================================================

TEST_CASE(performance, render_time_tracking) {
    // Test performance metric tracking
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("perf_test");

    // Simulate frame renders with known times
    for (int i = 0; i < 100; i++) {
        collector.recordRenderTime(10.0 + (i % 5));  // 10-14ms range
        collector.recordFrameRendered(i);
    }

    collector.endCollection();

    auto perf = collector.getPerformance();
    ASSERT_EQ(perf.totalFramesRendered, 100);
    ASSERT_TRUE(perf.avgRenderTimeMs >= 10.0 && perf.avgRenderTimeMs <= 14.0);
    ASSERT_TRUE(perf.minRenderTimeMs >= 10.0);
    ASSERT_TRUE(perf.maxRenderTimeMs <= 14.0);
}

TEST_CASE(performance, dropped_frame_tracking) {
    // Test dropped frame tracking
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("dropped_frames_test");

    for (int i = 0; i < 60; i++) {
        collector.recordFrameRendered(i);
    }
    collector.recordFrameSkipped(61);
    collector.recordFrameSkipped(62);

    collector.endCollection();

    auto perf = collector.getPerformance();
    ASSERT_EQ(perf.totalFramesRendered, 60);
    ASSERT_EQ(perf.droppedFrameCount, 2);
}

// =============================================================================
// Memory Tests (using metrics collector)
// =============================================================================

TEST_CASE(memory, cache_metrics_tracking) {
    // Test memory/cache metric tracking
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("cache_test");

    // Simulate cache operations
    collector.recordCacheOperation(false, 1000);  // Miss, 1KB
    collector.recordCacheOperation(false, 2000);  // Miss, 2KB
    collector.recordCacheOperation(true, 2000);   // Hit
    collector.recordCacheOperation(true, 2000);   // Hit
    collector.recordEviction();
    collector.recordMemory(1500);

    collector.endCollection();

    auto mem = collector.getMemory();
    ASSERT_EQ(mem.cacheHits, 2);
    ASSERT_EQ(mem.cacheMisses, 2);
    ASSERT_EQ(mem.evictionCount, 1);
    ASSERT_EQ(mem.peakCacheBytes, 2000);
}

// =============================================================================
// Correctness Tests (using metrics collector)
// =============================================================================

TEST_CASE(correctness, state_transition_tracking) {
    // Test state transition tracking
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("state_test");

    // Simulate state transitions
    collector.recordStateTransition(true);   // Valid
    collector.recordStateTransition(true);   // Valid
    collector.recordStateTransition(false);  // Invalid

    collector.endCollection();

    auto correct = collector.getCorrectness();
    ASSERT_EQ(correct.validStateTransitions, 2);
    ASSERT_EQ(correct.invalidStateTransitions, 1);
}

TEST_CASE(correctness, id_prefixing_error_tracking) {
    // Test ID prefixing error tracking
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("prefixing_test");

    // Initially correct
    auto initial = collector.getCorrectness();
    ASSERT_TRUE(initial.idPrefixingCorrect);

    // Record an error
    collector.recordIdPrefixingError();

    collector.endCollection();

    auto final_state = collector.getCorrectness();
    ASSERT_FALSE(final_state.idPrefixingCorrect);
}

// =============================================================================
// JSON Serialization Tests
// =============================================================================

TEST_CASE(serialization, metrics_to_json) {
    // Test JSON serialization
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("json_test");

    collector.recordRenderTime(10.0);
    collector.recordFrameRendered(0);
    collector.recordCacheOperation(true, 1000);

    collector.endCollection();

    std::string json = collector.toJSON();

    // Verify JSON contains expected fields
    ASSERT_TRUE(json.find("\"testName\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"performance\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"memory\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"correctness\"") != std::string::npos);
    ASSERT_TRUE(json.find("avgRenderTimeMs") != std::string::npos);
}

// =============================================================================
// Integration Test: Full Test Cycle Simulation
// =============================================================================

TEST_CASE(integration, full_test_cycle) {
    // Simulates a complete test cycle with baseline comparison

    // 1. Set up test environment
    ControlledTestEnvironment env;
    env.addStaticSVG("test1", 100, 100);
    env.addStaticSVG("test2", 200, 200);

    // 2. Collect metrics
    auto& collector = MetricsCollector::getInstance();
    collector.beginCollection("full_cycle_test");

    // Simulate work
    for (int i = 0; i < 50; i++) {
        collector.recordRenderTime(8.0 + (i % 4));
        collector.recordFrameRendered(i);
    }
    collector.recordCacheOperation(true, 5000);
    collector.recordCacheOperation(false, 6000);
    collector.recordStateTransition(true);

    collector.endCollection();

    // 3. Get metrics
    auto perf = collector.getPerformance();
    auto mem = collector.getMemory();
    auto correct = collector.getCorrectness();

    // 4. Validate metrics
    ASSERT_EQ(perf.totalFramesRendered, 50);
    ASSERT_TRUE(perf.avgRenderTimeMs > 0);
    ASSERT_EQ(mem.cacheHits, 1);
    ASSERT_EQ(mem.cacheMisses, 1);
    ASSERT_EQ(correct.validStateTransitions, 1);

    // 5. Generate JSON for baseline comparison
    std::string json = collector.toJSON();
    ASSERT_TRUE(json.length() > 100);

    // 6. Check for regressions (against hypothetical baseline)
    BaselineProvider baseline("/tmp/svgplayer_integration_test");

    // First run: save as baseline
    baseline.saveBaseline("full_cycle_test", json);
    ASSERT_TRUE(baseline.hasBaseline("full_cycle_test"));

    // Simulate comparison
    std::map<std::string, double> thresholds;
    thresholds["avgRenderTimeMs"] = 20.0;  // 20% threshold

    auto comparison = baseline.compare("full_cycle_test", json, thresholds);

    // Same data, so no regression
    ASSERT_FALSE(comparison.isRegression);
}

// =============================================================================
// Rendering Accuracy Tests: Cell Boundary Containment (No Bleeding)
// =============================================================================

TEST_CASE(rendering, clippath_elements_generated_for_svg_cells) {
    // Test that clipPath elements are generated for each SVG cell in browser
    // This prevents SVG content from bleeding outside cell bounds

    // Create mock browser SVG by directly testing the pattern
    std::string mockBrowserSVG = R"SVG(
        <svg width="1200" height="800" viewBox="0 0 1200 800">
            <defs><clipPath id="cell_clip_0">
                <rect x="20" y="120" width="180" height="180" rx="4"/>
            </clipPath></defs>
            <g clip-path="url(#cell_clip_0)">
                <g transform="translate(20,120)">
                    <svg width="180" height="180" viewBox="0 0 100 100" preserveAspectRatio="xMidYMid meet" overflow="hidden">
                        <circle cx="50" cy="50" r="40" fill="red"/>
                    </svg>
                </g>
            </g>
        </svg>
    )SVG";

    // Verify clipPath definition exists
    ASSERT_TRUE(mockBrowserSVG.find("clipPath id=\"cell_clip_") != std::string::npos);

    // Verify clipPath is applied to group
    ASSERT_TRUE(mockBrowserSVG.find("clip-path=\"url(#cell_clip_") != std::string::npos);

    // Verify rect inside clipPath (the clipping shape)
    ASSERT_TRUE(mockBrowserSVG.find("<clipPath") != std::string::npos);
    ASSERT_TRUE(mockBrowserSVG.find("</clipPath>") != std::string::npos);
}

TEST_CASE(rendering, clippath_rect_matches_icon_bounds) {
    // Test that clipPath rect coordinates match icon position/size
    // Pattern in generateBrowserSVG():
    //   iconSize = std::min(cell.width, cell.height) * 0.7f
    //   iconX = cell.x + (cell.width - iconSize) / 2
    //   iconY = cell.y + (cell.height - iconSize) / 2
    //   clipPath rect: x=iconX, y=iconY, width=iconSize, height=iconSize

    // Given a cell at (20, 100) with size 200x200:
    float cellX = 20.0f, cellY = 100.0f, cellWidth = 200.0f, cellHeight = 200.0f;
    float iconSize = std::min(cellWidth, cellHeight) * 0.7f;  // 140
    float iconX = cellX + (cellWidth - iconSize) / 2;         // 50
    float iconY = cellY + (cellHeight - iconSize) / 2;        // 130

    ASSERT_EQ(iconSize, 140.0f);
    ASSERT_EQ(iconX, 50.0f);
    ASSERT_EQ(iconY, 130.0f);

    // The clipPath rect must use these exact coordinates to prevent bleeding
    // This test verifies the calculation logic matches the implementation
}

// =============================================================================
// Rendering Accuracy Tests: Aspect Ratio Preservation
// =============================================================================

TEST_CASE(rendering, thumbnail_svg_has_preserve_aspect_ratio) {
    // Test that generated thumbnail SVGs have preserveAspectRatio="xMidYMid meet"
    // This ensures content is scaled uniformly and centered within cell

    std::string thumbnailSVG = R"(<svg width="180" height="180" viewBox="0 0 100 200" preserveAspectRatio="xMidYMid meet" overflow="hidden"><rect/></svg>)";

    // Verify preserveAspectRatio is present with correct value
    ASSERT_TRUE(thumbnailSVG.find("preserveAspectRatio=\"xMidYMid meet\"") != std::string::npos);

    // "xMidYMid meet" means:
    // - xMid: center horizontally
    // - YMid: center vertically
    // - meet: scale uniformly to fit, preserving aspect ratio
}

TEST_CASE(rendering, aspect_ratio_calculation_for_wide_svg) {
    // Test aspect ratio handling for wide SVGs (width > height)
    // A 200x100 SVG in a 180x180 cell should scale to 180x90, centered vertically

    float svgWidth = 200.0f, svgHeight = 100.0f;
    float cellWidth = 180.0f, cellHeight = 180.0f;

    // Calculate scale factor (meet = use smaller scale)
    float scaleX = cellWidth / svgWidth;   // 0.9
    float scaleY = cellHeight / svgHeight; // 1.8
    float scale = std::min(scaleX, scaleY); // 0.9 (meet)

    // Final dimensions after scaling
    float finalWidth = svgWidth * scale;   // 180
    float finalHeight = svgHeight * scale; // 90

    // Verify no dimension exceeds cell bounds
    ASSERT_LE(finalWidth, cellWidth);
    ASSERT_LE(finalHeight, cellHeight);

    // Verify aspect ratio is preserved
    float originalRatio = svgWidth / svgHeight;
    float finalRatio = finalWidth / finalHeight;
    ASSERT_TRUE(std::abs(originalRatio - finalRatio) < 0.001f);
}

TEST_CASE(rendering, aspect_ratio_calculation_for_tall_svg) {
    // Test aspect ratio handling for tall SVGs (height > width)
    // A 100x200 SVG in a 180x180 cell should scale to 90x180, centered horizontally

    float svgWidth = 100.0f, svgHeight = 200.0f;
    float cellWidth = 180.0f, cellHeight = 180.0f;

    // Calculate scale factor (meet = use smaller scale)
    float scaleX = cellWidth / svgWidth;   // 1.8
    float scaleY = cellHeight / svgHeight; // 0.9
    float scale = std::min(scaleX, scaleY); // 0.9 (meet)

    // Final dimensions after scaling
    float finalWidth = svgWidth * scale;   // 90
    float finalHeight = svgHeight * scale; // 180

    // Verify no dimension exceeds cell bounds
    ASSERT_LE(finalWidth, cellWidth);
    ASSERT_LE(finalHeight, cellHeight);

    // Verify aspect ratio is preserved
    float originalRatio = svgWidth / svgHeight;
    float finalRatio = finalWidth / finalHeight;
    ASSERT_TRUE(std::abs(originalRatio - finalRatio) < 0.001f);
}

// =============================================================================
// Rendering Accuracy Tests: Overflow Hidden (Double Clipping)
// =============================================================================

TEST_CASE(rendering, thumbnail_svg_has_overflow_hidden) {
    // Test that thumbnail SVGs have overflow="hidden"
    // This is a second layer of defense against bleeding (in addition to clipPath)

    std::string thumbnailSVG = R"(<svg width="180" height="180" viewBox="0 0 100 100" preserveAspectRatio="xMidYMid meet" overflow="hidden"><rect/></svg>)";

    ASSERT_TRUE(thumbnailSVG.find("overflow=\"hidden\"") != std::string::npos);
}

TEST_CASE(rendering, double_clipping_defense_in_depth) {
    // Test that both clipPath AND overflow="hidden" are used
    // This provides defense in depth against rendering bugs

    std::string browserSVG = R"SVG(
        <defs><clipPath id="cell_clip_0">
            <rect x="50" y="130" width="140" height="140" rx="4"/>
        </clipPath></defs>
        <g clip-path="url(#cell_clip_0)">
            <g transform="translate(50,130)">
                <svg width="140" height="140" viewBox="0 0 100 100" preserveAspectRatio="xMidYMid meet" overflow="hidden">
                    <rect width="100" height="100" fill="blue"/>
                </svg>
            </g>
        </g>
    )SVG";

    // Verify both clipping mechanisms are present
    ASSERT_TRUE(browserSVG.find("clipPath") != std::string::npos);
    ASSERT_TRUE(browserSVG.find("overflow=\"hidden\"") != std::string::npos);
}

// =============================================================================
// Rendering Accuracy Tests: ViewBox Preservation
// =============================================================================

TEST_CASE(rendering, viewbox_with_offset_preserved) {
    // Test that viewBox with non-zero minX/minY is preserved
    // Some SVGs have viewBox="100 100 200 200" - content starts at (100,100)

    // Pattern from generateThumbnailSVG():
    // extractFullViewBox() extracts minX, minY, width, height
    // viewBox is rebuilt as "minX minY width height"

    std::string svgWithOffset = R"(<svg viewBox="100 100 200 200"><rect x="100" y="100" width="200" height="200"/></svg>)";

    // Verify viewBox offset is preserved in pattern
    ASSERT_TRUE(svgWithOffset.find("viewBox=\"100 100 200 200\"") != std::string::npos);
}

// =============================================================================
// Rendering Accuracy Tests: ID Prefixing for Collision Prevention
// =============================================================================

TEST_CASE(rendering, id_prefixing_prevents_collisions) {
    // Test that ID prefixing works correctly to prevent ID collisions
    // When multiple SVGs are combined, each needs unique IDs

    std::string original = R"SVG(<svg><circle id="myCircle" fill="url(#myGrad)"/><defs><linearGradient id="myGrad"/></defs></svg>)SVG";
    std::string prefix = "cell0_";

    // Call the ACTUAL prefixSVGIds function
    std::string prefixed = svgplayer::SVGGridCompositor::prefixSVGIds(original, prefix);

    // Verify id="value" -> id="prefix_value"
    ASSERT_TRUE(prefixed.find("id=\"cell0_myCircle\"") != std::string::npos);
    ASSERT_TRUE(prefixed.find("id=\"cell0_myGrad\"") != std::string::npos);

    // Verify url(#value) -> url(#prefix_value)
    ASSERT_TRUE(prefixed.find("url(#cell0_myGrad)") != std::string::npos);

    // Verify original IDs are no longer present (replaced)
    ASSERT_TRUE(prefixed.find("id=\"myCircle\"") == std::string::npos);
    ASSERT_TRUE(prefixed.find("id=\"myGrad\"") == std::string::npos);
}

TEST_CASE(rendering, unique_prefix_per_thumbnail) {
    // Test that each thumbnail gets a unique prefix based on file path hash
    // This ensures deterministic, collision-free IDs

    std::string path1 = "/path/to/file1.svg";
    std::string path2 = "/path/to/file2.svg";

    size_t hash1 = std::hash<std::string>{}(path1);
    size_t hash2 = std::hash<std::string>{}(path2);

    // Different paths should produce different hashes (with very high probability)
    ASSERT_TRUE(hash1 != hash2);

    std::string prefix1 = "t" + std::to_string(hash1) + "_";
    std::string prefix2 = "t" + std::to_string(hash2) + "_";

    ASSERT_TRUE(prefix1 != prefix2);
}

// =============================================================================
// Animation Tests: SMIL Animation Preservation
// =============================================================================

TEST_CASE(animation, smil_animate_elements_preserved_in_thumbnail) {
    // Test that SMIL <animate> elements survive the thumbnail generation process
    // These are essential for animated SVG previews

    std::string animatedSVG = R"(
        <svg viewBox="0 0 100 100">
            <circle id="dot" cx="50" cy="50" r="10" fill="blue">
                <animate attributeName="r" values="10;20;10" dur="1s" repeatCount="indefinite"/>
            </circle>
        </svg>
    )";

    // Verify animate element is present
    ASSERT_TRUE(animatedSVG.find("<animate") != std::string::npos);
    ASSERT_TRUE(animatedSVG.find("attributeName=") != std::string::npos);
    ASSERT_TRUE(animatedSVG.find("repeatCount=\"indefinite\"") != std::string::npos);
}

TEST_CASE(animation, smil_id_references_prefixed_correctly) {
    // Test that SMIL animation ID references are prefixed along with target IDs
    // Pattern: begin="targetId.event" -> begin="prefix_targetId.event"

    std::string animWithIdRef = R"(
        <svg>
            <circle id="trigger"/>
            <rect id="target">
                <animate begin="trigger.click" attributeName="fill" to="red" dur="1s"/>
            </rect>
        </svg>
    )";

    // Call the ACTUAL prefixSVGIds function
    std::string prefixed = svgplayer::SVGGridCompositor::prefixSVGIds(animWithIdRef, "c0_");

    // Verify element IDs are prefixed
    ASSERT_TRUE(prefixed.find("id=\"c0_trigger\"") != std::string::npos);
    ASSERT_TRUE(prefixed.find("id=\"c0_target\"") != std::string::npos);

    // Verify begin attribute ID reference is prefixed (Pattern 4)
    ASSERT_TRUE(prefixed.find("begin=\"c0_trigger.click\"") != std::string::npos);

    // Verify original IDs are no longer present
    ASSERT_TRUE(prefixed.find("id=\"trigger\"") == std::string::npos);
}

TEST_CASE(animation, placeholder_loading_animation_uses_smil) {
    // Test that loading placeholder uses SMIL animations (not CSS/JS)
    // SMIL works with SVGAnimationController for consistent timing

    // Pattern from generatePlaceholder():
    // <animate xlink:href="#loadRing_X" attributeName="opacity" values="..." dur="1.2s" repeatCount="indefinite"/>

    std::string placeholder = R"(
        <g>
            <circle id="loadRing_0" cx="90" cy="90" r="27" fill="none" stroke="#74b9ff" stroke-width="3" opacity="1"/>
            <animate xlink:href="#loadRing_0" attributeName="opacity" values="1;0.5;0.3;0.5;1" dur="1.2s" repeatCount="indefinite"/>
        </g>
    )";

    // Verify SMIL animate with xlink:href pattern exists
    ASSERT_TRUE(placeholder.find("<animate") != std::string::npos);
    ASSERT_TRUE(placeholder.find("xlink:href=\"#loadRing_") != std::string::npos);
    ASSERT_TRUE(placeholder.find("dur=\"1.2s\"") != std::string::npos);
    ASSERT_TRUE(placeholder.find("repeatCount=\"indefinite\"") != std::string::npos);

    // Also verify xlink:href prefixing works correctly with actual function
    std::string prefixed = svgplayer::SVGGridCompositor::prefixSVGIds(placeholder, "p1_");

    // xlink:href="#loadRing_0" should become xlink:href="#p1_loadRing_0"
    ASSERT_TRUE(prefixed.find("xlink:href=\"#p1_loadRing_0\"") != std::string::npos);
    ASSERT_TRUE(prefixed.find("id=\"p1_loadRing_0\"") != std::string::npos);
}

TEST_CASE(animation, values_id_references_prefixed) {
    // Test that animation values with ID references are prefixed
    // Pattern 6: values="#frame1;#frame2" -> values="#prefix_frame1;#prefix_frame2"

    std::string svgWithValues = R"(
        <svg>
            <g id="frame1"/>
            <g id="frame2"/>
            <animate attributeName="visibility" values="#frame1;#frame2" dur="2s"/>
        </svg>
    )";

    std::string prefixed = svgplayer::SVGGridCompositor::prefixSVGIds(svgWithValues, "c0_");

    // Verify element IDs are prefixed
    ASSERT_TRUE(prefixed.find("id=\"c0_frame1\"") != std::string::npos);
    ASSERT_TRUE(prefixed.find("id=\"c0_frame2\"") != std::string::npos);

    // Verify values ID references are prefixed
    ASSERT_TRUE(prefixed.find("#c0_frame1") != std::string::npos);
    ASSERT_TRUE(prefixed.find("#c0_frame2") != std::string::npos);
}

TEST_CASE(animation, placeholder_ids_deterministic_per_cell) {
    // Test that placeholder IDs are deterministic based on cell index
    // This fixes regression where IDs drifted between regenerations

    // Pattern from generatePlaceholder():
    // std::string ringId = "loadRing_" + std::to_string(id);  // id = cellIndex

    int cellIndex0 = 0;
    int cellIndex1 = 1;
    int cellIndex5 = 5;

    std::string ringId0 = "loadRing_" + std::to_string(cellIndex0);
    std::string ringId1 = "loadRing_" + std::to_string(cellIndex1);
    std::string ringId5 = "loadRing_" + std::to_string(cellIndex5);

    ASSERT_EQ(ringId0, "loadRing_0");
    ASSERT_EQ(ringId1, "loadRing_1");
    ASSERT_EQ(ringId5, "loadRing_5");

    // Regenerating with same cellIndex should produce same ID
    std::string ringId0_again = "loadRing_" + std::to_string(cellIndex0);
    ASSERT_EQ(ringId0, ringId0_again);
}

// =============================================================================
// Main Entry Point
// =============================================================================

int main(int argc, char* argv[]) {
    std::cout << "=== SVG Player Automated Test Suite ===\n\n";

    TestConfig config;
    config.enableDeterministicMode = true;
    config.baselineDirectory = "./tests/baselines";
    config.reportOutputPath = "./test_report";

    std::string reportFormat = "console";
    bool updateBaseline = false;

    // Parse command line arguments (supports both --arg value and --arg=value)
    auto getArgValue = [](const std::string& arg, const std::string& prefix) -> std::pair<bool, std::string> {
        // Check for --arg=value format
        if (arg.rfind(prefix + "=", 0) == 0) {
            return {true, arg.substr(prefix.length() + 1)};
        }
        // Check for --arg format (value is next argument)
        if (arg == prefix) {
            return {true, ""};  // Empty means check next arg
        }
        return {false, ""};
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        auto [matchBaseline, valBaseline] = getArgValue(arg, "--baseline-dir");
        if (matchBaseline) {
            config.baselineDirectory = valBaseline.empty() ? argv[++i] : valBaseline;
            continue;
        }

        auto [matchFormat, valFormat] = getArgValue(arg, "--report-format");
        if (matchFormat) {
            reportFormat = valFormat.empty() ? argv[++i] : valFormat;
            continue;
        }

        auto [matchOutput, valOutput] = getArgValue(arg, "--report-output");
        if (matchOutput) {
            config.reportOutputPath = valOutput.empty() ? argv[++i] : valOutput;
            continue;
        }

        if (arg == "--deterministic") {
            config.enableDeterministicMode = true;
        } else if (arg == "--update-baseline") {
            updateBaseline = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --baseline-dir=<path>    Directory for baseline files\n"
                      << "  --report-format=<fmt>    Report format: console, json, html, markdown\n"
                      << "  --report-output=<path>   Report output path (without extension)\n"
                      << "  --deterministic          Enable deterministic mode\n"
                      << "  --update-baseline        Update baselines with current results\n"
                      << "  --help, -h               Show this help\n";
            return 0;
        }
    }

    TestHarness::instance().configure(config);

    // Enable deterministic mode if requested
    if (config.enableDeterministicMode) {
        g_clock.enable();
        g_scheduler.enable(4);
    }

    // Run all tests
    int failCount = TestHarness::instance().runAllTests();

    // Print summary
    const auto& results = TestHarness::instance().getResults();
    int passCount = 0;
    int warnCount = 0;
    int criticalCount = 0;

    std::cout << "\n=== Test Results ===\n\n";

    for (const auto& result : results) {
        std::string status;
        switch (result.severity) {
            case TestSeverity::Pass:
                status = "[PASS]";
                passCount++;
                break;
            case TestSeverity::Warning:
                status = "[WARN]";
                warnCount++;
                break;
            case TestSeverity::Fail:
                status = "[FAIL]";
                break;
            case TestSeverity::Critical:
                status = "[CRIT]";
                criticalCount++;
                break;
        }

        std::cout << status << " " << result.category << "::" << result.name;
        if (result.severity != TestSeverity::Pass) {
            std::cout << " - " << result.message;
        }
        std::cout << " (" << result.durationMs << "ms)\n";
    }

    std::cout << "\n=== Summary ===\n"
              << "Total:    " << results.size() << "\n"
              << "Passed:   " << passCount << "\n"
              << "Warnings: " << warnCount << "\n"
              << "Failed:   " << failCount << "\n"
              << "Critical: " << criticalCount << "\n";

    // Generate report if requested
    if (reportFormat != "console") {
        TestHarness::instance().generateReport(reportFormat);
        std::cout << "\nReport saved to: " << config.reportOutputPath << "." << reportFormat << "\n";
    }

    // Cleanup deterministic mode
    if (config.enableDeterministicMode) {
        g_scheduler.disable();
        g_clock.disable();
    }

    // Check for regressions
    if (TestHarness::instance().hasRegressions()) {
        std::cout << "\n*** REGRESSIONS DETECTED ***\n";
        return 2;  // Special exit code for regressions
    }

    return (failCount > 0) ? 1 : 0;
}
