// thumbnail_cache.cpp - Background-threaded SVG thumbnail cache implementation

#include "thumbnail_cache.h"
#include "../shared/SVGGridCompositor.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <iostream>

// Uncomment to enable verbose thumbnail loading logs
#define THUMBNAIL_CACHE_DEBUG 1

namespace svgplayer {

ThumbnailCache::ThumbnailCache() {
    // Constructor - loader not started yet
}

ThumbnailCache::~ThumbnailCache() {
    stopLoader();
}

void ThumbnailCache::startLoader() {
    // Fix Issue 2: Use atomic compare_exchange to prevent race condition
    bool expected = false;
    if (!loaderRunning_.compare_exchange_strong(expected, true)) {
        return;  // Already running
    }

    stopRequested_.store(false);

    // Start multiple loader threads for parallel processing
    loaderThreads_.clear();
    loaderThreads_.reserve(NUM_LOADER_THREADS);
    for (size_t i = 0; i < NUM_LOADER_THREADS; ++i) {
        loaderThreads_.emplace_back(&ThumbnailCache::loaderThread, this);
    }
#ifdef THUMBNAIL_CACHE_DEBUG
    std::cout << "[ThumbnailCache] Started " << NUM_LOADER_THREADS << " loader threads" << std::endl;
#endif
}

void ThumbnailCache::stopLoader() {
    if (!loaderRunning_.load()) return;

    // Signal stop and wake up all threads
    stopRequested_.store(true);
    queueCondition_.notify_all();

    // Wait for all threads to finish
    for (auto& thread : loaderThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    loaderThreads_.clear();

    loaderRunning_.store(false);
}

void ThumbnailCache::loaderThread() {
    // Get a simple thread index for logging (hash of thread id, mod 100 for readability)
    auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100;

    while (!stopRequested_.load()) {
        ThumbnailLoadRequest req;
        bool hasRequest = false;
        size_t queueSize = 0;

        // Wait for a request or stop signal
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this]() {
                return stopRequested_.load() || !requestQueue_.empty();
            });

            if (stopRequested_.load()) break;

            if (!requestQueue_.empty()) {
                req = requestQueue_.top();
                requestQueue_.pop();
                queueSize = requestQueue_.size();
                hasRequest = true;
            }
        }

        // Process request outside the lock
        if (hasRequest) {
            processLoadRequest(req);
        }
    }
#ifdef THUMBNAIL_CACHE_DEBUG
    std::cout << "[ThumbnailCache] Loader thread exiting" << std::endl;
#endif
}

void ThumbnailCache::processLoadRequest(const ThumbnailLoadRequest& req) {
    auto startTime = std::chrono::steady_clock::now();

    // Extract filename for logging
    std::string filename = req.filePath;
    auto lastSlash = filename.rfind('/');
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    std::cout << "[ThumbnailCache] Processing: " << filename << std::endl;

    // Check if already loaded or loading
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(req.filePath);
        if (it != cache_.end() &&
            (it->second.state == ThumbnailState::Ready ||
             it->second.state == ThumbnailState::Loading)) {
            return;  // Already processed
        }

        // Mark as loading
        if (it != cache_.end()) {
            it->second.state = ThumbnailState::Loading;
        } else {
            ThumbnailCacheEntry entry;
            entry.filePath = req.filePath;
            entry.width = req.width;
            entry.height = req.height;
            entry.state = ThumbnailState::Loading;
            entry.lastAccess = std::chrono::steady_clock::now();
            cache_[req.filePath] = std::move(entry);
        }
    }

    // Read file (outside lock - this is the slow part)
    std::string content;
    std::time_t modTime;
    bool success = readSVGFile(req.filePath, content, modTime);

    // Generate thumbnail SVG
    std::string thumbnailSVG;
    if (success) {
        thumbnailSVG = generateThumbnailSVG(req.filePath, content, req.width, req.height);
    }

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    std::cout << "[ThumbnailCache] " << filename << ": total=" << totalMs << "ms"
              << (success ? "" : " (FAILED)")
              << ", content=" << (thumbnailSVG.empty() ? "empty" : std::to_string(thumbnailSVG.size()) + " bytes")
              << std::endl;

    // Update cache with result
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(req.filePath);
        if (it != cache_.end()) {
            if (success && !thumbnailSVG.empty()) {
                // Fix Issue 3: Subtract old size before adding new size
                size_t newSize = thumbnailSVG.size();
                size_t oldSize = it->second.contentSize;

                it->second.svgContent = std::move(thumbnailSVG);
                it->second.state = ThumbnailState::Ready;
                it->second.fileModTime = modTime;
                it->second.contentSize = newSize;
                it->second.lastAccess = std::chrono::steady_clock::now();

                if (oldSize > 0) {
                    totalCacheBytes_.fetch_sub(oldSize);
                }
                totalCacheBytes_.fetch_add(newSize);

                // Signal new ready thumbnail
                hasNewReady_.store(true);
                std::cout << "[ThumbnailCache] " << filename << " -> Ready (flagged)" << std::endl;

                // Fix Issue 4: Check for duplicates before adding to LRU
                auto lruIt = std::find(lruOrder_.begin(), lruOrder_.end(), req.filePath);
                if (lruIt != lruOrder_.end()) {
                    lruOrder_.erase(lruIt);
                }
                lruOrder_.push_back(req.filePath);
            } else {
                it->second.state = ThumbnailState::Error;
                std::cout << "[ThumbnailCache] " << filename << " -> Error" << std::endl;
            }
        }
    }

    // Evict if over limits
    evictIfNeeded();
}

bool ThumbnailCache::readSVGFile(const std::string& path, std::string& content, std::time_t& modTime) {
    try {
        // Check file exists and get size
        std::filesystem::path fsPath(path);
        if (!std::filesystem::exists(fsPath)) {
            return false;
        }

        auto fileSize = std::filesystem::file_size(fsPath);
        if (fileSize > MAX_SVG_FILE_SIZE) {
            // File too large - read only the header for viewBox
            // This allows us to show a sized placeholder
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;

            // Read first 4KB to get viewBox
            const size_t headerSize = 4096;
            content.resize(headerSize);
            file.read(&content[0], headerSize);
            content.resize(file.gcount());

            // Mark as truncated so we know to show placeholder
            content = "<!--TRUNCATED-->" + content;
        } else if (fileSize > FAST_THUMBNAIL_THRESHOLD) {
            // File too large for fast thumbnail - read header only for quick preview
            // This prevents large files from blocking loader threads
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;

            // Read first 8KB to get viewBox and initial content
            const size_t headerSize = 8192;
            content.resize(headerSize);
            file.read(&content[0], headerSize);
            content.resize(file.gcount());

            // Mark as large file for static preview (not animated)
            content = "<!--LARGE_FILE:" + std::to_string(fileSize / 1024 / 1024) + "MB-->" + content;
        } else {
            // Read entire file
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;

            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();
        }

        // Get modification time
        auto ftime = std::filesystem::last_write_time(fsPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now()
        );
        modTime = std::chrono::system_clock::to_time_t(sctp);

        return !content.empty();
    } catch (const std::exception& e) {
        std::cerr << "[ThumbnailCache] Error reading SVG file " << path << ": " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[ThumbnailCache] Unknown error reading SVG file " << path << std::endl;
        return false;
    }
}

std::string ThumbnailCache::generateThumbnailSVG(const std::string& svgPath,
                                                  const std::string& content,
                                                  float width, float height) {
    // Check for truncated (oversized) file - over MAX_SVG_FILE_SIZE
    bool isTruncated = content.find("<!--TRUNCATED-->") == 0;

    // Check for large file (between FAST_THUMBNAIL_THRESHOLD and MAX_SVG_FILE_SIZE)
    // These get a static preview (first frame only) without animation processing
    bool isLargeFile = content.find("<!--LARGE_FILE:") == 0;
    std::string fileSizeStr;
    if (isLargeFile) {
        size_t colonPos = content.find(':');
        size_t mbPos = content.find("MB-->");
        if (colonPos != std::string::npos && mbPos != std::string::npos) {
            fileSizeStr = content.substr(colonPos + 1, mbPos - colonPos - 1);
        }
    }

    std::string actualContent;
    if (isTruncated) {
        actualContent = content.substr(16);  // Skip "<!--TRUNCATED-->"
    } else if (isLargeFile) {
        size_t endMarker = content.find("-->");
        actualContent = (endMarker != std::string::npos) ? content.substr(endMarker + 3) : content;
    } else {
        actualContent = content;
    }

    // Fix Issue 5: Use full hash for guaranteed uniqueness (no modulo collision risk)
    size_t pathHash = std::hash<std::string>{}(svgPath);
    std::string prefix = "t" + std::to_string(pathHash) + "_";

    // If truncated (over 50MB), just show a placeholder
    if (isTruncated) {
        float svgWidth = 100.0f, svgHeight = 100.0f;
        SVGGridCompositor::extractViewBox(actualContent, svgWidth, svgHeight);

        // Generate sized placeholder indicating file is too large
        std::ostringstream ss;
        float fontSize = std::max(10.0f, std::min(18.0f, width * 0.08f));
        ss << R"(<svg width=")" << width << R"(" height=")" << height
           << R"(" viewBox="0 0 )" << width << " " << height << R"(">)";
        ss << R"(<rect width="100%" height="100%" fill="#2d3436"/>)";
        ss << R"(<text x="50%" y="45%" text-anchor="middle" fill="#dfe6e9" font-size=")" << fontSize << R"(">)";
        ss << "Large File";
        ss << R"(</text>)";
        ss << R"(<text x="50%" y="60%" text-anchor="middle" fill="#636e72" font-size=")" << (fontSize * 0.7f) << R"(">)";
        ss << "(&gt;50MB)";
        ss << R"(</text>)";
        ss << R"(</svg>)";
        return ss.str();
    }

    // For large files (2-50MB), show static preview with file size indicator
    if (isLargeFile) {
        float svgWidth = 100.0f, svgHeight = 100.0f;
        SVGGridCompositor::extractViewBox(actualContent, svgWidth, svgHeight);

        // Generate a simple preview with file size badge
        std::ostringstream ss;
        float fontSize = std::max(8.0f, std::min(14.0f, width * 0.06f));
        float badgeFontSize = fontSize * 0.8f;
        ss << R"(<svg width=")" << width << R"(" height=")" << height
           << R"(" viewBox="0 0 )" << width << " " << height << R"(">)";
        ss << R"(<rect width="100%" height="100%" fill="#1e272e"/>)";
        // Film strip icon (static image indicator)
        float iconSize = width * 0.3f;
        float iconX = (width - iconSize) / 2;
        float iconY = height * 0.25f;
        ss << R"(<rect x=")" << iconX << R"(" y=")" << iconY
           << R"(" width=")" << iconSize << R"(" height=")" << (iconSize * 0.7f)
           << R"(" fill="#636e72" rx="4"/>)";
        // Play triangle inside
        float triSize = iconSize * 0.3f;
        float triX = iconX + iconSize / 2;
        float triY = iconY + iconSize * 0.35f;
        ss << R"(<polygon points=")"
           << (triX - triSize * 0.4f) << "," << (triY - triSize * 0.5f) << " "
           << (triX - triSize * 0.4f) << "," << (triY + triSize * 0.5f) << " "
           << (triX + triSize * 0.5f) << "," << triY
           << R"(" fill="#dfe6e9"/>)";
        // File size badge at bottom
        ss << R"(<text x="50%" y=")" << (height * 0.75f)
           << R"(" text-anchor="middle" fill="#74b9ff" font-size=")" << badgeFontSize << R"(">)";
        ss << fileSizeStr << " MB";
        ss << R"(</text>)";
        ss << R"(<text x="50%" y=")" << (height * 0.88f)
           << R"(" text-anchor="middle" fill="#636e72" font-size=")" << (badgeFontSize * 0.8f) << R"(">)";
        ss << "Click to load";
        ss << R"(</text>)";
        ss << R"(</svg>)";
        return ss.str();
    }

    // Full processing for normal-sized files
    std::string prefixedContent = SVGGridCompositor::prefixSVGIds(actualContent, prefix);

    // Extract FULL viewBox dimensions (including minX, minY offset)
    // Critical: Some SVGs have viewBox="100 100 200 200" - content starts at (100,100)
    // We must preserve the original viewBox to avoid clipping content
    float minX = 0.0f, minY = 0.0f, svgWidth = 100.0f, svgHeight = 100.0f;
    SVGGridCompositor::extractFullViewBox(prefixedContent, minX, minY, svgWidth, svgHeight);

    // Extract inner content
    std::string innerContent = SVGGridCompositor::extractSVGContent(prefixedContent);
    if (innerContent.empty()) {
        return "";  // Failed to extract content
    }

    // Build viewBox string preserving original minX/minY offset
    // This ensures content at (minX, minY) is properly visible
    std::ostringstream viewBoxSS;
    viewBoxSS << minX << " " << minY << " " << svgWidth << " " << svgHeight;
    std::string viewBox = viewBoxSS.str();

    // Build thumbnail SVG with overflow="hidden" to clip content within bounds
    // The viewBox defines coordinate space, but content can extend beyond it
    // overflow="hidden" ensures proper clipping for display
    std::ostringstream ss;
    ss << R"(<svg width=")" << width << R"(" height=")" << height
       << R"(" viewBox=")" << viewBox << R"(" preserveAspectRatio="xMidYMid meet" overflow="hidden">)";

    ss << innerContent
       << "</svg>";

    return ss.str();
}

ThumbnailState ThumbnailCache::getState(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(filePath);
    if (it == cache_.end()) {
        return ThumbnailState::NotLoaded;
    }
    return it->second.state;
}

std::optional<std::string> ThumbnailCache::getThumbnailSVG(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(filePath);
    if (it == cache_.end() || it->second.state != ThumbnailState::Ready) {
        return std::nullopt;
    }

    // Update LRU - move to back of lruOrder_ so it's evicted last
    // Find and remove from current position, then add to back
    auto lruIt = std::find(lruOrder_.begin(), lruOrder_.end(), filePath);
    if (lruIt != lruOrder_.end()) {
        lruOrder_.erase(lruIt);
        lruOrder_.push_back(filePath);
    }
    it->second.lastAccess = std::chrono::steady_clock::now();

    // Return a copy of the string (thread-safe - no dangling pointer risk)
    return it->second.svgContent;
}

bool ThumbnailCache::hasEntry(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.find(filePath) != cache_.end();
}

void ThumbnailCache::requestLoad(const std::string& filePath, float width, float height, int priority) {
    // Fix Issue 1: Test-and-set pattern under single lock to prevent race condition
    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex_);
        auto it = cache_.find(filePath);
        if (it != cache_.end()) {
            if (it->second.state == ThumbnailState::Ready ||
                it->second.state == ThumbnailState::Loading ||
                it->second.state == ThumbnailState::Pending) {
                return;  // Already handled
            }
            it->second.state = ThumbnailState::Pending;
            it->second.lastAccess = std::chrono::steady_clock::now();
        } else {
            ThumbnailCacheEntry entry;
            entry.filePath = filePath;
            entry.width = width;
            entry.height = height;
            entry.state = ThumbnailState::Pending;
            entry.lastAccess = std::chrono::steady_clock::now();
            cache_[filePath] = std::move(entry);
        }

        // Add to queue while still holding cache lock to ensure atomicity
        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            requestQueue_.push({filePath, width, height, priority});
        }
    }
    queueCondition_.notify_one();
}

void ThumbnailCache::cancelRequest(const std::string& filePath) {
    // Mark as not loaded (loader will skip if it picks this up)
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(filePath);
    if (it != cache_.end() && it->second.state == ThumbnailState::Pending) {
        cache_.erase(it);
    }
}

void ThumbnailCache::cancelAllRequests() {
    // Clear the request queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::priority_queue<ThumbnailLoadRequest> empty;
        std::swap(requestQueue_, empty);
    }

    // Remove all pending entries from cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (it->second.state == ThumbnailState::Pending) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

bool ThumbnailCache::hasNewReadyThumbnails() {
    bool result = hasNewReady_.exchange(false);  // Atomically check and clear
    if (result) {
        std::cout << "[ThumbnailCache] hasNewReadyThumbnails() = true (flag cleared)" << std::endl;
    }
    return result;
}

void ThumbnailCache::clearNewReadyFlag() {
    hasNewReady_.store(false);
}

void ThumbnailCache::clear() {
    cancelAllRequests();

    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    lruOrder_.clear();
    totalCacheBytes_.store(0);
}

void ThumbnailCache::evictIfNeeded() {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    // Evict while over limits
    while ((cache_.size() > MAX_CACHE_ENTRIES ||
            totalCacheBytes_.load() > MAX_CACHE_BYTES) &&
           !lruOrder_.empty()) {
        evictOldestEntry();
    }
}

void ThumbnailCache::evictOldestEntry() {
    // Must be called with cacheMutex_ held
    if (lruOrder_.empty()) return;

    std::string oldest = lruOrder_.front();
    lruOrder_.pop_front();

    auto it = cache_.find(oldest);
    if (it != cache_.end()) {
        totalCacheBytes_.fetch_sub(it->second.contentSize);
        cache_.erase(it);
    }
}

size_t ThumbnailCache::getEntryCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.size();
}

std::string ThumbnailCache::generatePlaceholder(float width, float height, ThumbnailState state, int cellIndex) {
    // Use cellIndex for unique IDs - deterministic per cell position, no race conditions
    // This ensures animations parsed from SVG always match DOM element IDs regardless of
    // how many times the browser SVG is regenerated (fixes regression where IDs drifted)
    uint32_t id = static_cast<uint32_t>(cellIndex);

    std::ostringstream ss;
    float fontSize = std::max(10.0f, std::min(20.0f, width * 0.1f));

    // CRITICAL: Use <g> instead of nested <svg> to allow findNodeById() to find
    // animation targets. Nested <svg> elements create separate subtrees that
    // Skia's findNodeById() cannot search into.
    ss << R"(<g>)";

    // Background - use absolute dimensions since we're in a <g>, not <svg>
    ss << R"(<rect width=")" << width << R"(" height=")" << height << R"(" fill="#2d3436"/>)";

    if (state == ThumbnailState::Loading || state == ThumbnailState::Pending) {
        // Animated loading spinner using SMIL (animated by browser's SVGAnimationController)
        // Uses discrete opacity values to create pulsing effect
        float cx = width / 2;
        float cy = height / 2;
        float r = std::min(width, height) * 0.15f;
        float innerR = r * 0.4f;

        // Generate unique IDs for this placeholder to avoid collisions in composite SVGs
        std::string ringId = "loadRing_" + std::to_string(id);
        std::string dotId = "loadDot_" + std::to_string(id);

        // Outer ring with animated opacity (pulsing effect)
        ss << R"(<circle id=")" << ringId << R"(" cx=")" << cx << R"(" cy=")" << cy
           << R"(" r=")" << r << R"(" fill="none" stroke="#74b9ff" stroke-width="3" opacity="1"/>)";

        // Inner dot with inverse animation (alternating pulse)
        ss << R"(<circle id=")" << dotId << R"(" cx=")" << cx << R"(" cy=")" << cy
           << R"(" r=")" << innerR << R"(" fill="#74b9ff" opacity="0.3"/>)";

        // SMIL animations for pulsing effect (discrete values work with our controller)
        ss << R"(<animate xlink:href="#)" << ringId << R"(" attributeName="opacity" )";
        ss << R"(values="1;0.5;0.3;0.5;1" dur="1.2s" repeatCount="indefinite"/>)";

        ss << R"(<animate xlink:href="#)" << dotId << R"(" attributeName="opacity" )";
        ss << R"(values="0.3;0.7;1;0.7;0.3" dur="1.2s" repeatCount="indefinite"/>)";

        // "Loading" text - use absolute x position since % doesn't work in <g>
        ss << R"(<text x=")" << cx << R"(" y=")" << (height * 0.75f)
           << R"(" text-anchor="middle" fill="#b2bec3" font-size=")" << (fontSize * 0.9f) << R"(">)";
        ss << "Loading...";
        ss << R"(</text>)";
    } else if (state == ThumbnailState::Error) {
        // Error icon and message - use absolute positions
        float cx = width / 2;
        ss << R"(<text x=")" << cx << R"(" y=")" << (height * 0.45f)
           << R"(" text-anchor="middle" fill="#e17055" font-size=")" << (fontSize * 1.5f) << R"(">)";
        ss << "!";
        ss << R"(</text>)";
        ss << R"(<text x=")" << cx << R"(" y=")" << (height * 0.65f)
           << R"(" text-anchor="middle" fill="#b2bec3" font-size=")" << fontSize << R"(">)";
        ss << "Error";
        ss << R"(</text>)";
    } else {
        // Generic placeholder - use absolute position
        float cx = width / 2;
        float cy = height / 2;
        ss << R"(<text x=")" << cx << R"(" y=")" << cy
           << R"(" text-anchor="middle" fill="#636e72" font-size=")" << fontSize << R"(">)";
        ss << "SVG";
        ss << R"(</text>)";
    }

    ss << R"(</g>)";
    return ss.str();
}

std::string ThumbnailCache::generateLoadingSpinner(float width, float height, int cellIndex) {
    return generatePlaceholder(width, height, ThumbnailState::Loading, cellIndex);
}

} // namespace svgplayer
