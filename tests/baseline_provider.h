// baseline_provider.h - Baseline storage and comparison for performance/correctness testing
// C++17 header-only implementation
#pragma once

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__ || __linux__
#include <sys/utsname.h>
#endif

namespace svgplayer {
namespace testing {

// Result of comparing current metrics against baseline
struct ComparisonResult {
    std::string testName;                            // Name of the test
    bool isRegression = false;
    std::string summary;
    std::map<std::string, double> deltas;            // metric name -> percent change
    std::vector<std::string> violations;             // threshold violations
};

// Baseline storage and comparison provider
class BaselineProvider {
public:
    explicit BaselineProvider(const std::string& baselineDir)
        : baselineDir_(baselineDir), platformId_(getPlatformId()) {
        // Ensure platform directory exists
        std::filesystem::create_directories(getBasePath());
    }

    // Detect platform identifier (e.g., "macos_arm64", "linux_x64")
    static std::string getPlatformId() {
#ifdef _WIN32
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        std::string arch = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64" :
                          (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? "arm64" : "unknown";
        return "windows_" + arch;
#elif defined(__APPLE__) || defined(__linux__)
        struct utsname buffer;
        if (uname(&buffer) != 0) {
            return "unknown";
        }

        std::string os;
#ifdef __APPLE__
        os = "macos";
#else
        os = "linux";
#endif

        std::string arch = buffer.machine;
        // Normalize architecture names
        if (arch == "x86_64" || arch == "amd64") {
            arch = "x64";
        } else if (arch == "aarch64") {
            arch = "arm64";
        }

        return os + "_" + arch;
#else
        return "unknown";
#endif
    }

    // Get baseline JSON for a test
    std::optional<std::string> getBaseline(const std::string& testName) const {
        auto path = getTestPath(testName);
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Save baseline JSON for a test
    bool saveBaseline(const std::string& testName, const std::string& jsonData) {
        auto path = getTestPath(testName);
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }

        file << jsonData;
        return file.good();
    }

    // Check if baseline exists for a test
    bool hasBaseline(const std::string& testName) const {
        return std::filesystem::exists(getTestPath(testName));
    }

    // Compare current metrics against baseline
    ComparisonResult compare(const std::string& testName, const std::string& currentJSON,
                            const std::map<std::string, double>& thresholds = {}) const {
        ComparisonResult result;
        result.testName = testName;

        auto baselineJSON = getBaseline(testName);
        if (!baselineJSON) {
            result.summary = "No baseline found for test: " + testName;
            return result;
        }

        // Parse both JSONs to extract metrics (simple key:value parsing)
        auto baselineMetrics = parseMetrics(*baselineJSON);
        auto currentMetrics = parseMetrics(currentJSON);

        // Calculate deltas for each metric
        std::vector<std::string> regressions;
        for (const auto& [key, currentValue] : currentMetrics) {
            auto it = baselineMetrics.find(key);
            if (it == baselineMetrics.end()) {
                continue;  // New metric, skip comparison
            }

            double baselineValue = it->second;
            if (std::abs(baselineValue) < 1e-9) {
                continue;  // Avoid division by zero
            }

            double percentChange = ((currentValue - baselineValue) / baselineValue) * 100.0;
            result.deltas[key] = percentChange;

            // Check against threshold
            auto thresholdIt = thresholds.find(key);
            if (thresholdIt != thresholds.end()) {
                double threshold = thresholdIt->second;
                if (std::abs(percentChange) > threshold) {
                    std::string violation = key + ": " +
                                          std::to_string(percentChange) + "% (threshold: " +
                                          std::to_string(threshold) + "%)";
                    result.violations.push_back(violation);
                    regressions.push_back(violation);
                    result.isRegression = true;
                }
            }
        }

        // Generate summary
        if (result.isRegression) {
            result.summary = "REGRESSION detected in " + testName + " (" +
                           std::to_string(regressions.size()) + " violation(s))";
        } else {
            result.summary = "No regressions detected in " + testName;
        }

        return result;
    }

    // Get last known good commit hash
    std::string getLastGoodCommit() const {
        auto path = getBasePath() / "commit_hash.txt";
        if (!std::filesystem::exists(path)) {
            return "";
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }

        std::string commit;
        std::getline(file, commit);
        return commit;
    }

    // Save last known good commit hash
    bool saveLastGoodCommit(const std::string& commitHash) {
        auto path = getBasePath() / "commit_hash.txt";
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }

        file << commitHash;
        return file.good();
    }

    // Get the platform-specific baseline directory
    std::filesystem::path getBasePath() const {
        return std::filesystem::path(baselineDir_) / platformId_;
    }

private:
    std::string baselineDir_;
    std::string platformId_;

    // Get path for a specific test baseline
    std::filesystem::path getTestPath(const std::string& testName) const {
        return getBasePath() / (testName + ".json");
    }

    // Simple JSON metric parser (extracts "key": value pairs)
    // NOTE: This is a minimal parser for numeric metrics only, not a full JSON parser
    std::map<std::string, double> parseMetrics(const std::string& json) const {
        std::map<std::string, double> metrics;

        std::istringstream stream(json);
        std::string line;

        while (std::getline(stream, line)) {
            // Look for pattern: "key": value or "key":value
            size_t quoteStart = line.find('"');
            if (quoteStart == std::string::npos) continue;

            size_t quoteEnd = line.find('"', quoteStart + 1);
            if (quoteEnd == std::string::npos) continue;

            std::string key = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

            size_t colonPos = line.find(':', quoteEnd);
            if (colonPos == std::string::npos) continue;

            // Extract numeric value after colon
            std::string valueStr;
            for (size_t i = colonPos + 1; i < line.size(); ++i) {
                char c = line[i];
                if (std::isdigit(c) || c == '.' || c == '-' || c == 'e' || c == 'E' || c == '+') {
                    valueStr += c;
                } else if (!valueStr.empty() && !std::isspace(c)) {
                    break;  // End of number
                }
            }

            if (!valueStr.empty()) {
                try {
                    double value = std::stod(valueStr);
                    metrics[key] = value;
                } catch (...) {
                    // Ignore parse errors
                }
            }
        }

        return metrics;
    }
};

}  // namespace testing
}  // namespace svgplayer
