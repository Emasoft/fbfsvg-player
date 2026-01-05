// Copyright 2025 SVG Player Project
// SPDX-License-Identifier: MIT

#ifndef SVG_DETERMINISTIC_CLOCK_H
#define SVG_DETERMINISTIC_CLOCK_H

#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <thread>

namespace svgplayer {
namespace testing {

/**
 * DeterministicClock - Provides controllable time for testing animations and timing logic.
 *
 * In normal mode, returns real system time.
 * In mocked mode, returns a controllable time that can be set or advanced programmatically.
 * Thread-safe for concurrent access.
 */
class DeterministicClock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    DeterministicClock()
        : enabled_(false)
        , mocked_time_(std::chrono::steady_clock::now()) {
    }

    /**
     * Enable deterministic mode - now() will return mocked time.
     * Initial mocked time is set to current real time.
     */
    inline void enable() {
        std::lock_guard<std::mutex> lock(mutex_);
        mocked_time_ = std::chrono::steady_clock::now();
        enabled_.store(true, std::memory_order_release);
    }

    /**
     * Disable deterministic mode - now() will return real system time.
     */
    inline void disable() {
        enabled_.store(false, std::memory_order_release);
    }

    /**
     * Check if deterministic mode is enabled.
     */
    inline bool isEnabled() const {
        return enabled_.load(std::memory_order_acquire);
    }

    /**
     * Set the mocked time to a specific time_point.
     * Only has effect when deterministic mode is enabled.
     */
    inline void setCurrentTime(time_point t) {
        std::lock_guard<std::mutex> lock(mutex_);
        mocked_time_ = t;
    }

    /**
     * Advance the mocked time by a specific duration.
     * Only has effect when deterministic mode is enabled.
     */
    template<typename Rep, typename Period>
    inline void advanceBy(std::chrono::duration<Rep, Period> delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        mocked_time_ += std::chrono::duration_cast<duration>(delta);
    }

    /**
     * Get current time - either real or mocked depending on mode.
     */
    inline time_point now() const {
        if (enabled_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(mutex_);
            return mocked_time_;
        }
        return std::chrono::steady_clock::now();
    }

    /**
     * Reset mocked time to current real time.
     */
    inline void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        mocked_time_ = std::chrono::steady_clock::now();
    }

private:
    std::atomic<bool> enabled_;
    mutable std::mutex mutex_;
    time_point mocked_time_;
};

/**
 * DeterministicScheduler - Provides controllable thread scheduling for testing concurrent code.
 *
 * In normal mode, operations execute on real threads immediately.
 * In deterministic mode, operations are queued and executed in a controlled order.
 * This allows reproducible testing of race conditions and thread interactions.
 */
class DeterministicScheduler {
public:
    using Operation = std::function<void()>;

    DeterministicScheduler()
        : enabled_(false)
        , num_threads_(0)
        , operations_pending_(0)
        , barrier_count_(0)
        , barrier_generation_(0) {
    }

    ~DeterministicScheduler() {
        disable();
    }

    /**
     * Enable deterministic scheduling mode.
     * @param numThreads Number of virtual worker threads to simulate
     */
    inline void enable(size_t numThreads) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled_.load(std::memory_order_acquire)) {
            return; // Already enabled
        }

        num_threads_ = numThreads;
        enabled_.store(true, std::memory_order_release);
        operations_pending_.store(0, std::memory_order_release);
    }

    /**
     * Disable deterministic scheduling - drain all pending operations first.
     */
    inline void disable() {
        if (!enabled_.load(std::memory_order_acquire)) {
            return;
        }

        drainQueue();

        std::lock_guard<std::mutex> lock(mutex_);
        enabled_.store(false, std::memory_order_release);
        num_threads_ = 0;
    }

    /**
     * Check if deterministic scheduling is enabled.
     */
    inline bool isEnabled() const {
        return enabled_.load(std::memory_order_acquire);
    }

    /**
     * Schedule an operation for execution.
     * In deterministic mode, queues the operation.
     * In normal mode, executes immediately.
     */
    inline void schedule(Operation op) {
        if (!enabled_.load(std::memory_order_acquire)) {
            op(); // Execute immediately in normal mode
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            operation_queue_.push(std::move(op));
            operations_pending_.fetch_add(1, std::memory_order_release);
        }
        cv_.notify_one();
    }

    /**
     * Execute exactly N queued operations in FIFO order.
     * Returns the actual number executed (may be less if queue is empty).
     */
    inline size_t executeOperations(size_t count) {
        if (!enabled_.load(std::memory_order_acquire)) {
            return 0;
        }

        size_t executed = 0;
        for (size_t i = 0; i < count; ++i) {
            Operation op;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (operation_queue_.empty()) {
                    break;
                }
                op = std::move(operation_queue_.front());
                operation_queue_.pop();
                operations_pending_.fetch_sub(1, std::memory_order_release);
            }

            if (op) {
                op();
                ++executed;
            }
        }

        return executed;
    }

    /**
     * Process all pending operations until queue is empty.
     * Returns the number of operations executed.
     */
    inline size_t drainQueue() {
        if (!enabled_.load(std::memory_order_acquire)) {
            return 0;
        }

        size_t executed = 0;
        while (true) {
            Operation op;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (operation_queue_.empty()) {
                    break;
                }
                op = std::move(operation_queue_.front());
                operation_queue_.pop();
                operations_pending_.fetch_sub(1, std::memory_order_release);
            }

            if (op) {
                op();
                ++executed;
            }
        }

        return executed;
    }

    /**
     * Get the number of pending operations in the queue.
     */
    inline size_t pendingOperations() const {
        return operations_pending_.load(std::memory_order_acquire);
    }

    /**
     * Synchronization barrier - blocks until all virtual threads reach this point.
     *
     * In deterministic mode, allows N virtual threads to synchronize.
     * In normal mode, this is a no-op.
     *
     * Call this from scheduled operations to create synchronization points.
     */
    inline void synchronize() {
        if (!enabled_.load(std::memory_order_acquire)) {
            return;
        }

        std::unique_lock<std::mutex> lock(barrier_mutex_);

        const size_t current_generation = barrier_generation_;
        ++barrier_count_;

        if (barrier_count_ >= num_threads_) {
            // Last thread to arrive - reset barrier and wake all
            barrier_count_ = 0;
            ++barrier_generation_;
            barrier_cv_.notify_all();
        } else {
            // Wait for all threads to arrive
            barrier_cv_.wait(lock, [this, current_generation]() {
                return barrier_generation_ != current_generation;
            });
        }
    }

    /**
     * Clear all pending operations without executing them.
     * Use with caution - may leave system in inconsistent state.
     */
    inline void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!operation_queue_.empty()) {
            operation_queue_.pop();
        }
        operations_pending_.store(0, std::memory_order_release);
    }

private:
    std::atomic<bool> enabled_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Operation> operation_queue_;
    std::atomic<size_t> operations_pending_;
    size_t num_threads_;

    // Barrier synchronization
    std::mutex barrier_mutex_;
    std::condition_variable barrier_cv_;
    size_t barrier_count_;
    size_t barrier_generation_;
};

} // namespace testing
} // namespace svgplayer

#endif // SVG_DETERMINISTIC_CLOCK_H
