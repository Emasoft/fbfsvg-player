# Suggested Commands

## Building the SVG Player

### Using Makefile (recommended):
```bash
cd /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64

# Install dependencies (Homebrew packages)
make deps

# Build Skia (one-time, takes 30-60 minutes)
make skia

# Build release binary
make

# Build debug binary
make debug

# Clean build artifacts
make clean

# Clean everything including Skia
make distclean

# Show all targets
make help
```

### Manual build command (if needed):
```bash
cd /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64
clang++ -std=c++17 -O2 \
  -Iskia-build/src/skia -Iskia-build/src/skia/include -Iskia-build/src/skia/modules \
  $(pkg-config --cflags sdl2) src/svg_player_animated.cpp -o build/svg_player_animated \
  skia-build/src/skia/out/release-macos/lib*.a \
  $(pkg-config --libs sdl2) -L$(brew --prefix icu4c)/lib -licuuc -licui18n -licudata \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework ApplicationServices -framework Metal -framework MetalKit -framework Cocoa \
  -framework IOKit -framework IOSurface -framework OpenGL -framework QuartzCore -liconv
```

## Running the SVG Player

### Using Makefile:
```bash
make run              # Run with test SVG
make run-fullscreen   # Run in fullscreen mode
```

### Direct execution:
```bash
./build/svg_player_animated svg_input_samples/girl_hair.fbf.svg
./build/svg_player_animated svg_input_samples/seagull.fbf.svg --fullscreen
./build/svg_player_animated svg_input_samples/seagull.fbf.svg -f
```

## Test SVG Files
Located in `svg_input_samples/`:
- `girl_hair.fbf.svg` - Hair animation
- `seagull.fbf.svg` - Seagull flight animation
- `panther_bird.fbf.svg` - Bird animation
- `walk_cycle.fbf.svg` - Walk cycle

## Building Skia from Source

### Using scripts (recommended):
```bash
./scripts/build-skia.sh
```

### Manual build:
```bash
cd skia-build
./fetch.sh                    # Fetch Skia source
./build-macos-universal.sh    # Build universal binary (x64 + arm64)
```

## System Utilities (macOS/Darwin)

### Check SDL2 installation:
```bash
pkg-config --cflags --libs sdl2
```

### Check ICU installation:
```bash
brew --prefix icu4c
brew list icu4c
```

### List Skia static libraries:
```bash
ls -la skia-build/src/skia/out/release-macos/*.a
```

### Verify architecture:
```bash
lipo -info skia-build/src/skia/out/release-macos/libskia.a
```
