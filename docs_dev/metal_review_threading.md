# Thread Safety Review: Metal GPU Backend Integration

**Generated:** 2026-01-18
**Files Reviewed:**
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated.cpp`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.mm`
- `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/metal_context.h`

## Executive Summary

The Metal GPU backend integration is **largely thread-safe by design** because Metal mode disables the `ThreadedRenderer` and performs all rendering on the main thread. However, several potential race conditions and lifecycle issues exist that could cause crashes or undefined behavior under specific conditions.

**Overall Risk Level:** MEDIUM

---

## Critical Issues

### 1. VSync Toggle Race Condition (MEDIUM-HIGH)

**Location:** `metal_context.mm:261-273`, `svg_player_animated.cpp:2981-2985`

**Problem:** The `setVSyncEnabled()` function modifies `impl_->metalLayer.displaySyncEnabled` without any synchronization. While the main loop is single-threaded, this property can be toggled via keyboard ('V' key) while `createSurface()` is potentially accessing the same `CAMetalLayer`.

```cpp
// In setVSyncEnabled():
impl_->metalLayer.displaySyncEnabled = enabled;  // No lock

// In createSurface():
impl_->metalLayer.drawableSize = CGSizeMake(width, height);  // Also no lock
```

**Impact:** Could cause undefined behavior if VSync is toggled during the frame acquisition window. CAMetalLayer property access is generally main-thread-safe, but rapid toggling during `nextDrawable` acquisition could cause frame drops or visual artifacts.

**Evidence:**
- `metal_context.mm` line 261-273: `setVSyncEnabled` modifies layer without lock
- `metal_context.mm` line 184: `createSurface` also modifies layer

### 2. isInitialized() Not Thread-Safe (LOW-MEDIUM)

**Location:** `metal_context.mm:157-159`

**Problem:** The `isInitialized()` function reads `impl_->initialized` without synchronization:

```cpp
bool MetalContext::isInitialized() const {
    return impl_->initialized;  // No atomic, no mutex
}
```

While currently only called from main thread, any future use from another thread would cause a data race.

**Impact:** Currently minimal (single-threaded access), but a latent bug waiting to happen.

### 3. Missing Explicit Metal Context Cleanup Order (LOW)

**Location:** `svg_player_animated.cpp:4861-4864`

**Problem:** The cleanup sequence destroys SDL resources before the `metalContext` unique_ptr is implicitly destroyed:

```cpp
SDL_DestroyTexture(texture);
SDL_DestroyRenderer(renderer);
SDL_DestroyWindow(window);  // Window destroyed HERE
SDL_Quit();
// metalContext destroyed implicitly when main() exits (after SDL_Quit!)
```

The `metalContext` holds references to the SDL window via `SDL_MetalView`. Destroying the window before the Metal context could cause issues.

**Impact:** May cause warnings or undefined behavior on shutdown, especially if Metal has pending command buffers.

---

## Warnings

### 1. No Protection Against Post-Destroy Access

**Location:** `metal_context.mm` (entire class)

**Problem:** After `MetalContext::destroy()` is called, `impl_->initialized` is set to `false`, but there's no prevention of concurrent calls during destruction:

```cpp
void MetalContext::destroy() {
    if (!impl_->initialized) return;  // Not atomic
    
    impl_->skiaContext.reset();
    // ... other cleanup ...
    impl_->initialized = false;  // Set after cleanup
}
```

If `destroy()` is called while another method is checking `isInitialized()`, there's a brief window where use-after-free could occur.

**Impact:** Low, but could cause crashes during rapid shutdown scenarios.

### 2. Drawable Exhaustion Handling

**Location:** `metal_context.mm:171-241`, `svg_player_animated.cpp:4067-4118`

**Observation:** The code correctly handles drawable exhaustion (when `nextDrawable` returns nil), but logs to stderr which could be noisy under load:

```cpp
if (!surface) {
    id<CAMetalDrawable> testDrawable = [impl_->metalLayer nextDrawable];
    if (!testDrawable) {
        fprintf(stderr, "[Metal] createSurface: CAMetalLayer.nextDrawable returned nil...\n");
    }
}
```

**Impact:** Performance degradation due to logging, but no crash risk.

### 3. Global State Sharing Between Browser and Animation Modes

**Location:** `svg_player_animated.cpp` (various)

**Observation:** The `surface` and `canvas` variables are shared between browser mode and animation mode:

```cpp
sk_sp<SkSurface> surface;
SkCanvas* canvas = nullptr;
```

Both modes recreate the surface per-frame in Metal mode, which is correct, but the shared variable pattern could lead to subtle bugs if code paths are changed.

**Impact:** Low currently, but fragile design.

### 4. CAMetalLayer Thread Safety Assumption

**Location:** `metal_context.mm:83-105`

**Observation:** Multiple CAMetalLayer properties are modified during initialization without explicit main thread dispatch:

```cpp
impl_->metalLayer.device = impl_->device;
impl_->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
impl_->metalLayer.framebufferOnly = NO;
impl_->metalLayer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
```

CAMetalLayer is documented to be main-thread-only, and the code does run on main thread, but there's no assertion to catch future violations.

**Impact:** None currently, but could cause hard-to-debug issues if called from background thread.

---

## Positive Findings

### 1. Correct Single-Threaded Metal Access (GOOD)

The design explicitly disables `ThreadedRenderer` when Metal is enabled:

```cpp
if (!useMetalBackend) {
    threadedRendererPtr = std::make_unique<ThreadedRenderer>();
    // ... start threaded rendering
} else {
    std::cout << "[Metal] GPU-accelerated rendering enabled - ThreadedRenderer disabled" << std::endl;
}
```

This ensures all Metal API calls happen on the main thread.

### 2. Proper Drawable Lifecycle Management (GOOD)

The drawable is correctly cleared after presentation:

```cpp
metalContext->presentDrawable(metalDrawable);
// ...
metalDrawable = nullptr;  // Clear for next frame
```

And a fresh drawable is acquired each frame:

```cpp
metalDrawable = nullptr;  // Clear previous drawable
surface = metalContext->createSurface(renderWidth, renderHeight, &metalDrawable);
```

### 3. Surface Validity Checks (GOOD)

The code properly validates surface and drawable before use:

```cpp
if (surface && metalDrawable) {
    canvas = surface->getCanvas();
    // ... render
} else {
    std::cerr << "[Metal] Failed to acquire drawable this frame" << std::endl;
}
```

### 4. Triple Buffering Configuration (GOOD)

The Metal layer is configured with triple buffering for smoother frame pacing:

```cpp
impl_->metalLayer.maximumDrawableCount = 3;
```

### 5. Proper Flush Before Presentation (GOOD)

Skia commands are flushed before presenting:

```cpp
impl_->skiaContext->flushAndSubmit();
id<MTLCommandBuffer> commandBuffer = [impl_->queue commandBuffer];
[commandBuffer presentDrawable:...];
[commandBuffer commit];
```

---

## Recommendations

### High Priority

1. **Add explicit Metal context cleanup before SDL cleanup:**
   ```cpp
   // BEFORE SDL cleanup:
   #ifdef __APPLE__
   if (metalContext) {
       metalContext->destroy();
       metalContext.reset();
   }
   #endif
   
   SDL_DestroyTexture(texture);
   SDL_DestroyRenderer(renderer);
   SDL_DestroyWindow(window);
   SDL_Quit();
   ```

2. **Make `initialized` flag atomic:**
   ```cpp
   struct MetalContextImpl {
       std::atomic<bool> initialized{false};
       // ...
   };
   ```

### Medium Priority

3. **Add mutex for VSync toggle during active rendering:**
   ```cpp
   // In MetalContextImpl:
   std::mutex configMutex;
   
   // In setVSyncEnabled:
   std::lock_guard<std::mutex> lock(impl_->configMutex);
   impl_->metalLayer.displaySyncEnabled = enabled;
   ```

4. **Add main thread assertions in debug builds:**
   ```objc
   #ifdef DEBUG
   NSAssert([NSThread isMainThread], @"Metal context must be used from main thread");
   #endif
   ```

### Low Priority

5. **Reduce stderr logging for drawable exhaustion:**
   - Rate-limit the logging
   - Or use debug-only logging

6. **Consider separating browser and animation surface management:**
   - Use distinct variables or a state machine pattern

---

## Investigation Trail

| Step | Action | Finding |
|------|--------|---------|
| 1 | Read metal_context.mm | No internal synchronization in MetalContext class |
| 2 | Search for useMetalBackend pattern | Metal mode disables ThreadedRenderer (good) |
| 3 | Search for mutex/atomic patterns | Main thread mutexes exist for browser mode, none in Metal code |
| 4 | Check VSync toggle code path | setVSyncEnabled modifies layer without lock |
| 5 | Check drawable lifecycle | Properly cleared after presentation |
| 6 | Check cleanup order | SDL destroyed before metalContext implicit destruction |
| 7 | Check isInitialized | Non-atomic read of initialized flag |

---

## Confidence Level

**High** - The analysis is based on direct code inspection of all Metal-related code paths. The main threading model (single-threaded Metal access) is sound. The issues identified are real but have limited practical impact in the current single-threaded design.

---

## Alternative Hypotheses

1. **Could command buffer conflicts occur?** - NO. Each frame creates a fresh command buffer, and `flushAndSubmit()` waits for completion.

2. **Could browser mode cause Metal threading issues?** - NO. Browser mode also runs on main thread when Metal is enabled.

3. **Could signal handlers cause Metal issues?** - POSSIBLE. Signal handlers during Metal operations could cause undefined state, but `SA_RESTART` flag mitigates this.
