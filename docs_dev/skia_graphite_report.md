# Skia Graphite Investigation Report

**Date:** 2026-01-19
**Purpose:** Evaluate Skia Graphite as the unified GPU backend for fbfsvg-player

---

## Executive Summary

**Skia Graphite is the recommended path forward.** It is Skia's next-generation GPU backend designed from scratch for modern graphics APIs. OpenGL/Ganesh is being deprecated, and Graphite is already shipping in Chrome on macOS.

### Key Findings

| Aspect | Ganesh (Old) | Graphite (New) |
|--------|--------------|----------------|
| **Design Era** | OpenGL-centric | Modern APIs (Metal, Vulkan, D3D12) |
| **Threading** | Single-threaded | Multi-threaded by default |
| **Shader Compilation** | Runtime (causes jank) | Pre-compilation support |
| **Chrome Status** | Being phased out | Default on Chrome Mac |
| **Future** | **Deprecated** | Sole GPU backend |

---

## What is Skia Graphite?

Graphite is Skia's ground-up rewrite of GPU rendering, designed specifically for:

- **Metal** (macOS, iOS, iPadOS, tvOS)
- **Vulkan** (Linux, Android, Windows)
- **Direct3D 12** (Windows)
- **Dawn/WebGPU** (Web, cross-platform)

It does **NOT** support OpenGL. This is intentional - Graphite is a clean break from legacy APIs.

---

## Architecture Differences

### Ganesh (Legacy)
```
SkCanvas → GrDirectContext → GPU Commands (immediate mode)
                ↓
         Single-threaded
```

### Graphite (Modern)
```
SkCanvas → Recorder → Recording → Context → GPU Commands
              ↓           ↓          ↓
        (thread A)  (immutable)  (thread B)
              ↓
      Multiple Recorders can run in parallel
```

**Key Innovation:** Recordings are immutable command lists that can be:
1. Created on multiple threads in parallel
2. Re-submitted without re-recording
3. Composed and reordered for optimal GPU batching

---

## Performance Benefits

From Chrome's deployment on macOS (July 2025):

| Metric | Improvement |
|--------|-------------|
| MotionMark 1.3 | **+15%** on M3 MacBook Pro |
| Interaction to Next Paint (INP) | Improved |
| Largest Contentful Paint (LCP) | Improved |
| Dropped frames | Reduced |
| GPU memory usage | Reduced |

From React Native Skia's adoption:

| Metric | iOS | Android |
|--------|-----|---------|
| Animation performance | **+50%** | **+200%** |
| Time to first animation | **+200%** | **+200%** |
| Crash rate | N/A | **-98%** |

---

## API Overview

### Context Creation (Metal)

```cpp
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/mtl/MtlBackendContext.h"
#include "include/gpu/graphite/mtl/MtlGraphiteUtils.h"

// 1. Create Metal device and queue
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
id<MTLCommandQueue> queue = [device newCommandQueue];

// 2. Configure Graphite backend context
skgpu::graphite::MtlBackendContext backendContext;
backendContext.fDevice.reset((__bridge_retained CFTypeRef)device);
backendContext.fQueue.reset((__bridge_retained CFTypeRef)queue);

// 3. Create Graphite Context
skgpu::graphite::ContextOptions options;
std::unique_ptr<skgpu::graphite::Context> context =
    skgpu::graphite::ContextFactory::MakeMetal(backendContext, options);
```

### Recording and Submission

```cpp
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"

// 1. Create a Recorder (can have multiple for multi-threading)
std::unique_ptr<skgpu::graphite::Recorder> recorder =
    context->makeRecorder();

// 2. Get SkCanvas and draw
SkCanvas* canvas = recorder->getDevice()->getCanvas();
canvas->drawRect(...);
canvas->drawPath(...);

// 3. Snap to immutable Recording
std::unique_ptr<skgpu::graphite::Recording> recording =
    recorder->snap();

// 4. Submit to Context for GPU execution
skgpu::graphite::InsertRecordingInfo info;
info.fRecording = recording.get();
context->insertRecording(info);
context->submit(skgpu::graphite::SyncToCpu::kNo);
```

### Context Creation (Vulkan)

```cpp
#include "include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "include/gpu/graphite/vk/VulkanGraphiteUtils.h"

// Similar pattern with VkDevice, VkQueue, VkInstance
skgpu::graphite::VulkanBackendContext backendContext;
backendContext.fInstance = vkInstance;
backendContext.fPhysicalDevice = vkPhysicalDevice;
backendContext.fDevice = vkDevice;
backendContext.fQueue = vkQueue;
backendContext.fGraphicsQueueIndex = graphicsQueueFamily;

std::unique_ptr<skgpu::graphite::Context> context =
    skgpu::graphite::ContextFactory::MakeVulkan(backendContext, options);
```

---

## Build Configuration

### GN Args for Graphite

```python
# Enable Graphite (required)
skia_use_graphite = true

# Metal backend (macOS/iOS)
skia_use_graphite_mtl = true

# Vulkan backend (Linux/Windows/Android)
skia_use_graphite_vulkan = true

# Dawn backend (WebGPU - for future web support)
skia_use_graphite_dawn = true

# Disable Ganesh if not needed (reduces binary size)
skia_enable_ganesh = false
```

### Platform-Specific Recommendations

| Platform | Primary Backend | Fallback |
|----------|-----------------|----------|
| macOS | Metal (Graphite) | None needed |
| iOS | Metal (Graphite) | None needed |
| Linux | Vulkan (Graphite) | CPU raster |
| Windows | Vulkan (Graphite) | D3D12 (Graphite) |
| Android | Vulkan (Graphite) | CPU raster |

---

## Migration Path for fbfsvg-player

### Current State

| Platform | Current Backend | Current Code |
|----------|-----------------|--------------|
| macOS | Metal (Ganesh) | `metal_context.mm` using `GrDirectContext` |
| Linux | CPU only | SDL2 texture blit |
| Windows | CPU only | SDL2 texture blit |
| iOS | Metal (Ganesh) | `SVGPlayerMetalRenderer.mm` |

### Target State

| Platform | Target Backend | New Code |
|----------|----------------|----------|
| macOS | Metal (Graphite) | `graphite_context_metal.mm` |
| Linux | Vulkan (Graphite) | `graphite_context_vulkan.cpp` |
| Windows | Vulkan (Graphite) | `graphite_context_vulkan.cpp` |
| iOS | Metal (Graphite) | Update `SVGPlayerMetalRenderer.mm` |

### Migration Steps

1. **Rebuild Skia with Graphite**
   ```bash
   # Update gn args
   skia_use_graphite = true
   skia_use_graphite_mtl = true
   skia_use_graphite_vulkan = true
   ```

2. **Create Platform Contexts**
   - `src/graphite_context_metal.mm` - macOS/iOS Metal
   - `src/graphite_context_vulkan.cpp` - Linux/Windows Vulkan

3. **Update Rendering Loop**
   - Replace `GrDirectContext` with `skgpu::graphite::Context`
   - Use `Recorder` for drawing
   - Call `snap()` and `insertRecording()` per frame

4. **Leverage Multi-threading** (optional, advanced)
   - Create multiple `Recorder` instances
   - Record on worker threads
   - Submit from main thread

---

## Compatibility Notes

### Minimum Platform Requirements

| Platform | Minimum Version | Notes |
|----------|-----------------|-------|
| macOS | 10.14 (Mojave) | Metal 2.0 required |
| iOS | 12.0 | Metal 2.0 required |
| Linux | Vulkan 1.1 | Most discrete GPUs since ~2016 |
| Windows | Windows 10 | Vulkan 1.1 via drivers |

### Skia Version Requirements

Graphite is stable and shipping in Chrome as of mid-2025. For production use:
- Use Skia milestone **M133+** for stable Graphite APIs
- M145+ recommended for latest features and fixes

---

## Revised Feature Alignment Plan

Based on this research, the Phase 1 GPU work should be updated:

### Phase 1: Graphite Migration (HIGH PRIORITY)

**Week 1: Skia Rebuild**
- [ ] Update `skia-build/build-macos.sh` with Graphite GN args
- [ ] Update `skia-build/build-linux.sh` with Graphite + Vulkan GN args
- [ ] Update `skia-build/build-ios.sh` with Graphite GN args
- [ ] Verify builds complete successfully

**Week 2: macOS Graphite Backend**
- [ ] Create `src/graphite_context_metal.mm` (~200 lines)
- [ ] Migrate `metal_context.mm` to Graphite API
- [ ] Update `svg_player_animated.cpp` to use Graphite context
- [ ] Benchmark performance vs Ganesh Metal

**Week 3: Linux/Windows Vulkan Backend**
- [ ] Create `src/graphite_context_vulkan.cpp` (~300 lines)
- [ ] Add Vulkan SDK detection to build scripts
- [ ] Update Linux player with `--vulkan` flag
- [ ] Update Windows player with `--vulkan` flag
- [ ] CPU fallback if Vulkan unavailable

**Week 4: iOS Graphite Migration**
- [ ] Update `SVGPlayerMetalRenderer.mm` to Graphite
- [ ] Test on device and simulator
- [ ] Update XCFramework build

---

## References

- [Chromium Blog: Introducing Skia Graphite](https://blog.chromium.org/2025/07/introducing-skia-graphite-chromes.html)
- [Skia Release Notes](https://github.com/google/skia/blob/main/RELEASE_NOTES.md)
- [Shopify: The Future of React Native Graphics](https://shopify.engineering/webgpu-skia-web-graphics)
- [Flutter Graphite Overview](https://gist.github.com/jezell/41012a13c6789fa7c1c3b0868a8b0942)
- [Skia Graphite Metal Headers](https://github.com/google/skia/tree/main/include/gpu/graphite/mtl)
- [Skia Vulkan Documentation](https://skia.org/docs/user/special/vulkan/)

---

## Conclusion

**Graphite is the clear choice** for fbfsvg-player's GPU backend:

1. **Future-proof**: Ganesh is deprecated; Graphite is the only path forward
2. **Cross-platform**: Single API for Metal (Apple) and Vulkan (Linux/Windows)
3. **Performance**: 15-200% improvements across platforms
4. **Threading**: Built-in multi-thread support for pre-buffering
5. **Production-ready**: Shipping in Chrome on macOS

The migration effort is comparable to what was planned for OpenGL/Vulkan, but results in a more maintainable, performant, and future-proof codebase.
