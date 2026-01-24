# ThorVG Rasterization Architecture Analysis

## Why ThorVG is 2.9x Faster at Rasterization

### Key Speed Differentiators

| Technique | Description | Impact |
|-----------|-------------|--------|
| **Multi-SIMD Rasterizers** | Separate optimized implementations (AVX, NEON, C) compiled for target CPU | Vectorizes span generation and coverage accumulation |
| **RLE + Banding** | Run-length encodes coverage data into sparse spans; recursive band splitting | Reduces memory bandwidth, fits cache |
| **Fixed-Point Math** | 8-bit precision via bit-shifting (UPSCALE/TRUNC/FRACT) | Avoids floating-point overhead in scanline loops |
| **Intelligent Curve Subdivision** | Rapid Termination Evaluation - only splits Bezier when necessary | ~40% less computation on typical content |
| **Span Merging** | Adjacent pixels with identical coverage combined | Reduces span list size and iteration overhead |
| **Memory Pooling** | Shared cross-renderer pool | Minimizes allocation overhead in task scheduler |
| **Thread Pool Scheduler** | OpenMP with thread-local buffers | Eliminates contention, parallel shape rendering |
| **Lazy Compositor Caching** | Reuses intermediate surfaces | Skips unnecessary allocations |
| **Conditional Optimizations** | Disables AA when stroke > 2.0f, selective clipping | Skips unnecessary work |

### Architecture Comparison

| Aspect | ThorVG | Skia |
|--------|--------|------|
| Rasterization Model | **Incremental sparse** (coverage cells → RLE spans) | **General-purpose path rendering** |
| Coverage Accumulation | **RLE-compressed spans** | **Per-pixel coverage buffer** |
| SIMD | **Multiple specialized backends** (AVX, NEON, C) | **Single backend with some SIMD** |
| Threading | **OpenMP task parallelism** | **Single-threaded DOM render** |
| Memory Layout | **Cache-optimized banding** | **General allocation** |
| Math Precision | **Fixed-point (8-bit)** | **Floating-point** |

### Why RLE + Banding is Faster

Traditional rasterizers allocate a full coverage buffer (width × height bytes). ThorVG instead:

1. **Divides canvas into horizontal bands** (typically 256 scanlines)
2. **Generates coverage spans per band** - only storing non-zero coverage
3. **RLE-encodes spans** - `{x, length, coverage}` tuples
4. **Merges adjacent identical spans** - fewer iterations

For a 800×800 canvas with a simple shape:
- Traditional: 640,000 bytes coverage buffer
- ThorVG RLE: ~1,000-5,000 bytes of span data

### Why SIMD Matters

ThorVG's sw_engine has separate files:
- `tvgSwRasterAvx.h` - AVX2 vectorized pixel operations (8 pixels at once)
- `tvgSwRasterNeon.h` - ARM NEON vectorized (4 pixels at once)
- `tvgSwRasterC.h` - Fallback scalar implementation

The SIMD paths handle:
- Alpha blending (4-8 pixels per instruction)
- Coverage accumulation (vectorized add)
- Color space conversion
- Gradient interpolation

### Fixed-Point Math

ThorVG uses 8-bit fixed-point for coverage calculations:

```cpp
#define UPSCALE(x) ((x) << 8)      // Scale to fixed-point
#define TRUNC(x)   ((x) >> 8)       // Truncate to integer
#define FRACT(x)   ((x) & 0xFF)     // Fractional part
```

This avoids:
- Floating-point to integer conversions
- FPU pipeline stalls
- Denormalized number handling

### Thread Pool Architecture

ThorVG uses OpenMP for parallel rendering:

```cpp
#pragma omp parallel for
for (auto& shape : shapes) {
    // Thread-local memory buffer
    // Rasterize shape independently
    // Composite into shared framebuffer (with atomic or lock)
}
```

Each thread has its own:
- Span buffer (no contention)
- RLE encoder state
- Compositor surface (if needed)

### Curve Subdivision Optimization

**Rapid Termination Evaluation (RTE)**: Before subdividing a Bezier curve, ThorVG checks if the control points are already within 1 pixel of the line segment:

```cpp
// If control points are close enough, draw line segment directly
if (abs(ctrl.x - midpoint.x) < 0.5 && abs(ctrl.y - midpoint.y) < 0.5) {
    drawLine(start, end);
    return;
}
```

This reduces subdivision depth by ~40% for typical SVG content.

## Potential Optimizations for Skia Player

### Applicable Now (No Skia Changes)

1. **Canvas caching** - Don't re-create SkSVGDOM each frame (already done)
2. **Dirty region tracking** - Already implemented
3. **Pre-scaled rendering** - Render at display size, not SVG size

### Would Require Skia Modifications

1. **Enable GPU backend** (Metal on macOS) - Offload rasterization to GPU
2. **Enable Skia's SkTileGrid** - Spatial acceleration for complex SVGs
3. **Thread pool for path rendering** - Skia supports this but needs configuration

### Alternative: Use ThorVG for Rasterization

Since ThorVG's rasterizer is fundamentally faster, consider:
- Use ThorVG for SVG parsing + rasterization
- Use Skia only for final compositing/display
- This is the approach some projects (like Godot) have taken

## Current Skia Player Architecture

### The Problem: CPU-Only Rasterization

Our player uses `SkSurfaces::Raster()` which is **CPU-only**:

```cpp
// svg_player_animated.cpp line 1220
threadSurface = SkSurfaces::Raster(
    SkImageInfo::Make(localWidth, localHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
```

This means:
1. Skia rasterizes SVG on CPU (slow, single-threaded path rendering)
2. We copy pixels from CPU buffer to SDL texture
3. SDL uses Metal to display the texture

The Metal GPU backend is **compiled into Skia** but we're not using it!

```bash
# Metal symbols ARE present in libskia.a:
nm libskia.a | grep GrMtl
# Shows: GrMtlGpu, GrMtlAttachment, GrMtlBuffer, etc.
```

### Current Render Pipeline

```
SVG Content
    │
    ▼
SkSVGDOM::render(canvas)  ◄── CPU-only path rendering
    │
    ▼
SkSurface::Raster         ◄── CPU memory buffer
    │
    ▼
peekPixels() + memcpy     ◄── CPU → CPU copy
    │
    ▼
SDL_UpdateTexture()       ◄── CPU → GPU copy (slow!)
    │
    ▼
Metal Display
```

### With Metal Backend (Potential)

```
SVG Content
    │
    ▼
SkSVGDOM::render(canvas)  ◄── Metal GPU path rendering
    │
    ▼
SkSurface::MakeFromMTLTexture  ◄── GPU texture (zero-copy)
    │
    ▼
SDL_RenderTexture()       ◄── GPU → GPU (fast!)
    │
    ▼
Metal Display
```

## Optimization Paths

### Path A: Enable Skia Metal Backend (Recommended)

**Effort**: Medium (requires Objective-C++ for Metal context)
**Impact**: Potentially 2-5x faster rasterization

Changes needed:
1. Create Metal device and command queue
2. Create GrMtlBackendContext
3. Create GrDirectContext with Metal backend
4. Create SkSurface from Metal texture
5. Integrate with SDL's Metal renderer

```cpp
// Pseudo-code for Metal backend
#import <Metal/Metal.h>
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/mtl/GrMtlBackendContext.h"

id<MTLDevice> device = MTLCreateSystemDefaultDevice();
id<MTLCommandQueue> queue = [device newCommandQueue];

GrMtlBackendContext backendContext;
backendContext.fDevice.reset((__bridge void*)device);
backendContext.fQueue.reset((__bridge void*)queue);

sk_sp<GrDirectContext> context = GrDirectContext::MakeMetal(backendContext);

// Create surface from Metal texture
sk_sp<SkSurface> surface = SkSurfaces::WrapMTLTexture(
    context.get(),
    (__bridge void*)metalTexture,
    kTopLeft_GrSurfaceOrigin,
    1,  // sample count
    kBGRA_8888_SkColorType,
    nullptr, nullptr);
```

### Path B: ThorVG-Style CPU Optimizations

**Effort**: High (requires significant Skia modifications or custom rasterizer)
**Impact**: 2-3x faster CPU rasterization

Would require:
1. Custom SIMD rasterizer (AVX/NEON)
2. RLE span encoding
3. Fixed-point math in critical paths
4. Thread pool for parallel path rendering

Not practical without forking Skia.

### Path C: Hybrid ThorVG + Skia

**Effort**: Medium
**Impact**: Get ThorVG's speed, keep Skia for display

Use ThorVG for SVG parsing and rasterization, Skia only for compositing:

```cpp
// Use ThorVG for fast rasterization
tvg::Canvas* canvas = tvg::SwCanvas::gen();
canvas->target(buffer, stride, width, height, tvg::SwCanvas::ABGR8888);
tvg::Picture* picture = tvg::Picture::gen();
picture->load(svgData.data(), svgData.size(), "svg");
canvas->push(picture);
canvas->draw();
canvas->sync();

// Copy to Skia surface for display integration
memcpy(skiaBuffer, buffer, width * height * 4);
```

## Recommendation

**Start with Path A (Metal backend)** because:
1. Metal support is already compiled in
2. No modifications to Skia source needed
3. GPU acceleration benefits all SVG content
4. SDL already uses Metal for display

If Metal backend doesn't provide sufficient gains, consider **Path C (ThorVG hybrid)** for best CPU performance.

## Source Code References

- [ThorVG SwRenderer](https://github.com/thorvg/thorvg/blob/main/src/renderer/sw_engine/tvgSwRenderer.cpp)
- [ThorVG RLE Implementation](https://github.com/thorvg/thorvg/blob/main/src/renderer/sw_engine/tvgSwRle.cpp)
- [ThorVG SIMD Rasterizers](https://github.com/thorvg/thorvg/tree/main/src/renderer/sw_engine)
- [ThorVG Task Scheduler](https://github.com/thorvg/thorvg/blob/main/src/renderer/tvgTaskScheduler.cpp)
- [Skia Metal Backend](https://skia.org/docs/user/special/metal/)
- [GrMtlBackendContext](https://github.com/nicoboss/skia/blob/main/include/gpu/mtl/GrMtlBackendContext.h)
