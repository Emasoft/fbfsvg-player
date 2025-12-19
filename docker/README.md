# SVGPlayer Linux Development Docker Environment

This Docker setup provides a complete Linux development environment for building Skia and the SVGPlayer SDK.

## Prerequisites

- Docker Desktop (macOS/Windows) or Docker Engine (Linux)
- docker-compose (usually included with Docker Desktop)
- At least 8GB RAM allocated to Docker
- At least 20GB free disk space (Skia build is large)

## Quick Start

### Option 1: Using docker-compose (Recommended)

```bash
# Navigate to docker directory
cd docker

# Build the image
docker-compose build

# Start the container in background
docker-compose up -d

# Enter the development shell
docker-compose exec dev bash

# When done, stop the container
docker-compose down
```

### Option 2: Manual Docker Commands

```bash
# Navigate to docker directory
cd docker

# Build the image
docker build -t svgplayer-linux-dev .

# Run interactively with project mounted
docker run -it --rm \
  -v "$(pwd)/..:/workspace" \
  -w /workspace \
  svgplayer-linux-dev

# Or run in background
docker run -d --name svgplayer-dev \
  -v "$(pwd)/..:/workspace" \
  -w /workspace \
  svgplayer-linux-dev \
  tail -f /dev/null

# Then attach to it
docker exec -it svgplayer-dev bash
```

## Building Inside the Container

Once inside the container:

### 1. Build Skia (First Time Only)

```bash
cd /workspace/skia-build
./build-linux.sh -y
```

This takes 30-60 minutes depending on your system. The `-y` flag skips confirmation prompts.

### 2. Build SVGPlayer SDK

```bash
cd /workspace
make linux-sdk
```

### 3. Test the Build

```bash
cd /workspace/linux-sdk/examples
gcc -o simple_player simple_player.c \
    -I../SVGPlayer \
    -L../../build/linux -lsvgplayer \
    -Wl,-rpath,../../build/linux -lm

./simple_player /workspace/svg_input_samples/girl_hair.fbf.svg
```

## Included Tools

| Category | Tools |
|----------|-------|
| Compilers | clang, clang++, gcc, g++ |
| Build | make, ninja, cmake |
| Python | python3, pip |
| Version Control | git, git-lfs |
| Debugging | gdb, valgrind, strace |
| Code Quality | clang-format, clang-tidy |

## Included Libraries

| Category | Libraries |
|----------|-----------|
| Graphics | EGL, GLES2, OpenGL, X11, Wayland |
| Fonts | FreeType, FontConfig, HarfBuzz |
| Images | libpng, libjpeg-turbo, libwebp, libgif |
| Other | zlib, expat, ICU, SDL2 |

## Health Check

Run the health check script to verify the environment:

```bash
healthcheck.sh
```

## Persistent Development

The docker-compose setup keeps your container running and preserves:

- **Project files**: Mounted from host at `/workspace`
- **Bash history**: Preserved in a Docker volume
- **Git config**: Mounted read-only from host

## Troubleshooting

### Permission Issues

If you encounter permission issues with mounted files:

```bash
# Inside container
sudo chown -R developer:developer /workspace
```

Or rebuild with your UID:

```bash
docker-compose build --build-arg USER_UID=$(id -u) --build-arg USER_GID=$(id -g)
```

### depot_tools Not Found

If depot_tools is not in PATH:

```bash
cd /workspace/skia-build
./get_depot_tools.sh
export PATH="/workspace/skia-build/depot_tools:$PATH"
```

### Out of Memory During Skia Build

Reduce parallel jobs:

```bash
cd /workspace/skia-build/src/skia
bin/ninja -C out/release-linux -j2
```

Or increase Docker memory allocation in Docker Desktop settings.

### Build Artifacts from macOS

If you previously built on macOS, clean before building on Linux:

```bash
cd /workspace
make clean
rm -rf skia-build/src/skia/out/release-linux
```

## Architecture Support

This image supports:
- **linux/amd64** (x86_64)
- **linux/arm64** (aarch64) - for Apple Silicon Macs running in emulation or ARM Linux

For best performance on Apple Silicon, use native arm64:

```bash
docker-compose build --build-arg BUILDPLATFORM=linux/arm64
```

## Resource Recommendations

| Use Case | RAM | CPU Cores |
|----------|-----|-----------|
| Minimal | 4GB | 2 |
| Recommended | 8GB | 4 |
| Fast builds | 16GB | 8 |

Adjust in `docker-compose.yml` under `deploy.resources`.
