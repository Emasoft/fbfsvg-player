# Benchmark Comparison: ThorVG vs fbfsvg-player

**Date**: 2026-01-17
**Test Environment**: macOS arm64 (Apple Silicon)

## Executive Summary

The benchmark comparison between ThorVG and fbfsvg-player revealed significant performance differences, but also **critical methodology limitations** that make direct comparison unfair.

## Key Finding: Apples vs Oranges Comparison

| Feature | ThorVG | fbfsvg-player |
|---------|--------|---------------|
| Animation Support | None (static SVG only) | Full SMIL animations |
| Use Case | Static SVG rendering | Animated FBF.SVG playback |
| GPU Acceleration | Software renderer | Metal (via Skia) |
| Threading | OpenMP parallelization | Threaded renderer with double buffering |
| Overhead | Minimal | Animation controller, frame sync |

**ThorVG does NOT support SMIL animations**, making this an unfair comparison since fbfsvg-player's entire purpose is animated SVG playback.

## Raw Results (Successful Tests Only)

| File | ThorVG FPS | fbfsvg FPS | Ratio | Notes |
|------|------------|------------|-------|-------|
| splat_button.fbf (20KB) | 438.1 | 132.7 | 0.30x | Small, simple animation |
| seagull.fbf (98KB) | 421.1 | 156.8 | 0.37x | Simple animation |
| girl_hair.fbf (11MB) | 427.0 | 25.8 | 0.06x | Complex animation |
| benchmark_1000 (282KB) | 6.1 | 0.4 | 0.07x | Synthetic benchmark |

## Failures Explained

9 out of 13 files showed 0 FPS for fbfsvg-player due to **benchmark timeout issues**:

The benchmark script uses 5-second duration, but large files require significant parsing time:

| File | Size | Approximate Parse Time |
|------|------|----------------------|
| demo_grid.fbf.svg | 17MB | ~30 seconds |
| girl_hair_with_composite.fbf.svg | 26MB | ~45 seconds |
| benchmark_partial.svg | 319MB | ~5+ minutes |

**Solution**: Extend timeout to `parse_time + benchmark_duration + buffer`

## Manual Verification Results

When run with adequate timeout:

```bash
# walk_cycle.fbf.svg (867KB) - Previously showed 0 FPS
timeout 30 ./svg_player_animated walk_cycle.fbf.svg --duration 3 --json
# Result: 139-153 FPS

# demo_grid.fbf.svg (17MB) - Previously showed 0 FPS
timeout 120 ./svg_player_animated demo_grid.fbf.svg --duration 10 --json
# Result: 30.90 FPS (took 39s total due to parsing)
```

## Performance Analysis

### Why ThorVG Appears Faster

1. **No Animation Overhead**: ThorVG renders static SVG; fbfsvg-player manages frame timing, animation state, SMIL parsing
2. **No Frame Synchronization**: ThorVG just renders; fbfsvg-player maintains consistent frame pacing
3. **Simpler Architecture**: ThorVG is a rendering library; fbfsvg-player is a complete player application
4. **Measurement Method**: ThorVG measures pure render throughput; fbfsvg-player includes animation controller overhead

### fbfsvg-player Overhead Components

- `SVGAnimationController`: SMIL animation parsing and timing
- `ThreadedRenderer`: Double-buffering, pre-rendering
- Frame timing and synchronization
- Input handling, UI overlays

## Conclusions

1. **ThorVG is faster at raw static SVG rendering** - This is expected given its focused design
2. **The comparison is fundamentally unfair** - ThorVG doesn't support animations
3. **fbfsvg-player performs well for its use case** - 100-150 FPS for typical animated SVGs
4. **Benchmark methodology needs improvement** - Parsing time must be accounted for

## Recommendations

1. **Don't use these results to compare engines** - They serve different purposes
2. **Optimize parsing for large files** - Consider lazy loading or streaming
3. **Consider ThorVG for static SVG** - If SMIL animation support is not needed
4. **Profile fbfsvg-player independently** - Focus on animation smoothness, not raw FPS

## Files

- Results JSON: `builds_dev/benchmark_results/benchmark_all_20260117_202324.json`
- ThorVG player: `builds_dev/thorvg/build/thorvg_player_macos`
- Benchmark script: `scripts/benchmark-native.sh`
