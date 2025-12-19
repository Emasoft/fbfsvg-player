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
├── src/                         # Core source code (shared logic)
│   ├── svg_player_animated.cpp  # macOS desktop player
│   ├── svg_player_animated_linux.cpp  # Linux desktop player
│   ├── svg_player_ios.cpp       # iOS C API implementation
│   ├── svg_player_ios.h         # iOS C API header (SHARED)
│   └── platform.h               # Cross-platform abstractions
│
├── ios-sdk/SVGPlayer/           # iOS SDK components
│   ├── SVGPlayer.h              # Umbrella header
│   ├── SVGPlayerView.h/.mm      # @IBDesignable UIView
│   ├── SVGPlayerController.h/.mm # Obj-C wrapper around C API
│   ├── SVGPlayerMetalRenderer.h/.mm # Metal rendering
│   ├── module.modulemap         # Swift module map
│   └── Info.plist               # Framework info
│
├── linux-sdk/SVGPlayer/         # Linux SDK components
│   ├── svg_player.h             # Public C API header
│   ├── svg_player.cpp           # Implementation
│   ├── libsvgplayer.map         # Symbol version script
│   └── examples/                # Usage examples
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

### Core C API (Foundation)

The `svg_player_ios.h` header defines the core C API used by all platforms:

```
src/svg_player_ios.h  (MASTER - defines the C API contract)
       │
       ├──► ios-sdk/SVGPlayer/  (Wraps C API in Obj-C/Swift)
       │    └── SVGPlayerController wraps SVGPlayer_* functions
       │
       └──► linux-sdk/SVGPlayer/  (Implements same C API)
            └── svg_player.h mirrors the API contract
```

### API Synchronization Rules

**CRITICAL**: The following files must stay synchronized:

| File | Role | When to Update |
|------|------|----------------|
| `src/svg_player_ios.h` | Master C API definition | When adding/changing API |
| `linux-sdk/SVGPlayer/svg_player.h` | Linux API header | Mirror changes from master |
| `ios-sdk/SVGPlayer/SVGPlayerController.h` | iOS Obj-C wrapper | Update to expose new APIs |

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

When modifying the public API:

1. **Update master header first**
   ```bash
   # Edit src/svg_player_ios.h
   ```

2. **Update Linux SDK header to match**
   ```bash
   # Edit linux-sdk/SVGPlayer/svg_player.h
   # Ensure function signatures match exactly
   ```

3. **Update iOS wrapper if needed**
   ```bash
   # Edit ios-sdk/SVGPlayer/SVGPlayerController.h/.mm
   ```

4. **Update implementations**
   ```bash
   # Edit src/svg_player_ios.cpp (iOS)
   # Edit linux-sdk/SVGPlayer/svg_player.cpp (Linux)
   ```

5. **Test on all platforms**
   ```bash
   # macOS/iOS
   make ios-framework

   # Linux (via Docker)
   cd docker && docker-compose exec dev bash -c 'cd /workspace && ./scripts/build-linux-sdk.sh'
   ```

### Commit Guidelines

```bash
# Stage related files together
git add src/svg_player_ios.h
git add linux-sdk/SVGPlayer/svg_player.h
git add ios-sdk/SVGPlayer/SVGPlayerController.h

# Use descriptive commit messages
git commit -m "$(cat <<'EOF'
Add SVGPlayer_GetElementBounds API

- Added to src/svg_player_ios.h (master)
- Synced to linux-sdk/SVGPlayer/svg_player.h
- Exposed via SVGPlayerController for iOS
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
| `src/svg_player_ios.h` | API Team | Master API definition |
| `ios-sdk/` | iOS Team | iOS-specific wrappers |
| `linux-sdk/` | Linux Team | Linux implementation |
| `docker/` | DevOps | Container configuration |
| `skia-build/` | Build System | Managed by scripts |
