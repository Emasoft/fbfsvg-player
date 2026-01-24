# Hit Testing System Analysis - Folder Browser

**Date:** 2026-01-22  
**Files Analyzed:**
- `src/folder_browser.h`
- `src/folder_browser.cpp`

---

## 1. HitTestResult Enum Values

**Location:** `folder_browser.h` (lines 68-79)

```cpp
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
```

**Current Values:** 11 distinct hit zones

---

## 2. GridCell Structure

**Location:** `folder_browser.h` (lines 53-58)

```cpp
struct GridCell {
    int index;                  // Cell index (0-based)
    float x, y;                 // Top-left position
    float width, height;        // Cell dimensions (thumbnail area only, excludes label)
    int entryIndex = -1;        // Index into currentPageEntries_ (-1 if empty)
};
```

**Key Insight:** `GridCell` defines the **thumbnail area only**, not including the label below.  
The label area is added during hit testing (see `hitTest()` implementation).

---

## 3. GridCell Calculation

**Location:** `folder_browser.cpp` (lines 1075-1102)

```cpp
void FolderBrowser::calculateGridCells() {
    gridCells_.clear();

    // Reserve space for header, nav bar, and button bar
    float gridTop = config_.headerHeight + config_.navBarHeight;
    float gridBottom = config_.containerHeight - config_.buttonBarHeight;
    float gridHeight = gridBottom - gridTop;

    // Subtract margins and label heights from available space
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
            cell.entryIndex = -1;
            gridCells_.push_back(cell);
        }
    }
}
```

**Grid Layout Calculation:**

1. **Vertical space allocation:**
   - Header: `config_.headerHeight`
   - Navigation bar: `config_.navBarHeight`
   - Grid area: `gridHeight = containerHeight - headerHeight - navBarHeight - buttonBarHeight`
   - Button bar: `config_.buttonBarHeight`

2. **Cell sizing:**
   - Columns: `cellWidth = (containerWidth - margins) / columns`
   - Rows: `cellHeight = (gridHeight - margins - labelHeights) / rows`

3. **Cell positioning:**
   - X: `cellMargin + col * (cellWidth + cellMargin)`
   - Y: `gridTop + cellMargin + row * (cellHeight + cellMargin + labelHeight)`

**Important:** Each row has `labelHeight` space added to its Y calculation for the filename/date label.

---

## 4. hitTest() Function Structure

**Location:** `folder_browser.cpp` (lines 1277-1354)

```cpp
HitTestResult FolderBrowser::hitTest(float screenX, float screenY,
                                     const BrowserEntry** outEntry,
                                     std::string* outBreadcrumbPath) const {
    if (outEntry) *outEntry = nullptr;
    if (outBreadcrumbPath) *outBreadcrumbPath = "";

    // 1. Check nav bar buttons (back/forward/sort)
    if (screenX >= backButton_.x && screenX <= backButton_.x + backButton_.width &&
        screenY >= backButton_.y && screenY <= backButton_.y + backButton_.height) {
        return HitTestResult::BackButton;
    }
    // ... similar for forwardButton_, sortButton_

    // 2. Check pagination buttons (prev/next page arrows)
    if (getTotalPages() > 1) {
        if (prevPageButton_.enabled && /* bounds check */) {
            return HitTestResult::PrevPage;
        }
        if (nextPageButton_.enabled && /* bounds check */) {
            return HitTestResult::NextPage;
        }
    }

    // 3. Check breadcrumb segments
    for (const auto& segment : breadcrumbs_) {
        if (!segment.fullPath.empty() &&  // Skip ellipsis
            /* bounds check */) {
            if (outBreadcrumbPath) *outBreadcrumbPath = segment.fullPath;
            return HitTestResult::Breadcrumb;
        }
    }

    // 4. Check Cancel and Load buttons
    if (/* cancelButton_ bounds check */) {
        return HitTestResult::CancelButton;
    }
    if (/* loadButton_ bounds check */) {
        return HitTestResult::LoadButton;
    }

    // 5. Check grid cells (thread-safe with mutex)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto& cell : gridCells_) {
            if (cell.entryIndex >= 0 && cell.entryIndex < static_cast<int>(currentPageEntries_.size())) {
                // CRITICAL: Cell bounds include label area
                float cellBottom = cell.y + cell.height + config_.labelHeight;
                if (screenX >= cell.x && screenX <= cell.x + cell.width &&
                    screenY >= cell.y && screenY <= cellBottom) {
                    if (outEntry) *outEntry = &currentPageEntries_[cell.entryIndex];
                    return HitTestResult::Entry;
                }
            }
        }
    }

    return HitTestResult::None;
}
```

**Hit Testing Order (Priority):**

1. **Nav bar buttons** (back/forward/sort) - highest priority
2. **Pagination arrows** (prev/next page)
3. **Breadcrumb segments** (path navigation)
4. **Action buttons** (Cancel/Load)
5. **Grid cells** (entries) - lowest priority, includes label area
6. **Empty space** (None) - fallback

**Thread Safety:** Grid cell hit testing locks `stateMutex_` because `currentPageEntries_` can be modified by async directory scans.

---

## 5. Cell Overlay System

The folder browser uses overlay highlights rendered on top of cell backgrounds:

### A. Selection Highlight

**Location:** `folder_browser.cpp` (lines 1425-1432)

```cpp
std::string FolderBrowser::generateSelectionHighlight(const GridCell& cell) const {
    std::ostringstream ss;
    // Blue highlight border for selected cell
    ss << R"(<rect x=")" << (cell.x - 3) << R"(" y=")" << (cell.y - 3)
       << R"(" width=")" << (cell.width + 6) << R"(" height=")" << (cell.height + 6)
       << R"(" fill="none" stroke="#007bff" stroke-width="4" rx="10"/>)";
    return ss.str();
}
```

### B. Hover Highlight

**Location:** `folder_browser.cpp` (lines 1434-1441)

```cpp
std::string FolderBrowser::generateHoverHighlight(const GridCell& cell) const {
    std::ostringstream ss;
    // Yellow glow effect for hovered cell
    ss << R"(<rect x=")" << (cell.x - 2) << R"(" y=")" << (cell.y - 2)
       << R"(" width=")" << (cell.width + 4) << R"(" height=")" << (cell.height + 4)
       << R"(" fill="none" stroke="#ffcc00" stroke-width="3" rx="10" stroke-opacity="0.8"/>)";
    return ss.str();
}
```

### C. Click Feedback Highlight

**Location:** `folder_browser.cpp` (lines 1443-1459)

```cpp
std::string FolderBrowser::generateClickFeedbackHighlight(const GridCell& cell) const {
    std::ostringstream ss;
    // White flash effect for click feedback - intensity determines opacity
    float fillOpacity = clickFeedbackIntensity_ * 0.7f;  // Max 70% opacity
    float strokeOpacity = clickFeedbackIntensity_;       // Full intensity for border
    ss << "<rect x=\"" << cell.x << "\" y=\"" << cell.y
       << "\" width=\"" << cell.width << "\" height=\"" << cell.height
       << "\" fill=\"#ffffff\" fill-opacity=\"" << fillOpacity << "\" rx=\"8\"/>";
    // Add bright border for emphasis
    ss << "<rect x=\"" << (cell.x - 3) << "\" y=\"" << (cell.y - 3)
       << "\" width=\"" << (cell.width + 6) << "\" height=\"" << (cell.height + 6)
       << "\" fill=\"none\" stroke=\"#ffffff\" stroke-opacity=\"" << strokeOpacity
       << "\" stroke-width=\"4\" rx=\"10\"/>";
    return ss.str();
}
```

**Rendering Order in `generateBrowserSVG()` (lines 1860-1985):**

```cpp
// For each cell:
1. Hover highlight (if hovered and not selected)
2. Selection highlight (if selected)
3. Cell background rect
4. Cell content (icon or thumbnail)
5. Label (filename + modified date)

// After all cells:
6. Click feedback flash (rendered on top for visibility)
```

---

## 6. How to Add Play Arrow Icon Hit Zone

To add a play button overlay icon within FBF.SVG animated cells (similar to YouTube thumbnails):

### Step 1: Add New HitTestResult Value

**File:** `folder_browser.h` (line ~79)

```cpp
enum class HitTestResult {
    None,
    Entry,
    CancelButton,
    LoadButton,
    PrevPage,
    NextPage,
    Breadcrumb,
    BackButton,
    ForwardButton,
    SortButton,
    PlayArrow       // NEW: Play button overlay within animated cell
};
```

### Step 2: Add PlayArrow Region to GridCell

**Option A: Add to GridCell structure**

```cpp
struct GridCell {
    int index;
    float x, y;
    float width, height;
    int entryIndex = -1;
    
    // NEW: Play arrow overlay region (only for FBFSVGFile entries)
    float playArrowX, playArrowY;
    float playArrowSize;
    bool hasPlayArrow = false;
};
```

**Option B: Calculate on-the-fly in hitTest()** (recommended for simplicity)

No structural changes needed - calculate bounds inline.

### Step 3: Update hitTest() Function

**File:** `folder_browser.cpp` (inside `hitTest()`, before general `Entry` check)

```cpp
// NEW: Check play arrow overlays (must come BEFORE general Entry check)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (const auto& cell : gridCells_) {
        if (cell.entryIndex >= 0 && cell.entryIndex < static_cast<int>(currentPageEntries_.size())) {
            const BrowserEntry& entry = currentPageEntries_[cell.entryIndex];
            
            // Only FBF.SVG files get play arrows
            if (entry.type == BrowserEntryType::FBFSVGFile) {
                // Calculate play arrow bounds (centered circle overlay)
                float arrowSize = std::min(cell.width, cell.height) * 0.25f;  // 25% of cell
                float arrowX = cell.x + (cell.width - arrowSize) / 2;
                float arrowY = cell.y + (cell.height - arrowSize) / 2;
                
                // Check if click is within circular play button
                float centerX = arrowX + arrowSize / 2;
                float centerY = arrowY + arrowSize / 2;
                float radius = arrowSize / 2;
                float dx = screenX - centerX;
                float dy = screenY - centerY;
                
                if (dx * dx + dy * dy <= radius * radius) {
                    if (outEntry) *outEntry = &entry;
                    return HitTestResult::PlayArrow;
                }
            }
        }
    }
}

// Existing grid cell check (now catches clicks outside play arrow)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (const auto& cell : gridCells_) {
        // ... existing Entry hit test code ...
    }
}
```

**Note:** Play arrow check must come **before** general `Entry` check to intercept clicks.

### Step 4: Render Play Arrow Overlay

**File:** `folder_browser.cpp` (in `generateBrowserSVG()`, after cell content rendering)

Add a new helper function:

```cpp
std::string FolderBrowser::generatePlayArrowOverlay(const GridCell& cell) const {
    std::ostringstream ss;
    
    // Calculate overlay position (centered)
    float arrowSize = std::min(cell.width, cell.height) * 0.25f;
    float centerX = cell.x + cell.width / 2;
    float centerY = cell.y + cell.height / 2;
    float radius = arrowSize / 2;
    
    // Semi-transparent dark circle background
    ss << R"(<circle cx=")" << centerX << R"(" cy=")" << centerY
       << R"(" r=")" << radius << R"(" fill="#000000" fill-opacity="0.7"/>)";
    
    // White play triangle (pointing right)
    float triangleSize = radius * 0.6f;  // Triangle is 60% of circle radius
    float triangleX = centerX - triangleSize * 0.3f;  // Offset left to visually center
    float triangleY = centerY;
    
    // Equilateral triangle pointing right
    float x1 = triangleX;
    float y1 = triangleY - triangleSize * 0.866f / 2;  // Top vertex
    float x2 = triangleX + triangleSize;
    float y2 = triangleY;  // Right vertex (tip)
    float x3 = triangleX;
    float y3 = triangleY + triangleSize * 0.866f / 2;  // Bottom vertex
    
    ss << R"(<polygon points=")" << x1 << "," << y1 << " "
       << x2 << "," << y2 << " " << x3 << "," << y3
       << R"(" fill="#ffffff"/>)";
    
    return ss.str();
}
```

Render it in the cell loop:

```cpp
// After generating cell content (thumbnail)
if (entry.type == BrowserEntryType::FBFSVGFile) {
    svg << generatePlayArrowOverlay(cell);
}
```

### Step 5: Handle PlayArrow Click in Main Event Loop

**File:** `src/svg_player_animated.cpp` (or wherever mouse events are handled)

```cpp
void handleMouseClick(float x, float y) {
    const BrowserEntry* entry = nullptr;
    HitTestResult result = browser.hitTest(x, y, &entry);
    
    switch (result) {
        case HitTestResult::PlayArrow:
            // Start playback preview (play in-place without opening)
            if (entry) {
                startInlinePreview(entry->fullPath);
            }
            break;
        
        case HitTestResult::Entry:
            // Selection only (no playback)
            if (entry) {
                browser.selectEntry(entry->gridIndex);
            }
            break;
        
        // ... other cases ...
    }
}
```

---

## 7. Implementation Checklist

- [ ] Add `PlayArrow` to `HitTestResult` enum
- [ ] Add `generatePlayArrowOverlay()` helper function
- [ ] Update `generateBrowserSVG()` to render play arrows for `FBFSVGFile` entries
- [ ] Update `hitTest()` to detect play arrow clicks (before general Entry check)
- [ ] Add `startInlinePreview()` function to handle playback preview
- [ ] Update main event loop to handle `PlayArrow` result
- [ ] Test with FBF.SVG files to verify correct hit zone detection

---

## 8. Design Rationale

**Why separate PlayArrow from Entry?**

- Allows **two distinct actions** on the same cell:
  - Click play arrow → **Preview playback** (in-place or modal)
  - Click elsewhere → **Select for loading** (into main player)

**Why circular hit detection?**

- More intuitive than rectangular bounds
- Matches visual appearance of circular play button
- Avoids accidental clicks outside icon

**Why render play arrow AFTER content?**

- Ensures overlay appears **on top** of thumbnail
- Maintains z-order: background < content < overlays < highlights

**Why check play arrow BEFORE general Entry?**

- Hit testing priority: specific zones first, general zones last
- Prevents general Entry handler from intercepting play arrow clicks

---

## 9. Alternative Designs Considered

### A. Hover-Triggered Play Button

Show play arrow only on hover (like YouTube).

**Pros:** Cleaner appearance when not hovering  
**Cons:** Requires additional hover state tracking per cell

### B. Play Arrow in Corner

Place play arrow in top-right corner instead of center.

**Pros:** Less visual obstruction of thumbnail  
**Cons:** Less discoverable, smaller target, conflicts with selection highlight

### C. Play on Double-Click

Use double-click on cell to start preview.

**Pros:** No overlay needed  
**Cons:** Conflicts with existing "double-click to open folder" behavior

**Selected Design:** Center overlay with always-visible icon (best discoverability).

---

## 10. Performance Considerations

**SVG Regeneration Cost:**

Adding play arrows increases SVG complexity:
- **Before:** ~1200 lines of SVG for 12-cell grid
- **After:** ~1250 lines (+4% increase, ~4 lines per play arrow)

**Hit Testing Cost:**

Circular hit detection adds ~5 float operations per FBF.SVG cell:
- **Best case:** 0 FBF.SVG files → no overhead
- **Worst case:** 12 FBF.SVG files → 12 circle checks (negligible < 1μs)

**Recommendation:** Acceptable performance impact.

---

## Summary

The hit testing system uses a **priority-based cascade** where specific zones (buttons, overlays) are checked before general zones (cells). To add a play arrow:

1. Add `PlayArrow` enum value
2. Render overlay in `generateBrowserSVG()`
3. Check circular bounds in `hitTest()` **before** general Entry check
4. Handle in event loop with preview playback logic

**Key Insight:** GridCell represents thumbnail area only. The label area is added during hit testing via `cellBottom = cell.y + cell.height + config_.labelHeight`.
