# Main Function Refactoring Plan

## Overview

The `main()` function in `svg_player_animated.cpp` spans **lines 1370-3395** (2025 lines of code). This document outlines a comprehensive refactoring plan to extract logical sections into separate, testable functions.

**Total Line Count**: 2025 lines
**Target**: Break into ~13 focused functions
**Estimated Complexity Reduction**: From O(2000) to O(150) average per function

---

## Refactoring Strategy

### Phase 1: Extract Pure Functions (No Side Effects)
1. `parseCommandLine()`
2. `printFinalStatistics()`

### Phase 2: Extract Setup Functions
3. `loadInitialSVG()`
4. `setupSkiaDom()`
5. `initializeSDL()`
6. `setupRenderers()`
7. `setupPerformanceTracking()`
8. `setupDebugFont()`

### Phase 3: Extract Core Loop Components
9. `handleEvents()`
10. `updateAnimations()`
11. `fetchAndRenderFrame()`
12. `drawDebugOverlay()`
13. `presentFrame()`

### Phase 4: Extract Cleanup
14. `cleanup()`

---

## Function Extraction Details

### 1. parseCommandLine()

**Lines**: 1377-1406
**Estimated Complexity**: Low (30 lines)

```cpp
struct CommandLineArgs {
    const char* inputPath;
    bool startFullscreen;
    bool showHelp;
    bool showVersion;
};

CommandLineArgs parseCommandLine(int argc, char* argv[]);
```

**Parameters**:
- `int argc` - argument count
- `char* argv[]` - argument values

**Return Value**:
- `CommandLineArgs` struct containing parsed options

**Dependencies**:
- None (pure function, only reads arguments)
- Calls `printHelp()` for `--help` flag

**Side Effects**:
- May print help/version and exit (lines 1383-1391)
- Prints error messages to stderr

**Notes**:
- Currently exits program on `--version` and `--help`
- Should return status code instead for better testability

---

### 2. loadInitialSVG()

**Lines**: 1408-1468
**Estimated Complexity**: Medium (60 lines)

```cpp
struct SVGLoadResult {
    std::string rawSvgContent;
    std::vector<SMILAnimation> animations;
    std::map<size_t, std::string> syntheticIds;
    SVGLoadError error;
};

SVGLoadResult loadInitialSVG(const char* inputPath);
```

**Parameters**:
- `const char* inputPath` - path to SVG file

**Return Value**:
- `SVGLoadResult` struct with loaded content and animations

**Dependencies**:
- `fileExists()` - file validation
- `getFileSize()` - size validation
- `validateSVGContent()` - content validation
- `preprocessSVGForAnimation()` - ID injection
- `extractAnimationsFromContent()` - SMIL parsing
- `initializeFontSupport()` - font initialization (side effect)

**Side Effects**:
- Initializes font support (line 1416)
- Prints status messages to stdout/stderr
- Reads file from disk

**Global Variables**: None

**Notes**:
- Currently returns early on errors - should collect all errors
- Font initialization is a side effect that should be moved

---

### 3. setupSkiaDom()

**Lines**: 1470-1523
**Estimated Complexity**: Medium (53 lines)

```cpp
struct SkiaDomResult {
    sk_sp<SkSVGDOM> svgDom;
    int svgWidth;
    int svgHeight;
    float aspectRatio;
    bool success;
};

SkiaDomResult setupSkiaDom(const std::string& processedContent,
                           const std::vector<SMILAnimation>& animations);
```

**Parameters**:
- `const std::string& processedContent` - preprocessed SVG content
- `const std::vector<SMILAnimation>& animations` - parsed animations

**Return Value**:
- `SkiaDomResult` struct with parsed DOM and dimensions

**Dependencies**:
- `SkData::MakeWithCopy()` - Skia data creation
- `SkMemoryStream::Make()` - Skia stream creation
- `makeSVGDOMWithFontSupport()` - DOM parsing

**Side Effects**:
- Prints status messages (animation target verification)
- None on Skia objects (read-only after creation)

**Global Variables**: None

**Notes**:
- Validates viewBox vs intrinsicSize dimensions
- Verifies all animation targets exist in DOM

---

### 4. initializeSDL()

**Lines**: 1524-1609
**Estimated Complexity**: Medium (85 lines)

```cpp
struct SDLContext {
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool isFullscreen;
    bool vsyncEnabled;
    int renderWidth;
    int renderHeight;
    float hiDpiScale;
    int displayRefreshRate;
};

SDLContext initializeSDL(int svgWidth, int svgHeight, float aspectRatio,
                         bool startFullscreen);
```

**Parameters**:
- `int svgWidth, int svgHeight` - SVG dimensions
- `float aspectRatio` - SVG aspect ratio
- `bool startFullscreen` - initial fullscreen state

**Return Value**:
- `SDLContext` struct with initialized SDL objects

**Dependencies**:
- `SDL_Init()` - SDL initialization
- `SDL_SetHint()` - SDL configuration
- `SDL_CreateWindow()` - window creation
- `SDL_CreateRenderer()` - renderer creation
- `SDL_GetRendererOutputSize()` - HiDPI detection
- `SDL_GetCurrentDisplayMode()` - refresh rate detection

**Side Effects**:
- Initializes SDL subsystems
- Creates window and renderer
- Sets SDL hints globally

**Global Variables**: None

**Notes**:
- Calculates window size with MIN_WINDOW_SIZE and max 1200px constraints
- Detects HiDPI scaling factor
- Configures Metal backend on macOS

---

### 5. setupDebugFont()

**Lines**: 1630-1673
**Estimated Complexity**: Low (43 lines)

```cpp
struct DebugFontContext {
    SkFont debugFont;
    SkPaint bgPaint;
    SkPaint textPaint;
    SkPaint highlightPaint;
    SkPaint animPaint;
    SkPaint keyPaint;
};

DebugFontContext setupDebugFont(float hiDpiScale);
```

**Parameters**:
- `float hiDpiScale` - HiDPI scaling factor

**Return Value**:
- `DebugFontContext` struct with configured fonts and paints

**Dependencies**:
- `createPlatformFontMgr()` - platform font manager
- Skia SkFont and SkPaint APIs

**Side Effects**: None

**Global Variables**: None

**Notes**:
- Tries "Menlo", then "Courier", then fallbacks
- Scales font size by HiDPI factor
- Creates 5 different paint styles for overlay

---

### 6. setupPerformanceTracking()

**Lines**: 1674-1717
**Estimated Complexity**: Low (43 lines)

```cpp
struct PerformanceTrackers {
    RollingAverage eventTimes;
    RollingAverage animTimes;
    RollingAverage fetchTimes;
    RollingAverage overlayTimes;
    RollingAverage copyTimes;
    RollingAverage presentTimes;
    RollingAverage frameTimes;
    RollingAverage renderTimes;
    RollingAverage idleTimes;
    uint64_t displayCycles;
    uint64_t framesDelivered;
    uint64_t frameCount;
    Clock::time_point startTime;
    Clock::time_point lastFrameTime;
    Clock::time_point animationStartTime;
    SteadyClock::time_point animationStartTimeSteady;
};

PerformanceTrackers setupPerformanceTracking();
```

**Parameters**: None

**Return Value**:
- `PerformanceTrackers` struct with initialized trackers

**Dependencies**:
- `RollingAverage` class (window size 30)
- `Clock` and `SteadyClock` types

**Side Effects**: None

**Global Variables**: None

**Notes**:
- All RollingAverage instances use 30-frame window
- Initializes both Clock and SteadyClock start times
- Sets up frame counters

---

### 7. setupRenderers()

**Lines**: 1750-1826
**Estimated Complexity**: High (76 lines)

```cpp
struct RendererContext {
    SkiaParallelRenderer parallelRenderer;
    ThreadedRenderer threadedRenderer;
    size_t preBufferTotalFrames;
    double preBufferTotalDuration;
    int totalCores;
    int availableCores;
};

RendererContext setupRenderers(const std::string& rawSvgContent,
                                int renderWidth, int renderHeight,
                                int svgWidth, int svgHeight,
                                const std::vector<SMILAnimation>& animations);
```

**Parameters**:
- `const std::string& rawSvgContent` - SVG content string
- `int renderWidth, renderHeight` - render dimensions
- `int svgWidth, svgHeight` - SVG dimensions
- `const std::vector<SMILAnimation>& animations` - parsed animations

**Return Value**:
- `RendererContext` struct with configured renderers

**Dependencies**:
- `SkiaParallelRenderer` class
- `ThreadedRenderer` class
- Animation timing calculations (maxFrames, maxDuration)

**Side Effects**:
- Starts parallel renderer in PreBuffer mode
- Starts threaded renderer
- Prints controls and configuration to stdout

**Global Variables**: None

**Notes**:
- Calculates maxFrames and maxDuration from animations
- Prints extensive controls help text
- Initializes cached mode state

---

### 8. handleEvents()

**Lines**: 1856-2622
**Estimated Complexity**: **VERY HIGH** (766 lines - largest section!)

```cpp
struct EventHandlerContext {
    bool running;
    bool skipStatsThisFrame;
    // All mutable state needed by event handlers
};

EventHandlerContext handleEvents(
    SDL_Event& event,
    EventHandlerContext& ctx,
    AnimationState& animState,
    RendererContext& renderers,
    SDLContext& sdlContext,
    PerformanceTrackers& perf
);
```

**Parameters**:
- `SDL_Event& event` - SDL event to handle
- Multiple context structs by reference (mutable state)

**Return Value**:
- Updated `EventHandlerContext` with running flag and stats skip flag

**Dependencies**:
- Massive - handles 9 different event types:
  - SDL_QUIT
  - SDL_KEYDOWN (17+ different keys!)
  - SDL_MOUSEMOTION
  - SDL_MOUSEBUTTONDOWN
  - SDL_WINDOWEVENT
- Browser mode interactions
- File loading dialog
- Screenshot capture
- VSync toggling (recreates renderer)

**Side Effects**:
- Modifies animation state
- Changes renderer modes
- Loads new SVG files
- Toggles fullscreen
- Resizes window
- Browser navigation

**Global Variables**:
- `g_shutdownRequested` (read)
- `g_browserMode` (read/write)
- `g_browserSvgDom` (read/write)
- `g_folderBrowser` (read/write)
- Many browser-related globals

**Notes**:
- **THIS IS THE BIGGEST SECTION - NEEDS SUB-REFACTORING**
- Should be split into multiple handlers:
  - `handleQuitEvent()`
  - `handleKeyDown()` → further split by key
  - `handleMouseMotion()`
  - `handleMouseButtonDown()`
  - `handleWindowEvent()`
- Browser click handling alone is 200+ lines (2255-2574)

**Sub-extraction candidates**:
- `handleLoadSVGFile()` (lines 2075-2143, 2354-2419, 2491-2560) - duplicated 3 times!
- `handleBrowserNavigation()` (lines 2255-2574)
- `handleWindowResize()` (lines 2576-2620)
- `handleVSyncToggle()` (lines 1928-1971)
- `handleModeToggle()` (lines 1994-2013)
- `handleFullscreenToggle()` (lines 2014-2037)

---

### 9. updateAnimations()

**Lines**: 2628-2681
**Estimated Complexity**: Medium (53 lines)

```cpp
struct AnimationState {
    bool animationPaused;
    double pausedTime;
    size_t lastFrameIndex;
    size_t currentFrameIndex;
    std::string lastFrameValue;
    size_t framesRendered;
    size_t framesSkipped;
    size_t lastRenderedAnimFrame;
    SteadyClock::time_point animationStartTimeSteady;
};

void updateAnimations(
    const std::vector<SMILAnimation>& animations,
    AnimationState& state,
    ThreadedRenderer& threadedRenderer,
    double preBufferTotalDuration,
    size_t preBufferTotalFrames
);
```

**Parameters**:
- `const std::vector<SMILAnimation>& animations` - animation definitions
- `AnimationState& state` - mutable animation state
- `ThreadedRenderer& threadedRenderer` - renderer to update
- Timing parameters for frame calculation

**Return Value**: None (modifies state)

**Dependencies**:
- `SMILAnimation::getCurrentValue()` - value interpolation
- `SMILAnimation::getCurrentFrameIndex()` - frame calculation
- `ThreadedRenderer::setAnimationState()` - state propagation

**Side Effects**:
- Updates threaded renderer animation state
- Modifies frame skip tracking

**Global Variables**: None

**Notes**:
- Time-based frame calculation (SMIL compliant)
- Handles both PreBuffer and Direct modes
- Tracks frame skips for sync verification

---

### 10. fetchAndRenderFrame()

**Lines**: 2690-2868
**Estimated Complexity**: High (178 lines)

```cpp
struct RenderResult {
    bool gotNewFrame;
    DurationMs fetchTime;
};

RenderResult fetchAndRenderFrame(
    sk_sp<SkSurface> surface,
    ThreadedRenderer& threadedRenderer,
    size_t currentFrameIndex,
    bool browserMode,
    bool animationPaused,
    int renderWidth,
    int renderHeight
);
```

**Parameters**:
- `sk_sp<SkSurface> surface` - Skia drawing surface
- `ThreadedRenderer& threadedRenderer` - frame source
- `size_t currentFrameIndex` - requested animation frame
- Browser and animation state flags
- Render dimensions

**Return Value**:
- `RenderResult` with frame availability and timing

**Dependencies**:
- Browser mode: `g_folderBrowser` and browser DOM
- Animation mode: `ThreadedRenderer::getFrontBufferIfReady()`
- Browser async operations

**Side Effects**:
- Renders to Skia canvas
- Updates browser DOM if dirty
- Handles browser scan completion

**Global Variables**:
- `g_browserMode` (read)
- `g_browserSvgDom` (read/write)
- `g_folderBrowser` (read/write)
- `g_browserAnimations` (read with mutex)
- `g_browserAsyncScanning` (read/write)
- Many browser-related globals

**Notes**:
- Dual mode: browser rendering vs animation rendering
- Browser mode includes async scan handling
- Browser mode includes live animation updates
- Progress bar rendering for loading state

---

### 11. drawDebugOverlay()

**Lines**: 2883-3191
**Estimated Complexity**: High (308 lines)

```cpp
void drawDebugOverlay(
    SkCanvas* canvas,
    const DebugFontContext& fontCtx,
    const PerformanceTrackers& perf,
    const AnimationState& animState,
    const std::vector<SMILAnimation>& animations,
    bool vsyncEnabled,
    bool frameLimiterEnabled,
    bool stressTestEnabled,
    int displayRefreshRate,
    int renderWidth,
    int renderHeight,
    int svgWidth,
    int svgHeight,
    float hiDpiScale,
    ThreadedRenderer& threadedRenderer
);
```

**Parameters**:
- `SkCanvas* canvas` - drawing surface
- `const DebugFontContext& fontCtx` - fonts and paints
- Many const references to display state
- Renderer reference for live stats

**Return Value**: None

**Dependencies**:
- Skia drawing APIs
- `getProcessCPUStats()` - CPU usage
- `SkFont::measureText()` - text measurement

**Side Effects**:
- Draws on canvas (modifies visual state only)

**Global Variables**: None

**Notes**:
- Two-pass rendering: measure, then draw
- Complex layout with 7 line types
- Dynamic box sizing based on content
- Heavy use of lambdas for line builders

---

### 12. presentFrame()

**Lines**: 3195-3317
**Estimated Complexity**: Medium (122 lines)

```cpp
struct PresentResult {
    DurationMs copyTime;
    DurationMs presentTime;
    DurationMs totalFrameTime;
    bool presented;
};

PresentResult presentFrame(
    bool gotNewFrame,
    sk_sp<SkSurface> surface,
    SDL_Texture* texture,
    SDL_Renderer* renderer,
    int renderWidth,
    int renderHeight,
    bool frameLimiterEnabled,
    bool vsyncEnabled,
    bool stressTestEnabled,
    int displayRefreshRate,
    const Clock::time_point& frameStart
);
```

**Parameters**:
- Frame availability flag
- SDL and Skia objects
- Render dimensions
- Limiter/sync flags
- Frame start time for duration calculation

**Return Value**:
- `PresentResult` with timing measurements

**Dependencies**:
- SDL rendering APIs
- Skia pixmap access
- `SDL_Delay()` for frame limiting

**Side Effects**:
- Copies pixels to texture
- Presents to screen
- May sleep for frame limiting

**Global Variables**: None

**Notes**:
- Only presents when new frame available
- Optimizes memcpy when pitch matches
- Implements soft frame limiter
- Detects and logs stutters

---

### 13. printFinalStatistics()

**Lines**: 3320-3358
**Estimated Complexity**: Low (38 lines)

```cpp
void printFinalStatistics(
    const PerformanceTrackers& perf,
    const Clock::time_point& startTime
);
```

**Parameters**:
- `const PerformanceTrackers& perf` - all performance data
- `const Clock::time_point& startTime` - session start time

**Return Value**: None

**Dependencies**: None (pure output function)

**Side Effects**:
- Prints to stdout

**Global Variables**: None

**Notes**:
- Calculates derived statistics
- Uses lambda for percentage calculation
- Mirrors overlay display format

---

### 14. cleanup()

**Lines**: 3359-3394
**Estimated Complexity**: Low (35 lines)

```cpp
void cleanup(
    ThreadedRenderer& threadedRenderer,
    SkiaParallelRenderer& parallelRenderer,
    SDL_Texture* texture,
    SDL_Renderer* renderer,
    SDL_Window* window
);
```

**Parameters**:
- All objects to clean up (by reference)

**Return Value**: None

**Dependencies**:
- Renderer stop methods
- SDL cleanup functions

**Side Effects**:
- Stops background threads
- Deallocates SDL resources
- Calls SDL_Quit()

**Global Variables**:
- `g_folderBrowser` (cleanup methods)

**Notes**:
- Order matters: DOM parse → scan → thumbnail → renderers
- Prints progress messages
- Ensures threads stop before static destruction

---

## Dependency Graph

```
main()
├── parseCommandLine()
├── loadInitialSVG()
│   └── initializeFontSupport() [side effect - should move]
├── setupSkiaDom()
├── initializeSDL()
├── setupDebugFont()
├── setupPerformanceTracking()
├── setupRenderers()
│   ├── SkiaParallelRenderer
│   └── ThreadedRenderer
├── EVENT LOOP
│   ├── handleEvents()
│   │   ├── handleQuitEvent() [to extract]
│   │   ├── handleKeyDown() [to extract]
│   │   │   ├── handleLoadSVGFile() [duplicated 3x!]
│   │   │   ├── handleVSyncToggle() [to extract]
│   │   │   ├── handleModeToggle() [to extract]
│   │   │   └── handleFullscreenToggle() [to extract]
│   │   ├── handleMouseMotion() [to extract]
│   │   ├── handleMouseButtonDown() [to extract]
│   │   │   └── handleBrowserNavigation() [to extract]
│   │   └── handleWindowEvent() [to extract]
│   │       └── handleWindowResize() [to extract]
│   ├── updateAnimations()
│   ├── fetchAndRenderFrame()
│   ├── drawDebugOverlay()
│   └── presentFrame()
├── printFinalStatistics()
└── cleanup()
```

---

## Implementation Plan

### Step 1: Create Struct Definitions
- Define all result/context structs
- Place in header file or anonymous namespace

### Step 2: Extract Pure Functions First
- `parseCommandLine()`
- `printFinalStatistics()`
- Test these standalone

### Step 3: Extract Setup Functions
- `loadInitialSVG()`
- `setupSkiaDom()`
- `initializeSDL()`
- `setupDebugFont()`
- `setupPerformanceTracking()`
- `setupRenderers()`
- Test initialization sequence

### Step 4: Extract Core Loop (Hardest Part)
- `updateAnimations()`
- `fetchAndRenderFrame()`
- `drawDebugOverlay()`
- `presentFrame()`
- Test with synthetic inputs

### Step 5: Extract Event Handling (Most Complex)
- Start with `handleWindowEvent()` (simplest)
- Then `handleMouseMotion()`, `handleMouseButtonDown()`
- Finally `handleKeyDown()` with sub-handlers
- Extract `handleLoadSVGFile()` to eliminate duplication
- Test each handler independently

### Step 6: Extract Cleanup
- `cleanup()`
- Test teardown sequence

### Step 7: Integration Testing
- Run refactored main() and verify behavior unchanged
- Compare performance before/after
- Verify all global state properly passed

---

## Benefits

### Testability
- Each function can be unit tested
- Synthetic inputs for event handlers
- Mocked dependencies

### Readability
- main() becomes ~150 lines of high-level flow
- Each function has clear purpose
- Easier to understand program structure

### Maintainability
- Changes localized to relevant functions
- Easier to find and fix bugs
- Simpler code review

### Reusability
- Setup functions can be reused in tests
- Event handlers can be shared with other players
- Statistics printing can be used elsewhere

---

## Risks and Mitigation

### Risk 1: Global State Mutations
**Mitigation**: Pass all state explicitly via structs, audit global variable access

### Risk 2: Performance Regression
**Mitigation**: Keep inline where needed, profile before/after

### Risk 3: Breaking Changes
**Mitigation**: Extensive testing, feature parity verification

### Risk 4: Complex Dependencies
**Mitigation**: Start with leaf functions (no dependencies), work upward

---

## Estimated Effort

| Phase | Functions | Lines | Effort (hours) |
|-------|-----------|-------|----------------|
| Struct definitions | - | ~200 | 2 |
| Pure functions | 2 | ~70 | 1 |
| Setup functions | 6 | ~400 | 6 |
| Core loop | 4 | ~660 | 10 |
| Event handling | 1 + subs | ~766 | 16 |
| Cleanup | 1 | ~35 | 1 |
| Testing | - | - | 8 |
| **TOTAL** | **14+** | **~2025** | **44 hours** |

---

## Success Metrics

- [ ] main() reduced to <200 lines
- [ ] No function exceeds 150 lines
- [ ] All functions have single responsibility
- [ ] 90%+ unit test coverage of extracted functions
- [ ] No performance regression (within 5% of baseline)
- [ ] All features work identically to current implementation

---

## Code Duplication to Eliminate

### 1. Load SVG File Logic (3 instances!)
- Lines 2075-2143 (O key handler)
- Lines 2354-2419 (Browser Load button)
- Lines 2491-2560 (Browser double-click)

**Proposed**: Extract to `handleLoadSVGFile(path, renderers, state)` - saves ~200 lines!

### 2. Reset Statistics Logic (3 instances)
- Lines 1906-1927 (R key)
- Lines 1955-1969 (V key toggle)
- Lines 1975-1989 (F key toggle)

**Proposed**: Extract to `resetStatistics(perfTrackers)` - saves ~60 lines

### 3. Browser Async Navigation Pattern (5 instances)
- Lines 2170-2180 (Initial open)
- Lines 2313-2316 (Navigate)
- Lines 2318-2323 (Back)
- Lines 2325-2330 (Forward)
- Lines 2332-2336 (Parent)

**Proposed**: Extract to `startAsyncNavigation(callback)` - saves ~50 lines

---

## Next Steps

1. **Review this plan** with team/stakeholders
2. **Create feature branch** for refactoring
3. **Implement Phase 1** (structs + pure functions)
4. **Commit incrementally** - one function extraction per commit
5. **Test continuously** - verify after each extraction
6. **Iterate** - adjust plan based on discoveries

---

**Document Version**: 1.0
**Date**: 2026-01-01
**Author**: Claude Code
**Status**: Draft - Awaiting Review
