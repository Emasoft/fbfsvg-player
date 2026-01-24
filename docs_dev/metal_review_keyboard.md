# Keyboard Handling Review - Metal Mode

**File Reviewed:** `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated.cpp`  
**Date:** 2026-01-18  
**Focus:** Keyboard input handling under Metal GPU backend

---

## Critical Issues

### 1. Screenshot `readPixels()` Blocks Main Thread

**Location:** Lines 3117-3138

**Problem:** When taking a screenshot in Metal mode (`SDLK_c`), the code calls `surface->readPixels()` which performs a **synchronous GPU-to-CPU data transfer**. This operation:
- Stalls the GPU pipeline (waits for all pending commands to complete)
- Transfers potentially millions of pixels from GPU to system memory
- Blocks the main event loop during the entire transfer

**Impact:** At 4K resolution (3840x2160), this transfers ~33MB and can take 10-50ms depending on GPU load, causing visible stutter and missed frames.

**Evidence:**
```cpp
if (surface->readPixels(info, screenshotPixels.data(),
                        screenshotWidth * sizeof(uint32_t), 0, 0)) {
    screenshotSuccess = true;
}
```

### 2. No Key Repeat Filtering

**Location:** Lines 2877-3287

**Problem:** The `SDL_KEYDOWN` event handler does not filter out auto-repeat events. SDL generates repeated `SDL_KEYDOWN` events when a key is held down. Most key handlers toggle state:
- `SDLK_v` (VSync toggle)
- `SDLK_d` (Debug overlay)
- `SDLK_b` (Browser mode)
- `SDLK_t` (Frame limiter)
- `SDLK_p` (Parallel mode)

**Impact:** Holding any toggle key causes rapid on/off/on/off cycling until key release.

**Evidence:** No check for `event.key.repeat` exists in the code:
```cpp
case SDL_KEYDOWN:
    if (event.key.keysym.sym == SDLK_ESCAPE) {
        // No repeat check here or in any other handler
```

---

## Warnings

### 3. VSync Toggle Mid-Frame is Safe But Suboptimal

**Location:** Lines 2950-3007

**Analysis:** The VSync toggle in Metal mode calls `metalContext->setVSyncEnabled()` which simply sets `CAMetalLayer.displaySyncEnabled`. This is:
- **Safe:** The property change takes effect on the next drawable presentation
- **Suboptimal:** The current frame being rendered will still use the old VSync setting

The code properly resets all stats after VSync change, so timing accuracy is preserved.

### 4. Browser Mode Works in Metal But Creates Per-Frame Surfaces

**Location:** Lines 3894-3901

**Analysis:** Browser mode in Metal creates a new surface every frame:
```cpp
if (useMetalBackend && metalContext && metalContext->isInitialized()) {
    metalDrawable = nullptr;
    surface = metalContext->createSurface(renderWidth, renderHeight, &metalDrawable);
```

This is correct behavior (Metal requires acquiring a new drawable each frame), but:
- Creates brief blocking when all 3 drawables are in use
- `allowsNextDrawableTimeout = YES` prevents infinite hangs (1 second timeout)

### 5. Event Queue Processing Has Potential Starvation

**Location:** Lines 2861-2866

**Analysis:** The event loop uses `SDL_PollEvent()` in a while loop:
```cpp
auto eventStart = Clock::now();
while (SDL_PollEvent(&event)) {
    // Process event...
}
```

If many events queue up (e.g., mouse movement during animation), all are processed before rendering continues. This is generally fine but could theoretically delay frame presentation if thousands of events accumulate.

**Mitigating Factor:** The early ESC/Q check at lines 2819-2829 uses `SDL_PumpEvents()` + `SDL_GetKeyboardState()` to guarantee quit responsiveness regardless of event queue depth.

### 6. File I/O During Screenshot Save Blocks Main Thread

**Location:** Lines 3147-3159

**Analysis:** After reading pixels, `saveScreenshotPPM()` performs synchronous file I/O. For large screenshots, this adds additional blocking time on top of the `readPixels()` delay.

The code sets `skipStatsThisFrame = true` to exclude this from timing stats, acknowledging the disruption.

---

## Recommendations

### High Priority

1. **Add Key Repeat Filtering**
   ```cpp
   case SDL_KEYDOWN:
       if (event.key.repeat) break;  // Ignore auto-repeat events
       if (event.key.keysym.sym == SDLK_ESCAPE) {
   ```

2. **Async Screenshot for Metal Mode**
   Consider using `MTLBlitCommandEncoder` with a completion handler to read pixels asynchronously, then save in a background thread.

### Medium Priority

3. **Rate-Limit Event Processing**
   Add a maximum event count per frame to prevent event queue starvation:
   ```cpp
   int eventCount = 0;
   while (SDL_PollEvent(&event) && eventCount++ < 100) {
   ```

4. **Debounce Toggle Keys**
   Add minimum time between toggle activations:
   ```cpp
   static auto lastToggleTime = Clock::now();
   if (Clock::now() - lastToggleTime < std::chrono::milliseconds(200)) break;
   lastToggleTime = Clock::now();
   ```

### Low Priority

5. **Background Thread for Screenshot Save**
   Move PPM writing to a detached thread to eliminate file I/O blocking.

6. **Consider `presentsWithTransaction`**
   For tighter VSync control, setting `presentsWithTransaction = YES` could provide more deterministic frame timing, at the cost of slightly higher latency.

---

## Key Functionality Summary

| Key | Function | Metal Status | Issues |
|-----|----------|--------------|--------|
| `ESC` | Exit/Close browser | OK | None (early check bypass) |
| `Q` | Quit | OK | None (early check bypass) |
| `V` | VSync toggle | OK | No repeat filter |
| `C` | Screenshot | BLOCKING | GPU→CPU sync stall |
| `D` | Debug overlay | OK | No repeat filter |
| `B` | Browser mode | OK | No repeat filter |
| `T` | Frame limiter | OK | No repeat filter |
| `P` | Parallel mode | OK | No repeat filter |
| `F/G` | Fullscreen | OK | No repeat filter |
| `M` | Maximize | OK | No repeat filter |
| `O` | Open file | BLOCKING | File dialog blocks |
| `R` | Reset stats | OK | No repeat filter |

---

## Conclusion

The keyboard handling in Metal mode is **functionally correct** but has two issues worth addressing:

1. **Key repeat filtering** - Simple fix with immediate UX improvement
2. **Screenshot blocking** - More complex fix, but impacts less common use case

The early quit key check (lines 2819-2829) is a well-designed safeguard ensuring the player remains responsive even under GPU load. The Metal VSync toggle is implemented correctly via `CAMetalLayer.displaySyncEnabled`.
