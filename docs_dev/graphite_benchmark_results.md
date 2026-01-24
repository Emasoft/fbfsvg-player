# Graphite vs Ganesh vs CPU Benchmark Results

**Date:** 2026-01-21 (Updated)
**Platform:** macOS arm64 (Apple Silicon M4 Pro)
**Test Content:** 1000 SVG files with heavy content (filters, clipPaths, gradients, text)

## Executive Summary

**Graphite Metal is the clear winner for heavy SVG content:**

- **1.56x faster than Ganesh Metal**
- **8.4x faster than CPU Raster**
- Competitive stutter count with best overall performance

## Test Configuration

- **Test folder:** `svg_input_samples/benchmark_1000_frames/`
- **Frame content:** Each SVG file contains:
  - Multiple `feGaussianBlur` filters
  - Complex gradients (linear and radial)
  - ClipPaths (circle, star, hex, rect, dynamic)
  - Masks with gradients
  - 1920x1080 resolution
  - ~28KB per frame
- **Mode:** Sequential (no frame rate cap), VSync OFF
- **Duration:** 30 seconds
- **Display:** Retina (2x HiDPI, 3840x2160 render resolution)

## Benchmark Results (2026-01-21)

| Backend | FPS | Frame Time | Stutters | Performance |
|---------|-----|------------|----------|-------------|
| **Graphite Metal** | **91.67** | **9.65ms** | 98 | **Baseline** |
| Ganesh Metal | 58.90 | 16.80ms | 116 | 0.64x |
| CPU Raster | 10.91 | 153.17ms | 328 | 0.12x |

## Performance Comparison

### Graphite Metal (Best Performance)

```
Display FPS: 91.67 (main loop rate)
Average frame time: 9.65ms
Stutters: 98 in 30 seconds

Key advantages:
- Modern Recorder/Recording architecture
- Non-blocking GPU submission
- Efficient command batching
```

### Ganesh Metal (Legacy GPU Backend)

```
Display FPS: 58.90 (main loop rate)
Average frame time: 16.80ms
Stutters: 116 in 30 seconds

Notes:
- Older deferred rendering model
- Still good performance for most content
- Useful for compatibility testing
```

### CPU Raster (Software Fallback)

```
Display FPS: 10.91 (main loop rate)
Average frame time: 153.17ms
Stutters: 328 in 30 seconds

Notes:
- Works on all systems without GPU
- Significantly slower for complex content
- Suitable for simple SVGs or headless rendering
```

## Key Findings

### Why Graphite outperforms Ganesh

1. **Modern architecture**: Recorder/Recording model vs deferred commands
2. **Async GPU submission**: Non-blocking command buffer submission
3. **Better batching**: More efficient draw call aggregation
4. **HiDPI optimization**: Canvas scaling handled efficiently

### Canvas Scaling Fix (2026-01-21)

A critical fix was applied to properly scale SVG content to HiDPI displays:
- Before: SVG content rendered at native 1920x1080, appearing in 1/4 of screen
- After: Canvas scaling applies 2x transform to fill 3840x2160 display

This fix improved both visual correctness and performance consistency.

## Recommendations

1. **Use `--graphite` for production** - Best performance on Apple Silicon
2. **Use `--metal` for compatibility testing** - Legacy Ganesh backend
3. **Keep CPU fallback** - Works on all systems, useful for headless rendering

## Command Line Flags

```bash
# Graphite Metal (recommended for production)
./build/fbfsvg-player --graphite animation.svg

# Ganesh Metal (legacy)
./build/fbfsvg-player --metal animation.svg

# CPU Raster (fallback)
./build/fbfsvg-player animation.svg
```

## Test Commands Used

```bash
# Graphite benchmark (VSync disabled by default)
./build/fbfsvg-player --graphite --sequential --duration=30 svg_input_samples/benchmark_1000_frames/

# Ganesh benchmark
./build/fbfsvg-player --metal --sequential --duration=30 svg_input_samples/benchmark_1000_frames/

# CPU benchmark
./build/fbfsvg-player --sequential --duration=30 svg_input_samples/benchmark_1000_frames/
```

## Previous Results (2026-01-19)

For reference, the previous benchmark showed significantly different results due to a canvas scaling bug that caused performance anomalies:

| Backend | FPS | Frame Time | Notes |
|---------|-----|------------|-------|
| Graphite | 58.80 | 16.54ms | Before canvas fix |
| Ganesh | 6.06 | 183.02ms | Severely impacted by bug |
| CPU | 20.17 | 48.19ms | Before canvas fix |

The canvas scaling fix resolved these anomalies, resulting in more consistent and accurate performance numbers.
