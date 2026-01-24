# Folder Browser File Filtering Analysis
Generated: 2026-01-22

## Summary

The folder browser in `src/folder_browser.cpp` implements a sophisticated 3-tier file type detection system:
1. **SVGFile** - Static SVG files (no animation)
2. **FBFSVGFile** - Animated FBF.SVG files (contains SMIL animation with frame IDs)
3. **FrameFolder** - Folders containing numbered SVG frame sequences

All detection happens during directory scanning with early filtering for performance.

---

## 1. BrowserEntryType Enum (src/folder_browser.h:22-29)

```cpp
enum class BrowserEntryType {
    ParentDir,      // ".." navigation
    Volume,         // Root volume/mount point
    Folder,         // Subdirectory (regular folder)
    FrameFolder,    // Folder containing numbered SVG frames (frame_001.svg, frame_002.svg, ...)
    SVGFile,        // Static SVG file (no SMIL animation)
    FBFSVGFile      // Animated FBF.SVG file (contains SMIL frames with IDs like frame_001, frame_002)
};
```

**Key Insight**: FrameFolder is a special folder type that is treated as a playable entity (like a video file).

---

## 2. SVG File Detection (scanDirectory)

### Entry Point: `void FolderBrowser::scanDirectory()` (line 814)

**Detection Flow**:

```
For each filesystem entry:
  ├─ Is Directory?
  │   ├─ Yes → Check if FrameFolder (isFrameSequenceFolder)
  │   │   ├─ Yes → BrowserEntryType::FrameFolder
  │   │   └─ No → BrowserEntryType::Folder
  │   └─ No → Continue
  │
  ├─ Is Regular File?
  │   ├─ Yes → Check extension
  │   │   ├─ Extension == ".svg" (case-insensitive)?
  │   │   │   ├─ Yes → Check if animated (isFBFSVGFile)
  │   │   │   │   ├─ Yes → BrowserEntryType::FBFSVGFile
  │   │   │   │   └─ No → BrowserEntryType::SVGFile
  │   │   │   └─ No → Skip (continue)
  │   │   └─ Extension != ".svg" → Skip (continue)
  │   └─ No → Skip
```

### Code Location: lines 933-962

```cpp
if (entry.is_directory()) {
    BrowserEntry folderEntry;
    // Check if folder contains numbered SVG frames (FrameFolder)
    if (isFrameSequenceFolder(entry.path().string())) {
        folderEntry.type = BrowserEntryType::FrameFolder;
    } else {
        folderEntry.type = BrowserEntryType::Folder;
    }
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
        // Check if SVG contains SMIL animation (FBF.SVG format)
        if (isFBFSVGFile(entry.path().string())) {
            fileEntry.type = BrowserEntryType::FBFSVGFile;
        } else {
            fileEntry.type = BrowserEntryType::SVGFile;
        }
        fileEntry.name = name;
        fileEntry.fullPath = entry.path().string();
        fileEntry.modifiedTime = modTime;
        svgFiles.push_back(fileEntry);
    }
}
```

**Key Points**:
- ✓ Extension check is **case-insensitive** (`std::transform` to lowercase)
- ✓ Non-SVG files are **immediately skipped** (`continue`)
- ✓ Hidden files (starting with `.`) are **filtered out** earlier (line 914)
- ✓ Folders and files are collected separately, then combined

---

## 3. FBFSVGFile Detection

### Function: `static bool isFBFSVGFile(const std::string& svgPath)` (lines 114-142)

**Purpose**: Distinguish animated FBF.SVG files from static SVG files.

**Detection Strategy**:
1. **Fast Partial Read** - Only reads first 64KB (performance optimization)
2. **Two-Phase Check**:
   - Phase 1: Detect SMIL animation elements
   - Phase 2: Confirm frame ID patterns

**Implementation**:

```cpp
static bool isFBFSVGFile(const std::string& svgPath) {
    // Read first 64KB of file (enough to detect SMIL animation patterns)
    constexpr size_t PEEK_SIZE = 65536;
    std::ifstream file(svgPath, std::ios::binary);
    if (!file) return false;

    std::string content(PEEK_SIZE, '\0');
    file.read(&content[0], PEEK_SIZE);
    size_t bytesRead = file.gcount();
    content.resize(bytesRead);

    // Look for SMIL animation elements (animate, animateTransform, animateMotion)
    // combined with frame ID pattern (frame_001, frame_002, etc.)
    bool hasSmilAnimation =
        content.find("<animate") != std::string::npos ||
        content.find("<animateTransform") != std::string::npos ||
        content.find("<animateMotion") != std::string::npos ||
        content.find("<set") != std::string::npos;

    if (!hasSmilAnimation) return false;

    // Also check for frame IDs or values pattern typical of FBF.SVG
    // Pattern: values="#frame_001;#frame_002" or begin/end timing with frame IDs
    std::regex frameValuesPattern(R"(values\s*=\s*["'][^"']*#frame_\d+)", std::regex::icase);
    std::regex frameIdPattern(R"(id\s*=\s*["']frame_\d+["'])", std::regex::icase);

    return std::regex_search(content, frameValuesPattern) ||
           std::regex_search(content, frameIdPattern);
}
```

**Detection Criteria** (all must match):
1. **SMIL animation elements present**:
   - `<animate`
   - `<animateTransform`
   - `<animateMotion`
   - `<set`

2. **Frame ID patterns present** (at least one):
   - `values="#frame_001;#frame_002"` (frame value lists)
   - `id="frame_001"` (frame ID declarations)

**Performance**: Only reads 64KB instead of entire file (critical for large SVGs).

**Edge Cases**:
- ✓ Returns `false` if file cannot be opened
- ✓ Returns `false` if SMIL animation absent
- ✓ Returns `false` if frame patterns absent (even if SMIL present)

---

## 4. FrameFolder Detection

### Function: `static bool isFrameSequenceFolder(const std::string& folderPath)` (lines 81-109)

**Purpose**: Detect folders containing numbered SVG frame sequences.

**Detection Strategy**:
1. Scan folder for files matching frame pattern
2. Extract frame numbers
3. Verify at least 2 consecutive frames exist

**Implementation**:

```cpp
static bool isFrameSequenceFolder(const std::string& folderPath) {
    namespace fs = std::filesystem;
    std::regex framePattern(R"(^(?:frame_)?(\d{1,5})\.svg$)", std::regex::icase);
    std::set<int> frameNumbers;

    try {
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            std::smatch match;
            if (std::regex_match(filename, match, framePattern)) {
                frameNumbers.insert(std::stoi(match[1].str()));
            }
            // Early exit if we found enough frames (optimization)
            if (frameNumbers.size() >= 2) {
                // Verify they are sequential (at least 2 consecutive numbers)
                auto it = frameNumbers.begin();
                int prev = *it++;
                while (it != frameNumbers.end()) {
                    if (*it == prev + 1) return true;  // Found consecutive frames
                    prev = *it++;
                }
            }
        }
    } catch (...) {
        // If folder can't be scanned, treat as regular folder
    }
    return false;
}
```

**Frame Naming Patterns Supported**:
- `frame_001.svg`, `frame_002.svg`, ...
- `001.svg`, `002.svg`, ...
- Case-insensitive (regex has `std::regex::icase`)
- Supports 1-5 digit frame numbers

**Detection Criteria**:
- Must have at least **2 consecutive numbered frames**
- Example: `frame_001.svg` + `frame_002.svg` → ✓ FrameFolder
- Example: `frame_001.svg` + `frame_003.svg` → ✗ Regular Folder (gap in sequence)

**Performance**: Early exit after finding 2 consecutive frames (no need to scan entire folder).

**Edge Cases**:
- ✓ Non-consecutive frames are rejected
- ✓ Single frame folders are treated as regular folders
- ✓ Scan errors are caught (returns `false`)

---

## 5. scanDirectory() Full Implementation

### Location: lines 814-983

### Phase 1: Special Root Handling (lines 827-890)

```cpp
bool atRoot = (current == current.root_path());

if (atRoot) {
    // Platform-specific volume enumeration
    #ifdef __APPLE__
        // macOS: Show /Volumes contents as mount points
        fs::path volumesPath("/Volumes");
        // ... scan /Volumes ...
        // Also show: /Users, /Applications, /Library, /System
    #else
        // Linux: Show /mnt, /media, /home, /tmp
    #endif
    return;
}
```

**Purpose**: At filesystem root, show volumes and system directories instead of raw root contents.

### Phase 2: Parent Directory Entry (lines 893-903)

```cpp
fs::path parent = current.parent_path();
if (!parent.empty() && parent != current) {
    BrowserEntry parentEntry;
    parentEntry.type = BrowserEntryType::ParentDir;
    parentEntry.name = "..";
    parentEntry.fullPath = parent.string();
    parentEntry.gridIndex = 0;
    parentEntry.modifiedTime = 0;
    allEntries_.push_back(parentEntry);
}
```

**Purpose**: Always show ".." for navigation (except at root).

### Phase 3: Directory Iteration (lines 905-978)

```cpp
std::vector<BrowserEntry> folders;
std::vector<BrowserEntry> svgFiles;

try {
    for (const auto& entry : fs::directory_iterator(currentDir_)) {
        std::string name = entry.path().filename().string();

        // Skip hidden files/folders
        if (name.empty() || name[0] == '.') continue;

        // Get last modified time
        std::time_t modTime = /* ... */;

        if (entry.is_directory()) {
            // Folder/FrameFolder detection (see section 4)
            folders.push_back(folderEntry);
        } else if (entry.is_regular_file()) {
            // SVG file detection (see section 2)
            if (ext == ".svg") {
                svgFiles.push_back(fileEntry);
            }
        }
    }
} catch (const std::exception& e) {
    fprintf(stderr, "FolderBrowser: Failed to scan directory %s: %s\n", 
            currentPath.c_str(), e.what());
}
```

**Key Behaviors**:
- ✓ Hidden files (`.name`) are **skipped**
- ✓ Folders and files are collected **separately**
- ✓ Modified time extraction is **exception-safe**
- ✓ Scan errors are caught and logged (non-fatal)

### Phase 4: Combination & Sorting (lines 970-982)

```cpp
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
```

**Order**:
1. ParentDir (if present)
2. Folders (including FrameFolders)
3. SVG files (both static and animated)
4. Then sorted according to `config_.sortMode` and `config_.sortDirection`

---

## 6. Selection & Loading (lines 809-812)

```cpp
bool FolderBrowser::canLoad() const {
    std::optional<BrowserEntry> selected = getSelectedEntry();
    if (!selected.has_value()) return false;
    // Allow loading: SVGFile (static), FBFSVGFile (animated), FrameFolder (image sequence)
    return selected->type == BrowserEntryType::SVGFile ||
           selected->type == BrowserEntryType::FBFSVGFile ||
           selected->type == BrowserEntryType::FrameFolder;
}
```

**All 3 types are loadable**:
- `SVGFile` → Static SVG rendering
- `FBFSVGFile` → Animated FBF.SVG playback
- `FrameFolder` → Frame sequence playback

---

## 7. Sorting Behavior (lines 985-1013)

```cpp
void FolderBrowser::sortEntries() {
    // Keep parent entry at the beginning if present
    std::vector<BrowserEntry> toSort;
    std::vector<BrowserEntry> fixed;  // ParentDir and Volume entries stay at top

    auto isFolderType = [](BrowserEntryType t) {
        return t == BrowserEntryType::Folder || t == BrowserEntryType::FrameFolder;
    };

    for (auto& entry : allEntries_) {
        if (entry.type == BrowserEntryType::ParentDir || 
            entry.type == BrowserEntryType::Volume) {
            fixed.push_back(entry);
        } else {
            toSort.push_back(entry);
        }
    }

    // Sort folders and files - respect direction (ascending/descending)
    bool ascending = (config_.sortDirection == BrowserSortDirection::Ascending);

    if (config_.sortMode == BrowserSortMode::Alphabetical) {
        std::sort(toSort.begin(), toSort.end(), 
            [ascending, isFolderType](const BrowserEntry& a, const BrowserEntry& b) {
                // Folders (including FrameFolders) before files
                bool aIsFolder = isFolderType(a.type);
                bool bIsFolder = isFolderType(b.type);
                if (aIsFolder && !bIsFolder) return true;
                if (!aIsFolder && bIsFolder) return false;
                // Then alphabetical within each group
                // ...
            });
    }
    // ... ModifiedTime sorting ...
}
```

**Sorting Rules**:
1. **Fixed at top**: ParentDir, Volume (never sorted)
2. **Folders first**: Folder + FrameFolder grouped together
3. **Files second**: SVGFile + FBFSVGFile grouped together
4. **Within groups**: Alphabetical or ModifiedTime (user-configurable)

**Key Insight**: FrameFolder is treated as a folder for sorting purposes (not a file).

---

## 8. Key Architectural Decisions

### File Type Hierarchy

```
Playable Types (can be loaded):
├─ SVGFile       (static SVG)
├─ FBFSVGFile    (animated SVG)
└─ FrameFolder   (frame sequence)

Non-Playable Types:
├─ Folder        (regular directory)
├─ Volume        (mount point)
└─ ParentDir     (".." navigation)
```

### Detection Performance

| Type | Cost | Optimization |
|------|------|--------------|
| SVGFile | O(1) | Extension check only |
| FBFSVGFile | O(1)* | Read first 64KB + regex |
| FrameFolder | O(n) | Early exit after 2 consecutive frames |

*Constant relative to file size (not full file read)

### Error Handling

All detection functions are **exception-safe**:
- File read errors → treated as static SVG
- Folder scan errors → treated as regular folder
- Regex match errors → caught and logged

---

## 9. Potential Issues & Limitations

### False Negatives (Type Misdetection)

| Scenario | Detected As | Should Be |
|----------|-------------|-----------|
| FBF.SVG with frame IDs after 64KB | SVGFile | FBFSVGFile |
| Frame folder with non-consecutive frames | Folder | FrameFolder (?) |
| SVG with `<animate>` but no frame IDs | SVGFile | Correct |

### False Positives

| Scenario | Detected As | Should Be |
|----------|-------------|-----------|
| SVG with random `id="frame_123"` | FBFSVGFile | SVGFile (?) |
| Folder with `001.svg` + `002.svg` (unrelated) | FrameFolder | Folder (?) |

### Performance Concerns

- **64KB limit**: Large SVGs with late frame declarations are missed
- **Regex on every file**: Can be slow for folders with 1000+ SVGs
- **Frame folder scan**: O(n) per folder (can't be optimized further)

---

## 10. Recommendations

### Short-Term Improvements

1. **Cache detection results** - Avoid re-scanning files on sort/filter
2. **Parallel detection** - Use thread pool for isFBFSVGFile() calls
3. **Incremental scanning** - Scan visible page first, background load rest

### Long-Term Improvements

1. **Metadata sidecar files** - Store detection results (`.svg.meta`)
2. **Database index** - SQLite cache for large folders
3. **Watch for file changes** - Invalidate cache on modification

### Testing Gaps

- No tests for 64KB boundary cases
- No tests for non-consecutive frame folders
- No tests for case-insensitive extension matching
- No tests for hidden file filtering

---

## File Structure Reference

```
src/folder_browser.cpp (2105 lines)
├─ Line 22-29:   BrowserEntryType enum (header)
├─ Line 51-67:   escapeXml() helper
├─ Line 79-109:  isFrameSequenceFolder() detection
├─ Line 114-142: isFBFSVGFile() detection
├─ Line 814-983: scanDirectory() main logic
│  ├─ 827-890:  Root level handling
│  ├─ 893-903:  Parent directory entry
│  ├─ 905-978:  Directory iteration & filtering
│  └─ 970-982:  Combination & sorting
├─ Line 985-1013: sortEntries() logic
└─ Line 809-812: canLoad() selection check
```

---

## Conclusion

The folder browser implements a **3-tier detection system** with:
- ✓ Fast extension filtering (case-insensitive)
- ✓ Content-based FBF.SVG detection (64KB peek)
- ✓ Frame sequence folder detection (consecutive numbering)
- ✓ Exception-safe error handling
- ✓ Performance optimizations (early exit, partial reads)

**Critical Paths**:
1. Extension check → **immediate rejection** of non-SVG files
2. SMIL check → **early rejection** if no animation elements
3. Frame check → **early exit** after finding 2 consecutive frames

**Design Philosophy**: Fail fast, scan incrementally, cache nothing (relies on OS filesystem cache).
