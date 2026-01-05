// test_harness.h - Extended test framework for SVG Player API
// C++17 header extending test_svg_player_api.cpp framework
// Compatible with existing TEST(), ASSERT_TRUE(), ASSERT_EQ() macros

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <chrono>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>

namespace TestFramework {

// Test result severity levels
enum class TestSeverity {
    Pass = 0,      // Test passed completely
    Warning = 1,   // Test passed with warnings
    Fail = 2,      // Test failed
    Critical = 3   // Critical failure requiring immediate attention
};

// Individual test result
struct TestResult {
    std::string name;                           // Test function name
    std::string category;                       // Test category (e.g., "API", "Performance", "Rendering")
    TestSeverity severity;                      // Result severity
    double durationMs;                          // Execution time in milliseconds
    std::string message;                        // Status/error message
    std::map<std::string, double> metrics;      // Performance metrics (fps, memory, etc.)

    TestResult()
        : severity(TestSeverity::Pass), durationMs(0.0) {}
};

// Test configuration
struct TestConfig {
    bool enableDeterministicMode;    // Enable deterministic testing (fixed seeds, etc.)
    std::string baselineDirectory;   // Directory for baseline comparison files
    std::string reportOutputPath;    // Output path for test reports
    int timeoutSeconds;              // Per-test timeout

    TestConfig()
        : enableDeterministicMode(true),
          baselineDirectory("./test_baselines"),
          reportOutputPath("./test_report"),
          timeoutSeconds(300) {}
};

// Singleton test harness
class TestHarness {
public:
    // Get singleton instance
    static TestHarness& instance() {
        static TestHarness instance;
        return instance;
    }

    // Configure test harness
    void configure(const TestConfig& config) {
        config_ = config;
    }

    // Get current configuration
    const TestConfig& getConfig() const {
        return config_;
    }

    // Register a test case
    void registerTest(const std::string& name,
                     const std::string& category,
                     std::function<void()> testFunc) {
        TestEntry entry;
        entry.name = name;
        entry.category = category;
        entry.function = testFunc;
        tests_.push_back(entry);
    }

    // Run all registered tests
    int runAllTests() {
        results_.clear();
        int failCount = 0;

        for (const auto& test : tests_) {
            TestResult result = runSingleTest(test);
            results_.push_back(result);

            if (result.severity == TestSeverity::Fail ||
                result.severity == TestSeverity::Critical) {
                failCount++;
            }
        }

        return failCount;
    }

    // Run tests in specific category
    int runCategory(const std::string& category) {
        results_.clear();
        int failCount = 0;

        for (const auto& test : tests_) {
            if (test.category == category) {
                TestResult result = runSingleTest(test);
                results_.push_back(result);

                if (result.severity == TestSeverity::Fail ||
                    result.severity == TestSeverity::Critical) {
                    failCount++;
                }
            }
        }

        return failCount;
    }

    // Get all test results
    const std::vector<TestResult>& getResults() const {
        return results_;
    }

    // Check if any regressions detected
    bool hasRegressions() const {
        for (const auto& result : results_) {
            if (result.severity == TestSeverity::Critical) {
                return true;
            }
        }
        return false;
    }

    // Generate test report in specified format
    void generateReport(const std::string& format) {
        if (format == "json") {
            generateJSONReport();
        } else if (format == "html") {
            generateHTMLReport();
        } else if (format == "markdown") {
            generateMarkdownReport();
        }
    }

    // Add metric to current test result (used by macros)
    void addMetric(const std::string& name, double value) {
        if (!currentResult_) return;
        currentResult_->metrics[name] = value;
    }

    // Set current test result (used internally)
    void setCurrentResult(TestResult* result) {
        currentResult_ = result;
    }

private:
    TestHarness() : currentResult_(nullptr) {}
    ~TestHarness() = default;

    // Prevent copying
    TestHarness(const TestHarness&) = delete;
    TestHarness& operator=(const TestHarness&) = delete;

    struct TestEntry {
        std::string name;
        std::string category;
        std::function<void()> function;
    };

    TestResult runSingleTest(const TestEntry& test) {
        TestResult result;
        result.name = test.name;
        result.category = test.category;
        result.severity = TestSeverity::Pass;

        currentResult_ = &result;

        auto startTime = std::chrono::high_resolution_clock::now();

        try {
            test.function();
            result.message = "PASS";
        } catch (const std::exception& e) {
            result.severity = TestSeverity::Fail;
            result.message = std::string("FAIL: ") + e.what();
        } catch (...) {
            result.severity = TestSeverity::Critical;
            result.message = "CRITICAL: Unknown exception";
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = endTime - startTime;
        result.durationMs = duration.count();

        currentResult_ = nullptr;

        return result;
    }

    void generateJSONReport() {
        std::string path = config_.reportOutputPath + ".json";
        std::ofstream out(path);

        // Calculate summary counts
        int passCount = 0, failCount = 0, criticalCount = 0, warnCount = 0;
        for (const auto& r : results_) {
            switch (r.severity) {
                case TestSeverity::Pass: passCount++; break;
                case TestSeverity::Warning: warnCount++; break;
                case TestSeverity::Fail: failCount++; break;
                case TestSeverity::Critical: criticalCount++; break;
            }
        }

        out << "{\n";

        // Summary section (expected by run_test_cycle.sh)
        out << "  \"summary\": {\n";
        out << "    \"total\": " << results_.size() << ",\n";
        out << "    \"passed\": " << passCount << ",\n";
        out << "    \"warnings\": " << warnCount << ",\n";
        out << "    \"failed\": " << failCount << ",\n";
        out << "    \"critical\": " << criticalCount << "\n";
        out << "  },\n";

        // Regressions section (expected by run_test_cycle.sh)
        out << "  \"regressions\": [\n";
        bool firstRegression = true;
        for (const auto& r : results_) {
            if (r.severity == TestSeverity::Critical || r.severity == TestSeverity::Fail) {
                if (!firstRegression) out << ",\n";
                firstRegression = false;
                out << "    {\"test\": \"" << r.name << "\", \"delta\": 0, \"threshold\": 0}";
            }
        }
        if (!firstRegression) out << "\n";
        out << "  ],\n";

        // Metrics section (aggregate metrics from all tests)
        out << "  \"metrics\": {\n";
        std::map<std::string, double> aggregateMetrics;
        for (const auto& r : results_) {
            for (const auto& [key, value] : r.metrics) {
                aggregateMetrics[key] = value;  // Use last value (could also average)
            }
        }
        size_t metricCount = 0;
        for (const auto& [key, value] : aggregateMetrics) {
            out << "    \"" << key << "\": " << value;
            if (++metricCount < aggregateMetrics.size()) out << ",";
            out << "\n";
        }
        out << "  },\n";

        // Results section (detailed test results)
        out << "  \"results\": [\n";
        for (size_t i = 0; i < results_.size(); ++i) {
            const auto& r = results_[i];
            out << "    {\n";
            out << "      \"name\": \"" << r.name << "\",\n";
            out << "      \"category\": \"" << r.category << "\",\n";
            out << "      \"severity\": \"" << severityToString(r.severity) << "\",\n";
            out << "      \"durationMs\": " << r.durationMs << ",\n";
            out << "      \"message\": \"" << r.message << "\",\n";
            out << "      \"metrics\": {";

            size_t metricIdx = 0;
            for (const auto& [key, value] : r.metrics) {
                out << "\"" << key << "\": " << value;
                if (++metricIdx < r.metrics.size()) out << ", ";
            }
            out << "}\n";
            out << "    }" << (i + 1 < results_.size() ? "," : "") << "\n";
        }

        out << "  ]\n";
        out << "}\n";
    }

    void generateHTMLReport() {
        std::string path = config_.reportOutputPath + ".html";
        std::ofstream out(path);

        out << "<!DOCTYPE html>\n<html>\n<head>\n";
        out << "<title>Test Report</title>\n";
        out << "<style>body{font-family:Arial;} table{border-collapse:collapse;width:100%;} ";
        out << "th,td{border:1px solid #ddd;padding:8px;text-align:left;} ";
        out << "th{background-color:#4CAF50;color:white;} ";
        out << ".pass{background-color:#d4edda;} .fail{background-color:#f8d7da;} ";
        out << ".warning{background-color:#fff3cd;} .critical{background-color:#f5c6cb;}</style>\n";
        out << "</head>\n<body>\n";
        out << "<h1>Test Report</h1>\n";
        out << "<table>\n";
        out << "<tr><th>Name</th><th>Category</th><th>Result</th><th>Duration (ms)</th><th>Message</th></tr>\n";

        for (const auto& r : results_) {
            std::string rowClass;
            switch (r.severity) {
                case TestSeverity::Pass: rowClass = "pass"; break;
                case TestSeverity::Warning: rowClass = "warning"; break;
                case TestSeverity::Fail: rowClass = "fail"; break;
                case TestSeverity::Critical: rowClass = "critical"; break;
            }

            out << "<tr class=\"" << rowClass << "\">";
            out << "<td>" << r.name << "</td>";
            out << "<td>" << r.category << "</td>";
            out << "<td>" << severityToString(r.severity) << "</td>";
            out << "<td>" << std::fixed << std::setprecision(2) << r.durationMs << "</td>";
            out << "<td>" << r.message << "</td>";
            out << "</tr>\n";
        }

        out << "</table>\n</body>\n</html>\n";
    }

    void generateMarkdownReport() {
        std::string path = config_.reportOutputPath + ".md";
        std::ofstream out(path);

        out << "# Test Report\n\n";
        out << "| Name | Category | Result | Duration (ms) | Message |\n";
        out << "|------|----------|--------|---------------|----------|\n";

        for (const auto& r : results_) {
            out << "| " << r.name << " | " << r.category << " | ";
            out << severityToString(r.severity) << " | ";
            out << std::fixed << std::setprecision(2) << r.durationMs << " | ";
            out << r.message << " |\n";
        }
    }

    std::string severityToString(TestSeverity severity) const {
        switch (severity) {
            case TestSeverity::Pass: return "Pass";
            case TestSeverity::Warning: return "Warning";
            case TestSeverity::Fail: return "Fail";
            case TestSeverity::Critical: return "Critical";
            default: return "Unknown";
        }
    }

    TestConfig config_;
    std::vector<TestEntry> tests_;
    std::vector<TestResult> results_;
    TestResult* currentResult_;
};

} // namespace TestFramework

// Macro for categorized test registration
#define TEST_CASE(category, name) \
    static void test_##category##_##name(); \
    namespace { \
        struct TestRegistrar_##category##_##name { \
            TestRegistrar_##category##_##name() { \
                TestFramework::TestHarness::instance().registerTest( \
                    #name, #category, test_##category##_##name); \
            } \
        }; \
        static TestRegistrar_##category##_##name registrar_##category##_##name; \
    } \
    static void test_##category##_##name()

// Macro for metric-based assertions with tolerance
#define ASSERT_METRIC_EQ(metric, expected, tolerance) \
    do { \
        double _actual = (metric); \
        double _expected = (expected); \
        double _tolerance = (tolerance); \
        TestFramework::TestHarness::instance().addMetric(#metric, _actual); \
        if (std::abs(_actual - _expected) > _tolerance) { \
            std::ostringstream _msg; \
            _msg << "Metric " << #metric << " = " << _actual \
                 << " differs from expected " << _expected \
                 << " by more than tolerance " << _tolerance; \
            throw std::runtime_error(_msg.str()); \
        } \
    } while(0)

#endif // TEST_HARNESS_H
