# SVG Video Player

An animated SVG player with SMIL animation support built using Skia and SDL2 for macOS.

## Features

- **SMIL Animation Support**: Full support for SVG animations via SMIL (Synchronized Multimedia Integration Language)
- **Frame-by-Frame Playback**: Smooth frame-based rendering with configurable FPS
- **Hardware Acceleration**: Uses Metal backend via Skia for GPU-accelerated rendering
- **Fullscreen Mode**: Toggle fullscreen with `--fullscreen` flag or `F` key
- **Playback Controls**: Play, pause, seek, and speed control
- **Pre-buffering**: Threaded rendering with consumer-producer pattern for smooth playback
- **Universal Binary**: Supports both x86_64 and arm64 architectures on macOS

## Requirements

- macOS 11+ (Big Sur or later)
- Apple Silicon (arm64) or Intel (x86_64)
- Homebrew package manager
- Xcode Command Line Tools

## Quick Start

```bash
# 1. Install system dependencies
make deps

# 2. Build Skia (one-time, takes 30-60 minutes)
make skia

# 3. Build the SVG player
make

# 4. Run with a test SVG
make run
```

## Build Targets

| Target | Description |
|--------|-------------|
| `make` | Build release binary |
| `make debug` | Build debug binary with symbols |
| `make deps` | Install system dependencies (SDL2, ICU, pkg-config) |
| `make skia` | Build Skia library (universal binary) |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove all artifacts including Skia |
| `make run` | Build and run with test SVG |
| `make run-fullscreen` | Build and run in fullscreen mode |
| `make help` | Show all available targets |

## Usage

```bash
# Basic usage
./build/svg_player_animated <svg_file>

# With fullscreen
./build/svg_player_animated <svg_file> --fullscreen

# Example
./build/svg_player_animated svg_input_samples/girl_hair.fbf.svg
```

## Keyboard Controls

| Key | Action |
|-----|--------|
| `Space` | Play/Pause |
| `F` | Toggle fullscreen |
| `Left/Right` | Seek backward/forward |
| `Up/Down` | Increase/decrease playback speed |
| `R` | Reset to beginning |
| `Q` / `Escape` | Quit |

## Project Structure

```
SKIA-BUILD-ARM64/
├── src/                          # Main source code
│   └── svg_player_animated.cpp   # SVG player application
├── scripts/                      # Build scripts
│   ├── install-deps.sh          # Install Homebrew dependencies
│   └── build-skia.sh            # Build Skia library
├── build/                        # Build output (gitignored)
├── examples/                     # Example programs
├── svg_input_samples/            # Sample SVG files for testing
├── skia-build/                   # Skia build system (git submodule)
│   ├── src/skia/                 # Skia source code
│   ├── depot_tools/              # Chromium build tools
│   └── build-macos-universal.sh  # Skia build script
├── Makefile                      # Main build configuration
└── README.md                     # This file
```

## Dependencies

### System Dependencies (via Homebrew)

- **SDL2**: Window creation and input handling
- **ICU**: Unicode support (required by Skia)
- **pkg-config**: Build configuration

### Skia Libraries

The following Skia static libraries are linked:

- `libsvg.a` - SVG parsing and rendering
- `libskia.a` - Core graphics library
- `libskresources.a` - Resource management
- `libskshaper.a` - Text shaping
- `libharfbuzz.a` - Text layout
- `libskunicode_core.a` / `libskunicode_icu.a` - Unicode support
- `libexpat.a` - XML parsing
- `libpng.a` / `libjpeg.a` / `libwebp.a` - Image codecs
- `libzlib.a` / `libwuffs.a` - Compression

### macOS Frameworks

- CoreGraphics, CoreText, CoreFoundation
- Metal, MetalKit (GPU rendering)
- Cocoa, ApplicationServices
- IOKit, IOSurface, QuartzCore
- OpenGL (fallback)

## Building from Scratch

If you need to rebuild everything from scratch:

```bash
# Clean everything
make distclean

# Reinstall dependencies
make deps

# Rebuild Skia (takes 30-60 minutes)
make skia

# Build the player
make
```

## Troubleshooting

### Skia libraries not found
Run `make skia` to build the Skia libraries first.

### SDL2 not found
Run `make deps` to install SDL2 via Homebrew.

### ICU linking errors
ICU is a keg-only Homebrew package. The Makefile automatically detects its location.

### Architecture mismatch
The Skia build creates universal binaries. Verify with:
```bash
lipo -info skia-build/src/skia/out/release-macos/libskia.a
```

## License

This project uses:
- [Skia](https://skia.org/) - BSD 3-Clause License
- [SDL2](https://www.libsdl.org/) - zlib License
