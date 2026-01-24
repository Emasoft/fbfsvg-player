# fbfsvg-player

**The first multi-platform player for the FBF.SVG vector video format.**

fbfsvg-player is a high-performance animated SVG player built using Skia that exclusively plays files conforming to the [FBF.SVG format specification](https://github.com/Emasoft/svg2fbf).

## What is FBF.SVG?

**FBF.SVG (Frame-by-Frame SVG)** is an open vector video format that enables declarative frame-by-frame animations as valid SVG 1.1/2.0 documents. Unlike traditional video formats, FBF.SVG files are:

- **Pure Vector**: Infinitely scalable without quality loss
- **Self-Contained**: Single SVG file with no external dependencies
- **Declarative**: Uses SMIL timing, not JavaScript or CSS code
- **Secure**: Strict CSP compliance with no embedded scripts
- **Universal**: Valid SVG viewable in any browser or SVG editor

### Animation Modes

FBF.SVG supports eight playback modes:

| Mode | Behavior |
|------|----------|
| `once` | Plays from start to end, then stops |
| `once_reversed` | Plays from end to start, then stops |
| `loop` | Continuous forward playback |
| `loop_reversed` | Continuous reverse playback |
| `pingpong_once` | Forward, backward, then stops |
| `pingpong_loop` | Continuous forward-backward cycle |
| `pingpong_once_reversed` | Backward, forward, then stops |
| `pingpong_loop_reversed` | Continuous backward-forward cycle |

### Format Specification

For complete format details, schema, and validation tools, see the [FBF.SVG Specification](https://github.com/Emasoft/svg2fbf).

## Supported Platforms

| Platform | Architectures | GPU Backend | Windowing |
|----------|---------------|-------------|-----------|
| macOS | x64, arm64, universal | Graphite (Metal) | SDL2 |
| Linux | x64, arm64 | Graphite (Vulkan) | SDL2 |
| Windows | x64 | Graphite (Vulkan) | SDL2 |
| iOS | arm64, simulator (x64+arm64) | Metal | UIKit |

## Features

- **Native FBF.SVG Support**: Optimized for the FBF.SVG vector video format
- **SMIL Animation Engine**: Full support for declarative SMIL timing
- **Frame-by-Frame Playback**: Smooth rendering with configurable FPS
- **Hardware Acceleration**: Graphite GPU backend (Metal on macOS, Vulkan on Linux/Windows)
- **Fullscreen Mode**: Toggle with `--fullscreen` flag or `F` key
- **Playback Controls**: Play, pause, seek, and speed control
- **Pre-buffering**: Threaded rendering for smooth playback
- **Universal Binary**: Supports both x86_64 and arm64 on macOS
- **iOS Integration**: XCFramework with C API for UIKit apps

## Quick Start

### macOS

```bash
# 1. Install system dependencies
make deps

# 2. Build Skia (one-time, takes 30-60 minutes)
make skia

# 3. Build the FBF.SVG player
make

# 4. Run with an FBF.SVG file
make run
```

### Linux

```bash
# 1. Install system dependencies
sudo apt-get update
sudo apt-get install build-essential clang pkg-config
sudo apt-get install libsdl2-dev libicu-dev
sudo apt-get install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
sudo apt-get install libfreetype6-dev libfontconfig1-dev libx11-dev

# 2. Build Skia for Linux
make skia-linux

# 3. Build the FBF.SVG player
make linux

# 4. Run with an FBF.SVG file
./build/svg_player_animated animation.fbf.svg
```

### Windows

```batch
REM 1. Install Visual Studio 2019+ with "Desktop development with C++" workload
REM    Download from: https://visualstudio.microsoft.com/

REM 2. Download and install SDL2 development libraries
REM    - Download from: https://libsdl.org/download-2.0.php
REM    - Extract to C:\SDL2 or the project's external\SDL2 folder

REM 3. Build Skia for Windows (one-time, requires depot_tools)
cd skia-build
build-windows.bat
cd ..

REM 4. Build the FBF.SVG player
scripts\build-windows.bat

REM 5. Run with an FBF.SVG file
build\windows\svg_player_animated.exe animation.fbf.svg
```

### iOS

```bash
# 1. Build Skia for iOS (device + simulator)
make skia-ios

# 2. Build XCFramework
make ios-xcframework

# 3. Integrate into your Xcode project
# Drag build/svg_player.xcframework into your project
```

## Usage

### Desktop (macOS/Linux/Windows)

```bash
# Basic usage
./build/svg_player_animated <file.fbf.svg>

# With fullscreen
./build/svg_player_animated <file.fbf.svg> --fullscreen

# Example
./build/svg_player_animated svg_input_samples/girl_hair.fbf.svg
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `--fullscreen` | Start in fullscreen mode |
| `--cpu` | Use CPU raster rendering instead of GPU (Graphite) |
| `--metal` | (macOS only) Use Metal Ganesh instead of Graphite |
| `--benchmark` | Run benchmark mode (exits after measuring) |
| `--remote-control[=PORT]` | Enable remote control server (default port 9999) |
| `--sequential` | Force sequential frame rendering (no prebuffering) |

### iOS Integration

```c
#include "svg_player_ios.h"

// Create player
SVGPlayerHandle player = SVGPlayer_Create();

// Load FBF.SVG file
SVGPlayer_LoadSVG(player, "animation.fbf.svg");

// Start playback
SVGPlayer_SetPlaybackState(player, SVGPlaybackState_Playing);

// In your CADisplayLink callback:
SVGPlayer_Update(player, deltaTime);
SVGPlayer_Render(player, pixelBuffer, width, height, scale);

// Cleanup
SVGPlayer_Destroy(player);
```

## Keyboard Controls (Desktop)

| Key | Action |
|-----|--------|
| `Space` | Play/Pause |
| `F` | Toggle fullscreen |
| `Left/Right` | Seek backward/forward |
| `Up/Down` | Increase/decrease playback speed |
| `R` | Reset to beginning |
| `Q` / `Escape` | Quit |

## Build Targets

### General Targets

| Target | Description |
|--------|-------------|
| `make` | Build for current platform |
| `make deps` | Install platform dependencies |
| `make skia` | Build Skia for current platform |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove all artifacts including Skia |
| `make info` | Show build environment info |
| `make help` | Show all available targets |

### macOS Targets

| Target | Description |
|--------|-------------|
| `make macos` | Build for current architecture |
| `make macos-universal` | Build universal binary (x64 + arm64) |
| `make macos-arm64` | Build for Apple Silicon |
| `make macos-x64` | Build for Intel |
| `make macos-debug` | Build with debug symbols |

### Linux Targets

| Target | Description |
|--------|-------------|
| `make linux` | Build for current architecture |
| `make linux-debug` | Build with debug symbols |
| `make linux-ci` | Build non-interactively (for CI) |

### Windows Targets

| Target | Description |
|--------|-------------|
| `make windows` | Show Windows build instructions |
| `make windows-info` | Show detailed Windows build info |

**Note:** Windows builds require Visual Studio. Run `scripts\build-windows.bat` directly on Windows.

### iOS Targets

| Target | Description |
|--------|-------------|
| `make ios` | Build for iOS device (arm64) |
| `make ios-device` | Build for iOS device |
| `make ios-simulator` | Build for iOS simulator |
| `make ios-simulator-universal` | Build universal simulator (x64 + arm64) |
| `make ios-xcframework` | Build XCFramework (device + simulator) |

### Skia Build Targets

| Target | Description |
|--------|-------------|
| `make skia` | Build Skia for current platform |
| `make skia-macos` | Build Skia for macOS (universal) |
| `make skia-linux` | Build Skia for Linux |
| `make skia-ios` | Build Skia XCFramework for iOS |

## Project Structure

```
fbfsvg-player/
├── src/                             # Main source code
│   ├── svg_player_animated.cpp      # macOS player
│   ├── svg_player_animated_linux.cpp # Linux player
│   ├── svg_player_animated_windows.cpp # Windows player
│   ├── svg_player_ios.cpp           # iOS library implementation
│   ├── svg_player_ios.h             # iOS public API header
│   ├── file_dialog_windows.cpp      # Windows file dialog
│   └── platform.h                   # Cross-platform abstractions
├── shared/                          # Unified cross-platform API
│   ├── svg_player_api.h             # Master C API header
│   ├── svg_player_api.cpp           # Platform-independent implementation
│   ├── SVGAnimationController.h/.cpp # Core animation logic
│   └── SVGTypes.h                   # Shared type definitions
├── scripts/                         # Build scripts
│   ├── build-macos.sh               # macOS build
│   ├── build-linux.sh               # Linux build
│   ├── build-windows.bat            # Windows build
│   ├── build-ios.sh                 # iOS build
│   └── ...
├── ios-sdk/                         # iOS SDK components
├── linux-sdk/                       # Linux SDK components
├── macos-sdk/                       # macOS SDK components
├── build/                           # Build output (gitignored)
├── svg_input_samples/               # Sample FBF.SVG files
├── skia-build/                      # Skia build system (submodule)
├── Makefile                         # Main build configuration
└── README.md                        # This file
```

## Platform-Specific Notes

### macOS

- Uses Graphite (Metal) for GPU-accelerated rendering (default). Use `--cpu` for CPU raster, `--metal` for Metal Ganesh.
- CoreText for font management
- Mach APIs for CPU/thread monitoring
- Homebrew for dependencies (SDL2, ICU)

### Linux

- Uses Graphite (Vulkan) for GPU-accelerated rendering (default). Use `--cpu` for CPU raster fallback.
- Fontconfig for font management
- /proc filesystem for CPU monitoring
- System packages for dependencies

### Windows

- Uses Graphite (Vulkan) for GPU-accelerated rendering (default). Use `--cpu` for CPU raster fallback.
- DirectWrite for font management
- Visual Studio required for building

### iOS

- Uses Metal for GPU-accelerated rendering
- CoreText for fonts (shared with macOS)
- Builds as XCFramework for UIKit integration
- No SDL2 - uses native UIKit windowing

## Dependencies

### macOS Dependencies (via Homebrew)

- **SDL2**: Window creation and input handling
- **ICU**: Unicode support
- **pkg-config**: Build configuration

### Linux Dependencies (via apt)

- **libsdl2-dev**: Window creation and input handling
- **libicu-dev**: Unicode support
- **libgl1-mesa-dev**: OpenGL support
- **libegl1-mesa-dev**: EGL support
- **libfontconfig1-dev**: Font configuration
- **libfreetype6-dev**: Font rendering
- **libx11-dev**: X11 windowing

### Windows Dependencies

- **Visual Studio 2019+**: With "Desktop development with C++" workload
- **SDL2**: Development libraries for Windows x64

### iOS Dependencies

- Xcode with iOS SDK
- Skia built for iOS (device + simulator)

## Troubleshooting

### Skia libraries not found
Run `make skia` (or `make skia-linux` / `make skia-ios`) to build Skia first.

### SDL2 not found (macOS)
Run `make deps` to install SDL2 via Homebrew.

### SDL2 not found (Linux)
Run `sudo apt-get install libsdl2-dev`.

### ICU linking errors
ICU is handled automatically by the build scripts. On macOS, it's a keg-only Homebrew package.

### Architecture mismatch
Verify library architectures with:
```bash
# macOS
lipo -info skia-build/src/skia/out/release-macos/libskia.a

# Linux
file skia-build/src/skia/out/release-linux/libskia.a
```

### iOS simulator build fails
Ensure you have Skia built for both simulator architectures:
```bash
make skia-ios-simulator
```

## Building from Scratch

```bash
# Clean everything
make distclean

# Build for current platform
make deps
make skia
make

# Or build for specific platform
make deps
make skia-linux
make linux
```

## Related Projects

- [svg2fbf](https://github.com/Emasoft/svg2fbf) - FBF.SVG format specification and conversion tools

## License

This project uses:
- [Skia](https://skia.org/) - BSD 3-Clause License
- [SDL2](https://www.libsdl.org/) - zlib License
