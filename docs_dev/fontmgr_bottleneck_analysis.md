# FontMgr Bottleneck Analysis and Fix

**Date**: January 17, 2026

## Executive Summary

Through detailed phase tracing, we discovered that **creating the CoreText FontMgr per frame** was the primary bottleneck in Skia's folder player, consuming 92.5% of frame time. After caching the FontMgr outside the render loop, **Skia became 2.25x faster than ThorVG** (previously appeared 10x slower).

## Discovery Process

### Phase Tracing Implementation

Added detailed timing for each pipeline phase:

**ThorVG Pipeline Phases:**
- canvas_create, canvas_target, picture_load, transform
- canvas_add, buffer_clear, canvas_update, canvas_draw
- canvas_sync, texture_update, sdl_present

**Skia Pipeline Phases:**
- data_copy, stream_create, font_mgr, dom_parse
- container_size, canvas_clear, dom_render
- pixel_extract, texture_update, sdl_present

### Phase Timing Results (Before Fix)

```json
ThorVG (avg frame: 156.4ms @ 6.26 FPS):
{
  "picture_load_ms": 0.48,      // 0.3% - SVG parsing
  "canvas_draw_ms": 143.2,      // 91.6% - rasterization
  "texture_update_ms": 9.66     // 6.2%
}

Skia (avg frame: 1333.8ms @ 0.74 FPS):
{
  "font_mgr_ms": 1234.0,        // 92.5% - THE BOTTLENECK!
  "dom_parse_ms": 0.75,         // 0.06%
  "dom_render_ms": 91.1         // 6.8% - rasterization
}
```

### Key Insight

The bottleneck was **NOT** SVG parsing or rendering - it was `SkFontMgr_New_CoreText(nullptr)` being called inside the render loop. This macOS system call:

1. Enumerates all system fonts
2. Builds font matching tables
3. Initializes CoreText font caches
4. Takes ~1.2 seconds each call

## The Fix

**Before** (in render loop):
```cpp
while (running) {
    // ... per frame ...
    auto fontMgr = SkFontMgr_New_CoreText(nullptr);  // 1234ms!
    auto dom = SkSVGDOM::Builder()
        .setFontManager(fontMgr)
        .make(*stream);
}
```

**After** (cached):
```cpp
// Create once at startup
auto fontMgr = SkFontMgr_New_CoreText(nullptr);

while (running) {
    // ... per frame ...
    auto dom = SkSVGDOM::Builder()
        .setFontManager(fontMgr)  // Reuse cached instance
        .make(*stream);
}
```

## Results After Fix

```
BEFORE FIX (FontMgr created every frame):
  Skia avg_fps: 0.74 FPS
  Skia avg_frame_time: 1333.76ms
  font_mgr phase: 1233.96ms (92.5%)

AFTER FIX (FontMgr cached):
  Skia avg_fps: 14.11 FPS
  Skia avg_frame_time: 70.25ms
  font_mgr phase: 0ms (cached!)

IMPROVEMENT: 19x FASTER!
```

## Comparison After Fix

| Metric | ThorVG | Skia (Fixed) | Winner |
|--------|--------|--------------|--------|
| FPS | 6.26 | 14.11 | Skia (2.25x) |
| Frame Time | 156.4ms | 70.25ms | Skia (2.22x) |
| Rasterization | 143.2ms | 65.28ms | Skia (2.19x) |
| Parsing | 0.48ms | 0.56ms | ThorVG (1.2x) |

**Result: Skia is now 2.25x faster than ThorVG** at complex SVG rendering.

## Root Cause Analysis

The original code was copied from a single-frame rendering example where FontMgr creation overhead was acceptable. When used in a frame sequence benchmark, this became catastrophic.

This pattern is common in naive Skia integration:
1. Simple examples create resources per-use
2. Production code must cache expensive resources
3. FontMgr, ResourceProviders, and SkSurface should be cached

## Lessons Learned

1. **Always trace individual phases** - Total frame time masks component bottlenecks
2. **Profile system calls** - Platform API overhead varies wildly
3. **Cache expensive resources** - Font managers, shaper factories, surfaces
4. **Don't trust aggregate benchmarks** - "Parsing is slow" was misleading

## Files Modified

- `src/skia_folder_player.cpp` - Moved FontMgr creation outside loop
- Added phase timing infrastructure for future profiling

## Test Environment

- macOS on Apple Silicon (M-series)
- Resolution: 3762x2054 (4K Retina)
- Test SVGs: 1000 frames with complex shapes, gradients, blur, text
