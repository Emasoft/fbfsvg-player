# SVG Video Player - Development Guide

## Project Overview

Multi-platform animated SVG player with SMIL animation support built using Skia.

| Platform | Architecture | GPU Backend | SDK Type |
|----------|--------------|-------------|----------|
| macOS | x64, arm64, universal | Metal | Desktop app |
| Linux | x64, arm64 | OpenGL/EGL | Shared library (.so) |
| iOS/iPadOS | arm64, simulator | Metal | XCFramework |

---

## Directory Structure

```
SKIA-BUILD-ARM64/
├── shared/                      # UNIFIED CROSS-PLATFORM API (single source of truth)
│   ├── svg_player_api.h         # Master C API header - all platforms use this
│   ├── svg_player_api.cpp       # Platform-independent implementation
│   ├── SVGAnimationController.h/.cpp # Core animation logic (C++17)
│   ├── SVGTypes.h               # Shared type definitions
│   └── platform.h               # Platform abstractions
│
├── src/                         # Desktop players (source)
│   ├── svg_player_animated.cpp  # macOS desktop player
│   ├── svg_player_animated_linux.cpp # Linux desktop player
│   └── svg_player_ios.h         # iOS header (forwards to shared/)
│
├── ios-sdk/SVGPlayer/           # iOS SDK components
│   ├── SVGPlayer.h              # Umbrella header
│   ├── SVGPlayerView.h/.mm      # @IBDesignable UIView
│   ├── SVGPlayerController.h/.mm # Obj-C wrapper (uses shared/svg_player_api.h)
│   ├── SVGPlayerMetalRenderer.h/.mm # Metal rendering
│   ├── module.modulemap         # Swift module map
│   └── Info.plist               # Framework info
│
├── macos-sdk/SVGPlayer/         # macOS SDK components
│   ├── svg_player.h             # Forwards to shared/svg_player_api.h
│   ├── SVGPlayerController.h/.mm # Obj-C wrapper for AppKit
│   └── (future: SVGPlayerView)  # NSView wrapper
│
├── linux-sdk/SVGPlayer/         # Linux SDK components
│   ├── svg_player.h             # Forwards to shared/svg_player_api.h
│   ├── svg_player.cpp           # Implementation (includes shared/svg_player_api.cpp)
│   ├── libsvgplayer.map         # Symbol version script
│   └── examples/                # Usage examples
│
├── windows-sdk/SVGPlayer/       # Windows SDK (stub only)
│   └── svg_player.h             # Stub header (forwards to shared/svg_player_api.h)
│
├── docker/                      # Linux development environment
│   ├── Dockerfile               # Ubuntu 24.04 with all deps
│   ├── docker-compose.yml       # Container configuration
│   ├── entrypoint.sh            # Container startup
│   ├── healthcheck.sh           # Environment verification
│   └── README.md                # Docker usage guide
│
├── scripts/                     # Build scripts
│   ├── build-ios-framework.sh   # iOS XCFramework
│   ├── build-linux-sdk.sh       # Linux shared library
│   ├── build-linux.sh           # Linux desktop player
│   ├── build-macos.sh           # macOS desktop player
│   └── ...
│
├── skia-build/                  # Skia build system
│   ├── src/skia/                # Skia source (via depot_tools)
│   │   └── out/
│   │       ├── release-macos/   # macOS Skia libraries
│   │       ├── release-linux/   # Linux Skia libraries
│   │       └── release-ios*/    # iOS Skia libraries
│   ├── depot_tools/             # Chromium build tools
│   ├── build-linux.sh           # Skia Linux build
│   └── build-ios.sh             # Skia iOS build
│
├── build/                       # Build output (gitignored)
│   ├── linux/                   # Linux SDK output
│   │   ├── libsvgplayer.so*     # Shared library
│   │   ├── svg_player.h         # Copied header
│   │   └── svgplayer.pc         # pkg-config
│   └── ios/                     # iOS build output
│
├── svg_input_samples/           # Test SVG files
├── Makefile                     # Main build configuration
└── README.md                    # User documentation
```

---

## Docker Linux Development Environment

### Purpose

The Docker environment provides a consistent Linux development sandbox that:
- Runs on any host (macOS, Windows, Linux)
- Contains all Skia build dependencies pre-installed
- Shares source code via volume mount (changes sync both ways)
- Persists across sessions (container stays running)

### Quick Start

```bash
# Navigate to docker directory
cd docker

# Build the image (first time only, ~5 minutes)
docker-compose build

# Start the container in background
docker-compose up -d

# Enter the development shell
docker-compose exec dev bash

# Inside container - build Skia (first time only, ~3 minutes)
cd /workspace/skia-build && ./build-linux.sh -y

# Inside container - build Linux SDK
cd /workspace && ./scripts/build-linux-sdk.sh

# Exit container (container keeps running)
exit

# Stop container when done
docker-compose down
```

### Container Details

| Property | Value |
|----------|-------|
| Base Image | Ubuntu 24.04 |
| User | developer (UID 1000) |
| Work Directory | /workspace |
| Shared Memory | 2GB (for parallel builds) |
| Memory Limit | 12GB |
| CPU Limit | 8 cores |

### Volume Mounts

| Host Path | Container Path | Purpose |
|-----------|----------------|---------|
| `..` (project root) | `/workspace` | Source code (read/write) |
| `~/.gitconfig` | `/home/developer/.gitconfig` | Git config (read-only) |
| Named volume | `/home/developer/.bash_history_dir` | Bash history |

### Important Notes

1. **Source code is shared**: Any changes inside `/workspace` immediately appear on host
2. **Build artifacts are shared**: `build/linux/` is visible on both host and container
3. **Skia source is shared**: `skia-build/` is the same on host and container
4. **Container persists**: Use `docker-compose down` to stop, not `exit`

### Troubleshooting

```bash
# Check container status
docker-compose ps

# View container logs
docker-compose logs

# Restart container
docker-compose restart

# Rebuild image after Dockerfile changes
docker-compose build --no-cache

# Run health check inside container
docker-compose exec dev healthcheck.sh

# Fix permission issues (inside container)
sudo chown -R developer:developer /workspace
```

---

## Shared Components Architecture

### Unified C API (Single Source of Truth)

The `shared/svg_player_api.h` header is the **single source of truth** for all platforms:

```
shared/svg_player_api.h  (MASTER - the only API definition)
       │
       ├──► ios-sdk/      (SVGPlayerController wraps C API)
       ├──► macos-sdk/    (SVGPlayerController wraps C API)
       ├──► linux-sdk/    (svg_player.h forwards to shared/)
       └──► windows-sdk/  (svg_player.h forwards to shared/ - stub)
```

All platform SDK headers simply `#include "../../shared/svg_player_api.h"`.

### API Modification Rules

**CRITICAL**: Only modify `shared/svg_player_api.h` when changing the public API.
Platform SDK headers are thin forwarders and should rarely need changes.

| File | Role | When to Update |
|------|------|----------------|
| `shared/svg_player_api.h` | Master C API definition | When adding/changing API |
| `shared/svg_player_api.cpp` | Implementation | When adding/changing API |
| `*-sdk/SVGPlayer/svg_player.h` | Platform forwarder | Rarely (platform extensions only) |
| `*-sdk/SVGPlayer/SVGPlayerController.h` | Obj-C wrapper | To expose new APIs to Obj-C/Swift |

### Shared Type Definitions

These types must be identical across all implementations:

```c
// Playback states
typedef enum {
    SVGPlaybackState_Stopped = 0,
    SVGPlaybackState_Playing,
    SVGPlaybackState_Paused
} SVGPlaybackState;

// Repeat modes
typedef enum {
    SVGRepeatMode_None = 0,
    SVGRepeatMode_Loop,
    SVGRepeatMode_Reverse,
    SVGRepeatMode_Count
} SVGRepeatMode;

// Statistics structure
typedef struct {
    double renderTimeMs;
    double updateTimeMs;
    double animationTimeMs;
    int currentFrame;
    int totalFrames;
    double fps;
    size_t peakMemoryBytes;
    int elementsRendered;
} SVGRenderStats;
```

---

## Development Phase Policy

**CRITICAL - NO BACKWARD COMPATIBILITY CODE**

This project is in **pre-alpha development**. The following rules apply:

1. **NO backward compatibility aliases** - No `typedef OldName NewName` for legacy support
2. **NO fallback code** - Code either works as intended or fails immediately
3. **NO legacy function wrappers** - Only one version of each function exists
4. **NO deprecated APIs** - Remove old APIs entirely when changing them
5. **NO commented-out legacy code** - Delete, don't comment out

**Rationale**: No public release has been made. Backward compatibility adds complexity
without benefit. When the API is finalized for 1.0, backward compatibility may be added.

**If you need to change an API**:
1. Update `shared/svg_player_api.h` directly
2. Update implementations in `shared/svg_player_api.cpp`
3. Update all callers (iOS/macOS/Linux SDK wrappers)
4. Test on all platforms
5. Commit as a single atomic change

---

## Safe Development Workflow

### Before Making Changes

1. **Check current branch and status**
   ```bash
   git status
   git branch -v
   ```

2. **Create feature branch if needed**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Ensure builds are working**
   ```bash
   # On macOS
   make macos

   # On Linux (via Docker)
   cd docker && docker-compose exec dev bash -c 'cd /workspace && make linux-sdk'
   ```

### Making API Changes

When modifying the public API (unified API only):

1. **Update master header**
   ```bash
   # Edit shared/svg_player_api.h
   ```

2. **Update master implementation**
   ```bash
   # Edit shared/svg_player_api.cpp
   ```

3. **Update Obj-C wrappers if exposing to Swift/Obj-C**
   ```bash
   # Edit ios-sdk/SVGPlayer/SVGPlayerController.h/.mm
   # Edit macos-sdk/SVGPlayer/SVGPlayerController.h/.mm
   ```

4. **Test on all platforms**
   ```bash
   # Build and test all platforms with single command:
   ./scripts/build-all.sh

   # Or individually:
   make macos              # macOS desktop player
   make ios-framework      # iOS XCFramework
   ./scripts/build-linux-sdk.sh  # Linux (in Docker)
   ```

### Commit Guidelines

```bash
# Stage unified API files together
git add shared/svg_player_api.h
git add shared/svg_player_api.cpp

# Stage Obj-C wrappers if changed
git add ios-sdk/SVGPlayer/SVGPlayerController.h
git add ios-sdk/SVGPlayer/SVGPlayerController.mm
git add macos-sdk/SVGPlayer/SVGPlayerController.h
git add macos-sdk/SVGPlayer/SVGPlayerController.mm

# Use descriptive commit messages
git commit -m "$(cat <<'EOF'
Add SVGPlayer_GetElementBounds API

- Added to shared/svg_player_api.h (unified API)
- Implemented in shared/svg_player_api.cpp
- Exposed via SVGPlayerController for iOS/macOS
EOF
)"
```

---

## Build Targets Quick Reference

### Skia (Build Once)

```bash
# macOS (on macOS host)
make skia-macos

# Linux (inside Docker container)
cd /workspace/skia-build && ./build-linux.sh -y

# iOS (on macOS host)
make skia-ios
```

### Platform SDKs

```bash
# iOS XCFramework (on macOS host)
make ios-framework

# Linux shared library (inside Docker container)
./scripts/build-linux-sdk.sh

# Or from host via docker-compose
cd docker && docker-compose exec dev bash -c 'cd /workspace && ./scripts/build-linux-sdk.sh'
```

### Desktop Players

```bash
# macOS
make macos

# Linux (inside Docker)
make linux
```

---

## CI/CD Considerations

### GitHub Actions for Linux

```yaml
# Use Docker for consistent Linux builds
jobs:
  linux:
    runs-on: ubuntu-latest
    container:
      image: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          apt-get update
          apt-get install -y build-essential clang ...
      - name: Build
        run: ./scripts/build-linux-sdk.sh -y
```

### Artifact Locations

| Platform | Primary Artifact | Location |
|----------|------------------|----------|
| iOS | XCFramework | `build/SVGPlayer.xcframework/` |
| Linux | Shared library | `build/linux/libsvgplayer.so` |
| macOS | Binary | `build/svg_player_animated` |

---

## Troubleshooting

### Skia Build Issues

```bash
# Check Skia output directory
ls -la skia-build/src/skia/out/

# Verify library architecture
file skia-build/src/skia/out/release-linux/libskia.a

# Check for macOS binaries in Linux build (common issue)
file skia-build/src/skia/bin/gn
file skia-build/src/skia/bin/ninja
# Should show "ELF 64-bit" for Linux, not "Mach-O"
```

### Docker Issues

```bash
# Docker daemon not running
docker info

# Container not starting
docker-compose logs

# Permission issues
docker-compose exec dev sudo chown -R developer:developer /workspace

# Out of disk space
docker system prune -a
```

### API Mismatch

If you get linker errors about undefined symbols:

1. Check that headers are synchronized
2. Verify implementation exists for all declared functions
3. Check symbol visibility in `libsvgplayer.map`

---

## Development Environment Requirements

### macOS Host (for iOS/macOS development)

- Xcode 15+ with iOS SDK
- Homebrew with: SDL2, pkg-config, icu4c
- Docker Desktop (for Linux development)

### Linux Host (native Linux development)

- Ubuntu 22.04+ or equivalent
- Clang 15+, CMake, Ninja
- All dependencies from `docker/Dockerfile`

### Docker (for cross-platform Linux development)

- Docker Desktop (macOS/Windows) or Docker Engine (Linux)
- At least 8GB RAM allocated to Docker
- At least 20GB free disk space

---

## File Ownership

| File/Directory | Owner | Notes |
|----------------|-------|-------|
| `shared/svg_player_api.h` | API Team | **Master API definition (single source of truth)** |
| `shared/svg_player_api.cpp` | API Team | **Master implementation** |
| `shared/SVGTypes.h` | API Team | Shared type definitions |
| `ios-sdk/` | iOS Team | iOS-specific wrappers |
| `macos-sdk/` | macOS Team | macOS-specific wrappers |
| `linux-sdk/` | Linux Team | Linux implementation |
| `windows-sdk/` | Windows Team | Windows stub (pending implementation) |
| `docker/` | DevOps | Container configuration |
| `skia-build/` | Build System | Managed by scripts |
