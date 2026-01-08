# SVG Video Player

A multi-platform animated SVG player with SMIL animation support built using Skia.

## Supported Platforms

| Platform | Architectures | GPU Backend | Windowing |
|----------|---------------|-------------|-----------|
| macOS | x64, arm64, universal | Metal | SDL2 |
| Linux | x64, arm64 | OpenGL/EGL | SDL2 |
| Windows | x64 | Direct3D/OpenGL | SDL2 |
| iOS | arm64, simulator (x64+arm64) | Metal | UIKit |

## Features

- **SMIL Animation Support**: Full support for SVG animations via SMIL (Synchronized Multimedia Integration Language)
- **Frame-by-Frame Playback**: Smooth frame-based rendering with configurable FPS
- **Hardware Acceleration**: Metal on Apple platforms, OpenGL/EGL on Linux
- **Fullscreen Mode**: Toggle fullscreen with `--fullscreen` flag or `F` key
- **Playback Controls**: Play, pause, seek, and speed control
- **Pre-buffering**: Threaded rendering with consumer-producer pattern for smooth playback
- **Universal Binary**: Supports both x86_64 and arm64 on macOS
- **iOS Integration**: Static library with C API for UIKit apps

## Quick Start

### macOS

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

# 3. Build the SVG player
make linux

# 4. Run with a test SVG
./build/svg_player_animated svg_input_samples/girl_hair.fbf.svg
```

### Windows

```batch
REM 1. Install Visual Studio 2019+ with "Desktop development with C++" workload
REM    Download from: https://visualstudio.microsoft.com/

REM 2. Download and install SDL2 development libraries
REM    - Download from: https://libsdl.org/download-2.0.php
REM    - Extract to C:\SDL2 or the project's external\SDL2 folder

REM 3. Build Skia for Windows (one-time, requires depot_tools)
REM    In skia-build\src\skia:
gn gen out/release-windows --args="is_debug=false is_official_build=true skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_zlib=false skia_use_system_expat=false skia_use_system_icu=false skia_use_system_harfbuzz=false skia_use_system_freetype2=false skia_enable_svg=true target_cpu=\"x64\""
ninja -C out/release-windows skia svg

REM 4. Build the SVG player
scripts\build-windows.bat

REM 5. Run with a test SVG
build\windows\svg_player_animated.exe svg_input_samples\girl_hair.fbf.svg
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

**Note:** Windows builds require Visual Studio on Windows. Run `scripts\build-windows.bat` directly.

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

## Usage

### Desktop (macOS/Linux)

```bash
# Basic usage
./build/svg_player_animated <svg_file>

# With fullscreen
./build/svg_player_animated <svg_file> --fullscreen

# Example
./build/svg_player_animated svg_input_samples/girl_hair.fbf.svg
```

### iOS Integration

```c
#include "svg_player_ios.h"

// Create player
SVGPlayerHandle player = SVGPlayer_Create();

// Load SVG
SVGPlayer_LoadSVG(player, "animation.svg");

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

## Project Structure

```
SKIA-BUILD-ARM64/
├── src/                             # Main source code
│   ├── svg_player_animated.cpp      # macOS version
│   ├── svg_player_animated_linux.cpp # Linux version
│   ├── svg_player_animated_windows.cpp # Windows version
│   ├── svg_player_ios.cpp           # iOS library implementation
│   ├── svg_player_ios.h             # iOS public API header
│   ├── file_dialog_windows.cpp      # Windows file dialog
│   └── platform.h                   # Cross-platform abstractions
├── scripts/                         # Build scripts
│   ├── build.sh                     # Master build script
│   ├── build-macos.sh              # macOS build
│   ├── build-macos-arch.sh         # macOS architecture-specific
│   ├── build-linux.sh              # Linux build
│   ├── build-windows.bat           # Windows build
│   ├── build-ios.sh                # iOS build
│   ├── build-skia.sh               # Skia for macOS
│   ├── build-skia-linux.sh         # Skia for Linux
│   └── install-deps.sh             # macOS dependencies
├── build/                           # Build output (gitignored)
├── svg_input_samples/               # Sample SVG files
├── skia-build/                      # Skia build system (submodule)
│   ├── src/skia/                    # Skia source code
│   └── depot_tools/                 # Chromium build tools
├── Makefile                         # Main build configuration
└── README.md                        # This file
```

## Platform-Specific Notes

### macOS

- Uses Metal for GPU-accelerated rendering
- CoreText for font management
- Mach APIs for CPU/thread monitoring
- Homebrew for dependencies (SDL2, ICU)

### Linux

- Uses OpenGL/EGL for rendering
- Fontconfig for font management
- /proc filesystem for CPU monitoring
- System packages for dependencies

### iOS

- Uses Metal for GPU-accelerated rendering
- CoreText for fonts (shared with macOS)
- Builds as static library for UIKit integration
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

## License

This project uses:
- [Skia](https://skia.org/) - BSD 3-Clause License
- [SDL2](https://www.libsdl.org/) - zlib License
