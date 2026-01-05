#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>

namespace svgplayer {
namespace testing {

struct PerformanceMetrics {
    double avgRenderTimeMs = 0.0;
    double maxRenderTimeMs = 0.0;
    double minRenderTimeMs = std::numeric_limits<double>::max();
    double p95RenderTimeMs = 0.0;
    double p99RenderTimeMs = 0.0;
    double measuredFPS = 0.0;
    int droppedFrameCount = 0;
    int totalFramesRendered = 0;
    double thumbnailsPerSecond = 0.0;
};

struct MemoryMetrics {
    size_t peakCacheBytes = 0;
    size_t currentCacheBytes = 0;
    int evictionCount = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    bool hasLeaks = false;
};

struct CorrectnessMetrics {
    int validStateTransitions = 0;
    int invalidStateTransitions = 0;
    bool idPrefixingCorrect = true;
    bool cacheConsistent = true;
};

class MetricsCollector {
public:
    static MetricsCollector& getInstance() {
        static MetricsCollector instance;
        return instance;
    }

    // Collection control
    void beginCollection(const std::string& testName) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentTestName_ = testName;
        reset();
        collectionStartTime_ = std::chrono::steady_clock::now();
        collecting_ = true;
    }

    void endCollection() {
        std::lock_guard<std::mutex> lock(mutex_);
        collectionEndTime_ = std::chrono::steady_clock::now();
        collecting_ = false;
        finalizeMetrics();
    }

    void reset() {
        renderTimes_.clear();
        thumbnailLoadTimes_.clear();

        performance_ = PerformanceMetrics();
        memory_ = MemoryMetrics();
        correctness_ = CorrectnessMetrics();

        performance_.minRenderTimeMs = std::numeric_limits<double>::max();
    }

    // Recording methods (called by instrumentation hooks)
    void recordRenderTime(double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        renderTimes_.push_back(ms);
    }

    void recordFrameRendered(int frameIndex) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        performance_.totalFramesRendered++;
    }

    void recordFrameSkipped(int frameIndex) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        performance_.droppedFrameCount++;
    }

    void recordThumbnailLoad(const std::string& path, double loadTimeMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        thumbnailLoadTimes_.push_back(loadTimeMs);
    }

    void recordCacheOperation(bool hit, size_t cacheSize) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;

        if (hit) {
            memory_.cacheHits++;
        } else {
            memory_.cacheMisses++;
        }

        memory_.currentCacheBytes = cacheSize;
        if (cacheSize > memory_.peakCacheBytes) {
            memory_.peakCacheBytes = cacheSize;
        }
    }

    void recordStateTransition(bool valid) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;

        if (valid) {
            correctness_.validStateTransitions++;
        } else {
            correctness_.invalidStateTransitions++;
        }
    }

    void recordMemory(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;

        memory_.currentCacheBytes = bytes;
        if (bytes > memory_.peakCacheBytes) {
            memory_.peakCacheBytes = bytes;
        }
    }

    void recordEviction() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        memory_.evictionCount++;
    }

    void recordMemoryLeak(bool hasLeak) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        memory_.hasLeaks = hasLeak;
    }

    void recordIdPrefixingError() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        correctness_.idPrefixingCorrect = false;
    }

    void recordCacheInconsistency() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!collecting_) return;
        correctness_.cacheConsistent = false;
    }

    // Accessors
    PerformanceMetrics getPerformance() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return performance_;
    }

    MemoryMetrics getMemory() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return memory_;
    }

    CorrectnessMetrics getCorrectness() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return correctness_;
    }

    // Serialization
    std::string toJSON() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);

        oss << "{\n";
        oss << "  \"testName\": \"" << currentTestName_ << "\",\n";

        oss << "  \"performance\": {\n";
        oss << "    \"avgRenderTimeMs\": " << performance_.avgRenderTimeMs << ",\n";
        oss << "    \"maxRenderTimeMs\": " << performance_.maxRenderTimeMs << ",\n";
        oss << "    \"minRenderTimeMs\": " << performance_.minRenderTimeMs << ",\n";
        oss << "    \"p95RenderTimeMs\": " << performance_.p95RenderTimeMs << ",\n";
        oss << "    \"p99RenderTimeMs\": " << performance_.p99RenderTimeMs << ",\n";
        oss << "    \"measuredFPS\": " << performance_.measuredFPS << ",\n";
        oss << "    \"droppedFrameCount\": " << performance_.droppedFrameCount << ",\n";
        oss << "    \"totalFramesRendered\": " << performance_.totalFramesRendered << ",\n";
        oss << "    \"thumbnailsPerSecond\": " << performance_.thumbnailsPerSecond << "\n";
        oss << "  },\n";

        oss << "  \"memory\": {\n";
        oss << "    \"peakCacheBytes\": " << memory_.peakCacheBytes << ",\n";
        oss << "    \"currentCacheBytes\": " << memory_.currentCacheBytes << ",\n";
        oss << "    \"evictionCount\": " << memory_.evictionCount << ",\n";
        oss << "    \"cacheHits\": " << memory_.cacheHits << ",\n";
        oss << "    \"cacheMisses\": " << memory_.cacheMisses << ",\n";
        oss << "    \"hasLeaks\": " << (memory_.hasLeaks ? "true" : "false") << "\n";
        oss << "  },\n";

        oss << "  \"correctness\": {\n";
        oss << "    \"validStateTransitions\": " << correctness_.validStateTransitions << ",\n";
        oss << "    \"invalidStateTransitions\": " << correctness_.invalidStateTransitions << ",\n";
        oss << "    \"idPrefixingCorrect\": " << (correctness_.idPrefixingCorrect ? "true" : "false") << ",\n";
        oss << "    \"cacheConsistent\": " << (correctness_.cacheConsistent ? "true" : "false") << "\n";
        oss << "  }\n";
        oss << "}\n";

        return oss.str();
    }

    bool fromJSON(const std::string& json) {
        // Basic JSON parsing - in production, use a proper JSON library
        // This is a simplified implementation for testing purposes
        std::lock_guard<std::mutex> lock(mutex_);

        // For now, return false to indicate not fully implemented
        // A full implementation would parse the JSON string
        return false;
    }

private:
    MetricsCollector() : collecting_(false) {}
    ~MetricsCollector() = default;

    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    void finalizeMetrics() {
        // Calculate render time statistics
        if (!renderTimes_.empty()) {
            std::vector<double> sorted = renderTimes_;
            std::sort(sorted.begin(), sorted.end());

            double sum = 0.0;
            for (double time : sorted) {
                sum += time;
                if (time > performance_.maxRenderTimeMs) {
                    performance_.maxRenderTimeMs = time;
                }
                if (time < performance_.minRenderTimeMs) {
                    performance_.minRenderTimeMs = time;
                }
            }

            performance_.avgRenderTimeMs = sum / sorted.size();
            performance_.p95RenderTimeMs = calculatePercentile(sorted, 0.95);
            performance_.p99RenderTimeMs = calculatePercentile(sorted, 0.99);
        }

        // Calculate FPS based on collection duration
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            collectionEndTime_ - collectionStartTime_
        ).count();

        if (duration > 0) {
            performance_.measuredFPS = (performance_.totalFramesRendered * 1000.0) / duration;

            if (!thumbnailLoadTimes_.empty()) {
                performance_.thumbnailsPerSecond = (thumbnailLoadTimes_.size() * 1000.0) / duration;
            }
        }
    }

    double calculatePercentile(const std::vector<double>& sorted, double percentile) const {
        if (sorted.empty()) return 0.0;

        double index = percentile * (sorted.size() - 1);
        size_t lower = static_cast<size_t>(std::floor(index));
        size_t upper = static_cast<size_t>(std::ceil(index));

        if (lower == upper) {
            return sorted[lower];
        }

        double fraction = index - lower;
        return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
    }

    mutable std::mutex mutex_;
    bool collecting_;
    std::string currentTestName_;

    std::chrono::steady_clock::time_point collectionStartTime_;
    std::chrono::steady_clock::time_point collectionEndTime_;

    std::vector<double> renderTimes_;
    std::vector<double> thumbnailLoadTimes_;

    PerformanceMetrics performance_;
    MemoryMetrics memory_;
    CorrectnessMetrics correctness_;
};

} // namespace testing
} // namespace svgplayer
