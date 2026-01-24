## Surface Lifecycle Review

**Files Reviewed:**
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.mm`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.h`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated.cpp` (Metal rendering sections)

**Review Date:** 2026-01-18

---

### Critical Issues

#### 1. Double Flush Causes Performance Overhead
**Location:** `metal_context.mm:208-214` and `metal_context.mm:246-247`

**Problem:** The `createSurface()` method performs a `flushAndSubmit()` to force lazy proxy instantiation, then `presentDrawable()` calls `flushAndSubmit()` again before presentation.

```cpp
// In createSurface() - line 212-214
canvas->clear(SK_ColorTRANSPARENT);
impl_->skiaContext->flushAndSubmit();  // FIRST FLUSH

// In presentDrawable() - line 247
impl_->skiaContext->flushAndSubmit();  // SECOND FLUSH
```

**Impact:** Each frame performs two GPU command flushes. On Apple Silicon, this may cause unnecessary GPU pipeline stalls.

**Severity:** Medium-High (Performance degradation)

---

#### 2. Drawable Output Parameter May Be Nil After Lazy Instantiation
**Location:** `metal_context.mm:196-227`

**Problem:** The `WrapCAMetalLayer()` uses Skia's lazy proxy mechanism. The `tempDrawable` is passed to Skia but may not be populated until actual GPU work is performed. The code attempts to force instantiation with a `clear()` + `flush()`, but there's no guarantee `tempDrawable` is valid after this.

```cpp
GrMTLHandle tempDrawable = nullptr;
sk_sp<SkSurface> surface = SkSurfaces::WrapCAMetalLayer(..., &tempDrawable);
// tempDrawable may still be nullptr here!

// Force instantiation attempt
canvas->clear(SK_ColorTRANSPARENT);
impl_->skiaContext->flushAndSubmit();

// tempDrawable SHOULD be set now, but not verified
if (outDrawable) {
    *outDrawable = tempDrawable;  // May pass nil
}
```

**Impact:** Caller receives nil drawable, leading to frame drops and the "[Metal] Failed to acquire drawable" errors seen in logs.

**Severity:** High (Frame drops, rendering failures)

---

### Warnings

#### 3. No Timeout Handling for macOS < 10.15.4
**Location:** `metal_context.mm:88-92`

**Problem:** The `allowsNextDrawableTimeout` property is only available on macOS 10.15.4+. On older systems, `nextDrawable` can block indefinitely if all drawables are in use.

```objc
if (@available(macOS 10.15.4, *)) {
    impl_->metalLayer.allowsNextDrawableTimeout = YES;
}
// No fallback for older macOS!
```

**Impact:** Potential UI freeze on macOS < 10.15.4 under GPU pressure.

**Severity:** Low (Affects old macOS versions only)

---

#### 4. No Adaptive Back-Pressure Mechanism
**Location:** `svg_player_animated.cpp:4111-4117`

**Problem:** When drawable acquisition fails, the frame is simply skipped with a log message. There's no adaptive mechanism to reduce frame rate or queue depth.

```cpp
// Failed to get drawable - Metal may be busy, skip this frame
if (!g_jsonOutput) {
    std::cerr << "[Metal] Failed to acquire drawable this frame" << ...;
}
// Frame is dropped, no recovery strategy
```

**Impact:** Under sustained GPU pressure (complex SVGs, high refresh rate), multiple consecutive frames may be dropped without any throttling.

**Severity:** Medium (Poor degradation under load)

---

#### 5. GPU Readback on Render Thread (Black Screen Detection)
**Location:** `svg_player_animated.cpp:4525-4549`

**Problem:** Every 60 frames, a GPU-to-CPU pixel readback is performed for black screen detection. This blocks the render thread waiting for GPU work to complete.

```cpp
if (frameCount % 60 == 0 && surface) {
    // GPU readback - blocks until GPU finishes current work
    if (surface->readPixels(info, checkPixels.data(), ...)) {
        // Inspect pixels...
    }
}
```

**Impact:** Periodic frame time spikes (~1ms on fast GPUs, potentially much more on complex frames).

**Severity:** Low-Medium (Debugging feature, but could affect smoothness)

---

#### 6. Surface Screenshot May Read Stale GPU Data
**Location:** `svg_player_animated.cpp:3119-3134`

**Problem:** Screenshot in Metal mode uses `surface->readPixels()` but doesn't ensure GPU work is complete first. The `flushAndSubmit()` in `presentDrawable()` commits work asynchronously.

```cpp
// Metal mode screenshot
if (metalContext && metalContext->isInitialized() && surface) {
    // No sync point before readback!
    if (surface->readPixels(info, screenshotPixels.data(), ...)) {
        screenshotSuccess = true;
    }
}
```

**Impact:** Screenshots may capture incomplete or stale frame data.

**Severity:** Medium (Affects user-facing feature)

---

### Positive Findings (Correctly Implemented)

1. **Drawable Reuse Prevention:** CORRECT
   - `metalDrawable = nullptr` before `createSurface()` (line 4072)
   - `metalDrawable = nullptr` after `presentDrawable()` (line 4574)
   - Each frame uses a fresh drawable

2. **Per-Frame Surface Creation:** CORRECT
   - Metal skips initial surface creation (lines 2391-2410)
   - Fresh surface created each frame in render loop (line 4073)

3. **Surface Reference Counting:** CORRECT
   - `sk_sp<SkSurface>` used throughout
   - Proper RAII semantics on reassignment

4. **Canvas Validity:** CORRECT
   - Canvas obtained fresh from surface after each `createSurface()` (line 4078)
   - No stale canvas references

5. **Resize Handling:** CORRECT
   - `updateDrawableSize()` called before `createSurface()` (line 2374)
   - Surface recreated on window resize event (lines 3689-3718)

6. **Triple Buffering Configuration:** CORRECT
   - `maximumDrawableCount = 3` (line 96)
   - Proper VSync configuration (lines 103-105)

---

### Recommendations

#### High Priority

1. **Eliminate Double Flush**
   Remove the `flushAndSubmit()` from `createSurface()`. The clear operation should not require an immediate flush - let the actual frame rendering accumulate commands, then flush once at presentation.

   ```cpp
   // In createSurface() - REMOVE lines 213-214:
   // canvas->clear(SK_ColorTRANSPARENT);
   // impl_->skiaContext->flushAndSubmit();
   ```

2. **Verify Drawable After Instantiation**
   Add explicit validation that the drawable was actually acquired:

   ```cpp
   if (!tempDrawable) {
       // Fallback: try explicit nextDrawable
       id<CAMetalDrawable> explicitDrawable = [impl_->metalLayer nextDrawable];
       if (explicitDrawable) {
           tempDrawable = (__bridge_retained GrMTLHandle)explicitDrawable;
       }
   }
   ```

3. **Add GPU Sync Before Screenshot**
   Call `metalContext->flush()` and wait for completion before reading pixels:

   ```cpp
   if (metalContext && metalContext->isInitialized() && surface) {
       impl_->skiaContext->flushAndSubmit(GrSyncCpu::kYes);  // Sync CPU
       if (surface->readPixels(...)) { ... }
   }
   ```

#### Medium Priority

4. **Implement Adaptive Frame Skipping**
   Track consecutive dropped frames and reduce target frame rate if GPU is consistently overloaded:

   ```cpp
   static int consecutiveDrops = 0;
   if (!metalDrawable) {
       consecutiveDrops++;
       if (consecutiveDrops > 5) {
           targetFps = std::max(30, targetFps - 10);  // Throttle
       }
   } else {
       consecutiveDrops = 0;
   }
   ```

5. **Make Black Screen Detection Async**
   Move periodic GPU readback to a separate thread or use async completion handlers:

   ```objc
   [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
       // Perform black screen detection here (off render thread)
   }];
   ```

#### Low Priority

6. **Add Fallback Timeout for Older macOS**
   For macOS < 10.15.4, implement a workaround using a semaphore with timeout:

   ```objc
   if (@available(macOS 10.15.4, *)) {
       // Use built-in timeout
   } else {
       // Manual timeout with dispatch_semaphore
   }
   ```

---

### Summary

| Category | Count | Severity |
|----------|-------|----------|
| Critical Issues | 2 | High |
| Warnings | 4 | Medium-Low |
| Correct Implementations | 6 | N/A |

**Overall Assessment:** The Metal surface lifecycle is mostly correct, but the double flush and drawable validation issues could cause performance problems and frame drops under load. The recommendations above should improve reliability and performance.
