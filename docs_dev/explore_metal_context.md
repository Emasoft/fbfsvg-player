# Metal Context Implementation Exploration

**Generated:** 2026-01-19  
**Files Analyzed:**
- `src/metal_context.h` (interface)
- `src/metal_context.mm` (implementation)
- `src/svg_player_animated.cpp` (integration)

---

## 1. Metal Context Interface (`src/metal_context.h`)

### Public API

```cpp
namespace svgplayer {
class MetalContext {
public:
    bool initialize(SDL_Window* window);
    void destroy();
    bool isInitialized() const;
    
    // Surface management
    void updateDrawableSize(int width, int height);
    sk_sp<SkSurface> createSurface(int width, int height, GrMTLHandle* outDrawable);
    void presentDrawable(GrMTLHandle drawable);
    void flush();
    
    // GPU context access
    GrDirectContext* getGrContext() const;
    
    // Display settings
    void setVSyncEnabled(bool enabled);
    bool isVSyncEnabled() const;
    void setMaximumDrawableCount(int count);
    int getMaximumDrawableCount() const;
};

// Factory function
std::unique_ptr<MetalContext> createMetalContext(SDL_Window* window);
}
```

### Key Design Decisions

1. **Pimpl idiom**: Implementation details hidden in `MetalContextImpl*` (line 127)
2. **Non-copyable**: Deleted copy constructor/assignment (lines 40-41)
3. **RAII**: Constructor/destructor manage lifetime
4. **Per-frame surfaces**: `createSurface()` acquires a fresh drawable each frame

---

## 2. Metal Context Implementation (`src/metal_context.mm`)

### Initialization Flow (`initialize()`, lines 46-144)

```
1. Create MTLDevice (system default GPU)
   └─> Line 53: MTLCreateSystemDefaultDevice()

2. Create MTLCommandQueue
   └─> Line 61: [device newCommandQueue]

3. Create SDL Metal view
   └─> Line 69: SDL_Metal_CreateView(window)
   └─> Line 77: SDL_Metal_GetLayer(metalView)
   └─> Returns: CAMetalLayer

4. Configure CAMetalLayer (lines 86-126)
   - Pixel format: MTLPixelFormatBGRA8Unorm
   - framebufferOnly: NO (Skia needs readPixels)
   - contentsScale: Retina/HiDPI scaling
   - maximumDrawableCount: 3 (triple buffering)
   - displaySyncEnabled: YES (VSync on)
   - presentsWithTransaction: NO (async presentation)

5. Create Skia GrDirectContext (lines 129-134)
   └─> GrDirectContexts::MakeMetal(backendContext)
```

### Surface Creation (`createSurface()`, lines 181-246)

**Critical: Per-Frame Surface Pattern**

```cpp
// Metal surfaces are single-use - acquire fresh drawable each frame
surface = SkSurfaces::WrapCAMetalLayer(
    grContext,
    metalLayer,
    kTopLeft_GrSurfaceOrigin,
    1,  // sample count (no MSAA)
    kBGRA_8888_SkColorType,
    nullptr,  // colorSpace (sRGB default)
    nullptr,  // surfaceProps
    &tempDrawable);  // OUT: drawable handle
```

**Why per-frame surfaces?**
- CAMetalLayer has a limited drawable pool (2-3 drawables)
- Holding drawables across frames exhausts the pool → freezing
- Solution: Create surface, render, present, destroy, repeat

**Lazy proxy instantiation** (lines 218-225):
- `WrapCAMetalLayer` returns a lazy proxy
- Drawable is only acquired when canvas is first accessed
- Force instantiation: `canvas->clear()` + `flushAndSubmit()`

### Presentation (`presentDrawable()`, lines 248-268)

```cpp
void presentDrawable(GrMTLHandle drawable) {
    // 1. Flush GPU work
    skiaContext->flushAndSubmit();
    
    // 2. Create command buffer
    commandBuffer = [queue commandBuffer];
    
    // 3. Schedule presentation
    [commandBuffer presentDrawable:(__bridge id<CAMetalDrawable>)drawable];
    
    // 4. Commit to GPU
    [commandBuffer commit];
}
```

---

## 3. Integration in Main Player (`src/svg_player_animated.cpp`)

### Flag Parsing (lines 1972-1974)

```cpp
#ifdef __APPLE__
} else if (strcmp(argv[i], "--metal") == 0) {
    useMetalBackend = true;
#endif
```

**Compile-time guard:** Metal only available on macOS (`__APPLE__`)

### Metal Context Creation (lines 2334-2355)

```cpp
#ifdef __APPLE__
std::unique_ptr<svgplayer::MetalContext> metalContext;
GrMTLHandle metalDrawable = nullptr;

if (useMetalBackend) {
    metalContext = svgplayer::createMetalContext(window);
    if (metalContext) {
        installSignalHandlers();  // Re-install after Metal setup
        std::cout << "[Metal] GPU backend enabled" << std::endl;
    } else {
        // FALLBACK TO CPU
        std::cerr << "[Metal] Failed to initialize, falling back to CPU" << std::endl;
        useMetalBackend = false;
    }
}
#endif
```

**Fallback strategy:** If Metal init fails, set `useMetalBackend = false` → CPU path

### Surface Creation Lambda (lines 2503-2522)

```cpp
auto createSurface = [&](int w, int h) -> bool {
#ifdef __APPLE__
    if (useMetalBackend && metalContext && metalContext->isInitialized()) {
        // Metal GPU-backed surface
        metalContext->updateDrawableSize(w, h);
        surface = metalContext->createSurface(w, h, &metalDrawable);
        if (surface) return true;
        
        // FALLBACK: Metal surface creation failed
        std::cerr << "[Metal] Failed to create GPU surface, falling back to CPU" << std::endl;
        useMetalBackend = false;
        // Fall through to CPU path
    }
#endif
    // CPU raster surface (fallback)
    SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(w, h);
    surface = SkSurfaces::Raster(imageInfo);
    return surface != nullptr;
};
```

**Multi-level fallback:**
1. Try Metal GPU surface
2. If fails → set `useMetalBackend = false`
3. Fall through to CPU raster surface

### Render Loop - Metal Path (lines 4244-4319)

```cpp
#ifdef __APPLE__
if (useMetalBackend && metalContext && metalContext->isInitialized()) {
    // === METAL GPU RENDERING PATH ===
    
    // 1. Clear previous drawable
    metalDrawable = nullptr;
    
    // 2. Acquire fresh surface + drawable
    surface = metalContext->createSurface(renderWidth, renderHeight, &metalDrawable);
    
    if (surface && metalDrawable) {
        // 3. Get canvas and clear
        canvas = surface->getCanvas();
        canvas->clear(SK_ColorBLACK);
        
        // 4. Render SVG or image sequence
        if (isImageSequence) {
            // Parse and render separate SVG file per frame
            ...
        } else {
            // Apply SMIL animations to single DOM
            for (const auto& anim : animations) {
                anim.apply(svgDom, animTime);
            }
            svgDom->render(canvas);
        }
        
        gotNewFrame = true;
        framesDelivered++;
    } else {
        // Failed to acquire drawable - skip this frame
        std::cerr << "[Metal] Failed to acquire drawable this frame" << std::endl;
    }
}
#endif
```

**Key difference from CPU path:**
- CPU: One persistent surface, reused across frames
- Metal: Fresh surface acquired each frame from CAMetalLayer

### Presentation Path (lines 4745-4803)

```cpp
if (useMetalBackend && metalContext && metalContext->isInitialized() && metalDrawable) {
    // === METAL GPU PRESENTATION PATH ===
    
    // 1. Periodic black screen detection (every 60 frames)
    if (frameCount % 60 == 0 && surface) {
        // GPU→CPU readPixels (expensive, so done rarely)
        surface->readPixels(info, checkPixels.data(), ...);
        int nonBlackPixels = countNonBlackPixels(...);
        if (nonBlackPixels < 10) {
            std::cerr << "[Metal WARNING] Black screen detected!" << std::endl;
        }
    }
    
    // 2. Present to screen
    metalContext->presentDrawable(metalDrawable);
    
    // 3. Clear drawable for next frame
    metalDrawable = nullptr;
}
```

**No CPU→GPU copy:** Data is already in GPU memory (Metal texture)

---

## 4. GPU/CPU Switching Mechanism

### Compile-Time Switching

All Metal code is guarded by `#ifdef __APPLE__`:

```cpp
#ifdef __APPLE__
    // Metal-specific code
#endif
```

**Purpose:** Metal is only available on macOS/iOS, not Linux/Windows

### Runtime Switching

**Primary flag:** `bool useMetalBackend`

**Initialization:** Set by `--metal` command-line flag (default: `false`)

**Runtime fallback points:**

| Location | Condition | Action |
|----------|-----------|--------|
| Line 2351 | `metalContext == nullptr` | Set `useMetalBackend = false` |
| Line 2513 | `createSurface()` fails | Set `useMetalBackend = false` |

**Switch logic pattern:**

```cpp
#ifdef __APPLE__
if (useMetalBackend && metalContext && metalContext->isInitialized()) {
    // Metal GPU path
} else {
#endif
    // CPU raster path
#ifdef __APPLE__
}
#endif
```

### Threading Model Differences

| Mode | Render Thread | Notes |
|------|---------------|-------|
| CPU | ThreadedRenderer (worker thread) | CPU→GPU texture upload each frame |
| Metal | Main thread | GPU→GPU rendering, no upload needed |

**Why Metal disables ThreadedRenderer:**
- GPU rendering is fast enough (no CPU bottleneck)
- Metal requires main-thread presentation
- Line 2582-2613: Skip ThreadedRenderer creation if `useMetalBackend == true`

---

## 5. Key Implementation Patterns

### Pattern 1: Triple Buffering

```cpp
// Line 103: Reduce frame latency
metalLayer.maximumDrawableCount = 3;
```

**Default:** 2 drawables (double buffering)  
**Metal player:** 3 drawables (triple buffering)  
**Benefit:** Smoother frame pacing, lower latency

### Pattern 2: VSync Control

```cpp
// Line 111: Enable VSync by default
if (@available(macOS 10.13, *)) {
    metalLayer.displaySyncEnabled = YES;
}
```

**Runtime toggle:** `metalContext->setVSyncEnabled(bool)`  
**Effect:** When disabled, frames present immediately (may tear)

### Pattern 3: Lazy Proxy Instantiation

```cpp
// Line 207: WrapCAMetalLayer returns lazy proxy
surface = SkSurfaces::WrapCAMetalLayer(..., &tempDrawable);

// Line 220: Force instantiation
canvas->clear(SK_ColorTRANSPARENT);
skiaContext->flushAndSubmit();
```

**Why:** Skia defers drawable acquisition until canvas is accessed  
**Solution:** Trigger instantiation immediately to get drawable handle

### Pattern 4: Drawable Pool Management

```cpp
// Line 2549: Clear drawable reference
metalDrawable = nullptr;

// Line 4250: Acquire fresh drawable
surface = metalContext->createSurface(w, h, &metalDrawable);
```

**Why:** CAMetalLayer has limited drawable pool (2-3)  
**Critical:** Release drawables promptly to avoid starvation

---

## 6. Conditional Compilation Summary

### Metal-Specific Headers

```cpp
// Lines 9-13 in metal_context.mm
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL_metal.h>
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
```

**Guard:** Only compiled on macOS (Objective-C++)

### Metal-Guarded Code Blocks in Main Player

| Lines | Purpose |
|-------|---------|
| 73-75 | Include metal_context.h |
| 1971-1975 | Parse --metal flag |
| 2330-2355 | Create MetalContext |
| 2359-2369 | Get drawable size (Metal vs CPU) |
| 2504-2516 | Metal surface creation |
| 2526-2542 | Skip initial surface for Metal |
| 4243-4319 | Metal rendering path |
| 4745-4803 | Metal presentation path |

---

## 7. Current Limitations & Edge Cases

### Limitation 1: macOS Only

**Current:** Metal backend is macOS-exclusive  
**Why:** Conditional compilation `#ifdef __APPLE__`  
**Future:** Could extend to iOS with `#if TARGET_OS_IPHONE`

### Limitation 2: No Threaded Rendering in Metal Mode

**Current:** Metal disables ThreadedRenderer (line 2582-2613)  
**Why:** Metal presentation requires main thread  
**Tradeoff:** GPU acceleration compensates for single-threaded rendering

### Limitation 3: Screenshot Capture Different in Metal Mode

**CPU mode (line 3260):** `peekPixels()` (zero-copy)  
**Metal mode (line 3263-3280):** `readPixels()` (GPU→CPU transfer)

```cpp
if (useMetalBackend) {
    metalContext->flush();  // Wait for GPU work
    surface->readPixels(info, pixels.data(), ...);  // Copy GPU→CPU
}
```

**Impact:** Screenshots slower in Metal mode (GPU readback)

### Edge Case 1: Drawable Acquisition Failure

**Scenario:** CAMetalLayer runs out of drawables (all in use by compositor)  
**Detection:** Line 4312-4318  
**Handling:** Skip frame, try again next frame

```cpp
if (!surface || !metalDrawable) {
    std::cerr << "[Metal] Failed to acquire drawable this frame" << std::endl;
    // Skip this frame, don't increment framesDelivered
}
```

### Edge Case 2: Black Screen Detection

**Scenario:** Metal renders but produces black output  
**Detection:** Line 4754-4778 (every 60 frames)  
**Method:** Read pixels from GPU, count non-black pixels

```cpp
if (frameCount % 60 == 0) {
    surface->readPixels(...);
    int nonBlackPixels = countNonBlackPixels(...);
    if (nonBlackPixels < 10) {
        std::cerr << "[Metal WARNING] Black screen detected!" << std::endl;
    }
}
```

**Frequency:** Every 60 frames (1 second at 60fps) to minimize GPU→CPU transfer overhead

---

## 8. Performance Characteristics

### Metal Path

| Phase | Time | Notes |
|-------|------|-------|
| Surface creation | ~1ms | Acquires drawable from CAMetalLayer pool |
| SVG rendering | GPU-accelerated | Skia draws to GPU texture |
| Presentation | ~16ms @ 60Hz | VSync-limited (displaySyncEnabled = YES) |
| Copy time | 0ms | No CPU→GPU copy needed |

### CPU Path (for comparison)

| Phase | Time | Notes |
|-------|------|-------|
| Surface creation | ~0.1ms | Raster surface in RAM |
| SVG rendering | CPU-bound | Single-threaded or ThreadedRenderer |
| Copy to texture | 1-3ms | CPU→GPU upload via SDL_UpdateTexture |
| Presentation | ~16ms @ 60Hz | VSync-limited |

**Metal advantage:** Eliminates CPU→GPU copy (saves 1-3ms per frame)

---

## 9. Debug Logging

### Environment Variable: `RENDER_DEBUG`

When set, enables verbose Metal logging:

```bash
RENDER_DEBUG=1 ./svg_player_animated --metal input.svg
```

**Logged events:**

| Phase | Log Prefix |
|-------|------------|
| Context creation | `[METAL_DEBUG]` |
| Surface creation | `[METAL_DEBUG]` |
| Frame rendering | `[METAL_RENDER_DEBUG]` |
| Presentation | `[METAL_PRESENT_DEBUG]` |

**Example output:**

```
[METAL_DEBUG] Before createMetalContext: g_shutdownRequested=0
[METAL_DEBUG] After createMetalContext: g_shutdownRequested=0
[Metal] GPU backend enabled - GPU-accelerated rendering active
[METAL_RENDER_DEBUG] Starting frame render
[METAL_RENDER_DEBUG] Got surface and drawable
[METAL_RENDER_DEBUG] Applying animations
[METAL_RENDER_DEBUG] SVG rendered
[METAL_PRESENT_DEBUG] About to present
[METAL_PRESENT_DEBUG] Present complete
```

---

## 10. Code Hotspots Summary

| File | Lines | Function | Purpose |
|------|-------|----------|---------|
| `metal_context.mm` | 46-144 | `initialize()` | Create Metal device, layer, Skia context |
| `metal_context.mm` | 181-246 | `createSurface()` | Acquire drawable, wrap in SkSurface |
| `metal_context.mm` | 248-268 | `presentDrawable()` | Flush GPU, present to screen |
| `svg_player_animated.cpp` | 1972-1974 | Flag parsing | Enable Metal backend |
| `svg_player_animated.cpp` | 2339-2354 | Metal init | Create context, install fallback |
| `svg_player_animated.cpp` | 2503-2522 | `createSurface` lambda | Metal vs CPU surface creation |
| `svg_player_animated.cpp` | 4244-4319 | Render loop (Metal) | Acquire surface, render, mark frame |
| `svg_player_animated.cpp` | 4745-4803 | Present (Metal) | Black screen check, present |

---

## 11. Summary for Developers

### To Enable Metal Backend

```bash
./svg_player_animated --metal input.svg
```

### To Add Metal Code

1. **Compile-time guard:**
   ```cpp
   #ifdef __APPLE__
       // Metal code here
   #endif
   ```

2. **Runtime check:**
   ```cpp
   if (useMetalBackend && metalContext && metalContext->isInitialized()) {
       // Metal path
   } else {
       // CPU fallback
   }
   ```

### To Debug Metal Issues

1. Set `RENDER_DEBUG=1`
2. Check for drawable acquisition failures (line 4312-4318)
3. Check for black screen warnings (line 4754-4778)
4. Verify Metal context initialization (line 2343-2353)

### Common Mistakes to Avoid

1. **Don't hold drawables across frames** → Exhausts CAMetalLayer pool
2. **Don't skip `flushAndSubmit()` before `presentDrawable()`** → Incomplete rendering
3. **Don't assume Metal is always available** → Always check `isInitialized()`
4. **Don't use `peekPixels()` on Metal surfaces** → Use `readPixels()` instead

---

**End of Report**
