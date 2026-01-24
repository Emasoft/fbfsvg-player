# Metal Pipeline Implementation Status

**Date**: January 18, 2026
**Estimated Progress**: ~90% complete

## Executive Summary

The Metal GPU backend for fbfsvg-player is now mostly functional. The core infrastructure is complete and all critical bugs have been fixed. The player now supports Metal rendering with VSync control, screenshots, and black screen detection.

---

## IMPLEMENTED (Core Infrastructure)

### 1. MetalContext Class (`src/metal_context.h`, `src/metal_context.mm`)

| Feature | Status | Notes |
|---------|--------|-------|
| Metal device creation | Done | `MTLCreateSystemDefaultDevice()` |
| Command queue creation | Done | `[device newCommandQueue]` |
| SDL Metal view | Done | `SDL_Metal_CreateView()` |
| CAMetalLayer setup | Done | Configured with BGRA8Unorm |
| Skia GrDirectContext | Done | `GrDirectContexts::MakeMetal()` |
| Surface creation | Done | `SkSurfaces::WrapCAMetalLayer()` |
| Drawable presentation | Done | `presentDrawable()` |
| HiDPI support | Done | `contentsScale` from NSScreen |
| Drawable timeout | Done | `allowsNextDrawableTimeout = YES` |
| VSync control | Done | `setVSyncEnabled()` / `isVSyncEnabled()` |
| Triple buffering | Done | `maximumDrawableCount = 3` |
| Async presentation | Done | `presentsWithTransaction = NO` |

### 2. Main Player Integration (`src/svg_player_animated.cpp`)

| Feature | Status | Notes |
|---------|--------|-------|
| `--metal` command line flag | Done | Enables Metal backend |
| `SDL_WINDOW_METAL` flag | Done | Creates Metal-backed window |
| SDL Metal hints | Done | `RENDER_DRIVER=metal` |
| Metal context initialization | Done | Creates at startup |
| Per-frame surface creation | Done | Fresh drawable each frame |
| SVG DOM rendering to Metal | Done | Renders via `canvas` |
| Metal presentation path | Done | Separate from CPU path |
| Debug logging | Done | `RENDER_DEBUG` env var |
| Fallback to CPU on failure | Done | Graceful degradation |
| Browser mode Metal support | Done | Creates Metal surface for browser |
| Debug overlay Metal support | Done | Fixed canvas nullptr issue |
| VSync toggle (V key) | Done | Uses `metalContext->setVSyncEnabled()` |
| Screenshot support (C key) | Done | Uses `surface->readPixels()` |
| Black screen detection | Done | Periodic check (every 60 frames) |

---

## FIXED BUGS

### 1. Canvas nullptr in Metal Mode (FIXED)

**Problem**: The `canvas` variable was `nullptr` in Metal mode, causing crashes when debug overlay or browser mode tried to draw.

**Solution**:
- Changed local `metalCanvas` to use global `canvas` variable in Metal rendering path
- Added null check protection around debug overlay drawing
- Added Metal surface creation at start of browser mode

### 2. Browser Mode Broken in Metal Mode (FIXED)

**Problem**: Browser mode rendering used `canvas` directly, which was `nullptr` in Metal mode.

**Solution**: Added Metal surface creation at the beginning of the browser mode section.

---

## REMAINING WORK (Minor Items)

### 1. Mode Toggle (PreBuffer/Direct)

**Location**: `svg_player_animated.cpp:3030`

**Status**: Not applicable for Metal mode since Metal doesn't use ThreadedRenderer.

### 2. Dirty Region Tracking Integration

**Status**: Infrastructure exists but not yet integrated with Metal rendering.

**Files**:
- `shared/DirtyRegionTracker.cpp`
- `shared/DirtyRegionTracker.h`
- `shared/ElementBoundsExtractor.cpp`
- `shared/ElementBoundsExtractor.h`

**Purpose**: Partial rendering optimization - only re-render changed regions.

### 3. Metal-specific Frame Statistics

**Current**: Some stats rely on ThreadedRenderer which isn't used in Metal mode.

**Future**: Could add Metal-specific timing metrics using Metal GPU timestamps.

---

## Implementation Summary

### Phase 1: Critical Bug Fixes (COMPLETED)

1. **Fixed canvas nullptr in Metal mode** - Debug overlay and browser now work
2. **Added VSync control to MetalContext** - V key toggles VSync

### Phase 2: Feature Parity (COMPLETED)

3. **Screenshot support in Metal mode** - C key captures screenshots
4. **Black screen detection in Metal mode** - Periodic check for debugging
5. **Metal-specific configuration** - Triple buffering, async presentation

### Phase 3: Optimization (PENDING)

6. **Dirty region tracking integration** - Future optimization
7. **Metal GPU timestamps** - Future detailed profiling

---

## File Summary

| File | Status | Changes Made |
|------|--------|--------------|
| `src/metal_context.h` | Complete | Added VSync and drawable count methods |
| `src/metal_context.mm` | Complete | Implemented VSync, drawable count, optimal settings |
| `src/svg_player_animated.cpp` | Complete | Fixed canvas, added browser Metal, screenshot, black screen detection |
| `shared/SVGAnimationController.cpp` | Complete | Frame tracking done |
| `shared/DirtyRegionTracker.cpp` | New | Not integrated (future optimization) |
| `shared/ElementBoundsExtractor.cpp` | New | Not integrated (future optimization) |

---

## Testing

### To Test Metal Mode

```bash
# Build macOS player
make macos

# Run with Metal backend
./build/svg_player_animated --metal svg_input_samples/girl_hair.fbf.svg

# With debug output
RENDER_DEBUG=1 ./build/svg_player_animated --metal svg_input_samples/girl_hair.fbf.svg
```

### Test Checklist

- [x] Metal mode starts without crash
- [x] Debug overlay (D key) works in Metal mode
- [x] VSync toggle (V key) works in Metal mode
- [x] Screenshots (C key) work in Metal mode
- [x] Browser mode (B key) works in Metal mode
- [x] Animation playback smooth in Metal mode
- [x] Black screen detection reports correctly

---

**Document Version**: 2.0
**Last Updated**: 2026-01-18
