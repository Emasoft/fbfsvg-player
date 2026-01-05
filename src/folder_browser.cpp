// folder_browser.cpp - Visual folder browser implementation with selection, button bar, and breadcrumb navigation
// Uses ThumbnailCache for non-blocking background thumbnail loading

#include "folder_browser.h"
#include "thumbnail_cache.h"
#include "shared/SVGGridCompositor.h"
#include "../shared/svg_instrumentation.h"
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>

namespace fs = std::filesystem;

namespace svgplayer {

FolderBrowser::FolderBrowser() {
    // Default config is set in struct initialization
    // Initialize button regions to empty (disabled)
    cancelButton_ = {0, 0, 0, 0, true};
    loadButton_ = {0, 0, 0, 0, false};
    backButton_ = {0, 0, 0, 0, false};
    forwardButton_ = {0, 0, 0, 0, false};
    sortButton_ = {0, 0, 0, 0, true};

    // Initialize thumbnail cache (loader started on demand)
    thumbnailCache_ = std::make_unique<ThumbnailCache>();
}

FolderBrowser::~FolderBrowser() {
    // Stop thumbnail loader first
    stopThumbnailLoader();

    // Cancel any running scan and wait for thread to finish
    cancelScan();
    if (scanThread_.joinable()) {
        scanThread_.join();
    }
}

// Helper function to escape special XML characters for safe SVG text content
// This ensures international filenames with special characters render correctly
static std::string escapeXml(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2);  // Reserve a bit more space for escapes
    for (char c : input) {
        switch (c) {
            case '<':  output += "&lt;";   break;
            case '>':  output += "&gt;";   break;
            case '&':  output += "&amp;";  break;
            case '"':  output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default:   output += c;        break;
        }
    }
    return output;
}

// Helper function for fluid typography - scales font size based on container dimensions
// Implements clamp-like behavior: min <= scaled <= max
// Reference width is 1920px (HD), font sizes scale proportionally
static float scaleFont(float baseSize, int containerWidth, float minScale = 0.6f, float maxScale = 2.5f) {
    const float refWidth = 1920.0f;  // Reference viewport width
    float scale = static_cast<float>(containerWidth) / refWidth;
    float clampedScale = std::max(minScale, std::min(maxScale, scale));
    return baseSize * clampedScale;
}

void FolderBrowser::setConfig(const BrowserConfig& config) {
    config_ = config;
    calculateGridCells();
    calculateButtonRegions();
    calculateBreadcrumbs();
    updateCurrentPage();
}

bool FolderBrowser::setDirectory(const std::string& path, bool addToHistory) {
    try {
        fs::path dirPath(path);
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            return false;
        }

        // Protect writes to currentDir_, currentPage_, selectedIndex_
        std::string canonicalPath = fs::canonical(dirPath).string();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            currentDir_ = canonicalPath;
            currentPage_ = 0;
            selectedIndex_ = -1;  // Clear selection when changing directory
        }

        // Add to navigation history if requested
        // Protected by historyMutex_ for thread-safe access
        if (addToHistory) {
            std::lock_guard<std::mutex> lock(historyMutex_);
            // Remove any forward history (we're branching)
            if (historyIndex_ >= 0 && historyIndex_ < static_cast<int>(history_.size()) - 1) {
                history_.erase(history_.begin() + historyIndex_ + 1, history_.end());
            }
            // Add new directory to history
            history_.push_back(canonicalPath);
            historyIndex_ = static_cast<int>(history_.size()) - 1;
        }

        scanDirectory();
        calculateGridCells();
        calculateButtonRegions();
        calculateBreadcrumbs();
        updateCurrentPage();
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "FolderBrowser::setDirectory failed: %s\n", e.what());
        return false;
    } catch (...) {
        fprintf(stderr, "FolderBrowser::setDirectory failed with unknown error\n");
        return false;
    }
}

bool FolderBrowser::goToParent() {
    // Copy currentDir_ under lock
    std::string currentPath;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPath = currentDir_;
    }

    fs::path current(currentPath);
    fs::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
        return false;  // Already at root
    }
    return setDirectory(parent.string());
}

bool FolderBrowser::enterFolder(const std::string& folderName) {
    // Copy currentDir_ under lock
    std::string currentPath;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPath = currentDir_;
    }

    fs::path newPath = fs::path(currentPath) / folderName;
    return setDirectory(newPath.string());
}

bool FolderBrowser::goBack() {
    // Copy path under lock, then navigate outside lock (avoids deadlock)
    std::string targetPath;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        if (historyIndex_ <= 0) return false;
        historyIndex_--;
        targetPath = history_[historyIndex_];
    }
    return setDirectory(targetPath, false);  // Don't add to history
}

bool FolderBrowser::goForward() {
    // Copy path under lock, then navigate outside lock (avoids deadlock)
    std::string targetPath;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        if (historyIndex_ >= static_cast<int>(history_.size()) - 1) return false;
        historyIndex_++;
        targetPath = history_[historyIndex_];
    }
    return setDirectory(targetPath, false);  // Don't add to history
}

// ============================================================
// ASYNC NAVIGATION HELPERS
// ============================================================

void FolderBrowser::goToParentAsync(ProgressCallback callback) {
    // Copy currentDir_ under lock
    std::string currentPath;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPath = currentDir_;
    }

    fs::path current(currentPath);
    fs::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
        // Already at root - nothing to do
        return;
    }
    setDirectoryAsync(parent.string(), callback);
}

void FolderBrowser::enterFolderAsync(const std::string& folderName, ProgressCallback callback) {
    // Copy currentDir_ under lock
    std::string currentPath;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPath = currentDir_;
    }

    fs::path newPath = fs::path(currentPath) / folderName;
    setDirectoryAsync(newPath.string(), callback);
}

void FolderBrowser::goBackAsync(ProgressCallback callback) {
    // Copy path under lock, then navigate outside lock (avoids deadlock)
    std::string targetPath;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        if (historyIndex_ <= 0) return;
        historyIndex_--;
        targetPath = history_[historyIndex_];
    }
    setDirectoryAsync(targetPath, callback, false);  // Don't add to history
}

void FolderBrowser::goForwardAsync(ProgressCallback callback) {
    // Copy path under lock, then navigate outside lock (avoids deadlock)
    std::string targetPath;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        if (historyIndex_ >= static_cast<int>(history_.size()) - 1) return;
        historyIndex_++;
        targetPath = history_[historyIndex_];
    }
    setDirectoryAsync(targetPath, callback, false);  // Don't add to history
}

// ============================================================
// ASYNC DIRECTORY SCANNING
// ============================================================

void FolderBrowser::setDirectoryAsync(const std::string& path, ProgressCallback callback, bool addToHistory) {
    // Cancel any existing scan first
    cancelScan();
    if (scanThread_.joinable()) {
        scanThread_.join();
    }

    // Validate path before starting thread
    try {
        fs::path dirPath(path);
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            if (callback) callback(1.0f, "Directory not found");
            return;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "FolderBrowser::setDirectoryAsync path validation failed: %s\n", e.what());
        if (callback) callback(1.0f, "Invalid path");
        return;
    } catch (...) {
        fprintf(stderr, "FolderBrowser::setDirectoryAsync path validation failed with unknown error\n");
        if (callback) callback(1.0f, "Invalid path");
        return;
    }

    // Issue 15 fix: Reset scan state atomically
    // Order matters: scanningInProgress_ must be set last to signal scan has started
    // scanCancelRequested_ must be cleared before starting the scan thread
    // These atomics can be observed individually, but the ordering ensures:
    // - If scanningInProgress_ is true, the scan thread has started
    // - If scanCancelRequested_ is false, the scan is allowed to proceed
    scanComplete_.store(false);
    scanCancelRequested_.store(false);
    scanningInProgress_.store(true);  // Signal scan started (observed last)

    // Issue 2 fix: protect pendingDir_ and pendingAddToHistory_ with pendingMutex_
    std::string pendingDirCopy;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingDir_ = fs::canonical(fs::path(path)).string();
        pendingAddToHistory_ = addToHistory;
        pendingDirCopy = pendingDir_;
    }

    // Show loading state in UI
    setLoading(true, "Scanning directory...");
    setProgress(0.0f);

    // Launch background scan thread
    scanThread_ = std::thread([this, callback, pendingDirCopy]() {
        SVG_INSTRUMENT_CALL(onScanStart);
        std::vector<BrowserEntry> entries;
        fs::path current(pendingDirCopy);
        bool atRoot = (current == current.root_path());

        try {
            if (atRoot) {
                // At root: scan volumes/mount points
                if (callback) callback(0.1f, "Scanning mount points...");

#ifdef __APPLE__
                fs::path volumesPath("/Volumes");
                if (fs::exists(volumesPath)) {
                    for (const auto& entry : fs::directory_iterator(volumesPath)) {
                        if (scanCancelRequested_.load()) break;
                        if (entry.is_directory()) {
                            std::string name = entry.path().filename().string();
                            if (name.empty() || name[0] == '.') continue;

                            BrowserEntry volumeEntry;
                            volumeEntry.type = BrowserEntryType::Volume;
                            volumeEntry.name = name;
                            volumeEntry.fullPath = entry.path().string();
                            volumeEntry.gridIndex = static_cast<int>(entries.size());
                            volumeEntry.modifiedTime = 0;
                            entries.push_back(volumeEntry);
                        }
                    }
                }

                std::vector<std::string> rootDirs = {"/Users", "/Applications", "/Library", "/System"};
                for (const auto& dir : rootDirs) {
                    if (scanCancelRequested_.load()) break;
                    if (fs::exists(dir) && fs::is_directory(dir)) {
                        BrowserEntry rootEntry;
                        rootEntry.type = BrowserEntryType::Folder;
                        rootEntry.name = dir.substr(1);
                        rootEntry.fullPath = dir;
                        rootEntry.gridIndex = static_cast<int>(entries.size());
                        rootEntry.modifiedTime = 0;
                        entries.push_back(rootEntry);
                    }
                }
#else
                std::vector<std::string> mountDirs = {"/mnt", "/media", "/home", "/tmp"};
                for (const auto& dir : mountDirs) {
                    if (scanCancelRequested_.load()) break;
                    if (fs::exists(dir) && fs::is_directory(dir)) {
                        BrowserEntry mountEntry;
                        mountEntry.type = BrowserEntryType::Volume;
                        mountEntry.name = dir.substr(1);
                        mountEntry.fullPath = dir;
                        mountEntry.gridIndex = static_cast<int>(entries.size());
                        mountEntry.modifiedTime = 0;
                        entries.push_back(mountEntry);
                    }
                }
#endif
            } else {
                // Not at root: add parent directory entry
                BrowserEntry parentEntry;
                parentEntry.type = BrowserEntryType::ParentDir;
                parentEntry.name = "..";
                parentEntry.fullPath = current.parent_path().string();
                parentEntry.gridIndex = 0;
                parentEntry.modifiedTime = 0;
                entries.push_back(parentEntry);

                // Count entries for progress calculation
                size_t totalEntries = 0;
                for (const auto& _ : fs::directory_iterator(current)) {
                    (void)_;  // Suppress unused variable warning
                    totalEntries++;
                    if (totalEntries > 10000) break;  // Cap counting at 10k
                }

                if (callback) callback(0.1f, "Scanning files...");

                // Scan directory entries
                size_t processedEntries = 0;
                for (const auto& entry : fs::directory_iterator(current)) {
                    if (scanCancelRequested_.load()) break;

                    std::string name = entry.path().filename().string();
                    if (name.empty() || name[0] == '.') continue;  // Skip hidden files

                    BrowserEntry browserEntry;
                    browserEntry.fullPath = entry.path().string();
                    browserEntry.name = name;

                    // Get last modified time
                    try {
                        auto ftime = fs::last_write_time(entry);
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                        );
                        browserEntry.modifiedTime = std::chrono::system_clock::to_time_t(sctp);
                    } catch (const std::exception& e) {
                        fprintf(stderr, "FolderBrowser: Failed to get modified time for %s: %s\n",
                                entry.path().string().c_str(), e.what());
                        browserEntry.modifiedTime = 0;
                    } catch (...) {
                        fprintf(stderr, "FolderBrowser: Failed to get modified time for %s: unknown error\n",
                                entry.path().string().c_str());
                        browserEntry.modifiedTime = 0;
                    }

                    if (entry.is_directory()) {
                        browserEntry.type = BrowserEntryType::Folder;
                    } else if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".svg") {
                            browserEntry.type = BrowserEntryType::SVGFile;
                        } else {
                            continue;  // Skip non-SVG files
                        }
                    } else {
                        continue;  // Skip symlinks, etc.
                    }

                    browserEntry.gridIndex = static_cast<int>(entries.size());
                    entries.push_back(browserEntry);

                    // Update progress
                    processedEntries++;
                    if (callback && totalEntries > 0) {
                        float progress = 0.1f + 0.8f * (static_cast<float>(processedEntries) / totalEntries);
                        callback(progress, "Scanning files... " + std::to_string(processedEntries) + "/" + std::to_string(totalEntries));
                    }
                }
            }

            if (callback) callback(0.95f, "Finalizing...");

        } catch (const std::exception& e) {
            fprintf(stderr, "FolderBrowser: Directory scan failed: %s\n", e.what());
            if (callback) callback(1.0f, std::string("Error: ") + e.what());
        } catch (...) {
            fprintf(stderr, "FolderBrowser: Directory scan failed with unknown error\n");
            if (callback) callback(1.0f, "Unknown error during scan");
        }

        // Store results for main thread to pick up
        {
            std::lock_guard<std::mutex> lock(scanMutex_);
            pendingEntries_ = std::move(entries);
        }

        if (callback) callback(1.0f, "Complete");
        SVG_INSTRUMENT_CALL(onScanComplete);
        scanningInProgress_.store(false);
        scanComplete_.store(true);
    });
}

void FolderBrowser::cancelScan() {
    scanCancelRequested_.store(true);
}

bool FolderBrowser::pollScanComplete() {
    return scanComplete_.load();
}

void FolderBrowser::finalizeScan() {
    if (!scanComplete_.load()) return;

    // Wait for thread to finish
    if (scanThread_.joinable()) {
        scanThread_.join();
    }

    // Cancel any pending thumbnail requests for the old directory
    // New thumbnails will be requested when generateBrowserSVG() is called
    if (thumbnailCache_) {
        thumbnailCache_->cancelAllRequests();
    }

    // Move results from pending to active
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        allEntries_ = std::move(pendingEntries_);
        pendingEntries_.clear();
    }

    // Update directory state - protect with stateMutex_ (Issue 3 fix: copy before using under different lock)
    std::string dirCopy;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentDir_ = pendingDir_;
        dirCopy = currentDir_;
        currentPage_ = 0;
        selectedIndex_ = -1;
    }

    // Add to navigation history if requested
    // Protected by historyMutex_ for thread-safe access
    if (pendingAddToHistory_) {
        std::lock_guard<std::mutex> lock(historyMutex_);
        if (historyIndex_ >= 0 && historyIndex_ < static_cast<int>(history_.size()) - 1) {
            history_.erase(history_.begin() + historyIndex_ + 1, history_.end());
        }
        history_.push_back(dirCopy);
        historyIndex_ = static_cast<int>(history_.size()) - 1;
    }

    // Sort and finalize UI
    sortEntries();
    calculateGridCells();
    calculateButtonRegions();
    calculateBreadcrumbs();

    // Issue 12 fix: Validate currentPage_ after allEntries_ changes
    int totalPages = getTotalPages();
    if (totalPages > 0) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPage_ = std::min(currentPage_, std::max(0, totalPages - 1));
    }

    updateCurrentPage();

    // Clear loading state
    setLoading(false);
    scanComplete_.store(false);

    // Trigger browser SVG regeneration with new directory content
    markDirty();
}

void FolderBrowser::setSortMode(BrowserSortMode mode) {
    if (config_.sortMode != mode) {
        config_.sortMode = mode;
        sortEntries();
        updateCurrentPage();
        markDirty();  // Trigger browser SVG regeneration
    }
}

void FolderBrowser::toggleSortMode() {
    // Cycle through: A-Z Asc -> A-Z Desc -> Date Asc -> Date Desc -> repeat
    if (config_.sortMode == BrowserSortMode::Alphabetical) {
        if (config_.sortDirection == BrowserSortDirection::Ascending) {
            // A-Z Ascending -> A-Z Descending
            config_.sortDirection = BrowserSortDirection::Descending;
        } else {
            // A-Z Descending -> Date Ascending
            config_.sortMode = BrowserSortMode::ModifiedTime;
            config_.sortDirection = BrowserSortDirection::Ascending;
        }
    } else {
        if (config_.sortDirection == BrowserSortDirection::Ascending) {
            // Date Ascending -> Date Descending
            config_.sortDirection = BrowserSortDirection::Descending;
        } else {
            // Date Descending -> A-Z Ascending (cycle complete)
            config_.sortMode = BrowserSortMode::Alphabetical;
            config_.sortDirection = BrowserSortDirection::Ascending;
        }
    }
    sortEntries();
    updateCurrentPage();
    markDirty();  // Trigger browser SVG regeneration
}

int FolderBrowser::getTotalPages() const {
    // Issue 11 fix: Return 0 for empty directories instead of 1
    if (allEntries_.empty()) return 0;

    int entriesPerPage = getEntriesPerPage();
    if (entriesPerPage <= 0) return 1;
    return std::max(1, static_cast<int>(std::ceil(
        static_cast<float>(allEntries_.size()) / entriesPerPage)));
}

void FolderBrowser::nextPage() {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentPage_ < getTotalPages() - 1) {
            currentPage_++;
            selectedIndex_ = -1;  // Clear selection when changing page
            changed = true;
        }
    }
    if (changed) {
        updateCurrentPage();
        calculateButtonRegions();  // Update button enabled states
        markDirty();  // Page changed, trigger browser SVG regeneration
    }
}

void FolderBrowser::prevPage() {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentPage_ > 0) {
            currentPage_--;
            selectedIndex_ = -1;  // Clear selection when changing page
            changed = true;
        }
    }
    if (changed) {
        updateCurrentPage();
        calculateButtonRegions();  // Update button enabled states
        markDirty();  // Page changed, trigger browser SVG regeneration
    }
}

void FolderBrowser::setPage(int page) {
    page = std::max(0, std::min(page, getTotalPages() - 1));
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (page != currentPage_) {
            currentPage_ = page;
            selectedIndex_ = -1;  // Clear selection when changing page
            changed = true;
        }
    }
    if (changed) {
        updateCurrentPage();
        calculateButtonRegions();  // Update button enabled states
        markDirty();  // Page changed, trigger browser SVG regeneration
        SVG_INSTRUMENT_VALUE(onPageChange, page);
    }
}

void FolderBrowser::selectEntry(int index) {
    int newIndex = (index >= 0 && index < static_cast<int>(currentPageEntries_.size())) ? index : -1;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (newIndex != selectedIndex_) {
            selectedIndex_ = newIndex;
            changed = true;
        }
    }
    if (changed) {
        markDirty();  // Selection changed, trigger browser SVG regeneration
        SVG_INSTRUMENT_VALUE(onSelectionChange, newIndex);
    }
}

void FolderBrowser::clearSelection() {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (selectedIndex_ != -1) {
            selectedIndex_ = -1;
            changed = true;
        }
    }
    if (changed) {
        markDirty();  // Selection cleared, trigger browser SVG regeneration
    }
}

void FolderBrowser::setHoveredEntry(int index) {
    int newIndex = (index >= 0 && index < static_cast<int>(currentPageEntries_.size())) ? index : -1;
    if (newIndex != hoveredIndex_) {
        hoveredIndex_ = newIndex;
        markDirty();  // Hover changed, trigger browser SVG regeneration
    }
}

void FolderBrowser::triggerClickFeedback(int index) {
    // Start click flash animation on the specified entry
    if (index >= 0 && index < static_cast<int>(currentPageEntries_.size())) {
        clickFeedbackIndex_ = index;
        clickFeedbackIntensity_ = 1.0f;  // Start at full intensity (white flash)
    }
}

void FolderBrowser::updateClickFeedback() {
    // Decay the click feedback intensity each frame (fast decay for snappy feel)
    if (clickFeedbackIntensity_ > 0.0f) {
        clickFeedbackIntensity_ -= 0.15f;  // ~7 frames to fully decay at 60fps
        if (clickFeedbackIntensity_ <= 0.0f) {
            clickFeedbackIntensity_ = 0.0f;
            clickFeedbackIndex_ = -1;
        }
    }
}

void FolderBrowser::setLoading(bool loading, const std::string& message) {
    isLoading_ = loading;
    loadingMessage_ = message;
    if (!loading) {
        loadingProgress_.store(0.0f);
    }
}

void FolderBrowser::setProgress(float progress) {
    float clampedProgress = std::max(0.0f, std::min(1.0f, progress));
    loadingProgress_.store(clampedProgress);
    // Debug: trace progress updates (only log significant changes)
    static float lastLoggedProgress = -1.0f;
    if (progress - lastLoggedProgress > 0.1f || progress >= 0.99f) {
        std::cout << "Progress: " << static_cast<int>(progress * 100) << "%" << std::endl;
        lastLoggedProgress = progress;
    }
}

std::optional<BrowserEntry> FolderBrowser::getSelectedEntry() const {
    int index;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        index = selectedIndex_;
    }
    if (index >= 0 && index < static_cast<int>(currentPageEntries_.size())) {
        return currentPageEntries_[index];  // Return copy (Issue 6 fix: return by value)
    }
    return std::nullopt;
}

bool FolderBrowser::canLoad() const {
    std::optional<BrowserEntry> selected = getSelectedEntry();
    return selected.has_value() && selected->type == BrowserEntryType::SVGFile;
}

void FolderBrowser::scanDirectory() {
    SVG_INSTRUMENT_CALL(onScanStart);
    allEntries_.clear();

    // Copy currentDir_ under lock for thread-safe access
    std::string currentPath;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPath = currentDir_;
    }

    fs::path current(currentPath);

    // Check if we are at root level
    bool atRoot = (current == current.root_path());

    if (atRoot) {
        // At root: show available volumes/mount points
        // On macOS, volumes are in /Volumes/
        // On Linux, mount points are typically in /mnt/ or /media/
        // Also show common root directories

#ifdef __APPLE__
        // macOS: Show /Volumes contents as mount points
        fs::path volumesPath("/Volumes");
        if (fs::exists(volumesPath)) {
            try {
                for (const auto& entry : fs::directory_iterator(volumesPath)) {
                    if (entry.is_directory()) {
                        std::string name = entry.path().filename().string();
                        if (name.empty() || name[0] == '.') continue;

                        BrowserEntry volumeEntry;
                        volumeEntry.type = BrowserEntryType::Volume;
                        volumeEntry.name = name;
                        volumeEntry.fullPath = entry.path().string();
                        volumeEntry.gridIndex = static_cast<int>(allEntries_.size());
                        volumeEntry.modifiedTime = 0;  // Volumes don't have meaningful modified time
                        allEntries_.push_back(volumeEntry);
                    }
                }
            } catch (const std::exception& e) {
                fprintf(stderr, "FolderBrowser: Failed to scan /Volumes: %s\n", e.what());
            } catch (...) {
                fprintf(stderr, "FolderBrowser: Failed to scan /Volumes: unknown error\n");
            }
        }

        // Also show common root directories
        std::vector<std::string> rootDirs = {"/Users", "/Applications", "/Library", "/System"};
        for (const auto& dir : rootDirs) {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                BrowserEntry rootEntry;
                rootEntry.type = BrowserEntryType::Folder;
                rootEntry.name = dir.substr(1);  // Remove leading /
                rootEntry.fullPath = dir;
                rootEntry.gridIndex = static_cast<int>(allEntries_.size());
                rootEntry.modifiedTime = 0;
                allEntries_.push_back(rootEntry);
            }
        }
#else
        // Linux: Show mount points and common directories
        std::vector<std::string> mountDirs = {"/mnt", "/media", "/home", "/tmp"};
        for (const auto& dir : mountDirs) {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                BrowserEntry mountEntry;
                mountEntry.type = BrowserEntryType::Volume;
                mountEntry.name = dir.substr(1);  // Remove leading /
                mountEntry.fullPath = dir;
                mountEntry.gridIndex = static_cast<int>(allEntries_.size());
                mountEntry.modifiedTime = 0;
                allEntries_.push_back(mountEntry);
            }
        }
#endif
        return;
    }

    // Not at root: show parent directory entry first
    fs::path parent = current.parent_path();
    if (!parent.empty() && parent != current) {
        BrowserEntry parentEntry;
        parentEntry.type = BrowserEntryType::ParentDir;
        parentEntry.name = "..";
        parentEntry.fullPath = parent.string();
        parentEntry.gridIndex = 0;
        parentEntry.modifiedTime = 0;  // Parent entry doesn't show modified time
        allEntries_.push_back(parentEntry);
    }

    // Collect folders and SVG files
    std::vector<BrowserEntry> folders;
    std::vector<BrowserEntry> svgFiles;

    try {
        for (const auto& entry : fs::directory_iterator(currentDir_)) {
            std::string name = entry.path().filename().string();

            // Skip hidden files/folders
            if (name.empty() || name[0] == '.') continue;

            // Get last modified time
            std::time_t modTime = 0;
            try {
                auto ftime = fs::last_write_time(entry.path());
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                modTime = std::chrono::system_clock::to_time_t(sctp);
            } catch (const std::exception& e) {
                fprintf(stderr, "FolderBrowser: Failed to get modified time for %s: %s\n",
                        entry.path().string().c_str(), e.what());
                modTime = 0;
            } catch (...) {
                fprintf(stderr, "FolderBrowser: Failed to get modified time for %s: unknown error\n",
                        entry.path().string().c_str());
                modTime = 0;
            }

            if (entry.is_directory()) {
                BrowserEntry folderEntry;
                folderEntry.type = BrowserEntryType::Folder;
                folderEntry.name = name;
                folderEntry.fullPath = entry.path().string();
                folderEntry.modifiedTime = modTime;
                folders.push_back(folderEntry);
            } else if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                // Case-insensitive SVG check
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".svg") {
                    BrowserEntry fileEntry;
                    fileEntry.type = BrowserEntryType::SVGFile;
                    fileEntry.name = name;
                    fileEntry.fullPath = entry.path().string();
                    fileEntry.modifiedTime = modTime;
                    svgFiles.push_back(fileEntry);
                }
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "FolderBrowser: Failed to scan directory %s: %s\n", currentPath.c_str(), e.what());
    } catch (...) {
        fprintf(stderr, "FolderBrowser: Failed to scan directory %s: unknown error\n", currentPath.c_str());
    }

    // Add folders first, then SVG files (sorting applied later in sortEntries)
    for (auto& f : folders) {
        f.gridIndex = static_cast<int>(allEntries_.size());
        allEntries_.push_back(f);
    }
    for (auto& f : svgFiles) {
        f.gridIndex = static_cast<int>(allEntries_.size());
        allEntries_.push_back(f);
    }

    // Apply current sort mode
    sortEntries();
    SVG_INSTRUMENT_CALL(onScanComplete);
}

void FolderBrowser::sortEntries() {
    // Keep parent entry at the beginning if present
    std::vector<BrowserEntry> toSort;
    std::vector<BrowserEntry> fixed;  // ParentDir and Volume entries stay at top

    for (auto& entry : allEntries_) {
        if (entry.type == BrowserEntryType::ParentDir || entry.type == BrowserEntryType::Volume) {
            fixed.push_back(entry);
        } else {
            toSort.push_back(entry);
        }
    }

    // Sort folders and files - respect direction (ascending/descending)
    bool ascending = (config_.sortDirection == BrowserSortDirection::Ascending);

    if (config_.sortMode == BrowserSortMode::Alphabetical) {
        std::sort(toSort.begin(), toSort.end(), [ascending](const BrowserEntry& a, const BrowserEntry& b) {
            // Folders before files, then alphabetical within each group
            if (a.type == BrowserEntryType::Folder && b.type != BrowserEntryType::Folder) return true;
            if (a.type != BrowserEntryType::Folder && b.type == BrowserEntryType::Folder) return false;
            // Ascending: A-Z, Descending: Z-A
            return ascending ? (a.name < b.name) : (a.name > b.name);
        });
    } else {
        // Sort by modified time
        std::sort(toSort.begin(), toSort.end(), [ascending](const BrowserEntry& a, const BrowserEntry& b) {
            // Folders before files, then by modified time within each group
            if (a.type == BrowserEntryType::Folder && b.type != BrowserEntryType::Folder) return true;
            if (a.type != BrowserEntryType::Folder && b.type == BrowserEntryType::Folder) return false;
            // Ascending: oldest first, Descending: newest first
            return ascending ? (a.modifiedTime < b.modifiedTime) : (a.modifiedTime > b.modifiedTime);
        });
    }

    // Rebuild allEntries_ with fixed entries first
    allEntries_.clear();
    for (auto& entry : fixed) {
        entry.gridIndex = static_cast<int>(allEntries_.size());
        allEntries_.push_back(entry);
    }
    for (auto& entry : toSort) {
        entry.gridIndex = static_cast<int>(allEntries_.size());
        allEntries_.push_back(entry);
    }

    // Issue 12 fix: Validate currentPage_ after allEntries_ changes
    int totalPages = getTotalPages();
    if (totalPages > 0) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPage_ = std::min(currentPage_, std::max(0, totalPages - 1));
    }
}

void FolderBrowser::updateCurrentPage() {
    // Issue 8 fix: Protect currentPageEntries_ and gridCells_ modifications with mutex
    std::lock_guard<std::mutex> lock(stateMutex_);

    currentPageEntries_.clear();
    int entriesPerPage = getEntriesPerPage();
    int startIdx = currentPage_ * entriesPerPage;
    int endIdx = std::min(startIdx + entriesPerPage, static_cast<int>(allEntries_.size()));

    for (int i = startIdx; i < endIdx; i++) {
        BrowserEntry entry = allEntries_[i];
        entry.gridIndex = i - startIdx;  // Local grid index
        currentPageEntries_.push_back(entry);
    }

    // Update grid cell entry indices (Issue 5 fix: use index instead of pointer)
    for (auto& cell : gridCells_) {
        cell.entryIndex = -1;
        for (size_t i = 0; i < currentPageEntries_.size(); i++) {
            if (currentPageEntries_[i].gridIndex == cell.index) {
                cell.entryIndex = static_cast<int>(i);
                break;
            }
        }
    }
}

void FolderBrowser::calculateGridCells() {
    gridCells_.clear();

    // Reserve space for header, nav bar, and button bar
    float gridTop = config_.headerHeight + config_.navBarHeight;
    float gridBottom = config_.containerHeight - config_.buttonBarHeight;
    float gridHeight = gridBottom - gridTop;

    float availableWidth = config_.containerWidth - config_.cellMargin * (config_.columns + 1);
    float availableHeight = gridHeight - config_.cellMargin * (config_.rows + 1)
                           - config_.labelHeight * config_.rows;

    float cellWidth = availableWidth / config_.columns;
    float cellHeight = availableHeight / config_.rows;

    for (int row = 0; row < config_.rows; row++) {
        for (int col = 0; col < config_.columns; col++) {
            GridCell cell;
            cell.index = row * config_.columns + col;
            cell.x = config_.cellMargin + col * (cellWidth + config_.cellMargin);
            cell.y = gridTop + config_.cellMargin + row * (cellHeight + config_.cellMargin + config_.labelHeight);
            cell.width = cellWidth;
            cell.height = cellHeight;
            cell.entryIndex = -1;  // Issue 5 fix: use index instead of pointer
            gridCells_.push_back(cell);
        }
    }
}

void FolderBrowser::calculateButtonRegions() {
    // All button dimensions use vh-based sizing for proportional scaling
    // vh = containerHeight / 100 (1% of viewport height)
    float vh = static_cast<float>(config_.containerHeight) / 100.0f;

    // Nav bar buttons (back/forward/sort) - in the nav bar below header
    float navY = config_.headerHeight + 0.5f * vh;  // Small offset into nav bar
    float navBtnSize = 3.0f * vh;   // 3vh button size
    float navSpacing = 1.0f * vh;   // 1vh spacing

    // Back button on the left
    backButton_.x = 1.5f * vh;
    backButton_.y = navY;
    backButton_.width = navBtnSize;
    backButton_.height = navBtnSize;
    backButton_.enabled = canGoBack();

    // Forward button next to back
    forwardButton_.x = backButton_.x + navBtnSize + navSpacing;
    forwardButton_.y = navY;
    forwardButton_.width = navBtnSize;
    forwardButton_.height = navBtnSize;
    forwardButton_.enabled = canGoForward();

    // Sort button on the right side of nav bar
    // Extra width for triangle indicators and padding around text
    float sortBtnWidth = 14.0f * vh;  // 14vh width (increased for triangles + padding)
    sortButton_.x = config_.containerWidth - sortBtnWidth - 1.5f * vh;  // Right-aligned
    sortButton_.y = navY;
    sortButton_.width = sortBtnWidth;
    sortButton_.height = navBtnSize;
    sortButton_.enabled = true;

    // Bottom button bar (Cancel/Load)
    float buttonY = config_.containerHeight - config_.buttonBarHeight + 1.0f * vh;  // Small offset into bar
    float buttonWidth = 12.0f * vh;   // 12vh width
    float buttonHeight = 4.0f * vh;   // 4vh height
    float buttonSpacing = 2.0f * vh;  // 2vh spacing

    // Cancel button on the left side of button bar
    cancelButton_.x = config_.containerWidth / 2 - buttonWidth - buttonSpacing / 2;
    cancelButton_.y = buttonY;
    cancelButton_.width = buttonWidth;
    cancelButton_.height = buttonHeight;
    cancelButton_.enabled = true;  // Cancel is always enabled

    // Load button on the right side of button bar
    loadButton_.x = config_.containerWidth / 2 + buttonSpacing / 2;
    loadButton_.y = buttonY;
    loadButton_.width = buttonWidth;
    loadButton_.height = buttonHeight;
    loadButton_.enabled = false;  // Starts disabled until an SVG is selected

    // Pagination buttons in header bar (only visible when multiple pages)
    // Positioned on the right side of header, around the "Page X/Y" text
    float paginationBtnSize = 3.5f * vh;   // 3.5vh button size
    float paginationY = (config_.headerHeight - paginationBtnSize) / 2;  // Vertically centered in header

    // Next page button (rightmost)
    nextPageButton_.x = config_.containerWidth - paginationBtnSize - 1.5f * vh;
    nextPageButton_.y = paginationY;
    nextPageButton_.width = paginationBtnSize;
    nextPageButton_.height = paginationBtnSize;
    nextPageButton_.enabled = currentPage_ < getTotalPages() - 1;

    // Prev page button (to the left of text, which is to the left of next button)
    // Text width estimate: "Page 99 / 99" ~ 12 characters at ~1vh per char = 12vh
    float textWidth = 14.0f * vh;  // Approximate text width
    prevPageButton_.x = nextPageButton_.x - textWidth - paginationBtnSize;
    prevPageButton_.y = paginationY;
    prevPageButton_.width = paginationBtnSize;
    prevPageButton_.height = paginationBtnSize;
    prevPageButton_.enabled = currentPage_ > 0;
}

void FolderBrowser::calculateBreadcrumbs() {
    breadcrumbs_.clear();

    // Copy currentDir_ under lock for thread-safe access
    std::string currentPath;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentPath = currentDir_;
    }

    if (currentPath.empty()) return;

    fs::path path(currentPath);
    std::vector<std::string> parts;
    std::vector<std::string> paths;

    // Build list of path components
    std::string accumulated;
    for (const auto& part : path) {
        std::string partStr = part.string();
        if (partStr.empty()) continue;

        if (accumulated.empty() && partStr == "/") {
            accumulated = "/";
            parts.push_back("/");
            paths.push_back("/");
        } else if (accumulated == "/") {
            accumulated += partStr;
            parts.push_back(partStr);
            paths.push_back(accumulated);
        } else {
            accumulated += "/" + partStr;
            parts.push_back(partStr);
            paths.push_back(accumulated);
        }
    }

    // vh-based sizing for proportional scaling
    float vh = static_cast<float>(config_.containerHeight) / 100.0f;

    float x = 1.5f * vh;                           // 1.5vh left margin
    float y = config_.headerHeight / 2 - 1.0f * vh; // Centered in header
    float segmentHeight = 2.5f * vh;               // 2.5vh segment height
    float charWidth = 1.1f * vh;                   // 1.1vh per character (matches ~1.8vh font)
    float chevronSlant = 1.5f * vh;                // 1.5vh chevron point extension (more pronounced)
    float arrowSize = 0.8f * vh;                   // 0.8vh separator triangle size
    float arrowSpacing = 0.6f * vh;                // 0.6vh spacing around separator
    float separatorWidth = chevronSlant + arrowSpacing * 2 + arrowSize;  // Total separator space
    float padding = 2.0f * vh;                     // 2vh padding (1vh each side)

    for (size_t i = 0; i < parts.size(); i++) {
        PathSegment segment;
        segment.name = parts[i];
        segment.fullPath = paths[i];
        segment.x = x;
        segment.y = y;
        segment.width = segment.name.length() * charWidth + padding;  // Text width + padding both sides
        segment.height = segmentHeight;

        breadcrumbs_.push_back(segment);

        x += segment.width + separatorWidth;

        // If we're running out of space, truncate and show "..." at start
        float rightMargin = 10.0f * vh;  // 10vh right margin
        if (x > config_.containerWidth - rightMargin && i < parts.size() - 1) {
            // Recalculate from end
            breadcrumbs_.clear();

            // Show ellipsis and last few segments
            PathSegment ellipsis;
            ellipsis.name = "...";
            ellipsis.fullPath = "";
            ellipsis.x = 1.5f * vh;
            ellipsis.y = y;
            ellipsis.width = 3.0f * vh;  // 3vh ellipsis width
            ellipsis.height = segmentHeight;
            breadcrumbs_.push_back(ellipsis);

            // Show last 2-3 segments
            x = 1.5f * vh + 3.0f * vh + separatorWidth;
            size_t startFrom = (parts.size() > 3) ? parts.size() - 3 : 0;
            for (size_t j = startFrom; j < parts.size(); j++) {
                PathSegment seg;
                seg.name = parts[j];
                seg.fullPath = paths[j];
                seg.x = x;
                seg.y = y;
                seg.width = seg.name.length() * charWidth + padding;
                seg.height = segmentHeight;
                breadcrumbs_.push_back(seg);
                x += seg.width + separatorWidth;
            }
            break;
        }
    }
}

HitTestResult FolderBrowser::hitTest(float screenX, float screenY,
                                      const BrowserEntry** outEntry,
                                      std::string* outBreadcrumbPath) const {
    if (outEntry) *outEntry = nullptr;
    if (outBreadcrumbPath) *outBreadcrumbPath = "";

    // Check nav bar buttons (back/forward/sort)
    if (screenX >= backButton_.x && screenX <= backButton_.x + backButton_.width &&
        screenY >= backButton_.y && screenY <= backButton_.y + backButton_.height) {
        return HitTestResult::BackButton;
    }

    if (screenX >= forwardButton_.x && screenX <= forwardButton_.x + forwardButton_.width &&
        screenY >= forwardButton_.y && screenY <= forwardButton_.y + forwardButton_.height) {
        return HitTestResult::ForwardButton;
    }

    if (screenX >= sortButton_.x && screenX <= sortButton_.x + sortButton_.width &&
        screenY >= sortButton_.y && screenY <= sortButton_.y + sortButton_.height) {
        return HitTestResult::SortButton;
    }

    // Check pagination buttons (prev/next page arrows in header)
    if (getTotalPages() > 1) {
        if (prevPageButton_.enabled &&
            screenX >= prevPageButton_.x && screenX <= prevPageButton_.x + prevPageButton_.width &&
            screenY >= prevPageButton_.y && screenY <= prevPageButton_.y + prevPageButton_.height) {
            return HitTestResult::PrevPage;
        }
        if (nextPageButton_.enabled &&
            screenX >= nextPageButton_.x && screenX <= nextPageButton_.x + nextPageButton_.width &&
            screenY >= nextPageButton_.y && screenY <= nextPageButton_.y + nextPageButton_.height) {
            return HitTestResult::NextPage;
        }
    }

    // Check breadcrumb segments (in header area)
    for (const auto& segment : breadcrumbs_) {
        if (!segment.fullPath.empty() &&  // Skip ellipsis
            screenX >= segment.x && screenX <= segment.x + segment.width &&
            screenY >= segment.y && screenY <= segment.y + segment.height) {
            if (outBreadcrumbPath) *outBreadcrumbPath = segment.fullPath;
            return HitTestResult::Breadcrumb;
        }
    }

    // Check Cancel button
    if (screenX >= cancelButton_.x && screenX <= cancelButton_.x + cancelButton_.width &&
        screenY >= cancelButton_.y && screenY <= cancelButton_.y + cancelButton_.height) {
        return HitTestResult::CancelButton;
    }

    // Check Load button
    if (screenX >= loadButton_.x && screenX <= loadButton_.x + loadButton_.width &&
        screenY >= loadButton_.y && screenY <= loadButton_.y + loadButton_.height) {
        return HitTestResult::LoadButton;
    }

    // Check grid cells
    for (const auto& cell : gridCells_) {
        if (cell.entryIndex >= 0 && cell.entryIndex < static_cast<int>(currentPageEntries_.size())) {
            // Check if click is within cell bounds (including label area)
            float cellBottom = cell.y + cell.height + config_.labelHeight;
            if (screenX >= cell.x && screenX <= cell.x + cell.width &&
                screenY >= cell.y && screenY <= cellBottom) {
                if (outEntry) *outEntry = &currentPageEntries_[cell.entryIndex];
                return HitTestResult::Entry;
            }
        }
    }

    return HitTestResult::None;
}

std::string FolderBrowser::generateFolderIconSVG(float size) const {
    // Simple folder icon - yellow/orange folder shape
    std::ostringstream ss;
    float scale = size / 100.0f;
    ss << "<g transform=\"scale(" << scale << ")\">"
       << R"(<path d="M10,25 L10,80 L90,80 L90,35 L45,35 L40,25 Z" fill="#f4a623" stroke="#c78418" stroke-width="2"/>)"
       << R"(<path d="M10,35 L90,35 L90,80 L10,80 Z" fill="#ffc107"/>)"
       << "</g>";
    return ss.str();
}

std::string FolderBrowser::generateParentIconSVG(float size) const {
    // Parent directory icon - folder with up arrow
    std::ostringstream ss;
    float scale = size / 100.0f;
    ss << "<g transform=\"scale(" << scale << ")\">"
       << R"(<path d="M10,25 L10,80 L90,80 L90,35 L45,35 L40,25 Z" fill="#6c757d" stroke="#495057" stroke-width="2"/>)"
       << R"(<path d="M10,35 L90,35 L90,80 L10,80 Z" fill="#adb5bd"/>)"
       << R"(<path d="M50,45 L35,60 L45,60 L45,75 L55,75 L55,60 L65,60 Z" fill="#212529"/>)"
       << "</g>";
    return ss.str();
}

std::string FolderBrowser::generateVolumeIconSVG(float size) const {
    // Volume/drive icon - hard drive shape
    std::ostringstream ss;
    float scale = size / 100.0f;
    ss << "<g transform=\"scale(" << scale << ")\">"
       << R"(<rect x="10" y="30" width="80" height="50" rx="5" fill="#495057" stroke="#343a40" stroke-width="2"/>)"
       << R"(<rect x="15" y="35" width="70" height="35" rx="3" fill="#6c757d"/>)"
       << R"(<circle cx="75" cy="60" r="5" fill="#28a745"/>)"  // Green LED
       << R"(<rect x="20" y="45" width="40" height="4" fill="#adb5bd"/>)"  // Label
       << "</g>";
    return ss.str();
}

std::string FolderBrowser::generateSVGThumbnail(const std::string& svgPath, float width, float height, int cellIndex) {
    // NON-BLOCKING: Uses ThumbnailCache for background loading
    // Main thread NEVER blocks on file I/O - always returns immediately
    // cellIndex is used for deterministic placeholder IDs (fixes race condition regression)

    if (!thumbnailCache_) {
        // Fallback if cache not initialized (shouldn't happen)
        return ThumbnailCache::generatePlaceholder(width, height, ThumbnailState::Error, cellIndex);
    }

    // Check cache for ready thumbnail
    ThumbnailState state = thumbnailCache_->getState(svgPath);

    if (state == ThumbnailState::Ready) {
        // Return cached thumbnail (fast path)
        auto cached = thumbnailCache_->getThumbnailSVG(svgPath);
        if (cached.has_value() && !cached.value().empty()) {
            return cached.value();
        }
    }

    // Use cellIndex for priority - lower index = higher priority (visible cells load first)
    int priority = cellIndex;

    // Request background loading (non-blocking)
    // Issue 10 fix: Remove debug spam from thumbnail requests
    thumbnailCache_->requestLoad(svgPath, width, height, priority);

    // Return animated placeholder while loading
    // cellIndex ensures deterministic IDs that match between SVG regenerations
    return ThumbnailCache::generatePlaceholder(width, height, state, cellIndex);
}

std::string FolderBrowser::generateSelectionHighlight(const GridCell& cell) const {
    std::ostringstream ss;
    // Blue highlight border for selected cell
    ss << R"(<rect x=")" << (cell.x - 3) << R"(" y=")" << (cell.y - 3)
       << R"(" width=")" << (cell.width + 6) << R"(" height=")" << (cell.height + 6)
       << R"(" fill="none" stroke="#007bff" stroke-width="4" rx="10"/>)";
    return ss.str();
}

std::string FolderBrowser::generateHoverHighlight(const GridCell& cell) const {
    std::ostringstream ss;
    // Yellow glow effect for hovered cell (simple yellow border, no filter for compatibility)
    ss << R"(<rect x=")" << (cell.x - 2) << R"(" y=")" << (cell.y - 2)
       << R"(" width=")" << (cell.width + 4) << R"(" height=")" << (cell.height + 4)
       << R"(" fill="none" stroke="#ffcc00" stroke-width="3" rx="10" stroke-opacity="0.8"/>)";
    return ss.str();
}

std::string FolderBrowser::generateClickFeedbackHighlight(const GridCell& cell) const {
    std::ostringstream ss;
    // White flash effect for click feedback - intensity determines opacity
    // Flash starts white (full intensity) and fades to transparent
    // Use fill-opacity instead of rgba() to avoid raw string literal issues
    float fillOpacity = clickFeedbackIntensity_ * 0.7f;  // Max 70% opacity
    float strokeOpacity = clickFeedbackIntensity_;       // Full intensity for border
    ss << "<rect x=\"" << cell.x << "\" y=\"" << cell.y
       << "\" width=\"" << cell.width << "\" height=\"" << cell.height
       << "\" fill=\"#ffffff\" fill-opacity=\"" << fillOpacity << "\" rx=\"8\"/>";
    // Add a bright border for emphasis
    ss << "<rect x=\"" << (cell.x - 3) << "\" y=\"" << (cell.y - 3)
       << "\" width=\"" << (cell.width + 6) << "\" height=\"" << (cell.height + 6)
       << "\" fill=\"none\" stroke=\"#ffffff\" stroke-opacity=\"" << strokeOpacity
       << "\" stroke-width=\"4\" rx=\"10\"/>";
    return ss.str();
}

std::string FolderBrowser::generateProgressOverlay() const {
    if (!isLoading_) return "";

    std::ostringstream ss;
    float vh = static_cast<float>(config_.containerHeight) / 100.0f;

    // Semi-transparent dark overlay covering entire viewport
    ss << R"(<rect width="100%" height="100%" fill="#000000" fill-opacity="0.7"/>)";

    // Centered progress box
    float boxWidth = 50.0f * vh;   // 50vh width
    float boxHeight = 12.0f * vh;  // 12vh height
    float boxX = (config_.containerWidth - boxWidth) / 2;
    float boxY = (config_.containerHeight - boxHeight) / 2;
    float boxRadius = 1.5f * vh;   // Rounded corners

    // Progress box background
    ss << R"(<rect x=")" << boxX << R"(" y=")" << boxY
       << R"(" width=")" << boxWidth << R"(" height=")" << boxHeight
       << R"(" fill="#1a1a2e" stroke="#4a5568" stroke-width="2" rx=")" << boxRadius << R"("/>)";

    // Message text
    float messageFontSize = 2.0f * vh;
    float messageY = boxY + 3.5f * vh;
    ss << R"(<text x=")" << (config_.containerWidth / 2) << R"(" y=")" << messageY
       << R"(" fill="#e2e8f0" font-family="sans-serif" font-size=")" << messageFontSize
       << R"(" text-anchor="middle" font-weight="500">)"
       << (loadingMessage_.empty() ? "Loading..." : loadingMessage_) << R"(</text>)";

    // Progress bar track
    float barMargin = 3.0f * vh;
    float barWidth = boxWidth - barMargin * 2;
    float barHeight = 2.5f * vh;
    float barX = boxX + barMargin;
    float barY = boxY + boxHeight - barMargin - barHeight;
    float barRadius = barHeight / 2;

    // Track background
    ss << R"(<rect x=")" << barX << R"(" y=")" << barY
       << R"(" width=")" << barWidth << R"(" height=")" << barHeight
       << R"(" fill="#2d3748" rx=")" << barRadius << R"("/>)";

    // Progress fill (atomic load for thread safety)
    float currentProgress = loadingProgress_.load();
    float fillWidth = barWidth * currentProgress;
    if (fillWidth > 0) {
        ss << R"(<rect x=")" << barX << R"(" y=")" << barY
           << R"(" width=")" << fillWidth << R"(" height=")" << barHeight
           << R"(" fill="#4299e1" rx=")" << barRadius << R"("/>)";
    }

    // Percentage text
    float percentFontSize = 1.6f * vh;
    float percentY = barY + barHeight / 2 + percentFontSize * 0.35f;
    int percent = static_cast<int>(currentProgress * 100);
    ss << R"(<text x=")" << (barX + barWidth / 2) << R"(" y=")" << percentY
       << R"(" fill="#ffffff" font-family="sans-serif" font-size=")" << percentFontSize
       << R"(" text-anchor="middle" font-weight="bold">)"
       << percent << R"(%</text>)";

    return ss.str();
}

std::string FolderBrowser::generateBreadcrumbBar() const {
    std::ostringstream ss;

    // vh-based sizing for proportional scaling
    float vh = static_cast<float>(config_.containerHeight) / 100.0f;

    // vh-based font sizes for breadcrumbs (must match calculateBreadcrumbs)
    float breadcrumbFontSize = 1.8f * vh;   // 1.8vh font size
    float textOffset = 0.5f * vh;           // 0.5vh vertical offset
    float chevronSlant = 1.5f * vh;         // 1.5vh chevron point (more pronounced slant)
    float arrowSize = 0.8f * vh;            // 0.8vh separator triangle size
    float arrowSpacing = 0.6f * vh;         // 0.6vh spacing around separator

    // Breadcrumb segments with chevron shapes and triangle separators
    for (size_t i = 0; i < breadcrumbs_.size(); i++) {
        const PathSegment& seg = breadcrumbs_[i];

        // Chevron/slanted shape (parallelogram with pointed right edge)
        // Shape: flat left edge, pointed right edge like an arrow tab
        std::string fillColor = seg.fullPath.empty() ? "#555" : "#3d4448";
        float x1 = seg.x;                          // Top-left
        float x2 = seg.x + seg.width;              // Top-right (before point)
        float x3 = seg.x + seg.width + chevronSlant; // Right point (middle)
        float y1 = seg.y;                          // Top
        float y2 = seg.y + seg.height / 2;         // Middle (for point)
        float y3 = seg.y + seg.height;             // Bottom

        // Draw chevron polygon: left-top -> right-top -> point -> right-bottom -> left-bottom
        ss << R"(<polygon points=")"
           << x1 << "," << y1 << " "               // Top-left
           << x2 << "," << y1 << " "               // Top-right
           << x3 << "," << y2 << " "               // Right point (middle)
           << x2 << "," << y3 << " "               // Bottom-right
           << x1 << "," << y3                      // Bottom-left
           << R"(" fill=")" << fillColor << R"(" opacity="0.9"/>)";

        // Segment text (centered in the non-pointed part)
        float textX = seg.x + seg.width / 2;
        float textY = seg.y + seg.height / 2 + textOffset;
        std::string textColor = seg.fullPath.empty() ? "#888" : "#00bfff";
        ss << R"(<text x=")" << textX << R"(" y=")" << textY
           << R"(" text-anchor="middle" fill=")" << textColor
           << R"(" font-family="sans-serif" font-size=")" << breadcrumbFontSize << R"(" font-weight="bold">)"
           << escapeXml(seg.name) << R"(</text>)";

        // SVG triangle separator pointing right (except after last segment)
        if (i < breadcrumbs_.size() - 1) {
            float triX = x3 + arrowSpacing + arrowSize / 2;  // Center of triangle
            float triY = y2;  // Vertically centered
            // Triangle pointing RIGHT
            ss << R"(<polygon points=")"
               << (triX - arrowSize * 0.4f) << "," << (triY - arrowSize * 0.6f) << " "  // Top-left
               << (triX + arrowSize * 0.6f) << "," << triY << " "                        // Right point
               << (triX - arrowSize * 0.4f) << "," << (triY + arrowSize * 0.6f)          // Bottom-left
               << R"(" fill="#666"/>)";
        }
    }

    return ss.str();
}

std::string FolderBrowser::generateButtonBar() const {
    std::ostringstream ss;

    // vh-based sizing for proportional scaling
    float vh = static_cast<float>(config_.containerHeight) / 100.0f;

    // Button bar background
    float barY = config_.containerHeight - config_.buttonBarHeight;
    ss << R"(<rect x="0" y=")" << barY << R"(" width=")" << config_.containerWidth
       << R"(" height=")" << config_.buttonBarHeight << R"(" fill="#1a1a2e" opacity="0.9"/>)";

    // vh-based font sizes for buttons (larger, more readable)
    float buttonFontSize = 2.5f * vh;      // 2.5vh font size
    float buttonTextOffset = 0.9f * vh;    // 0.9vh vertical offset for centering
    float buttonRadius = 0.6f * vh;        // 0.6vh border radius
    float strokeWidth = 0.2f * vh;         // 0.2vh stroke width

    // Cancel button (always enabled)
    ss << R"(<rect x=")" << cancelButton_.x << R"(" y=")" << cancelButton_.y
       << R"(" width=")" << cancelButton_.width << R"(" height=")" << cancelButton_.height
       << R"(" rx=")" << buttonRadius << R"(" fill="#6c757d" stroke="#495057" stroke-width=")" << strokeWidth << R"("/>)"
       << R"(<text x=")" << (cancelButton_.x + cancelButton_.width / 2)
       << R"(" y=")" << (cancelButton_.y + cancelButton_.height / 2 + buttonTextOffset)
       << R"(" text-anchor="middle" fill="#ffffff" font-family="sans-serif" font-size=")" << buttonFontSize
       << R"(" font-weight="bold">Cancel</text>)";

    // Load button (enabled only when an SVG file is selected)
    bool loadEnabled = canLoad();
    std::string loadFill = loadEnabled ? "#28a745" : "#495057";    // Green when enabled, gray when disabled
    std::string loadStroke = loadEnabled ? "#1e7e34" : "#343a40";
    std::string loadTextFill = loadEnabled ? "#ffffff" : "#868e96";

    ss << R"(<rect x=")" << loadButton_.x << R"(" y=")" << loadButton_.y
       << R"(" width=")" << loadButton_.width << R"(" height=")" << loadButton_.height
       << R"(" rx=")" << buttonRadius << R"(" fill=")" << loadFill << R"(" stroke=")" << loadStroke << R"(" stroke-width=")" << strokeWidth << R"("/>)"
       << R"(<text x=")" << (loadButton_.x + loadButton_.width / 2)
       << R"(" y=")" << (loadButton_.y + loadButton_.height / 2 + buttonTextOffset)
       << R"(" text-anchor="middle" fill=")" << loadTextFill
       << R"(" font-family="sans-serif" font-size=")" << buttonFontSize << R"(" font-weight="bold">Load</text>)";

    return ss.str();
}

std::string FolderBrowser::generateNavBar() const {
    std::ostringstream ss;

    // vh-based sizing for proportional scaling
    float vh = static_cast<float>(config_.containerHeight) / 100.0f;

    // Nav bar background
    float navY = config_.headerHeight;
    ss << R"(<rect x="0" y=")" << navY << R"(" width=")" << config_.containerWidth
       << R"(" height=")" << config_.navBarHeight << R"(" fill="#12121f"/>)";

    // vh-based font sizes for nav bar elements
    float navButtonFontSize = 2.0f * vh;   // 2vh for arrow buttons
    float sortFontSize = 1.6f * vh;        // 1.6vh for sort label
    float navTextOffset = 0.7f * vh;       // 0.7vh vertical offset
    float sortTextOffset = 0.5f * vh;      // 0.5vh for sort text offset
    float navRadius = 0.5f * vh;           // 0.5vh border radius
    float strokeWidth = 0.1f * vh;         // 0.1vh stroke width

    // Back button (left arrow)
    std::string backFill = canGoBack() ? "#007bff" : "#495057";
    std::string backTextFill = canGoBack() ? "#ffffff" : "#6c757d";
    ss << R"(<rect x=")" << backButton_.x << R"(" y=")" << backButton_.y
       << R"(" width=")" << backButton_.width << R"(" height=")" << backButton_.height
       << R"(" rx=")" << navRadius << R"(" fill=")" << backFill << R"("/>)"
       << R"(<text x=")" << (backButton_.x + backButton_.width / 2)
       << R"(" y=")" << (backButton_.y + backButton_.height / 2 + navTextOffset)
       << R"(" text-anchor="middle" fill=")" << backTextFill
       << R"(" font-family="sans-serif" font-size=")" << navButtonFontSize << R"(" font-weight="bold">)" << "&lt;" << R"(</text>)";

    // Forward button (right arrow)
    std::string forwardFill = canGoForward() ? "#007bff" : "#495057";
    std::string forwardTextFill = canGoForward() ? "#ffffff" : "#6c757d";
    ss << R"(<rect x=")" << forwardButton_.x << R"(" y=")" << forwardButton_.y
       << R"(" width=")" << forwardButton_.width << R"(" height=")" << forwardButton_.height
       << R"(" rx=")" << navRadius << R"(" fill=")" << forwardFill << R"("/>)"
       << R"(<text x=")" << (forwardButton_.x + forwardButton_.width / 2)
       << R"(" y=")" << (forwardButton_.y + forwardButton_.height / 2 + navTextOffset)
       << R"(" text-anchor="middle" fill=")" << forwardTextFill
       << R"(" font-family="sans-serif" font-size=")" << navButtonFontSize << R"(" font-weight="bold">)" << "&gt;" << R"(</text>)";

    // Sort button (shows current sort mode with direction triangle)
    std::string modeLabel = (config_.sortMode == BrowserSortMode::Alphabetical) ? "A-Z" : "Date";

    // Button background
    ss << R"(<rect x=")" << sortButton_.x << R"(" y=")" << sortButton_.y
       << R"(" width=")" << sortButton_.width << R"(" height=")" << sortButton_.height
       << R"(" rx=")" << navRadius << R"(" fill="#3d4448" stroke="#636e72" stroke-width=")" << strokeWidth << R"("/>)";

    // Text label (offset left to make room for triangle)
    float textCenterX = sortButton_.x + sortButton_.width / 2 - 1.0f * vh;
    ss << R"(<text x=")" << textCenterX
       << R"(" y=")" << (sortButton_.y + sortButton_.height / 2 + sortTextOffset)
       << R"(" text-anchor="middle" fill="#00bfff" font-family="sans-serif" font-size=")" << sortFontSize << R"(">)"
       << modeLabel << R"(</text>)";

    // SVG triangle indicator (pointing up for ascending, down for descending)
    float triSize = 1.0f * vh;  // Triangle size
    float triX = textCenterX + 3.5f * vh;  // Position right of text
    float triY = sortButton_.y + sortButton_.height / 2;

    if (config_.sortDirection == BrowserSortDirection::Ascending) {
        // Triangle pointing UP: peak at top, base at bottom
        ss << R"(<polygon points=")"
           << triX << "," << (triY - triSize * 0.6f) << " "  // Top peak
           << (triX - triSize * 0.6f) << "," << (triY + triSize * 0.4f) << " "  // Bottom left
           << (triX + triSize * 0.6f) << "," << (triY + triSize * 0.4f)  // Bottom right
           << R"(" fill="#00bfff"/>)";
    } else {
        // Triangle pointing DOWN: peak at bottom, base at top
        ss << R"(<polygon points=")"
           << triX << "," << (triY + triSize * 0.6f) << " "  // Bottom peak
           << (triX - triSize * 0.6f) << "," << (triY - triSize * 0.4f) << " "  // Top left
           << (triX + triSize * 0.6f) << "," << (triY - triSize * 0.4f)  // Top right
           << R"(" fill="#00bfff"/>)";
    }

    return ss.str();
}

std::string FolderBrowser::formatModifiedTime(std::time_t time) const {
    if (time == 0) return "";

    // Issue 7 fix: use localtime_r for thread safety
    std::tm tm_storage;
    std::tm* tm = localtime_r(&time, &tm_storage);
    if (!tm) return "";

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(4) << (tm->tm_year + 1900) << "-"
       << std::setw(2) << (tm->tm_mon + 1) << "-"
       << std::setw(2) << tm->tm_mday << " "
       << std::setw(2) << tm->tm_hour << ":"
       << std::setw(2) << tm->tm_min;
    return ss.str();
}

std::string FolderBrowser::generateCellLabel(const BrowserEntry& entry, float cellX, float cellWidth, float labelY) const {
    std::ostringstream ss;

    float labelX = cellX + cellWidth / 2;

    // Label fonts scale proportionally with labelHeight
    // Reference: 45px labelHeight at 1920x1080 with 22px/16px fonts
    // Scale factor adjusts fonts for HiDPI (e.g., 90px labelHeight at 3840x2160)
    float labelScale = config_.labelHeight / 45.0f;
    float filenameFontSize = 22.0f * labelScale;   // Scales with resolution
    float modTimeFontSize = 16.0f * labelScale;    // Scales with resolution
    float modTimeOffset = 18.0f * labelScale;      // Scales with resolution

    // Truncate long names
    std::string displayName = entry.name;
    size_t maxLen = config_.showModifiedTime ? 18 : 22;
    if (displayName.length() > maxLen) {
        displayName = displayName.substr(0, maxLen - 3) + "...";
    }

    // Filename (first line) - escape XML entities for safe display
    // Fluid font size scales with container
    ss << R"(<text x=")" << labelX << R"(" y=")" << labelY
       << R"(" text-anchor="middle" fill="#ffffff" font-family="sans-serif" font-size=")" << filenameFontSize << R"(">)"
       << escapeXml(displayName) << R"(</text>)";

    // Modified time (second line, smaller, gray)
    if (config_.showModifiedTime && entry.modifiedTime > 0) {
        std::string modTimeStr = formatModifiedTime(entry.modifiedTime);
        float modTimeY = labelY + modTimeOffset;  // Below filename (scaled offset)
        ss << R"(<text x=")" << labelX << R"(" y=")" << modTimeY
           << R"(" text-anchor="middle" fill="#868e96" font-family="sans-serif" font-size=")" << modTimeFontSize << R"(">)"
           << modTimeStr << R"(</text>)";
    }

    return ss.str();
}

std::string FolderBrowser::generateBrowserSVG() {
    std::ostringstream svg;

    // SVG header
    svg << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" )"
        << R"(width=")" << config_.containerWidth << R"(" height=")" << config_.containerHeight << R"(" )"
        << R"(viewBox="0 0 )" << config_.containerWidth << " " << config_.containerHeight << R"(">)";

    // Background
    svg << R"(<rect width="100%" height="100%" fill=")" << config_.bgColor << R"("/>)";

    // Header bar background
    svg << R"(<rect x="0" y="0" width=")" << config_.containerWidth
        << R"(" height=")" << config_.headerHeight << R"(" fill="#0d0d1a"/>)";

    // Breadcrumb navigation (clickable path segments)
    svg << generateBreadcrumbBar();

    // Pagination controls on the right side of header with clickable arrows
    // Layout: [◀ prev] "Page X / Y" [next ▶]
    if (getTotalPages() > 1) {
        float headerMaxScale = config_.headerHeight / 40.0f;  // ~1.25 at default 50px
        float pageIndicatorFontSize = scaleFont(18.0f, config_.containerWidth, 0.6f, headerMaxScale);
        float arrowSize = prevPageButton_.width;
        float arrowY = prevPageButton_.y;

        // Previous page arrow button (◀)
        std::string prevColor = prevPageButton_.enabled ? "#74b9ff" : "#4a5568";
        std::string prevHover = prevPageButton_.enabled ? "cursor:pointer" : "";
        svg << R"(<g style=")" << prevHover << R"(">)"
            << R"(<rect x=")" << prevPageButton_.x << R"(" y=")" << arrowY
            << R"(" width=")" << arrowSize << R"(" height=")" << arrowSize
            << R"(" fill=")" << (prevPageButton_.enabled ? "#2d3748" : "#1a202c") << R"(" rx=")" << (arrowSize * 0.15f) << R"("/>)"
            << R"(<text x=")" << (prevPageButton_.x + arrowSize / 2) << R"(" y=")" << (arrowY + arrowSize * 0.7f)
            << R"(" fill=")" << prevColor << R"(" font-family="sans-serif" font-size=")" << (arrowSize * 0.6f)
            << R"(" text-anchor="middle" font-weight="bold">◀</text>)"
            << R"(</g>)";

        // Page indicator text centered between arrows
        float textCenterX = (prevPageButton_.x + prevPageButton_.width + nextPageButton_.x) / 2;
        float textY = config_.headerHeight / 2 + pageIndicatorFontSize * 0.35f;
        svg << R"(<text x=")" << textCenterX << R"(" y=")" << textY
            << R"(" fill="#e2e8f0" font-family="sans-serif" font-size=")" << pageIndicatorFontSize
            << R"(" text-anchor="middle" font-weight="500">)"
            << "Page " << (currentPage_ + 1) << " / " << getTotalPages() << R"(</text>)";

        // Next page arrow button (▶)
        std::string nextColor = nextPageButton_.enabled ? "#74b9ff" : "#4a5568";
        std::string nextHover = nextPageButton_.enabled ? "cursor:pointer" : "";
        svg << R"(<g style=")" << nextHover << R"(">)"
            << R"(<rect x=")" << nextPageButton_.x << R"(" y=")" << arrowY
            << R"(" width=")" << arrowSize << R"(" height=")" << arrowSize
            << R"(" fill=")" << (nextPageButton_.enabled ? "#2d3748" : "#1a202c") << R"(" rx=")" << (arrowSize * 0.15f) << R"("/>)"
            << R"(<text x=")" << (nextPageButton_.x + arrowSize / 2) << R"(" y=")" << (arrowY + arrowSize * 0.7f)
            << R"(" fill=")" << nextColor << R"(" font-family="sans-serif" font-size=")" << (arrowSize * 0.6f)
            << R"(" text-anchor="middle" font-weight="bold">▶</text>)"
            << R"(</g>)";
    }

    // Nav bar with back/forward/sort buttons
    svg << generateNavBar();

    // Draw grid cells
    for (const auto& cell : gridCells_) {
        if (cell.entryIndex < 0 || cell.entryIndex >= static_cast<int>(currentPageEntries_.size())) continue;

        const BrowserEntry& entry = currentPageEntries_[cell.entryIndex];

        // Hover highlight for hovered cell (only if not selected)
        if (cell.index == hoveredIndex_ && cell.index != selectedIndex_) {
            svg << generateHoverHighlight(cell);
        }

        // Selection highlight for selected cell
        if (cell.index == selectedIndex_) {
            svg << generateSelectionHighlight(cell);
        }

        // Cell background (lighter for hovered or selected)
        std::string cellFill = (cell.index == selectedIndex_) ? "#3d4448" :
                               (cell.index == hoveredIndex_) ? "#363d40" : "#2d3436";
        svg << R"(<rect x=")" << cell.x << R"(" y=")" << cell.y
            << R"(" width=")" << cell.width << R"(" height=")" << cell.height
            << R"(" fill=")" << cellFill << R"(" stroke="#636e72" stroke-width="1" rx="8"/>)";

        // Cell content based on type
        float iconSize = std::min(cell.width, cell.height) * 0.7f;
        float iconX = cell.x + (cell.width - iconSize) / 2;
        float iconY = cell.y + (cell.height - iconSize) / 2;

        // For SVG thumbnails, use clipPath to ensure content doesn't overflow cell bounds
        // Skia's SVG renderer doesn't reliably honor overflow="hidden" on nested SVGs
        if (entry.type == BrowserEntryType::SVGFile) {
            // Define unique clipPath for this cell
            std::string clipId = "cell_clip_" + std::to_string(cell.index);
            svg << R"(<defs><clipPath id=")" << clipId << R"(">)"
                << R"(<rect x=")" << iconX << R"(" y=")" << iconY
                << R"(" width=")" << iconSize << R"(" height=")" << iconSize << R"(" rx="4"/>)"
                << R"(</clipPath></defs>)";

            // Apply clipPath to thumbnail group
            svg << "<g clip-path=\"url(#" << clipId << ")\">";
            svg << "<g transform=\"translate(" << iconX << "," << iconY << ")\">";
            // Pass cell.index for deterministic placeholder IDs (fixes race condition regression)
            svg << generateSVGThumbnail(entry.fullPath, iconSize, iconSize, cell.index);
            svg << R"(</g></g>)";
        } else {
            // Non-SVG icons don't need clipping
            svg << "<g transform=\"translate(" << iconX << "," << iconY << ")\">";

            switch (entry.type) {
                case BrowserEntryType::ParentDir:
                    svg << generateParentIconSVG(iconSize);
                    break;
                case BrowserEntryType::Volume:
                    svg << generateVolumeIconSVG(iconSize);
                    break;
                case BrowserEntryType::Folder:
                    svg << generateFolderIconSVG(iconSize);
                    break;
                default:
                    break;
            }

            svg << R"(</g>)";
        }

        // Label below cell (filename + optional modified time)
        // Baseline offset scales proportionally with labelHeight
        float labelScale = config_.labelHeight / 45.0f;
        float baselineOffset = 15.0f * labelScale;  // Scales with resolution
        float labelY = cell.y + cell.height + baselineOffset;
        svg << generateCellLabel(entry, cell.x, cell.width, labelY);
    }

    // Click feedback flash (rendered on top of all cells for visibility)
    if (hasClickFeedback() && clickFeedbackIndex_ >= 0) {
        for (const auto& cell : gridCells_) {
            if (cell.index == clickFeedbackIndex_ && cell.entryIndex >= 0) {
                svg << generateClickFeedbackHighlight(cell);
                break;
            }
        }
    }

    // Button bar with Cancel and Load buttons
    svg << generateButtonBar();

    // Help text (above button bar) - fluid typography
    // Constrained to a reasonable max scale (help text is in the margin area)
    float helpMaxScale = 1.5f;  // More generous since it's in open space
    float helpOffset = scaleFont(10.0f, config_.containerWidth, 0.6f, helpMaxScale);
    float helpY = config_.containerHeight - config_.buttonBarHeight - helpOffset;
    float helpFontSize = scaleFont(14.0f, config_.containerWidth, 0.6f, helpMaxScale);
    svg << R"(<text x=")" << (config_.containerWidth / 2) << R"(" y=")" << helpY
        << "\" text-anchor=\"middle\" fill=\"#6c757d\" font-family=\"sans-serif\" font-size=\"" << helpFontSize
        << "\">Click to select | Double-click to open folder | LEFT/RIGHT for pages"
        << R"(</text>)";

    // Progress overlay (rendered last so it appears on top of everything)
    svg << generateProgressOverlay();

    svg << R"(</svg>)";

    SVG_INSTRUMENT_CALL(onBrowserSVGRegenerated);
    return svg.str();
}

// ============================================================================
// Dirty Flag System - Avoids regenerating browser SVG every frame
// ============================================================================

void FolderBrowser::markDirty() {
    dirty_.store(true);
}

bool FolderBrowser::regenerateBrowserSVGIfNeeded() {
    // Check if state has changed (explicit dirty flag OR detected changes)
    bool needsRegen = dirty_.load();

    // Also check for state changes that might have been missed
    if (!needsRegen) {
        needsRegen = (currentPage_ != lastPage_) ||
                     (selectedIndex_ != lastSelectedIndex_) ||
                     (hoveredIndex_ != lastHoveredIndex_) ||
                     (clickFeedbackIndex_ != lastClickFeedbackIndex_) ||
                     (clickFeedbackIntensity_ != lastClickFeedbackIntensity_) ||
                     (currentDir_ != lastDirectory_) ||
                     (allEntries_.size() != lastEntryCount_) ||
                     (isLoading_ != lastIsLoading_) ||
                     (loadingProgress_.load() != lastLoadingProgress_);
    }

    // Check if thumbnail cache has new ready thumbnails
    bool thumbnailsReady = false;
    if (!needsRegen && thumbnailCache_) {
        thumbnailsReady = thumbnailCache_->hasNewReadyThumbnails();
        needsRegen = thumbnailsReady;
    }

    if (needsRegen) {
        if (thumbnailsReady) {
            std::cout << "[FolderBrowser] Regenerating SVG: new thumbnails ready" << std::endl;
        }
        // Update state tracking
        lastPage_ = currentPage_;
        lastSelectedIndex_ = selectedIndex_;
        lastHoveredIndex_ = hoveredIndex_;
        lastClickFeedbackIndex_ = clickFeedbackIndex_;
        lastClickFeedbackIntensity_ = clickFeedbackIntensity_;
        lastDirectory_ = currentDir_;
        lastEntryCount_ = allEntries_.size();
        lastIsLoading_ = isLoading_;
        lastLoadingProgress_ = loadingProgress_.load();

        // Regenerate the browser SVG
        cachedBrowserSVG_ = generateBrowserSVG();
        dirty_.store(false);
        return true;
    }

    return false;
}

// ============================================================================
// ThumbnailCache Lifecycle
// ============================================================================

void FolderBrowser::startThumbnailLoader() {
    if (thumbnailCache_) {
        thumbnailCache_->startLoader();
    }
}

void FolderBrowser::stopThumbnailLoader() {
    if (thumbnailCache_) {
        thumbnailCache_->stopLoader();
    }
}

} // namespace svgplayer
