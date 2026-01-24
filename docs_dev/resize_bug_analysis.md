# Window Resize Aspect Ratio Bug Analysis

**Date**: 2026-01-22  
**File**: `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated_linux.cpp`  
**Issue**: Window resize causes aspect ratio distortion instead of proper letterboxing

---

## Root Cause: Double Letterboxing

The Linux player applies letterboxing **twice**, causing aspect ratio distortion:

1. **First letterboxing**: Resize handler (lines 2699-2707) creates a pre-letterboxed render buffer
2. **Second letterboxing**: Rendering code (lines 2982-2990) letterboxes the SVG again within that buffer

### Buggy Code (Linux Resize Handler)

**Location**: `src/svg_player_animated_linux.cpp:2693-2707`

```cpp
case SDL_WINDOWEVENT:
    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        int actualW, actualH;
        SDL_GetRendererOutputSize(renderer, &actualW, &actualH);

        float windowAspect = static_cast<float>(actualW) / actualH;

        // BUG: Pre-letterboxing the render buffer
        if (windowAspect > aspectRatio) {
            renderHeight = actualH;
            renderWidth = static_cast<int>(actualH * aspectRatio);  // ← Letterboxed width
        } else {
            renderWidth = actualW;
            renderHeight = static_cast<int>(actualW / aspectRatio);  // ← Letterboxed height
        }

        SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, 
                                    SDL_TEXTUREACCESS_STREAMING,
                                    renderWidth, renderHeight);
        // ...
    }
```

**Problem**: The render buffer is sized to match the SVG aspect ratio, not the window dimensions.

### Rendering Code (Applied to Already-Letterboxed Buffer)

**Location**: `src/svg_player_animated_linux.cpp:2982-2999`

```cpp
// Calculate transform to fit SVG in render area with aspect ratio
int effectiveSvgW = (svgWidth > 0) ? svgWidth : renderWidth;
int effectiveSvgH = (svgHeight > 0) ? svgHeight : renderHeight;
float scaleX = static_cast<float>(renderWidth) / effectiveSvgW;
float scaleY = static_cast<float>(renderHeight) / effectiveSvgH;
float scale = std::min(scaleX, scaleY);  // ← Second letterboxing calculation
float offsetX = (renderWidth - effectiveSvgW * scale) / 2;
float offsetY = (renderHeight - effectiveSvgH * scale) / 2;

canvas->save();
canvas->translate(offsetX, offsetY);  // ← Second letterbox offset
canvas->scale(scale, scale);          // ← Second letterbox scale
g_animController.renderFrame(canvas, renderWidth, renderHeight);
canvas->restore();
```

**Problem**: This code expects `renderWidth` x `renderHeight` to be the FULL window size, but it's already letterboxed!

### Correct Behavior (macOS Reference)

**Location**: `src/svg_player_animated.cpp:4125-4128`

```cpp
// Use full output size - SVG's preserveAspectRatio handles centering
// This ensures debug overlay at (0,0) is truly at top-left of window
renderWidth = actualW;
renderHeight = actualH;
```

The macOS version uses the **full window dimensions** for the render buffer, allowing the rendering code to handle letterboxing correctly.

---

## The Fix

**File**: `src/svg_player_animated_linux.cpp`  
**Lines**: 2699-2707

### Replace This:

```cpp
float windowAspect = static_cast<float>(actualW) / actualH;

if (windowAspect > aspectRatio) {
    renderHeight = actualH;
    renderWidth = static_cast<int>(actualH * aspectRatio);
} else {
    renderWidth = actualW;
    renderHeight = static_cast<int>(actualW / aspectRatio);
}
```

### With This:

```cpp
// Use full output size - letterboxing handled by rendering code
// This matches macOS behavior and avoids double-letterboxing
renderWidth = actualW;
renderHeight = actualH;
```

---

## Why This Works

| Stage | Before Fix (Buggy) | After Fix (Correct) |
|-------|-------------------|---------------------|
| **Resize Event** | Creates letterboxed buffer (e.g., 800x600 → 800x450 for 16:9 SVG) | Creates full-size buffer (800x600) |
| **Rendering** | Letterboxes SVG into already-letterboxed buffer → distortion | Letterboxes SVG into full buffer → correct aspect ratio |
| **Display** | Stretches distorted buffer to window | Displays correctly letterboxed content |

### Example Scenario

**SVG**: 1920x1080 (16:9 aspect ratio)  
**Window**: Resized to 800x600 (4:3 aspect ratio)

#### Before Fix (Double Letterboxing):
1. Resize handler: Creates 800x450 render buffer (pre-letterboxed to 16:9)
2. Rendering code: Tries to letterbox 1920x1080 into 800x450 → incorrect calculations
3. Display: Stretches 800x450 buffer to 800x600 window → **distortion**

#### After Fix (Single Letterboxing):
1. Resize handler: Creates 800x600 render buffer (full window size)
2. Rendering code: Letterboxes 1920x1080 into 800x600 → correct black bars
3. Display: Shows 800x600 buffer as-is → **correct aspect ratio**

---

## Testing Checklist

After applying the fix, verify:

- [ ] Window resize maintains correct aspect ratio (no stretching)
- [ ] Black bars appear on correct sides (letterboxing or pillarboxing)
- [ ] Debug overlay remains at top-left corner
- [ ] HiDPI displays work correctly
- [ ] Browser mode resizing works correctly
- [ ] Fullscreen mode transitions smoothly

---

## Related Code Paths

All three render paths apply the same letterboxing transform, so they will all benefit from this fix:

1. **Graphite GPU path** (lines 2982-2999)
2. **Threaded CPU renderer** (lines 1084-1100)
3. **Parallel pre-buffer renderer** (lines 749-766)

The fix only needs to be applied to the resize handler - no changes needed to rendering code.

---

## Additional Notes

- The `aspectRatio` variable (set at line 1426) is still needed for other purposes but should NOT be used for render buffer sizing
- The SDL_RenderCopy destination rect calculation (lines 3430-3436) correctly centers the letterboxed content
- The macOS version had the correct implementation all along - this fix aligns Linux with that behavior
