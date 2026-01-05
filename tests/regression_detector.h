// Copyright 2025 Emasoft. All rights reserved.
// SPDX-License-Identifier: MIT

#ifndef REGRESSION_DETECTOR_H
#define REGRESSION_DETECTOR_H

#include "test_harness.h"
#include "baseline_provider.h"
#include <string>
#include <vector>
#include <sstream>
#include <cmath>

namespace svgplayer {
namespace testing {

// Thresholds for detecting performance regressions
struct RegressionThresholds {
    // Performance thresholds (percent degradation)
    double maxRenderTimeIncrease = 20.0;      // 20% slower = regression
    double maxFPSDrop = 10.0;                 // 10% lower FPS = regression
    double maxMemoryIncrease = 25.0;          // 25% more memory = regression
    double maxCacheMissRateIncrease = 15.0;   // 15% more misses = regression

    // Correctness thresholds (absolute values)
    int maxFrameErrors = 0;                   // Any error = regression
    int maxStateTransitionErrors = 0;         // Any invalid = regression

    // Improvement thresholds (for baseline updates)
    double minImprovementForUpdate = 5.0;     // 5% improvement to update baseline
};

// Regression detection and reporting
class RegressionDetector {
private:
    RegressionThresholds thresholds_;

    // Helper: Calculate percentage change
    static double percentChange(double oldValue, double newValue) {
        if (std::abs(oldValue) < 1e-9) return 0.0;
        return ((newValue - oldValue) / oldValue) * 100.0;
    }

    // Helper: Check if metric name indicates a "lower is better" metric
    static bool isLowerBetter(const std::string& metric) {
        return metric.find("time") != std::string::npos ||
               metric.find("Time") != std::string::npos ||
               metric.find("memory") != std::string::npos ||
               metric.find("Memory") != std::string::npos ||
               metric.find("miss") != std::string::npos ||
               metric.find("Miss") != std::string::npos ||
               metric.find("error") != std::string::npos ||
               metric.find("Error") != std::string::npos;
    }

    // Helper: Check if metric name indicates a "higher is better" metric
    static bool isHigherBetter(const std::string& metric) {
        return metric.find("fps") != std::string::npos ||
               metric.find("FPS") != std::string::npos ||
               metric.find("throughput") != std::string::npos ||
               metric.find("Throughput") != std::string::npos;
    }

public:
    // Constructors
    RegressionDetector() = default;
    explicit RegressionDetector(const RegressionThresholds& thresholds)
        : thresholds_(thresholds) {}

    // Detection: Check if comparison shows a regression
    bool isRegression(const ComparisonResult& comparison) const {
        // If already flagged as regression by baseline comparison
        if (comparison.isRegression) {
            return true;
        }

        // Check each metric delta against thresholds
        for (const auto& [metric, delta] : comparison.deltas) {
            // Render time increase
            if ((metric.find("renderTime") != std::string::npos ||
                 metric.find("render_time") != std::string::npos) &&
                delta > thresholds_.maxRenderTimeIncrease) {
                return true;
            }

            // FPS drop
            if ((metric.find("fps") != std::string::npos ||
                 metric.find("FPS") != std::string::npos) &&
                delta < -thresholds_.maxFPSDrop) {
                return true;
            }

            // Memory increase
            if ((metric.find("memory") != std::string::npos ||
                 metric.find("Memory") != std::string::npos) &&
                delta > thresholds_.maxMemoryIncrease) {
                return true;
            }

            // Cache miss rate increase
            if ((metric.find("miss") != std::string::npos ||
                 metric.find("Miss") != std::string::npos) &&
                delta > thresholds_.maxCacheMissRateIncrease) {
                return true;
            }

            // Any frame errors
            if ((metric.find("error") != std::string::npos ||
                 metric.find("Error") != std::string::npos) &&
                std::abs(delta) > thresholds_.maxFrameErrors) {
                return true;
            }
        }

        return false;
    }

    // Get severity level based on threshold violations
    TestFramework::TestSeverity getSeverity(const ComparisonResult& comparison) const {
        // Check for critical violations (correctness errors or severe degradation)
        for (const auto& [metric, delta] : comparison.deltas) {
            // Any correctness errors are critical
            if (metric.find("error") != std::string::npos ||
                metric.find("Error") != std::string::npos) {
                if (std::abs(delta) > thresholds_.maxFrameErrors) {
                    return TestFramework::TestSeverity::Critical;
                }
            }

            // Severe performance degradation (>2x threshold)
            if (isLowerBetter(metric) && delta > thresholds_.maxRenderTimeIncrease * 2.0) {
                return TestFramework::TestSeverity::Critical;
            }
            if (isHigherBetter(metric) && delta < -thresholds_.maxFPSDrop * 2.0) {
                return TestFramework::TestSeverity::Critical;
            }
        }

        // Check for fail severity (threshold violations = regression)
        if (isRegression(comparison)) {
            return TestFramework::TestSeverity::Fail;
        }

        // Check for warning severity (approaching threshold, >50%)
        for (const auto& [metric, delta] : comparison.deltas) {
            if (isLowerBetter(metric) && delta > thresholds_.maxRenderTimeIncrease * 0.5) {
                return TestFramework::TestSeverity::Warning;
            }
            if (isHigherBetter(metric) && delta < -thresholds_.maxFPSDrop * 0.5) {
                return TestFramework::TestSeverity::Warning;
            }
        }

        return TestFramework::TestSeverity::Pass;
    }

    // Get list of threshold violations
    std::vector<std::string> getViolations(const ComparisonResult& comparison) const {
        std::vector<std::string> violations;

        for (const auto& [metric, delta] : comparison.deltas) {
            std::ostringstream oss;

            // Render time violations
            if ((metric.find("renderTime") != std::string::npos ||
                 metric.find("render_time") != std::string::npos) &&
                delta > thresholds_.maxRenderTimeIncrease) {
                oss << metric << " increased by " << delta
                    << "% (threshold: " << thresholds_.maxRenderTimeIncrease << "%)";
                violations.push_back(oss.str());
            }

            // FPS violations
            if ((metric.find("fps") != std::string::npos ||
                 metric.find("FPS") != std::string::npos) &&
                delta < -thresholds_.maxFPSDrop) {
                oss << metric << " dropped by " << -delta
                    << "% (threshold: " << thresholds_.maxFPSDrop << "%)";
                violations.push_back(oss.str());
            }

            // Memory violations
            if ((metric.find("memory") != std::string::npos ||
                 metric.find("Memory") != std::string::npos) &&
                delta > thresholds_.maxMemoryIncrease) {
                oss << metric << " increased by " << delta
                    << "% (threshold: " << thresholds_.maxMemoryIncrease << "%)";
                violations.push_back(oss.str());
            }

            // Cache miss violations
            if ((metric.find("miss") != std::string::npos ||
                 metric.find("Miss") != std::string::npos) &&
                delta > thresholds_.maxCacheMissRateIncrease) {
                oss << metric << " increased by " << delta
                    << "% (threshold: " << thresholds_.maxCacheMissRateIncrease << "%)";
                violations.push_back(oss.str());
            }

            // Correctness violations
            if ((metric.find("error") != std::string::npos ||
                 metric.find("Error") != std::string::npos) &&
                std::abs(delta) > thresholds_.maxFrameErrors) {
                oss << metric << " changed by " << delta
                    << " (threshold: " << thresholds_.maxFrameErrors << ")";
                violations.push_back(oss.str());
            }
        }

        return violations;
    }

    // Improvement detection: Check if metrics improved
    bool isImprovement(const ComparisonResult& comparison) const {
        if (comparison.deltas.empty()) return false;

        int improvementCount = 0;
        int totalMetrics = 0;

        for (const auto& [metric, delta] : comparison.deltas) {
            totalMetrics++;

            // Lower is better metrics (time, memory, errors)
            if (isLowerBetter(metric) && delta < -thresholds_.minImprovementForUpdate) {
                improvementCount++;
            }

            // Higher is better metrics (fps, throughput)
            if (isHigherBetter(metric) && delta > thresholds_.minImprovementForUpdate) {
                improvementCount++;
            }
        }

        // At least 50% of metrics must improve
        return improvementCount > 0 && improvementCount >= (totalMetrics / 2);
    }

    // Check if baseline should be updated due to significant improvement
    bool shouldUpdateBaseline(const ComparisonResult& comparison) const {
        return isImprovement(comparison) && !isRegression(comparison);
    }

    // Generate human-readable report
    std::string generateReport(const ComparisonResult& comparison) const {
        std::ostringstream report;

        report << "=== Regression Analysis ===\n";
        report << "Test: " << comparison.testName << "\n";
        report << "Severity: " << severityToString(getSeverity(comparison)) << "\n";
        report << "Regression: " << (isRegression(comparison) ? "YES" : "NO") << "\n";
        report << "Improvement: " << (isImprovement(comparison) ? "YES" : "NO") << "\n\n";

        auto violations = getViolations(comparison);
        if (!violations.empty()) {
            report << "Threshold Violations:\n";
            for (const auto& violation : violations) {
                report << "  - " << violation << "\n";
            }
            report << "\n";
        }

        report << "Metric Deltas:\n";
        for (const auto& [metric, delta] : comparison.deltas) {
            report << "  " << metric << ": " << (delta > 0 ? "+" : "") << delta << "%\n";
        }

        if (shouldUpdateBaseline(comparison)) {
            report << "\n[RECOMMENDATION] Significant improvement detected. Consider updating baseline.\n";
        }

        return report.str();
    }

    // Generate JSON report
    std::string generateJSONReport(const ComparisonResult& comparison) const {
        std::ostringstream json;

        json << "{\n";
        json << "  \"test\": \"" << comparison.testName << "\",\n";
        json << "  \"severity\": \"" << severityToString(getSeverity(comparison)) << "\",\n";
        json << "  \"isRegression\": " << (isRegression(comparison) ? "true" : "false") << ",\n";
        json << "  \"isImprovement\": " << (isImprovement(comparison) ? "true" : "false") << ",\n";
        json << "  \"shouldUpdateBaseline\": " << (shouldUpdateBaseline(comparison) ? "true" : "false") << ",\n";

        auto violations = getViolations(comparison);
        json << "  \"violations\": [\n";
        for (size_t i = 0; i < violations.size(); ++i) {
            json << "    \"" << violations[i] << "\"";
            if (i < violations.size() - 1) json << ",";
            json << "\n";
        }
        json << "  ],\n";

        json << "  \"deltas\": {\n";
        size_t i = 0;
        for (const auto& [metric, delta] : comparison.deltas) {
            json << "    \"" << metric << "\": " << delta;
            if (i < comparison.deltas.size() - 1) json << ",";
            json << "\n";
            ++i;
        }
        json << "  }\n";
        json << "}\n";

        return json.str();
    }

private:
    static std::string severityToString(TestFramework::TestSeverity severity) {
        switch (severity) {
            case TestFramework::TestSeverity::Pass: return "Pass";
            case TestFramework::TestSeverity::Warning: return "Warning";
            case TestFramework::TestSeverity::Fail: return "Fail";
            case TestFramework::TestSeverity::Critical: return "Critical";
            default: return "Unknown";
        }
    }
};

} // namespace testing
} // namespace svgplayer

#endif // REGRESSION_DETECTOR_H
