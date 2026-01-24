# Debug Overlay Analysis - Linux SVG Player

Generated: 2026-01-22

## Summary

The Linux SVG player implements a comprehensive debug overlay system using a structured line-based approach with typed entries for different visual styles. The overlay displays detailed performance metrics, animation state, and system information.

## DebugLine Structure

**Location:** `src/svg_player_animated_linux.cpp:3042-3047`

```cpp
struct DebugLine {
    int type;           // Visual style: 0-6
    std::string label;  // Left-side text (e.g., "FPS (avg):")
    std::string value;  // Right-side text (e.g., "60.0")
    std::string key;    // For key lines only (e.g., "[SPACE]")
};
```

### Line Types

| Type | Name | Visual Style | Purpose |
|------|------|--------------|---------|
| 0 | Normal | White text | Standard metrics |
| 1 | Highlight | Orange text | Important metrics (FPS, warnings) |
| 2 | Animation | Purple text | Animation-specific info |
| 3 | Key | Key in brackets | Keyboard shortcuts |
| 4 | Small gap | — | Vertical spacing (6px) |
| 5 | Large gap | — | Vertical spacing (11px) |
| 6 | Single | White text | Full-width text line |

## Line Building System (PASS 1)

**Location:** `src/svg_player_animated_linux.cpp:3052-3066`

The overlay uses lambda helper functions to build lines:

```cpp
auto addLine = [&](const std::string& label, const std::string& value) {
    lines.push_back({0, label, value, ""});
};

auto addHighlight = [&](const std::string& label, const std::string& value) {
    lines.push_back({1, label, value, ""});
};

auto addAnim = [&](const std::string& label, const std::string& value) {
    lines.push_back({2, label, value, ""});
};

auto addKey = [&](const std::string& key, const std::string& label, const std::string& value) {
    lines.push_back({3, label, value, key});
};

auto addSmallGap = [&]() { lines.push_back({4, "", "", ""}); };
auto addLargeGap = [&]() { lines.push_back({5, "", "", ""}); };
auto addSingle = [&](const std::string& text) { lines.push_back({6, text, "", ""}); };
```

## Frame Information Displayed

**Location:** `src/svg_player_animated_linux.cpp:3068-3250`

### Performance Metrics (Highlighted)

- **FPS (avg):** Rolling average FPS [HIGHLIGHT]
- **FPS (instant):** Current frame FPS
- **Skia FPS:** Frame delivery rate with ready percentage [HIGHLIGHT]
- **Frame time:** Average frame duration

### Pipeline Timing Breakdown

Single-line header: `--- Pipeline ---`

- **Event:** Event processing time + % of total
- **Anim:** Animation update time + % of total
- **Fetch:** Frame fetch time + % of total
- **Wait Skia:** Idle time waiting for Skia worker [HIGHLIGHT] + % idle
- **Overlay:** Overlay rendering time + % of total
- **Copy:** Texture copy time + % of total
- **Present:** Presentation time + % of total
- **Skia work:** Async render time (min/max shown)
- **Active work:** Sum of all active phases

### Display Information

- **Resolution:** Render buffer size (e.g., "1920 x 1080")
- **SVG size:** Original SVG dimensions
- **Scale:** Scaling factor applied
- **Frames:** Total frame count in animation

### Animation Information (if present)

- **Anim mode:** Mode string (e.g., "loop") [ANIM style]
- **Anim duration:** Duration in seconds [ANIM style]
- **Frames shown:** Total frames rendered
- **Frames skipped:** Dropped frames [HIGHLIGHT if > 0]
- **Skip rate:** Percentage of frames dropped [HIGHLIGHT if > 10%]
- **Anim target:** Target FPS of animation

### Controls & Status

- **[V] VSync:** ON/OFF [KEY style]
- **[F] Limiter:** ON (60 FPS) / OFF [KEY style]
- **[P] Mode:** PreBuffer / Off [KEY style]
- **Threads:** Active threads / total threads
- **CPU usage:** Process CPU percentage
- **[SPACE] Animation:** PAUSED / PLAYING [KEY style]
- **[S] Stress test:** ON (50ms delay) / OFF [KEY style]

### Footer

Single line: `[R] Reset stats  [D] Toggle overlay  [G] Fullscreen`

## Rendering System (PASS 2)

**Location:** `src/svg_player_animated_linux.cpp:3289-3323`

### Width Measurement Phase

The system measures the maximum width needed by iterating all lines (3256-3274):
- Measures label + value width for normal lines
- Measures key + label + value for key lines
- Measures single text for single lines
- Skips gaps (types 4, 5)

### Height Calculation Phase

Calculates total box height by iterating all lines (3279-3287):
- Adds `lineHeight` for text lines (types 0, 1, 2, 3, 6)
- Adds 6px for small gaps (type 4)
- Adds 11px for large gaps (type 5)

### Drawing Phase

Draws background box, then renders each line (3295-3323):

**Type 4/5 (Gaps):** Add vertical spacing
**Type 6 (Single):** Draw full-width text in white
**Type 3 (Key):** Draw `[KEY]` in cyan, label in white, value in orange
**Type 0/1/2 (Normal/Highlight/Anim):** Draw label in white, value in type-specific color:
  - Type 0: White
  - Type 1: Orange (highlight)
  - Type 2: Purple (animation)

## Renderer Type Display

**Status:** NOT CURRENTLY DISPLAYED ✗ MISSING

### Available Backend Information

**Location:** `src/svg_player_animated_linux.cpp:1443, 1774-1783`

The player has a `useGraphiteBackend` flag that determines rendering path:

```cpp
bool useGraphiteBackend = false;  // Set by --graphite flag

if (useGraphiteBackend) {
    graphiteContext = svgplayer::createGraphiteContext(window);
    if (graphiteContext && graphiteContext->isInitialized()) {
        std::cout << "[Graphite] Next-gen GPU backend enabled - "
                  << graphiteContext->getBackendName() << " rendering active" << std::endl;
    } else {
        std::cerr << "[Graphite] Failed to initialize Graphite context (Vulkan), "
                  << "falling back to CPU raster" << std::endl;
        useGraphiteBackend = false;
    }
}
```

### Backend States

| State | Backend | API |
|-------|---------|-----|
| `useGraphiteBackend=true` && initialized | Graphite | Vulkan |
| `useGraphiteBackend=false` OR failed init | CPU Raster | Software |

**Note:** OpenGL/EGL backend was mentioned in usage help text but not found in actual rendering code. Linux appears to use either Graphite (Vulkan) or CPU raster.

### Recommendation

Add backend information to overlay after "Mode:" line:

```cpp
// After addKey("[P]", "Mode:", parallelStatus);

std::string backendName = "CPU Raster";
if (useGraphiteBackend && graphiteContext && graphiteContext->isInitialized()) {
    backendName = graphiteContext->getBackendName();  // Returns "Vulkan" or similar
}
addLine("Backend:", backendName);
```

## Paint Colors

**Location:** `src/svg_player_animated_linux.cpp:3313-3320`

| Paint | Color | Usage |
|-------|-------|-------|
| `bgPaint` | Semi-transparent black | Background box |
| `textPaint` | White | Normal labels and values |
| `highlightPaint` | Orange | Highlighted values (FPS, warnings) |
| `keyPaint` | Cyan | Keyboard shortcut keys `[X]` |
| `animPaint` | Purple | Animation-specific values |

## Open Questions

1. **OpenGL/EGL Backend:** Usage help mentions "OpenGL/EGL rendering" but code only shows Graphite (Vulkan) or CPU paths. Was OpenGL removed?

2. **Parallel Rendering Mode:** The overlay shows "PreBuffer" or "Off" for parallel mode, but the relationship to backend choice is unclear. Can Graphite use PreBuffer mode?

3. **Metal Backend:** Mentioned in project docs for macOS/iOS. Linux equivalent is Graphite (Vulkan)?
