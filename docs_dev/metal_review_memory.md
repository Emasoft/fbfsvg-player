## Memory Management Review

**Files Reviewed:**
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.h`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.mm`

**Review Date:** 2026-01-18

---

### Critical Issues

#### 1. Resource Leak on Partial Initialization Failure (CRITICAL)

**Location:** `metal_context.mm:46-137` (`initialize()` method)

**Problem:** When initialization fails midway, previously allocated resources are not cleaned up. Each early return leaks all resources allocated before the failure point.

| Failure Point | Leaked Resources |
|---------------|------------------|
| Line 62-65 (queue creation fails) | `device` |
| Line 69-72 (metalView creation fails) | `device`, `queue` |
| Line 76-79 (layer retrieval fails) | `device`, `queue`, `metalView` |
| Line 129-132 (Skia context fails) | `device`, `queue`, `metalView`, `metalLayer` |

**Impact:** Memory leak every time initialization fails. In scenarios with retries or fallback paths, this accumulates.

**Fix Required:** Call `destroy()` before returning `false`, or refactor to use RAII pattern:

```cpp
bool MetalContext::initialize(SDL_Window* window) {
    if (!window) {
        fprintf(stderr, "[Metal] Error: NULL window\n");
        return false;
    }

    // Create Metal device
    impl_->device = MTLCreateSystemDefaultDevice();
    if (!impl_->device) {
        fprintf(stderr, "[Metal] Error: Failed to create Metal device\n");
        return false;
    }

    // Create command queue
    impl_->queue = [impl_->device newCommandQueue];
    if (!impl_->queue) {
        fprintf(stderr, "[Metal] Error: Failed to create command queue\n");
        destroy();  // <-- ADD THIS
        return false;
    }
    // ... continue pattern for all subsequent allocations
}
```

---

#### 2. Drawable Not Released on Diagnostic Path (MODERATE)

**Location:** `metal_context.mm:231-237`

**Problem:** In the error diagnostic block, `nextDrawable` is called but the acquired drawable is never presented. While ARC releases the reference, Metal's drawable pool has limited capacity (2-3 drawables). Acquiring without presenting exhausts the pool.

```cpp
id<CAMetalDrawable> testDrawable = [impl_->metalLayer nextDrawable];
if (!testDrawable) {
    // ...
} else {
    fprintf(stderr, "[Metal] createSurface: Skia WrapCAMetalLayer failed...\n");
    // testDrawable goes out of scope - released by ARC but never presented
}
```

**Impact:** If this diagnostic code runs frequently, it can exhaust the drawable pool and cause subsequent `nextDrawable` calls to timeout or fail.

**Fix Required:** Remove the diagnostic drawable acquisition or explicitly present/discard it:

```cpp
// Option 1: Remove diagnostic acquisition entirely (recommended)
if (!surface) {
    fprintf(stderr, "[Metal] createSurface: Skia WrapCAMetalLayer failed\n");
}

// Option 2: If diagnostic is needed, don't acquire a drawable
if (!surface) {
    fprintf(stderr, "[Metal] createSurface: Skia WrapCAMetalLayer failed "
                    "(layer size: %.0fx%.0f)\n", actualSize.width, actualSize.height);
}
```

---

### Warnings

#### 1. NSScreen mainScreen May Return Nil (LOW RISK)

**Location:** `metal_context.mm:86` and `metal_context.mm:117`

```cpp
impl_->metalLayer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
// and
float scale = [[NSScreen mainScreen] backingScaleFactor];
```

**Problem:** `[NSScreen mainScreen]` can return `nil` in headless environments or during system startup. Messaging `nil` returns 0, which sets `contentsScale` to 0.0 (invalid).

**Impact:** Extremely rare in practice (requires headless Mac server or unusual system state). Would cause rendering artifacts.

**Recommendation:** Add defensive check:

```cpp
NSScreen* screen = [NSScreen mainScreen];
CGFloat scale = screen ? [screen backingScaleFactor] : 2.0;  // Default to Retina
```

---

#### 2. No Width/Height Validation in updateDrawableSize (LOW RISK)

**Location:** `metal_context.mm:161-165`

```cpp
void MetalContext::updateDrawableSize(int width, int height) {
    if (!impl_->initialized || !impl_->metalLayer) return;
    impl_->metalLayer.drawableSize = CGSizeMake(width, height);
}
```

**Problem:** Zero or negative dimensions are passed directly to Metal without validation.

**Impact:** Could cause Metal validation layer errors or undefined behavior.

**Recommendation:** Add validation:

```cpp
if (width <= 0 || height <= 0) return;
```

---

#### 3. Thread Safety Not Guaranteed (DESIGN NOTE)

**Location:** All methods in `MetalContext`

**Problem:** No mutex or synchronization primitives. Calling `destroy()` from one thread while `createSurface()` runs on another causes undefined behavior.

**Impact:** Race conditions if used incorrectly by caller.

**Recommendation:** Either:
1. Document that `MetalContext` must only be used from the main thread, OR
2. Add `std::mutex` protection for `impl_` access

---

### Recommendations

#### 1. Verify ARC is Enabled (CORRECTNESS CHECK)

The code assumes Objective-C ARC (Automatic Reference Counting) is enabled. Verify the build flags include `-fobjc-arc`. Without ARC, every `nil` assignment leaks.

**Verification command:**
```bash
grep -r "fobjc-arc" Makefile scripts/*.sh
```

---

#### 2. Skia Context Retain Semantics Are Correct

**Location:** `metal_context.mm:125-126`

```cpp
backendContext.fDevice.retain((__bridge void*)impl_->device);
backendContext.fQueue.retain((__bridge void*)impl_->queue);
```

**Analysis:** This is CORRECT. Skia's `sk_cfp` wrapper uses explicit retain/release. The `.retain()` call increments the reference count, which Skia balances when `GrDirectContext` is destroyed. Since `impl_->skiaContext.reset()` is called before releasing `device` and `queue` in `destroy()`, the ownership chain is correct.

---

#### 3. SDL Metal View Ownership Is Correct

**Analysis:** `SDL_Metal_CreateView` returns a view owned by SDL. `SDL_Metal_DestroyView` properly releases it. The `__bridge` cast to get the layer does NOT transfer ownership, which is correct.

---

#### 4. Destroy Order Is Correct

**Analysis:** In `destroy()`:
1. Skia context is released first (frees GPU resources)
2. SDL Metal view is destroyed (frees CAMetalLayer)
3. Metal objects are released

This order ensures no dangling references.

---

#### 5. Consider Adding Drawable Lifetime Documentation

The `GrMTLHandle* outDrawable` parameter in `createSurface()` returns a handle that MUST be passed to `presentDrawable()` after rendering. If the caller forgets to present, the drawable is held indefinitely (until Metal's internal timeout, default 1 second with `allowsNextDrawableTimeout = YES`).

**Recommendation:** Add documentation in header:

```cpp
/**
 * @param outDrawable Receives the drawable handle. MUST be presented via
 *                    presentDrawable() after rendering, otherwise the drawable
 *                    pool will be exhausted and future createSurface() calls
 *                    will fail or timeout.
 */
```

---

### Summary

| Severity | Count | Items |
|----------|-------|-------|
| Critical | 1 | Resource leak on partial init failure |
| Moderate | 1 | Diagnostic drawable not released |
| Low | 3 | NSScreen nil, size validation, thread safety |

**Overall Assessment:** The code is well-structured with correct ARC bridge casts and proper destruction order. The critical issue (partial init resource leak) should be fixed before production use. The diagnostic drawable issue is a latent bug that only manifests under specific failure conditions.
