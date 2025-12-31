// folder_browser.h - Visual folder browser for SVG files
// Displays a grid of SVG thumbnails with folder navigation

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <optional>

namespace svgplayer {

// Forward declaration for thumbnail cache
class ThumbnailCache;

// Entry types in the browser
enum class BrowserEntryType {
    ParentDir,      // ".." navigation
    Volume,         // Root volume/mount point
    Folder,         // Subdirectory
    SVGFile         // SVG file (animated thumbnail)
};

// Sort mode for entries
enum class BrowserSortMode {
    Alphabetical,   // Sort by name
    ModifiedTime    // Sort by last modified
};

// Sort direction
enum class BrowserSortDirection {
    Ascending,      // A-Z or oldest first
    Descending      // Z-A or newest first
};

// Single entry in the browser grid
struct BrowserEntry {
    BrowserEntryType type;
    std::string name;           // Display name
    std::string fullPath;       // Full path to file/folder
    int gridIndex;              // Position in grid (0-based)
    std::time_t modifiedTime;   // Last modified timestamp
};

// Grid cell for hit testing
struct GridCell {
    int index;                  // Cell index
    float x, y;                 // Top-left position
    float width, height;        // Cell dimensions
    int entryIndex = -1;        // Index into currentPageEntries_ (-1 if empty)
};

// UI button regions for hit testing
struct ButtonRegion {
    float x, y;
    float width, height;
    bool enabled;
};

// Hit test result type
enum class HitTestResult {
    None,           // Clicked on empty space
    Entry,          // Clicked on a grid entry
    CancelButton,   // Clicked Cancel button
    LoadButton,     // Clicked Load button
    PrevPage,       // Clicked previous page arrow
    NextPage,       // Clicked next page arrow
    Breadcrumb,     // Clicked on a breadcrumb path segment
    BackButton,     // Clicked back arrow (navigation history)
    ForwardButton,  // Clicked forward arrow (navigation history)
    SortButton      // Clicked sort mode toggle button
};

// Breadcrumb path segment for navigation
struct PathSegment {
    std::string name;       // Display name (folder name or "/")
    std::string fullPath;   // Full path up to and including this segment
    float x, y;             // Position of segment
    float width, height;    // Size of clickable region
};

// Folder browser configuration
// NOTE: Layout dimensions stay at original proportions (relative to container)
// Font sizes are scaled 6x in the SVG generation code for HiDPI visibility
struct BrowserConfig {
    int columns = 4;            // Grid columns
    int rows = 3;               // Grid rows per page
    float cellMargin = 20.0f;   // Margin between cells (original proportion)
    float labelHeight = 45.0f;  // Height for filename + modified date labels
    float headerHeight = 50.0f; // Height for path header with breadcrumbs
    float navBarHeight = 40.0f; // Height for back/forward/sort bar
    float buttonBarHeight = 60.0f; // Height for Cancel/Load buttons
    int containerWidth = 1920;  // Browser viewport width (set to renderWidth)
    int containerHeight = 1080; // Browser viewport height (set to renderHeight)
    std::string bgColor = "#1a1a2e";  // Dark background
    BrowserSortMode sortMode = BrowserSortMode::Alphabetical;  // Current sort mode
    BrowserSortDirection sortDirection = BrowserSortDirection::Ascending;  // Current sort direction
    bool showModifiedTime = true;  // Show last modified date below filename
};

// Callback when user selects an entry
using BrowserCallback = std::function<void(const BrowserEntry& entry)>;

class FolderBrowser {
public:
    FolderBrowser();
    ~FolderBrowser();

    // Configuration
    void setConfig(const BrowserConfig& config);
    const BrowserConfig& getConfig() const { return config_; }

    // Navigation
    bool setDirectory(const std::string& path, bool addToHistory = true);
    std::string getCurrentDirectory() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return currentDir_;
    }
    bool goToParent();
    bool enterFolder(const std::string& folderName);

    // Async navigation (non-blocking with progress callback)
    // Callback receives progress value 0.0-1.0, returns false to cancel
    using ProgressCallback = std::function<bool(float progress, const std::string& message)>;
    void setDirectoryAsync(const std::string& path, ProgressCallback callback, bool addToHistory = true);
    bool isScanningInProgress() const { return scanningInProgress_.load(); }
    void cancelScan();  // Request cancellation of async scan
    bool pollScanComplete();  // Check if async scan finished (call from main thread)
    void finalizeScan();  // Finalize scan results after pollScanComplete returns true

    // Async navigation helpers (non-blocking versions of navigation methods)
    void goToParentAsync(ProgressCallback callback);
    void enterFolderAsync(const std::string& folderName, ProgressCallback callback);
    void goBackAsync(ProgressCallback callback);
    void goForwardAsync(ProgressCallback callback);

    // History navigation (back/forward arrows)
    // Thread-safe accessors (lock historyMutex_)
    bool canGoBack() const {
        std::lock_guard<std::mutex> lock(historyMutex_);
        return historyIndex_ > 0;
    }
    bool canGoForward() const {
        std::lock_guard<std::mutex> lock(historyMutex_);
        return historyIndex_ < static_cast<int>(history_.size()) - 1;
    }
    bool goBack();
    bool goForward();

    // Sorting
    BrowserSortMode getSortMode() const { return config_.sortMode; }
    void setSortMode(BrowserSortMode mode);
    void toggleSortMode();  // Switch between alphabetical and modified time

    // Pagination
    int getCurrentPage() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return currentPage_;
    }
    int getTotalPages() const;
    void nextPage();
    void prevPage();
    void setPage(int page);

    // Selection state
    void selectEntry(int index);           // Select entry by index (-1 to deselect)
    void clearSelection();                 // Clear current selection
    int getSelectedIndex() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return selectedIndex_;
    }
    std::optional<BrowserEntry> getSelectedEntry() const;
    bool hasSelection() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return selectedIndex_ >= 0;
    }
    bool canLoad() const;                  // True if selection is an SVG file

    // Hover state (for visual feedback on mouse over)
    void setHoveredEntry(int index);       // Set hovered entry by index (-1 for none)
    int getHoveredIndex() const { return hoveredIndex_; }
    bool hasHover() const { return hoveredIndex_ >= 0; }

    // Loading progress overlay (for async directory scanning)
    void setLoading(bool loading, const std::string& message = "");
    void setProgress(float progress);      // 0.0 to 1.0
    bool isLoading() const { return isLoading_; }
    float getProgress() const { return loadingProgress_.load(); }
    const std::string& getLoadingMessage() const { return loadingMessage_; }

    // Click feedback state (for click flash animation)
    void triggerClickFeedback(int index);  // Start flash animation on entry
    void updateClickFeedback();            // Decay animation (call each frame)
    int getClickFeedbackIndex() const { return clickFeedbackIndex_; }
    float getClickFeedbackIntensity() const { return clickFeedbackIntensity_; }
    bool hasClickFeedback() const { return clickFeedbackIntensity_ > 0.0f; }

    // Get entries for current page
    const std::vector<BrowserEntry>& getCurrentPageEntries() const { return currentPageEntries_; }
    int getEntriesPerPage() const { return config_.columns * config_.rows; }

    // Generate composite SVG for current view
    // Returns SVG content string ready for rendering
    std::string generateBrowserSVG();  // Not const: may queue thumbnail requests

    // Hit testing - returns what was clicked and optionally the entry or breadcrumb path
    HitTestResult hitTest(float screenX, float screenY,
                         const BrowserEntry** outEntry = nullptr,
                         std::string* outBreadcrumbPath = nullptr) const;

    // Get grid cells for current page (for rendering/debugging)
    const std::vector<GridCell>& getGridCells() const { return gridCells_; }

    // Get button regions for hit testing
    const ButtonRegion& getCancelButton() const { return cancelButton_; }
    const ButtonRegion& getLoadButton() const { return loadButton_; }
    const ButtonRegion& getBackButton() const { return backButton_; }
    const ButtonRegion& getForwardButton() const { return forwardButton_; }
    const ButtonRegion& getSortButton() const { return sortButton_; }
    const ButtonRegion& getPrevPageButton() const { return prevPageButton_; }
    const ButtonRegion& getNextPageButton() const { return nextPageButton_; }

    // Get breadcrumb segments for path navigation
    const std::vector<PathSegment>& getBreadcrumbs() const { return breadcrumbs_; }

    // Dirty flag system - avoids regenerating browser SVG every frame
    void markDirty();                       // Mark browser SVG as needing regeneration
    bool isDirty() const { return dirty_.load(); }
    bool regenerateBrowserSVGIfNeeded();    // Returns true if regenerated
    const std::string& getCachedBrowserSVG() const { return cachedBrowserSVG_; }

    // ThumbnailCache lifecycle (call from main app)
    void startThumbnailLoader();            // Start background loader thread
    void stopThumbnailLoader();             // Stop background loader thread
    ThumbnailCache* getThumbnailCache() { return thumbnailCache_.get(); }

private:
    void scanDirectory();
    void updateCurrentPage();
    void sortEntries();
    void calculateGridCells();
    void calculateButtonRegions();
    void calculateBreadcrumbs();
    std::string generateFolderIconSVG(float size) const;
    std::string generateParentIconSVG(float size) const;
    std::string generateVolumeIconSVG(float size) const;
    std::string generateSVGThumbnail(const std::string& svgPath, float width, float height, int cellIndex);  // Not const: may queue load requests
    std::string generateButtonBar() const;
    std::string generateBreadcrumbBar() const;
    std::string generateNavBar() const;        // Back/forward/sort navigation bar
    std::string generateSelectionHighlight(const GridCell& cell) const;
    std::string generateHoverHighlight(const GridCell& cell) const;
    std::string generateClickFeedbackHighlight(const GridCell& cell) const;
    std::string generateProgressOverlay() const;   // Loading progress bar overlay
    std::string generateCellLabel(const BrowserEntry& entry, float cellX, float cellWidth, float labelY) const;
    std::string formatModifiedTime(std::time_t time) const;

    BrowserConfig config_;
    std::string currentDir_;
    int currentPage_ = 0;
    int selectedIndex_ = -1;               // Currently selected entry index (-1 = none)
    int hoveredIndex_ = -1;                // Currently hovered entry index (-1 = none)
    int clickFeedbackIndex_ = -1;          // Entry showing click flash (-1 = none)
    float clickFeedbackIntensity_ = 0.0f;  // Flash intensity (1.0 = full, decays to 0)

    // Loading progress state
    bool isLoading_ = false;               // True when directory scan in progress
    std::atomic<float> loadingProgress_{0.0f};  // Progress value 0.0 to 1.0 (atomic for thread safety)
    std::string loadingMessage_;           // Message to display during loading

    // Navigation history for back/forward
    // Protected by historyMutex_ for thread-safe access from main and scan threads
    mutable std::mutex historyMutex_;
    std::vector<std::string> history_;
    int historyIndex_ = -1;                // Current position in history (-1 = no history)

    // State mutex for protecting currentDir_, currentPage_, selectedIndex_
    // These variables are accessed from both synchronous setDirectory() and async finalizeScan()
    mutable std::mutex stateMutex_;

    // All entries in current directory
    std::vector<BrowserEntry> allEntries_;

    // Entries visible on current page
    std::vector<BrowserEntry> currentPageEntries_;

    // Grid cells for hit testing
    std::vector<GridCell> gridCells_;

    // Button regions for hit testing
    ButtonRegion cancelButton_;
    ButtonRegion loadButton_;
    ButtonRegion backButton_;
    ButtonRegion forwardButton_;
    ButtonRegion sortButton_;
    ButtonRegion prevPageButton_;   // Pagination: previous page arrow
    ButtonRegion nextPageButton_;   // Pagination: next page arrow

    // Breadcrumb path segments for navigation
    std::vector<PathSegment> breadcrumbs_;

    // Async scanning infrastructure
    std::atomic<bool> scanningInProgress_{false};  // True while background scan running
    std::atomic<bool> scanCancelRequested_{false}; // True when cancel requested
    std::atomic<bool> scanComplete_{false};        // True when scan finished (main thread polls)
    std::thread scanThread_;                       // Background scanning thread
    std::mutex scanMutex_;                         // Protects pendingEntries_
    std::mutex pendingMutex_;                      // Protects pendingDir_ and pendingAddToHistory_
    std::vector<BrowserEntry> pendingEntries_;     // Results from background scan
    std::string pendingDir_;                       // Directory being scanned
    bool pendingAddToHistory_ = false;             // Whether to add to history when complete

    // Dirty flag system - avoids regenerating browser SVG every frame
    mutable std::atomic<bool> dirty_{true};        // True = needs regeneration
    std::string cachedBrowserSVG_;                 // Cached result of generateBrowserSVG()

    // State tracking for dirty detection (detect changes)
    int lastPage_ = -1;
    int lastSelectedIndex_ = -2;                   // -2 = never set
    int lastHoveredIndex_ = -2;
    int lastClickFeedbackIndex_ = -2;
    float lastClickFeedbackIntensity_ = -1.0f;
    std::string lastDirectory_;
    size_t lastEntryCount_ = 0;
    bool lastIsLoading_ = false;
    float lastLoadingProgress_ = -1.0f;

    // ThumbnailCache for background loading of SVG thumbnails
    std::unique_ptr<ThumbnailCache> thumbnailCache_;
};

} // namespace svgplayer
