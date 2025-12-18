# Task Completion Checklist

## After Making Code Changes

### 1. Build the Project
```bash
cd /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/examples && \
clang++ -std=c++17 -O2 \
  -I../skia-build/src/skia -I../skia-build/src/skia/include -I../skia-build/src/skia/modules \
  $(pkg-config --cflags sdl2) svg_player_animated.cpp -o svg_player_animated \
  ../skia-build/src/skia/out/release-macos/libsvg.a ../skia-build/src/skia/out/release-macos/libskia.a \
  ../skia-build/src/skia/out/release-macos/libskresources.a ../skia-build/src/skia/out/release-macos/libskshaper.a \
  ../skia-build/src/skia/out/release-macos/libharfbuzz.a ../skia-build/src/skia/out/release-macos/libskunicode_core.a \
  ../skia-build/src/skia/out/release-macos/libskunicode_icu.a ../skia-build/src/skia/out/release-macos/libexpat.a \
  ../skia-build/src/skia/out/release-macos/libpng.a ../skia-build/src/skia/out/release-macos/libzlib.a \
  ../skia-build/src/skia/out/release-macos/libjpeg.a ../skia-build/src/skia/out/release-macos/libwebp.a \
  ../skia-build/src/skia/out/release-macos/libwuffs.a \
  $(pkg-config --libs sdl2) -L/opt/homebrew/opt/icu4c@78/lib -licuuc -licui18n -licudata \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework ApplicationServices -framework Metal -framework MetalKit -framework Cocoa \
  -framework IOKit -framework IOSurface -framework OpenGL -framework QuartzCore -liconv
```

Note: Build from examples/ directory with paths relative to project root.

### 2. Verify No Compiler Warnings
- Check for unused variables
- Check for shadowed variables
- Check for type mismatches

### 3. Test Basic Functionality
```bash
./svg_player_animated ../svg_input_samples/girl_hair.fbf.svg
```

### 4. Test Specific Feature Changes
- If changed fullscreen: test with `--fullscreen` flag
- If changed overlay: toggle with `D` key
- If changed timing: check metrics in overlay
- If changed threading: test with `P` key (toggle parallel mode)

### 5. Test Edge Cases
- Window resize
- Fullscreen toggle (Enter key)
- Mode switching (V, F, P keys)
- Animation pause/resume (Space)

## Code Quality Checks

### Before Committing
1. Remove debug `std::cout` statements (unless part of overlay)
2. Check for memory leaks (no `new` without `delete`, prefer smart pointers)
3. Verify thread safety (atomic operations, mutex locks)
4. Check resource cleanup in error paths

### Documentation
- Update comments if behavior changed
- Document new keyboard shortcuts
- Update debug overlay if new metrics added

## No Automated Tests
This project uses manual testing through the UI. Key test scenarios:
- Animation plays smoothly (check frame skip rate)
- VSync works (60 FPS limit with V key)
- Debug overlay shows correct metrics
- Screenshot captures work (C key)
- Fullscreen mode works correctly
