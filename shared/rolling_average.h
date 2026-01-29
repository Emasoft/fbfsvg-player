// rolling_average.h - Rolling average calculator for FPS and performance metrics
// Cross-platform statistics helper for FBF.SVG Player
// Copyright (c) 2024 FBF.SVG Project

#ifndef SHARED_ROLLING_AVERAGE_H
#define SHARED_ROLLING_AVERAGE_H

#include <deque>
#include <cstddef>

namespace svgplayer {

// Rolling average calculator for tracking FPS and performance metrics
// Uses a sliding window of samples for smoothed statistics
class RollingAverage {
   public:
    explicit RollingAverage(size_t windowSize = 120) : maxSize_(windowSize) {}

    void add(double value) {
        values_.push_back(value);
        if (values_.size() > maxSize_) {
            values_.pop_front();
        }
    }

    double average() const {
        if (values_.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values_) sum += v;
        return sum / static_cast<double>(values_.size());
    }

    double min() const {
        if (values_.empty()) return 0.0;
        double m = values_[0];
        for (double v : values_)
            if (v < m) m = v;
        return m;
    }

    double max() const {
        if (values_.empty()) return 0.0;
        double m = values_[0];
        for (double v : values_)
            if (v > m) m = v;
        return m;
    }

    double last() const {
        if (values_.empty()) return 0.0;
        return values_.back();
    }

    size_t count() const { return values_.size(); }

    void reset() { values_.clear(); }

   private:
    std::deque<double> values_;
    size_t maxSize_;
};

}  // namespace svgplayer

#endif  // SHARED_ROLLING_AVERAGE_H
