# CI Run 20872933417 Results (Rerun)

**Repository:** Emasoft/fbfsvg-player
**Trigger:** push
**Run Duration:** ~12 minutes
**Overall Status:** FAILURE

## Job Summary

| Job | Status | Duration |
|-----|--------|----------|
| API Header Test | PASS | 8s |
| iOS Build | PASS | 42s |
| Linux Build | FAIL | 42s |
| macOS Build | FAIL | 25s |
| Windows Build | FAIL | 11m37s |
| Build Summary | FAIL | 3s |

## Detailed Failure Analysis

### Linux Build - FAIL

**Step Failed:** Build Linux SDK

**Error:**
```
/home/runner/work/fbfsvg-player/fbfsvg-player/linux-sdk/SVGPlayer/../../shared/svg_player_api.cpp:288:9: error: value of type 'const SkTLazy<SkSVGViewBoxType>' (aka 'const SkTLazy<SkRect>') is not contextually convertible to 'bool'
  288 |     if (root->getViewBox()) {
      |         ^~~~~~~~~~~~~~~~~~
1 error generated.
```

**Root Cause:** The Skia API changed. `root->getViewBox()` returns `SkTLazy<SkRect>` which cannot be used directly in a boolean context. Must use `.isValid()` or `*viewBox` pattern.

### macOS Build - FAIL

**Step Failed:** Build macOS Desktop Player

**Error:**
```
/Users/runner/work/fbfsvg-player/fbfsvg-player/src/svg_player_animated.cpp:1367:9: error: value of type 'const SkTLazy<SkSVGViewBoxType>' (aka 'const SkTLazy<SkRect>') is not contextually convertible to 'bool'
    if (viewBox) {
        ^~~~~~~
/Users/runner/work/fbfsvg-player/fbfsvg-player/src/svg_player_animated.cpp:1539:9: error: value of type 'const SkTLazy<SkSVGViewBoxType>' (aka 'const SkTLazy<SkRect>') is not contextually convertible to 'bool'
    if (viewBox) {
        ^~~~~~~
2 errors generated.
```

**Root Cause:** Same as Linux - SkTLazy API compatibility issue in `svg_player_animated.cpp` at lines 1367 and 1539.

### Windows Build - FAIL

**Step Failed:** Build Skia (if not cached)

**Error:**
```
ninja: build stopped: interrupted by user.
[ERROR] Build failed
The operation was canceled.
```

**Root Cause:** Build was canceled due to "higher priority waiting request for CI-refs/heads/main exists". This is a GitHub Actions concurrency control behavior, not an actual code issue.

## Artifacts

- `ios-xcframework` - Successfully uploaded (iOS build passed)

## Required Fixes

1. **shared/svg_player_api.cpp:288** - Change `if (root->getViewBox())` to use proper SkTLazy API:
   ```cpp
   const auto& viewBox = root->getViewBox();
   if (viewBox.isValid()) {  // or: if (viewBox.get())
   ```

2. **src/svg_player_animated.cpp:1367 and 1539** - Same fix pattern:
   ```cpp
   if (viewBox.isValid()) {  // instead of: if (viewBox)
   ```

## Run URL

https://github.com/Emasoft/fbfsvg-player/actions/runs/20872933417
