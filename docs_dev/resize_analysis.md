# Window Resize Analysis - Linux Player

**File Analyzed**: `src/svg_player_animated_linux.cpp`
**Date**: 2026-01-22

---

## 1. SDL_WINDOWEVENT_RESIZED Event Handling

**Location**: Lines 2692-2742

```cpp
case SDL_WINDOWEVENT:
    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        // Resize handling...
    }
    break;
```

**Triggers**: Both `SDL_WINDOWEVENT_RESIZED` and `SDL_WINDOWEVENT_SIZE_CHANGED` events are handled.

---

## 2. RenderWidth/RenderHeight Update Logic

**Location**: Lines 2695-2707

### Step 1: Get HiDPI-Aware Renderer Size
```cpp
int actualW, actualH;
SDL_GetRendererOutputSize(renderer, &actualW, &actualH);
```
Uses SDL's HiDPI-aware function to get actual pixel dimensions (not window logical size).

### Step 2: Calculate Window Aspect Ratio
```cpp
float windowAspect = static_cast<float>(actualW) / actualH;
```

### Step 3: Letterboxing Logic
```cpp
if (windowAspect > aspectRatio) {
    // Window is wider than SVG - fit to height
    renderHeight = actualH;
    renderWidth = static_cast<int>(actualH * aspectRatio);
} else {
    // Window is taller than SVG - fit to width
    renderWidth = actualW;
    renderHeight = static_cast<int>(actualW / aspectRatio);
}
```

**Behavior**: Maintains SVG aspect ratio via letterboxing (pillarbox if wide, letterbox if tall).

---

## 3. Aspect Ratio Recalculation

### Initial Calculation
**Location**: Line 1572 (in `main()`)
```cpp
float aspectRatio = static_cast<float>(svgWidth) / svgHeight;
```

### Updates on File Load
**Location**: Line 1426 (in `loadSVGFile()` function)
```cpp
aspectRatio = static_cast<float>(svgWidth) / svgHeight;
```

**Called When**:
- Initial SVG load (line 1572)
- Loading new file via keyboard navigation (lines 2112, 2399, 2542, 2632)

### During Resize
**Important**: `aspectRatio` is **NOT recalculated** during resize. The resize handler uses the **existing aspectRatio value** from the currently loaded SVG.

**Rationale**: Correct behavior - resizing the window should preserve the SVG's intrinsic aspect ratio, not adopt the window's new aspect ratio.

---

## 4. Post-Resize Operations

**Location**: Lines 2709-2739

### 4.1. Texture Recreation
```cpp
SDL_DestroyTexture(texture);
texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, 
                            SDL_TEXTUREACCESS_STREAMING,
                            renderWidth, renderHeight);
```

### 4.2. Surface Recreation
```cpp
createSurface(renderWidth, renderHeight);
```

### 4.3. Renderer Backend Notification
```cpp
threadedRenderer.resize(renderWidth, renderHeight);   // Clears frame buffer
parallelRenderer.resize(renderWidth, renderHeight);   // Clears pre-buffered frames
```

**Note**: Both renderers clear their cached frames since they're now the wrong size (see ParallelRenderer::resize() at lines 536-548).

### 4.4. Browser Mode Handling
```cpp
if (g_browserMode) {
    // Use viewport-height-based sizing (vh units)
    float vh = static_cast<float>(renderHeight) / 100.0f;
    
    svgplayer::BrowserConfig config = g_folderBrowser.getConfig();
    config.containerWidth = renderWidth;
    config.containerHeight = renderHeight;
    config.cellMargin = 2.0f * vh;        // 2vh
    config.labelHeight = 6.0f * vh;       // 6vh
    config.headerHeight = 5.0f * vh;      // 5vh
    config.navBarHeight = 4.0f * vh;      // 4vh
    config.buttonBarHeight = 6.0f * vh;   // 6vh
    g_folderBrowser.setConfig(config);
    g_folderBrowser.markDirty();
}
```

**Browser-Specific**: Uses responsive vh (viewport height) units for UI element sizing, ensuring proportional scaling.

---

## 5. Issues Found

### ✓ No Critical Issues

The resize logic is **correctly implemented**:

1. **HiDPI Support**: Uses `SDL_GetRendererOutputSize()` for accurate pixel dimensions
2. **Aspect Ratio Preservation**: Maintains SVG aspect ratio via letterboxing
3. **Resource Management**: Properly destroys and recreates texture/surface
4. **Backend Synchronization**: Notifies both rendering backends to clear cached frames
5. **Browser Mode Support**: Handles browser UI resizing with responsive vh units

### ⚠️ Minor Observations

1. **No Early Exit**: The resize handler doesn't check if dimensions actually changed before recreating resources. Consider adding:
   ```cpp
   static int lastRenderW = 0, lastRenderH = 0;
   if (renderWidth == lastRenderW && renderHeight == lastRenderH) break;
   lastRenderW = renderWidth; lastRenderH = renderHeight;
   ```

2. **Renderer Backend Resize Check**: The `ParallelRenderer::resize()` function (line 536-548) already has an early exit check:
   ```cpp
   if (width == renderWidth && height == renderHeight) return;
   ```
   But the main resize handler recreates texture/surface unconditionally.

3. **Frame Buffer Clearing**: Both backends clear their frame buffers on resize, which is correct (old frames are wrong size). However, this causes a brief rendering pause while new frames are generated.

---

## 6. Resize Flow Diagram

```
User Resizes Window
       |
       v
SDL_WINDOWEVENT_RESIZED/SIZE_CHANGED
       |
       v
Get HiDPI-Aware Size (SDL_GetRendererOutputSize)
       |
       v
Calculate Window Aspect Ratio
       |
       v
Compare with SVG Aspect Ratio
       |
       +--> Window Wider? -> Fit to Height (renderHeight = actualH)
       |                     renderWidth = actualH * aspectRatio
       |
       +--> Window Taller? -> Fit to Width (renderWidth = actualW)
                              renderHeight = actualW / aspectRatio
       |
       v
Destroy Old Texture
       |
       v
Create New Texture (renderWidth x renderHeight)
       |
       v
Recreate Skia Surface (createSurface)
       |
       v
Notify Threaded Renderer (clear frame buffer)
       |
       v
Notify Parallel Renderer (clear pre-buffered frames)
       |
       v
If Browser Mode: Update BrowserConfig with vh units
       |
       v
Continue Rendering Loop
```

---

## 7. Variable Scope & Lifetime

| Variable | Scope | Declaration | Updated When |
|----------|-------|-------------|--------------|
| `aspectRatio` | `main()` local | Line 1572 | On SVG file load (via `loadSVGFile()`) |
| `renderWidth` | `main()` local | Line 1650 (initial) | On window resize (line 2703/2705) |
| `renderHeight` | `main()` local | Line 1651 (initial) | On window resize (line 2702/2706) |
| `svgWidth` | `main()` local | Line 1570 | On SVG file load (via `loadSVGFile()`) |
| `svgHeight` | `main()` local | Line 1571 | On SVG file load (via `loadSVGFile()`) |

**Key Insight**: `aspectRatio` is tied to the **SVG document**, not the window. It only changes when loading a different SVG file, not when resizing the window.

---

## Conclusion

The resize handling in the Linux player is **correctly implemented** with proper:
- HiDPI support
- Aspect ratio preservation via letterboxing
- Resource cleanup and recreation
- Multi-backend synchronization
- Browser mode responsiveness

No critical bugs found. Minor optimization possible by avoiding redundant resource recreation when dimensions haven't actually changed.
