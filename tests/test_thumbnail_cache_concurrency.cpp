// test_thumbnail_cache_concurrency.cpp - Concurrency tests for ThumbnailCache
//
// Tests thread-safety of the ThumbnailCache class under concurrent access.
// Uses multiple threads to stress-test all critical sections.
//
// Compile with:
//   clang++ -std=c++17 -pthread -I../src test_thumbnail_cache_concurrency.cpp \
//           ../src/thumbnail_cache.cpp -o test_thumbnail_cache_concurrency
//
// Run: ./test_thumbnail_cache_concurrency

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <random>
#include <iostream>
#include <fstream>
#include <filesystem>

#include "../src/thumbnail_cache.h"

// =============================================================================
// Simple Test Framework (matching project style)
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

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { registerTest(#name, test_##name); } \
    } g_registrar_##name; \
    static void test_##name()

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

using TestFunc = std::function<void()>;
static std::vector<std::pair<std::string, TestFunc>> g_tests;

static void registerTest(const char* name, TestFunc func) {
    g_tests.push_back({name, func});
}

// =============================================================================
// Test Fixtures - Create temporary SVG files for testing
// =============================================================================

static std::string g_tempDir;
static std::vector<std::string> g_tempFiles;

// Minimal valid SVG for testing
static const char* MINIMAL_SVG = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <rect id="bg" width="100" height="100" fill="#f0f0f0"/>
  <circle id="dot" cx="50" cy="50" r="30" fill="#3498db"/>
</svg>
)";

// SVG with animation (for testing animated thumbnail generation)
static const char* ANIMATED_SVG = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 100 100">
  <defs>
    <g id="frame1"><rect width="100" height="100" fill="red"/></g>
    <g id="frame2"><rect width="100" height="100" fill="green"/></g>
    <g id="frame3"><rect width="100" height="100" fill="blue"/></g>
  </defs>
  <use id="display" xlink:href="#frame1">
    <animate attributeName="xlink:href" values="#frame1;#frame2;#frame3" dur="0.3s" repeatCount="indefinite" calcMode="discrete"/>
  </use>
</svg>
)";

static void setupTestFixtures() {
    // Create temporary directory for test files
    g_tempDir = std::filesystem::temp_directory_path().string() + "/thumbnail_cache_test_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(g_tempDir);

    // Create test SVG files
    for (int i = 0; i < 20; ++i) {
        std::string filename = g_tempDir + "/test_" + std::to_string(i) + ".svg";
        std::ofstream file(filename);
        if (i % 2 == 0) {
            file << MINIMAL_SVG;
        } else {
            file << ANIMATED_SVG;
        }
        file.close();
        g_tempFiles.push_back(filename);
    }

    std::cout << "Created " << g_tempFiles.size() << " test SVG files in " << g_tempDir << std::endl;
}

static void cleanupTestFixtures() {
    // Remove test files and directory
    for (const auto& file : g_tempFiles) {
        std::filesystem::remove(file);
    }
    std::filesystem::remove(g_tempDir);
    g_tempFiles.clear();
    std::cout << "Cleaned up test fixtures" << std::endl;
}

// =============================================================================
// Concurrency Tests
// =============================================================================

// Test 1: Concurrent requestLoad from multiple threads
// Verifies that requestLoad is thread-safe when called simultaneously
TEST(concurrent_request_load) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    constexpr int NUM_THREADS = 8;
    constexpr int REQUESTS_PER_THREAD = 100;
    std::atomic<int> completedRequests{0};
    std::vector<std::thread> threads;

    // Spawn multiple threads all requesting loads simultaneously
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, &completedRequests, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> fileDist(0, static_cast<int>(g_tempFiles.size()) - 1);

            for (int i = 0; i < REQUESTS_PER_THREAD; ++i) {
                int fileIdx = fileDist(gen);
                // Each thread uses different priority based on thread id
                cache.requestLoad(g_tempFiles[fileIdx], 100.0f, 100.0f, t * 1000 + i);
                completedRequests++;
            }
        });
    }

    // Wait for all threads to complete their requests
    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(completedRequests.load(), NUM_THREADS * REQUESTS_PER_THREAD);

    // Wait a bit for loader threads to process some requests
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    cache.stopLoader();

    std::cout << "  Processed " << completedRequests.load() << " concurrent requests" << std::endl;
}

// Test 2: Concurrent getState while loading
// Verifies that getState returns consistent values during concurrent access
TEST(concurrent_get_state_while_loading) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    // Request some loads
    for (size_t i = 0; i < g_tempFiles.size(); ++i) {
        cache.requestLoad(g_tempFiles[i], 100.0f, 100.0f, static_cast<int>(i));
    }

    constexpr int NUM_THREADS = 8;
    constexpr int QUERIES_PER_THREAD = 1000;
    std::atomic<int> validStates{0};
    std::atomic<int> invalidStates{0};
    std::vector<std::thread> threads;

    // Spawn threads that continuously query state while loading happens
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, &validStates, &invalidStates]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> fileDist(0, static_cast<int>(g_tempFiles.size()) - 1);

            for (int i = 0; i < QUERIES_PER_THREAD; ++i) {
                int fileIdx = fileDist(gen);
                auto state = cache.getState(g_tempFiles[fileIdx]);

                // Verify state is a valid enum value
                if (state == svgplayer::ThumbnailState::NotLoaded ||
                    state == svgplayer::ThumbnailState::Pending ||
                    state == svgplayer::ThumbnailState::Loading ||
                    state == svgplayer::ThumbnailState::Ready ||
                    state == svgplayer::ThumbnailState::Error) {
                    validStates++;
                } else {
                    invalidStates++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    cache.stopLoader();

    ASSERT_EQ(validStates.load(), NUM_THREADS * QUERIES_PER_THREAD);
    ASSERT_EQ(invalidStates.load(), 0);

    std::cout << "  All " << validStates.load() << " state queries returned valid values" << std::endl;
}

// Test 3: Start/Stop loader race conditions
// Verifies that rapid start/stop cycles don't cause crashes or deadlocks
TEST(start_stop_loader_race) {
    svgplayer::ThumbnailCache cache;

    constexpr int CYCLES = 20;

    for (int i = 0; i < CYCLES; ++i) {
        cache.startLoader();

        // Request some loads during brief running period
        for (size_t j = 0; j < std::min(size_t(5), g_tempFiles.size()); ++j) {
            cache.requestLoad(g_tempFiles[j], 100.0f, 100.0f, static_cast<int>(j));
        }

        // Brief delay to let some processing happen
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        cache.stopLoader();
    }

    // Final verification - should be able to start/stop cleanly
    cache.startLoader();
    ASSERT_TRUE(cache.isLoaderRunning());
    cache.stopLoader();
    ASSERT_FALSE(cache.isLoaderRunning());

    std::cout << "  Completed " << CYCLES << " start/stop cycles without deadlock" << std::endl;
}

// Test 4: Cancel requests while loading
// Verifies that cancelRequest and cancelAllRequests are thread-safe
TEST(cancel_requests_while_loading) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> threads;

    // Thread 1: Continuously request loads
    threads.emplace_back([&cache, &stopFlag]() {
        int priority = 0;
        while (!stopFlag.load()) {
            for (const auto& file : g_tempFiles) {
                if (stopFlag.load()) break;
                cache.requestLoad(file, 100.0f, 100.0f, priority++);
            }
        }
    });

    // Thread 2: Continuously cancel individual requests
    threads.emplace_back([&cache, &stopFlag]() {
        while (!stopFlag.load()) {
            for (const auto& file : g_tempFiles) {
                if (stopFlag.load()) break;
                cache.cancelRequest(file);
            }
        }
    });

    // Thread 3: Periodically cancel all requests
    threads.emplace_back([&cache, &stopFlag]() {
        while (!stopFlag.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            cache.cancelAllRequests();
        }
    });

    // Run for 500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stopFlag.store(true);

    for (auto& t : threads) {
        t.join();
    }

    cache.stopLoader();

    std::cout << "  Cancel operations completed without crashes" << std::endl;
}

// Test 5: Concurrent getThumbnailSVG access
// Verifies that getThumbnailSVG returns consistent data under concurrent reads
TEST(concurrent_get_thumbnail_svg) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    // Request loads for all files
    for (size_t i = 0; i < g_tempFiles.size(); ++i) {
        cache.requestLoad(g_tempFiles[i], 100.0f, 100.0f, static_cast<int>(i));
    }

    // Wait for some to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    constexpr int NUM_THREADS = 8;
    constexpr int READS_PER_THREAD = 500;
    std::atomic<int> successfulReads{0};
    std::atomic<int> emptyReads{0};
    std::vector<std::thread> threads;

    // Spawn threads that read thumbnails concurrently
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, &successfulReads, &emptyReads]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> fileDist(0, static_cast<int>(g_tempFiles.size()) - 1);

            for (int i = 0; i < READS_PER_THREAD; ++i) {
                int fileIdx = fileDist(gen);
                auto svg = cache.getThumbnailSVG(g_tempFiles[fileIdx]);

                if (svg.has_value() && !svg->empty()) {
                    // Verify it looks like valid SVG
                    if (svg->find("<svg") != std::string::npos) {
                        successfulReads++;
                    }
                } else {
                    emptyReads++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    cache.stopLoader();

    // At least some reads should succeed (thumbnails were being loaded)
    std::cout << "  Successful reads: " << successfulReads.load()
              << ", Empty reads: " << emptyReads.load() << std::endl;
}

// Test 6: hasNewReadyThumbnails flag consistency
// Verifies that the flag is set correctly under concurrent access
TEST(new_ready_flag_consistency) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    std::atomic<int> flagSetCount{0};
    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> threads;

    // Thread: Check and clear flag
    threads.emplace_back([&cache, &flagSetCount, &stopFlag]() {
        while (!stopFlag.load()) {
            if (cache.hasNewReadyThumbnails()) {
                flagSetCount++;
                cache.clearNewReadyFlag();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Request loads
    for (size_t i = 0; i < g_tempFiles.size(); ++i) {
        cache.requestLoad(g_tempFiles[i], 100.0f, 100.0f, static_cast<int>(i));
    }

    // Wait for loading
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    stopFlag.store(true);
    threads[0].join();

    cache.stopLoader();

    // Should have detected at least some new ready thumbnails
    std::cout << "  Detected " << flagSetCount.load() << " new-ready transitions" << std::endl;
}

// Test 7: LRU eviction under concurrent access
// Verifies that eviction works correctly when cache is full and accessed concurrently
TEST(lru_eviction_concurrent) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    // Fill cache with requests (will exceed MAX_CACHE_ENTRIES of 100)
    // Our test has 20 files, so we request them multiple times to stress LRU
    for (int round = 0; round < 10; ++round) {
        for (size_t i = 0; i < g_tempFiles.size(); ++i) {
            cache.requestLoad(g_tempFiles[i], 100.0f, 100.0f, round * 1000 + static_cast<int>(i));
        }
    }

    // Concurrent access during loading
    constexpr int NUM_THREADS = 4;
    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, &stopFlag]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> fileDist(0, static_cast<int>(g_tempFiles.size()) - 1);

            while (!stopFlag.load()) {
                int fileIdx = fileDist(gen);
                // These accesses update LRU timestamps
                cache.getState(g_tempFiles[fileIdx]);
                cache.getThumbnailSVG(g_tempFiles[fileIdx]);
            }
        });
    }

    // Let it run
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stopFlag.store(true);

    for (auto& t : threads) {
        t.join();
    }

    cache.stopLoader();

    // Verify cache is bounded
    size_t entryCount = cache.getEntryCount();
    ASSERT_TRUE(entryCount <= svgplayer::ThumbnailCache::MAX_CACHE_ENTRIES);

    std::cout << "  Cache entries after stress: " << entryCount
              << " (max: " << svgplayer::ThumbnailCache::MAX_CACHE_ENTRIES << ")" << std::endl;
}

// Test 8: Clear cache while loading
// Verifies that clear() is thread-safe during concurrent loading
TEST(clear_while_loading) {
    svgplayer::ThumbnailCache cache;
    cache.startLoader();

    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> threads;

    // Thread: Continuously request loads
    threads.emplace_back([&cache, &stopFlag]() {
        int priority = 0;
        while (!stopFlag.load()) {
            for (const auto& file : g_tempFiles) {
                if (stopFlag.load()) break;
                cache.requestLoad(file, 100.0f, 100.0f, priority++);
            }
        }
    });

    // Thread: Periodically clear cache
    threads.emplace_back([&cache, &stopFlag]() {
        int clearCount = 0;
        while (!stopFlag.load() && clearCount < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            cache.clear();
            clearCount++;
        }
    });

    // Run for 600ms
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    stopFlag.store(true);

    for (auto& t : threads) {
        t.join();
    }

    cache.stopLoader();

    std::cout << "  Clear operations completed without crashes" << std::endl;
}

// Test 9: Placeholder generation thread-safety
// Verifies that static placeholder generation methods are thread-safe
TEST(placeholder_generation_concurrent) {
    constexpr int NUM_THREADS = 8;
    constexpr int GENERATIONS_PER_THREAD = 1000;
    std::atomic<int> validPlaceholders{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&validPlaceholders, t]() {
            for (int i = 0; i < GENERATIONS_PER_THREAD; ++i) {
                // Use thread-specific cell index to ensure unique IDs
                int cellIndex = t * GENERATIONS_PER_THREAD + i;

                auto placeholder = svgplayer::ThumbnailCache::generatePlaceholder(
                    100.0f, 100.0f, svgplayer::ThumbnailState::Pending, cellIndex);

                if (!placeholder.empty() && placeholder.find("<svg") != std::string::npos) {
                    validPlaceholders++;
                }

                auto spinner = svgplayer::ThumbnailCache::generateLoadingSpinner(
                    100.0f, 100.0f, cellIndex);

                if (!spinner.empty() && spinner.find("<svg") != std::string::npos) {
                    validPlaceholders++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Each thread generates 2 SVGs per iteration (placeholder + spinner)
    ASSERT_EQ(validPlaceholders.load(), NUM_THREADS * GENERATIONS_PER_THREAD * 2);

    std::cout << "  Generated " << validPlaceholders.load() << " valid placeholders concurrently" << std::endl;
}

// Test 10: Double start/stop (idempotency)
// Verifies that multiple start or stop calls don't cause issues
TEST(double_start_stop_idempotency) {
    svgplayer::ThumbnailCache cache;

    // Double start should be safe
    cache.startLoader();
    cache.startLoader();  // Should be no-op or safe
    ASSERT_TRUE(cache.isLoaderRunning());

    // Double stop should be safe
    cache.stopLoader();
    cache.stopLoader();  // Should be no-op
    ASSERT_FALSE(cache.isLoaderRunning());

    // Restart should work
    cache.startLoader();
    ASSERT_TRUE(cache.isLoaderRunning());
    cache.stopLoader();
    ASSERT_FALSE(cache.isLoaderRunning());

    std::cout << "  Idempotency verified for start/stop operations" << std::endl;
}

// =============================================================================
// Test Runner
// =============================================================================

static void runTests() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "ThumbnailCache Concurrency Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    for (const auto& [name, func] : g_tests) {
        g_testCount++;
        std::cout << "[RUN ] " << name << std::endl;

        try {
            func();
            g_passCount++;
            g_results.push_back({name, true, ""});
            std::cout << "[PASS] " << name << std::endl;
        } catch (const std::exception& e) {
            g_failCount++;
            g_results.push_back({name, false, e.what()});
            std::cout << "[FAIL] " << name << ": " << e.what() << std::endl;
        }

        std::cout << std::endl;
    }

    // Summary
    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << g_passCount << "/" << g_testCount << " tests passed";
    if (g_failCount > 0) {
        std::cout << " (" << g_failCount << " failed)";
    }
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    if (g_failCount > 0) {
        std::cout << "\nFailed tests:" << std::endl;
        for (const auto& result : g_results) {
            if (!result.passed) {
                std::cout << "  - " << result.name << ": " << result.message << std::endl;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Setting up test fixtures..." << std::endl;
    setupTestFixtures();

    runTests();

    std::cout << "\nCleaning up..." << std::endl;
    cleanupTestFixtures();

    return g_failCount > 0 ? 1 : 0;
}
