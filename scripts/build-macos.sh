#!/bin/bash

# build-macos.sh - Build SVG player for macOS
# Usage: ./build-macos.sh [--universal]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# Parse command line arguments
build_universal=false
build_type="release"
non_interactive=false

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  --universal         Build universal binary (x64 + arm64)"
    echo "  --debug             Build with debug symbols"
    echo "  -y, --non-interactive  Run without prompts (for CI/CD)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build for current architecture"
    echo "  $0 --universal        # Build universal binary"
    echo "  $0 --universal --debug  # Build universal debug binary"
}

for arg in "$@"; do
    case $arg in
        --universal)
            build_universal=true
            shift
            ;;
        --debug)
            build_type="debug"
            shift
            ;;
        -y|--non-interactive)
            non_interactive=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $arg"
            usage
            exit 1
            ;;
    esac
done

# Detect current architecture
current_arch=$(uname -m)
if [ "$current_arch" = "x86_64" ]; then
    current_arch="x64"
elif [ "$current_arch" = "arm64" ]; then
    current_arch="arm64"
else
    log_error "Unsupported architecture: $current_arch"
    exit 1
fi

BUILD_DIR="$PROJECT_ROOT/build"
mkdir -p "$BUILD_DIR"

if [ "$build_universal" = true ]; then
    log_step "Building universal binaries (x64 + arm64)..."

    # Build both architectures with specified build type
    log_step "Building for x64..."
    "$SCRIPT_DIR/build-macos-arch.sh" x64 "$build_type"

    log_step "Building for arm64..."
    "$SCRIPT_DIR/build-macos-arch.sh" arm64 "$build_type"

    # Create universal binary using lipo
    log_step "Creating universal binary..."

    X64_BIN="$BUILD_DIR/svg_player_animated-macos-x64"
    ARM64_BIN="$BUILD_DIR/svg_player_animated-macos-arm64"
    UNIVERSAL_BIN="$BUILD_DIR/svg_player_animated"

    if [ -f "$X64_BIN" ] && [ -f "$ARM64_BIN" ]; then
        lipo -create "$X64_BIN" "$ARM64_BIN" -output "$UNIVERSAL_BIN"

        log_info "Universal binary created: $UNIVERSAL_BIN"

        # Verify architectures
        log_info "Architectures in universal binary:"
        lipo -info "$UNIVERSAL_BIN"

        # Clean up architecture-specific binaries
        rm -f "$X64_BIN" "$ARM64_BIN"
    else
        log_error "Failed to create universal binary - architecture builds missing"
        exit 1
    fi
else
    log_step "Building for current architecture ($current_arch)..."

    "$SCRIPT_DIR/build-macos-arch.sh" "$current_arch" "$build_type"

    # Copy to generic name
    ARCH_BIN="$BUILD_DIR/svg_player_animated-macos-$current_arch"
    GENERIC_BIN="$BUILD_DIR/svg_player_animated"

    if [ -f "$ARCH_BIN" ]; then
        cp "$ARCH_BIN" "$GENERIC_BIN"
        rm -f "$ARCH_BIN"
        log_info "Build complete: $GENERIC_BIN"
    fi
fi

echo ""
log_info "=== Build Summary ==="
log_info "Platform: macOS"
if [ "$build_universal" = true ]; then
    log_info "Architecture: Universal (x64 + arm64)"
else
    log_info "Architecture: $current_arch"
fi
log_info "Build type: $build_type"
log_info "Output: $BUILD_DIR/svg_player_animated"
echo ""
log_info "Run with: $BUILD_DIR/svg_player_animated <svg_file>"
