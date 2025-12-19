#!/bin/bash

# build-macos-arch.sh - Build SVG player for specific macOS architecture
# Usage: ./build-macos-arch.sh <arch> [build_type]
# Where <arch> is either "arm64" or "x64"
# And <build_type> is either "release" (default) or "debug"

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Function to detect ICU installation
detect_icu() {
    log_info "Detecting ICU installation..."

    # Check Homebrew ICU first
    if command -v brew >/dev/null 2>&1; then
        icu_prefix=$(brew --prefix icu4c 2>/dev/null)
        if [ -n "$icu_prefix" ] && [ -d "$icu_prefix" ]; then
            if [ -d "$icu_prefix/include/unicode" ]; then
                export ICU_ROOT="$icu_prefix"
                icu_version=$(brew list --versions icu4c 2>/dev/null | head -1 | awk '{print $2}')
                log_info "Found ICU via Homebrew at: $ICU_ROOT"
                if [ -n "$icu_version" ]; then
                    log_info "ICU version: $icu_version"
                fi
                return 0
            fi
        fi
    fi

    # Check standard macOS locations
    for path in /opt/homebrew /usr/local; do
        if [ -d "$path/include/unicode" ] && [ -d "$path/lib" ]; then
            if ls "$path/lib/"*icu* >/dev/null 2>&1; then
                export ICU_ROOT="$path"
                log_info "Found ICU at: $ICU_ROOT"
                return 0
            fi
        fi
    done

    log_error "No compatible system ICU found"
    log_info "To install ICU via Homebrew: brew install icu4c"
    return 1
}

# Check for SDL2
check_sdl2() {
    log_info "Checking for SDL2..."

    if ! command -v pkg-config >/dev/null 2>&1; then
        log_error "pkg-config not found"
        log_info "To install: brew install pkg-config"
        return 1
    fi

    if ! pkg-config --exists sdl2 2>/dev/null; then
        log_error "SDL2 not found"
        log_info "To install: brew install sdl2"
        return 1
    fi

    sdl_version=$(pkg-config --modversion sdl2 2>/dev/null)
    log_info "Found SDL2 version: $sdl_version"
    return 0
}

# Check for Skia libraries
check_skia() {
    log_info "Checking for Skia libraries..."

    local skia_dir="$PROJECT_ROOT/skia-build/src/skia/out/release-macos"

    if [ ! -f "$skia_dir/libskia.a" ]; then
        log_error "Skia libraries not found at: $skia_dir"
        log_info "Run './scripts/build-skia.sh' to build Skia first"
        return 1
    fi

    # Verify architecture
    local arch_info=$(lipo -info "$skia_dir/libskia.a" 2>/dev/null || echo "unknown")
    log_info "Skia library architectures: $arch_info"
    return 0
}

# Parse arguments
if [ $# -eq 0 ]; then
    log_error "Architecture required"
    echo "Usage: $0 <arch> [build_type]"
    echo "  <arch>        Target architecture (arm64 or x64)"
    echo "  [build_type]  Optional: release (default) or debug"
    exit 1
fi

arch=$1
build_type="${2:-release}"

# Validate architecture
if [ "$arch" != "arm64" ] && [ "$arch" != "x64" ]; then
    log_error "Invalid architecture '$arch'"
    echo "Supported architectures: arm64, x64"
    exit 1
fi

# Validate build type
if [ "$build_type" != "release" ] && [ "$build_type" != "debug" ]; then
    log_error "Invalid build type '$build_type'"
    echo "Supported build types: release, debug"
    exit 1
fi

log_info "Building SVG Player for macOS ($arch)..."

# Check dependencies
if ! check_sdl2; then
    exit 1
fi

if ! detect_icu; then
    exit 1
fi

if ! check_skia; then
    exit 1
fi

# Compiler settings
CXX="clang++"
if [ "$build_type" = "debug" ]; then
    CXXFLAGS="-std=c++17 -g -O0 -DDEBUG"
    log_info "Build type: DEBUG"
else
    CXXFLAGS="-std=c++17 -O2 -DNDEBUG"
    log_info "Build type: RELEASE"
fi

# Set architecture flag
if [ "$arch" = "arm64" ]; then
    ARCH_FLAG="-arch arm64"
else
    ARCH_FLAG="-arch x86_64"
fi

# Directories
SKIA_DIR="$PROJECT_ROOT/skia-build/src/skia"
SRC_DIR="$PROJECT_ROOT/src"
SHARED_DIR="$PROJECT_ROOT/shared"
BUILD_DIR="$PROJECT_ROOT/build"

# Create build directory
mkdir -p "$BUILD_DIR"

# Output binary name
TARGET="$BUILD_DIR/svg_player_animated-macos-$arch"

# Include paths (includes shared/ for SVGAnimationController)
INCLUDES="-I$SKIA_DIR -I$SKIA_DIR/include -I$SKIA_DIR/modules -I$PROJECT_ROOT $(pkg-config --cflags sdl2)"

# Skia static libraries (order matters for linking)
SKIA_LIBS="$SKIA_DIR/out/release-macos/libsvg.a \
           $SKIA_DIR/out/release-macos/libskia.a \
           $SKIA_DIR/out/release-macos/libskresources.a \
           $SKIA_DIR/out/release-macos/libskshaper.a \
           $SKIA_DIR/out/release-macos/libharfbuzz.a \
           $SKIA_DIR/out/release-macos/libskunicode_core.a \
           $SKIA_DIR/out/release-macos/libskunicode_icu.a \
           $SKIA_DIR/out/release-macos/libexpat.a \
           $SKIA_DIR/out/release-macos/libpng.a \
           $SKIA_DIR/out/release-macos/libzlib.a \
           $SKIA_DIR/out/release-macos/libjpeg.a \
           $SKIA_DIR/out/release-macos/libwebp.a \
           $SKIA_DIR/out/release-macos/libwuffs.a"

# External libraries
LDFLAGS="$(pkg-config --libs sdl2) -L$ICU_ROOT/lib -licuuc -licui18n -licudata -liconv"

# macOS frameworks
FRAMEWORKS="-framework CoreGraphics \
            -framework CoreText \
            -framework CoreFoundation \
            -framework ApplicationServices \
            -framework Metal \
            -framework MetalKit \
            -framework Cocoa \
            -framework IOKit \
            -framework IOSurface \
            -framework OpenGL \
            -framework QuartzCore"

log_info "Compiling SVG player with shared animation controller..."
log_info "Sources: $SRC_DIR/svg_player_animated.cpp"
log_info "         $SHARED_DIR/SVGAnimationController.cpp"
log_info "Target: $TARGET"

# Build command - includes shared animation controller
$CXX $CXXFLAGS $ARCH_FLAG $INCLUDES \
    "$SRC_DIR/svg_player_animated.cpp" \
    "$SHARED_DIR/SVGAnimationController.cpp" \
    -o "$TARGET" \
    $SKIA_LIBS \
    $LDFLAGS \
    $FRAMEWORKS

if [ $? -eq 0 ]; then
    log_info "Build successful: $TARGET"

    # Show binary info
    file "$TARGET"
    lipo -info "$TARGET" 2>/dev/null || true
else
    log_error "Build failed"
    exit 1
fi
