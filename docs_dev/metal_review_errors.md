# Metal GPU Backend Error Handling Review

**Generated:** 2025-01-18
**Files Reviewed:**
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated.cpp`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.mm`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.h`

---

## Error Handling Review

### Critical Issues

#### 1. Metal Context Not Explicitly Destroyed Before SDL_Quit (CRASH RISK)

**Location:** `svg_player_animated.cpp:4861-4864`

**Problem:** The cleanup sequence calls `SDL_Quit()` BEFORE the `metalContext` unique_ptr is destroyed:

```cpp
SDL_DestroyTexture(texture);
SDL_DestroyRenderer(renderer);
SDL_DestroyWindow(window);
SDL_Quit();
// metalContext destructor called AFTER SDL_Quit() when it goes out of scope at line 4867
return 0;
```

The `MetalContext::destroy()` method (`metal_context.mm:139-155`) calls `SDL_Metal_DestroyView()` which requires SDL to still be active. Calling this after `SDL_Quit()` results in undefined behavior.

**Severity:** CRITICAL - Can cause crash on exit or resource leak.

**Fix:** Explicitly destroy metalContext BEFORE calling `SDL_Quit()`:
```cpp
#ifdef __APPLE__
metalContext.reset();  // Destroy Metal context first
#endif
SDL_DestroyTexture(texture);
SDL_DestroyRenderer(renderer);
SDL_DestroyWindow(window);
SDL_Quit();
```

---

#### 2. No Recovery From Metal Surface Creation Failure in Main Render Loop

**Location:** `svg_player_animated.cpp:4073-4117`

**Problem:** When `createSurface()` fails during rendering, the frame is simply skipped with a warning. If Metal continues to fail (e.g., GPU resource exhaustion), the player shows a black screen indefinitely without attempting to recover.

```cpp
if (surface && metalDrawable) {
    // ... render ...
} else {
    // Failed to get drawable - Metal may be busy, skip this frame
    if (!g_jsonOutput) {
        std::cerr << "[Metal] Failed to acquire drawable this frame..." << std::endl;
    }
    // NO RECOVERY PATH - just continues with blank screen
}
```

**Severity:** HIGH - Player becomes unusable if Metal fails repeatedly.

**Missing:** 
- Counter for consecutive failures
- Fallback to CPU rendering after N failures
- Graceful degradation mechanism

---

#### 3. Exception Handling Absent for Objective-C Metal APIs

**Location:** `metal_context.mm` (entire file)

**Problem:** The Metal context uses Objective-C APIs that can throw `NSException` on failure (e.g., invalid drawable, command buffer errors, device disconnection). No `@try/@catch` blocks wrap these calls:

```objc
// metal_context.mm:250-252 - No exception handling
id<MTLCommandBuffer> commandBuffer = [impl_->queue commandBuffer];
[commandBuffer presentDrawable:(__bridge id<CAMetalDrawable>)drawable];
[commandBuffer commit];
```

If `commandBuffer` is nil or the drawable is invalid, this can crash without recovery.

**Severity:** HIGH - Unhandled exceptions cause hard crash.

---

### Warnings

#### 4. Drawable Timeout Handling Is Silent

**Location:** `metal_context.mm:90-92`

**Problem:** The code enables drawable timeout (`allowsNextDrawableTimeout = YES`) but doesn't explicitly handle the timeout case. When `nextDrawable` times out, it returns `nil`, but this is only detected indirectly when `WrapCAMetalLayer` fails.

```objc
if (@available(macOS 10.15.4, *)) {
    impl_->metalLayer.allowsNextDrawableTimeout = YES;
}
```

**Impact:** Medium - Timeouts are handled by returning nullptr, but no metrics or warnings about drawable exhaustion.

---

#### 5. Signal Handler Re-installation May Not Be Atomic

**Location:** `svg_player_animated.cpp:2215-2217`

**Problem:** Signal handlers are re-installed after Metal context creation, but there's a window where signals could be caught by Metal/SDL handlers:

```cpp
metalContext = svgplayer::createMetalContext(window);
// WINDOW: Signals during Metal init go to Metal/SDL handlers
installSignalHandlers();  // Re-install our handlers
```

**Impact:** Low - Unlikely but could cause missed shutdown signals during Metal initialization.

---

#### 6. GrDirectContext Stale Check Missing

**Location:** `metal_context.mm:236`

**Problem:** The error diagnostic in `createSurface()` mentions "Skia context may be stale" but there's no mechanism to detect or recover from a stale Skia GPU context:

```cpp
fprintf(stderr, "[Metal] createSurface: Skia WrapCAMetalLayer failed (drawable available, Skia context may be stale)\n");
// No attempt to recreate skiaContext
```

**Impact:** Medium - Stale GPU context requires app restart to fix.

---

#### 7. No Metrics for Metal Health Monitoring

**Location:** Throughout Metal code paths

**Problem:** No counters or metrics track:
- Consecutive drawable acquisition failures
- Average drawable wait time
- GPU command buffer submission errors
- VSync miss rate

**Impact:** Medium - Difficult to diagnose intermittent Metal issues in production.

---

### Recommendations

#### R1: Add Explicit Metal Context Cleanup (Priority: HIGH)

```cpp
// Before SDL_Quit() in cleanup section (~line 4860):
#ifdef __APPLE__
if (metalContext) {
    std::cout << "Destroying Metal context..." << std::endl;
    metalContext.reset();
    std::cout << "Metal context destroyed." << std::endl;
}
#endif
```

#### R2: Implement Metal-to-CPU Fallback (Priority: HIGH)

Add consecutive failure tracking and fallback:

```cpp
static int consecutiveMetalFailures = 0;
const int MAX_METAL_FAILURES = 10;

if (surface && metalDrawable) {
    consecutiveMetalFailures = 0;
    // ... render ...
} else {
    consecutiveMetalFailures++;
    if (consecutiveMetalFailures >= MAX_METAL_FAILURES) {
        std::cerr << "[Metal] Too many failures, falling back to CPU rendering" << std::endl;
        useMetalBackend = false;
        metalContext.reset();
        // Initialize CPU rendering path
        if (!createSurface(renderWidth, renderHeight)) {
            std::cerr << "FATAL: Both Metal and CPU rendering failed" << std::endl;
            running = false;
        }
    }
}
```

#### R3: Add Objective-C Exception Handling (Priority: HIGH)

Wrap Metal API calls in `metal_context.mm`:

```objc
void MetalContext::presentDrawable(GrMTLHandle drawable) {
    if (!impl_->initialized || !drawable) return;
    
    @try {
        impl_->skiaContext->flushAndSubmit();
        id<MTLCommandBuffer> commandBuffer = [impl_->queue commandBuffer];
        if (!commandBuffer) {
            fprintf(stderr, "[Metal] Failed to create command buffer\n");
            return;
        }
        [commandBuffer presentDrawable:(__bridge id<CAMetalDrawable>)drawable];
        [commandBuffer commit];
    } @catch (NSException *exception) {
        fprintf(stderr, "[Metal] Exception during present: %s - %s\n",
                [[exception name] UTF8String],
                [[exception reason] UTF8String]);
    }
}
```

#### R4: Add Drawable Timeout Metrics (Priority: MEDIUM)

Track drawable acquisition performance:

```cpp
struct MetalMetrics {
    std::atomic<uint64_t> drawableSuccesses{0};
    std::atomic<uint64_t> drawableTimeouts{0};
    std::atomic<double> lastDrawableWaitMs{0};
};
```

#### R5: Validate Metal Device Periodically (Priority: LOW)

Check for GPU disconnection (e.g., eGPU removal):

```objc
bool MetalContext::isDeviceValid() const {
    if (!impl_->initialized || !impl_->device) return false;
    // Metal devices can become invalid when eGPU is disconnected
    // Check by attempting a simple operation
    return impl_->device != nil;
}
```

---

## Summary Table

| Issue | Severity | Status | Fix Complexity |
|-------|----------|--------|----------------|
| Metal context destroyed after SDL_Quit | CRITICAL | Not Fixed | Easy |
| No CPU fallback on Metal failure | HIGH | Not Fixed | Medium |
| No Obj-C exception handling | HIGH | Not Fixed | Easy |
| Silent drawable timeout | MEDIUM | Not Fixed | Easy |
| Non-atomic signal handler reinstall | LOW | Acceptable | N/A |
| Stale GPU context not detected | MEDIUM | Not Fixed | Medium |
| No Metal health metrics | MEDIUM | Not Fixed | Medium |

---

## Files Modified Summary

If all recommendations are implemented:

- `metal_context.mm` - Add @try/@catch, metrics, device validation
- `svg_player_animated.cpp` - Add explicit cleanup order, CPU fallback logic

---

## Test Cases for Verification

1. **Ctrl+C during Metal rendering** - Should exit cleanly without crash
2. **Resize window rapidly** - Drawable exhaustion should recover
3. **eGPU disconnect** - Should fallback to CPU (if implemented)
4. **Long-running animation** - No memory leak from drawables
5. **Benchmark mode exit** - Metal resources fully released
