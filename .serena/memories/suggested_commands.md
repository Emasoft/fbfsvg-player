# Suggested Commands

## Building the SVG Player

### Full build command (svg_player_animated):
```bash
cd /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/skia-build/examples && \
clang++ -std=c++17 -O2 \
  -I../src/skia -I../src/skia/include -I../src/skia/modules \
  $(pkg-config --cflags sdl2) svg_player_animated.cpp -o svg_player_animated \
  ../src/skia/out/release-macos/libsvg.a ../src/skia/out/release-macos/libskia.a \
  ../src/skia/out/release-macos/libskresources.a ../src/skia/out/release-macos/libskshaper.a \
  ../src/skia/out/release-macos/libharfbuzz.a ../src/skia/out/release-macos/libskunicode_core.a \
  ../src/skia/out/release-macos/libskunicode_icu.a ../src/skia/out/release-macos/libexpat.a \
  ../src/skia/out/release-macos/libpng.a ../src/skia/out/release-macos/libzlib.a \
  ../src/skia/out/release-macos/libjpeg.a ../src/skia/out/release-macos/libwebp.a \
  ../src/skia/out/release-macos/libwuffs.a \
  $(pkg-config --libs sdl2) -L/opt/homebrew/opt/icu4c@78/lib -licuuc -licui18n -licudata \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework ApplicationServices -framework Metal -framework MetalKit -framework Cocoa \
  -framework IOKit -framework IOSurface -framework OpenGL -framework QuartzCore -liconv
```

### Debug build (with symbols):
Add `-g` flag and remove `-O2`:
```bash
clang++ -std=c++17 -g ...
```

## Running the SVG Player

### Basic usage:
```bash
cd /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/skia-build/examples
./svg_player_animated ../svg_input_samples/girl_hair.fbf.svg
```

### Fullscreen mode:
```bash
./svg_player_animated ../svg_input_samples/seagull.fbf.svg --fullscreen
```
or
```bash
./svg_player_animated ../svg_input_samples/seagull.fbf.svg -f
```

## Test SVG Files
Located in `svg_input_samples/`:
- `girl_hair.fbf.svg` - Hair animation
- `seagull.fbf.svg` - Seagull flight animation
- `panther_bird.fbf.svg` - Bird animation
- `walk_cycle.fbf.svg` - Walk cycle

## Building Skia from Source

### Fetch Skia source:
```bash
cd skia-build
./fetch.sh
```

### Build for macOS ARM64:
```bash
./build-macos-arm64.sh
```

### Build universal binary:
```bash
./build-macos.sh --universal
```

## System Utilities (macOS/Darwin)

### Check SDL2 installation:
```bash
pkg-config --cflags --libs sdl2
```

### Check ICU installation:
```bash
brew list icu4c@78
```

### List Skia static libraries:
```bash
ls -la skia-build/src/skia/out/release-macos/*.a
```
