// thumbnail_cache.h - Background-threaded SVG thumbnail cache
// Provides non-blocking thumbnail loading with LRU eviction
// Main thread NEVER blocks - always returns cached content or placeholder

#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <deque>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <ctime>

namespace svgplayer {

// Thumbnail loading state
enum class ThumbnailState {
    NotLoaded,      // Not in cache, not requested
    Pending,        // Load request queued
    Loading,        // Currently being loaded
    Ready,          // Loaded and cached
    Error           // Failed to load
};

// Cached thumbnail entry
struct ThumbnailCacheEntry {
    std::string filePath;           // Full path to SVG file
    std::string svgContent;         // Processed SVG content (prefixed, wrapped)
    ThumbnailState state = ThumbnailState::NotLoaded;
    float width = 0.0f;             // Requested thumbnail width
    float height = 0.0f;            // Requested thumbnail height
    std::time_t fileModTime = 0;    // File modification time (for invalidation)
    std::chrono::steady_clock::time_point lastAccess;  // For LRU eviction
    size_t contentSize = 0;         // Size of svgContent in bytes
};

// Load request for background thread
struct ThumbnailLoadRequest {
    std::string filePath;
    float width;
    float height;
    int priority;                   // Lower = higher priority (grid index)

    // Priority queue comparison (lower priority value = higher queue priority)
    bool operator<(const ThumbnailLoadRequest& other) const {
        return priority > other.priority;  // Inverted for min-heap behavior
    }
};

class ThumbnailCache {
public:
    // Cache limits
    static constexpr size_t MAX_CACHE_ENTRIES = 100;
    static constexpr size_t MAX_CACHE_BYTES = 100 * 1024 * 1024;  // 100MB total cache
    static constexpr size_t MAX_SVG_FILE_SIZE = 50 * 1024 * 1024;  // 50MB max per file
    static constexpr size_t FAST_THUMBNAIL_THRESHOLD = 2 * 1024 * 1024;  // 2MB - files larger show static preview

    ThumbnailCache();
    ~ThumbnailCache();

    // Non-copyable, non-movable (owns thread)
    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    // Lifecycle
    void startLoader();             // Start background loader thread
    void stopLoader();              // Stop loader (waits for thread)
    bool isLoaderRunning() const { return loaderRunning_.load(); }

    // Cache queries (thread-safe, non-blocking)
    ThumbnailState getState(const std::string& filePath) const;
    std::optional<std::string> getThumbnailSVG(const std::string& filePath);
    bool hasEntry(const std::string& filePath) const;

    // Load requests (thread-safe, non-blocking)
    void requestLoad(const std::string& filePath, float width, float height, int priority);
    void cancelRequest(const std::string& filePath);
    void cancelAllRequests();       // Cancel all pending requests (on directory change)

    // Change detection (for dirty flag)
    bool hasNewReadyThumbnails();   // True if any thumbnail became ready since last check
    void clearNewReadyFlag();       // Reset the new-ready flag

    // Cache management
    void clear();                   // Clear all cached entries
    void evictIfNeeded();           // Evict old entries if over limits
    size_t getCacheSize() const { return totalCacheBytes_.load(); }
    size_t getEntryCount() const;

    // Placeholder SVG generation (for display while loading)
    // cellIndex ensures unique IDs per cell position (deterministic, no race conditions)
    static std::string generatePlaceholder(float width, float height, ThumbnailState state, int cellIndex = 0);
    static std::string generateLoadingSpinner(float width, float height, int cellIndex = 0);

private:
    // Background loader thread function
    void loaderThread();

    // Process a single load request (called by loader thread)
    void processLoadRequest(const ThumbnailLoadRequest& req);

    // Generate thumbnail SVG from file content
    std::string generateThumbnailSVG(const std::string& svgPath,
                                      const std::string& content,
                                      float width, float height);

    // Read SVG file with size limit
    bool readSVGFile(const std::string& path, std::string& content, std::time_t& modTime);

    // LRU eviction helper
    void evictOldestEntry();

    // Cache storage
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, ThumbnailCacheEntry> cache_;
    std::deque<std::string> lruOrder_;  // Front = oldest, back = newest
    std::atomic<size_t> totalCacheBytes_{0};

    // Request queue
    mutable std::mutex queueMutex_;
    std::priority_queue<ThumbnailLoadRequest> requestQueue_;
    std::condition_variable queueCondition_;

    // Loader thread pool state (multiple threads for parallel processing)
    static constexpr size_t NUM_LOADER_THREADS = 4;  // 4 parallel loaders
    std::vector<std::thread> loaderThreads_;
    std::atomic<bool> loaderRunning_{false};
    std::atomic<bool> stopRequested_{false};

    // Change detection
    std::atomic<bool> hasNewReady_{false};
};

} // namespace svgplayer
