#!/bin/bash
# setup-benchmark.sh - Build and configure the SVG benchmark environment
#
# This script:
# 1. Builds the Docker image with ThorVG
# 2. Builds Skia for Linux (if not already built)
# 3. Builds fbfsvg-player for Linux
# 4. Installs the player into the container
#
# REQUIREMENTS:
#   - Docker with NVIDIA Container Toolkit (for NVIDIA GPUs)
#   - X11 display server running
#   - At least 20GB free disk space
#
# USAGE:
#   ./setup-benchmark.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

echo ""
echo "========================================"
echo "  SVG Player Benchmark Setup"
echo "========================================"
echo ""

# Check Docker
if ! command -v docker &> /dev/null; then
    log_error "Docker not found. Please install Docker first."
    exit 1
fi

# Check for NVIDIA Container Toolkit
if command -v nvidia-smi &> /dev/null; then
    log_info "NVIDIA GPU detected"
    if docker info 2>/dev/null | grep -q "nvidia"; then
        log_info "NVIDIA Container Toolkit is configured"
    else
        log_warn "NVIDIA Container Toolkit may not be configured"
        log_info "Install with: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html"
    fi
else
    log_warn "No NVIDIA GPU detected - will use Intel/AMD GPU or software rendering"
fi

# Step 1: Build Docker image
log_step "1/4 Building Docker benchmark image..."
cd "$SCRIPT_DIR"
docker-compose build benchmark

# Step 2: Check if Skia is built for Linux
log_step "2/4 Checking Skia build..."
SKIA_LINUX="$PROJECT_ROOT/skia-build/src/skia/out/release-linux"
if [ -f "$SKIA_LINUX/libskia.a" ]; then
    # Verify it's actually a Linux binary
    if file "$SKIA_LINUX/libskia.a" | grep -q "current ar archive"; then
        log_info "Skia Linux build found"
    else
        log_warn "Skia build exists but may be for wrong platform"
        log_info "Building Skia for Linux inside container..."
        docker-compose run --rm benchmark bash -c '
            cd /workspace/fbfsvg-player/skia-build
            ./build-linux.sh -y
        '
    fi
else
    log_info "Building Skia for Linux inside container..."
    docker-compose run --rm benchmark bash -c '
        cd /workspace/fbfsvg-player/skia-build
        ./build-linux.sh -y
    '
fi

# Step 3: Build fbfsvg-player for Linux
log_step "3/4 Building fbfsvg-player for Linux..."
docker-compose run --rm benchmark bash -c '
    cd /workspace/fbfsvg-player
    ./scripts/build-linux.sh -y

    # Copy to system path
    ARCH=$(uname -m)
    if [ "$ARCH" = "x86_64" ]; then
        ARCH_SUFFIX="x64"
    else
        ARCH_SUFFIX="arm64"
    fi

    if [ -f "build/svg_player_animated_linux_$ARCH_SUFFIX" ]; then
        sudo cp "build/svg_player_animated_linux_$ARCH_SUFFIX" /usr/local/bin/svg_player_animated_linux
        sudo chmod +x /usr/local/bin/svg_player_animated_linux
        echo "Installed svg_player_animated_linux to /usr/local/bin/"
    else
        echo "Warning: Could not find built player binary"
        ls -la build/
    fi
'

# Step 4: Verify installation
log_step "4/4 Verifying installation..."
docker-compose run --rm benchmark bash -c '
    echo "=== Installed Players ==="
    echo ""
    echo "ThorVG player:"
    which thorvg_player && thorvg_player --help 2>&1 | head -3 || echo "  Not found"
    echo ""
    echo "fbfsvg-player:"
    which svg_player_animated_linux && svg_player_animated_linux --help 2>&1 | head -3 || echo "  Not found"
    echo ""
    echo "=== GPU Status ==="
    if [ -d /dev/dri ]; then
        echo "DRI devices:"
        ls -la /dev/dri/
    else
        echo "No GPU devices found"
    fi
'

# Create results directory
mkdir -p "$SCRIPT_DIR/results"

log_info ""
log_info "========================================"
log_info "  Setup Complete!"
log_info "========================================"
log_info ""
log_info "To run benchmarks:"
log_info ""
log_info "  # Interactive shell:"
log_info "  cd $SCRIPT_DIR"
log_info "  docker-compose run --rm benchmark"
log_info ""
log_info "  # Quick comparison:"
log_info "  docker-compose run --rm benchmark quick-compare /workspace/svg_input_samples/seagull.fbf.svg 10"
log_info ""
log_info "  # Full benchmark suite:"
log_info "  docker-compose run --rm benchmark run-benchmark --duration 30 --fullscreen /workspace/svg_input_samples/*.svg"
log_info ""
log_info "Results will be saved to: $SCRIPT_DIR/results/"
log_info ""
