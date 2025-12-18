#!/bin/bash

# build.sh - Master build script for SVG Video Player
# Automatically detects platform and delegates to appropriate build script
#
# Usage: ./build.sh [OPTIONS]
#
# General Options:
#   --platform <name>    Override platform detection (macos, linux, ios)
#   --debug              Build with debug symbols
#   -h, --help           Show this help message
#
# macOS Options:
#   --universal          Build universal binary (x64 + arm64)
#
# iOS Options:
#   --device             Build for iOS device (arm64)
#   --simulator          Build for iOS simulator
#   --xcframework        Build XCFramework
#
# Linux Options:
#   -y, --non-interactive  Skip confirmation prompts

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# Detect platform
detect_platform() {
    local os=$(uname -s)
    case "$os" in
        Darwin)
            echo "macos"
            ;;
        Linux)
            echo "linux"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# Detect architecture
detect_arch() {
    local arch=$(uname -m)
    case "$arch" in
        x86_64)
            echo "x64"
            ;;
        arm64|aarch64)
            echo "arm64"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# Show banner
show_banner() {
    echo -e "${BOLD}"
    echo "=================================================="
    echo "       SVG Video Player - Build System"
    echo "=================================================="
    echo -e "${NC}"
}

# Show help
show_help() {
    show_banner
    cat << 'EOF'
Usage: ./build.sh [OPTIONS]

General Options:
  --platform <name>      Override platform detection (macos, linux, ios)
  --debug                Build with debug symbols
  --clean                Clean build directory before building
  -h, --help             Show this help message

macOS Options:
  --universal            Build universal binary (x64 + arm64)

iOS Options:
  --device               Build for iOS device (arm64) [default for iOS]
  --simulator            Build for iOS simulator
  --xcframework          Build XCFramework (device + universal simulator)

Linux Options:
  -y, --non-interactive  Skip confirmation prompts (useful for CI)

Skia Options:
  --build-skia           Build Skia before building SVG player
  --skia-only            Only build Skia, not the SVG player

Examples:
  ./build.sh                        # Build for current platform
  ./build.sh --universal            # Build macOS universal binary
  ./build.sh --platform linux       # Build for Linux
  ./build.sh --platform ios         # Build for iOS device
  ./build.sh --platform ios --xcframework  # Build iOS XCFramework
  ./build.sh --build-skia           # Build Skia then SVG player
  ./build.sh --clean --debug        # Clean build, then debug build

Supported Platforms:
  - macOS (x64, arm64, universal)
  - Linux (x64, arm64)
  - iOS (device arm64, simulator x64/arm64, XCFramework)

EOF
}

# Parse arguments
platform=""
build_skia=false
skia_only=false
clean_build=false
extra_args=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --platform)
            platform="$2"
            shift 2
            ;;
        --build-skia)
            build_skia=true
            shift
            ;;
        --skia-only)
            skia_only=true
            build_skia=true
            shift
            ;;
        --clean)
            clean_build=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            # Pass through to platform-specific script
            extra_args+=("$1")
            shift
            ;;
    esac
done

# Auto-detect platform if not specified
if [ -z "$platform" ]; then
    platform=$(detect_platform)
fi

arch=$(detect_arch)

show_banner
log_info "Platform: $platform"
log_info "Architecture: $arch"
echo ""

# Validate platform
case "$platform" in
    macos|linux|ios)
        ;;
    *)
        log_error "Unsupported platform: $platform"
        log_info "Supported platforms: macos, linux, ios"
        exit 1
        ;;
esac

# Clean if requested
if [ "$clean_build" = true ]; then
    log_step "Cleaning build directory..."
    rm -rf "$PROJECT_ROOT/build"
    mkdir -p "$PROJECT_ROOT/build"
fi

# Build Skia if requested
if [ "$build_skia" = true ]; then
    log_step "Building Skia dependency..."

    case "$platform" in
        macos)
            "$SCRIPT_DIR/build-skia.sh"
            ;;
        linux)
            "$SCRIPT_DIR/build-skia-linux.sh"
            ;;
        ios)
            # iOS Skia build requires specific targets
            if [[ " ${extra_args[*]} " =~ " --xcframework " ]]; then
                log_info "Building Skia for iOS (device + simulator)..."
                cd "$PROJECT_ROOT/skia-build"
                ./build-ios.sh --device
                ./build-ios.sh --simulator --universal
            elif [[ " ${extra_args[*]} " =~ " --simulator " ]]; then
                log_info "Building Skia for iOS simulator..."
                cd "$PROJECT_ROOT/skia-build"
                ./build-ios.sh --simulator
            else
                log_info "Building Skia for iOS device..."
                cd "$PROJECT_ROOT/skia-build"
                ./build-ios.sh --device
            fi
            ;;
    esac

    if [ "$skia_only" = true ]; then
        log_info "Skia build complete (--skia-only specified)"
        exit 0
    fi

    echo ""
fi

# Build SVG player
log_step "Building SVG Video Player..."

case "$platform" in
    macos)
        "$SCRIPT_DIR/build-macos.sh" "${extra_args[@]}"
        ;;
    linux)
        "$SCRIPT_DIR/build-linux.sh" "${extra_args[@]}"
        ;;
    ios)
        "$SCRIPT_DIR/build-ios.sh" "${extra_args[@]}"
        ;;
esac

echo ""
log_info "Build complete!"
