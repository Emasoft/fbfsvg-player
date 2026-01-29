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

# Check for Skia libraries (architecture-specific)
check_skia() {
    log_info "Checking for Skia libraries ($arch)..."

    # Use architecture-specific Skia directory
    local skia_dir="$PROJECT_ROOT/skia-build/src/skia/out/release-macos-$arch"

    # Fallback to generic release-macos if arch-specific not found
    if [ ! -f "$skia_dir/libskia.a" ]; then
        skia_dir="$PROJECT_ROOT/skia-build/src/skia/out/release-macos"
    fi

    if [ ! -f "$skia_dir/libskia.a" ]; then
        log_error "Skia libraries not found"
        log_info "Expected at: $PROJECT_ROOT/skia-build/src/skia/out/release-macos-$arch"
        log_info "Run 'cd skia-build && ./build-macos-$arch.sh' to build Skia first"
        return 1
    fi

    # Verify architecture matches
    local arch_info=$(lipo -info "$skia_dir/libskia.a" 2>/dev/null || echo "unknown")
    log_info "Skia library: $skia_dir"
    log_info "Skia architectures: $arch_info"

    # Export for use in build
    export SKIA_OUT_DIR="$skia_dir"
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
    CXXFLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -g -O0 -DDEBUG"
    log_info "Build type: DEBUG"
else
    CXXFLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -O2 -DNDEBUG"
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
TARGET="$BUILD_DIR/fbfsvg-player-macos-$arch"

# Include paths (includes shared/ for SVGAnimationController)
INCLUDES="-I$SKIA_DIR -I$SKIA_DIR/include -I$SKIA_DIR/modules -I$PROJECT_ROOT $(pkg-config --cflags sdl2)"

# Skia static libraries (order matters for linking)
# Uses SKIA_OUT_DIR set by check_skia() for architecture-specific build
SKIA_LIBS="$SKIA_OUT_DIR/libsvg.a \
           $SKIA_OUT_DIR/libskia.a \
           $SKIA_OUT_DIR/libskresources.a \
           $SKIA_OUT_DIR/libskshaper.a \
           $SKIA_OUT_DIR/libskparagraph.a \
           $SKIA_OUT_DIR/libharfbuzz.a \
           $SKIA_OUT_DIR/libskunicode_core.a \
           $SKIA_OUT_DIR/libskunicode_icu.a \
           $SKIA_OUT_DIR/libexpat.a \
           $SKIA_OUT_DIR/libpng.a \
           $SKIA_OUT_DIR/libzlib.a \
           $SKIA_OUT_DIR/libjpeg.a \
           $SKIA_OUT_DIR/libwebp.a \
           $SKIA_OUT_DIR/libwuffs.a"

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
            -framework QuartzCore \
            -framework UniformTypeIdentifiers"

log_info "Compiling SVG player with shared animation controller..."
log_info "Sources: $SRC_DIR/svg_player_animated.cpp"
log_info "         $SHARED_DIR/SVGAnimationController.cpp"
log_info "         $SHARED_DIR/SVGGridCompositor.cpp"
log_info "         $SHARED_DIR/svg_instrumentation.cpp"
log_info "         $SHARED_DIR/DirtyRegionTracker.cpp"
log_info "         $SHARED_DIR/ElementBoundsExtractor.cpp"
log_info "         $SRC_DIR/file_dialog_macos.mm"
log_info "         $SRC_DIR/metal_context.mm"
log_info "         $SRC_DIR/graphite_context_metal.mm"
log_info "         $SRC_DIR/folder_browser.cpp"
log_info "         $SRC_DIR/thumbnail_cache.cpp"
log_info "         $SRC_DIR/remote_control.cpp"
log_info "Target: $TARGET"

# Build command - includes shared animation controller, grid compositor, instrumentation, dirty region tracker, bounds extractor, Obj-C++ files (file dialog, Metal context), folder browser, thumbnail cache, and remote control
$CXX $CXXFLAGS $ARCH_FLAG $INCLUDES \
    "$SRC_DIR/svg_player_animated.cpp" \
    "$SHARED_DIR/SVGAnimationController.cpp" \
    "$SHARED_DIR/SVGGridCompositor.cpp" \
    "$SHARED_DIR/svg_instrumentation.cpp" \
    "$SHARED_DIR/DirtyRegionTracker.cpp" \
    "$SHARED_DIR/ElementBoundsExtractor.cpp" \
    "$SRC_DIR/file_dialog_macos.mm" \
    "$SRC_DIR/metal_context.mm" \
    "$SRC_DIR/graphite_context_metal.mm" \
    "$SRC_DIR/folder_browser.cpp" \
    "$SRC_DIR/thumbnail_cache.cpp" \
    "$SRC_DIR/remote_control.cpp" \
    -o "$TARGET" \
    $SKIA_LIBS \
    $LDFLAGS \
    $FRAMEWORKS

if [ $? -eq 0 ]; then
    log_info "Build successful: $TARGET"

    # Show binary info
    file "$TARGET"
    lipo -info "$TARGET" 2>/dev/null || true

    # Code signing
    if [ -n "${CODESIGN_IDENTITY:-}" ]; then
        log_info "Code signing with identity: $CODESIGN_IDENTITY"
        ENTITLEMENTS_FILE="$PROJECT_ROOT/macos-sdk/FBFSVGPlayer/entitlements.plist"
        if [ -f "$ENTITLEMENTS_FILE" ]; then
            codesign --force --sign "$CODESIGN_IDENTITY" \
                     --options runtime \
                     --entitlements "$ENTITLEMENTS_FILE" \
                     "$TARGET"
        else
            log_warn "Entitlements file not found at $ENTITLEMENTS_FILE, signing without entitlements"
            codesign --force --sign "$CODESIGN_IDENTITY" \
                     --options runtime \
                     "$TARGET"
        fi
        # Verify signature
        codesign --verify --verbose=2 "$TARGET"
        if [ $? -eq 0 ]; then
            log_info "Code signing successful (identity: $CODESIGN_IDENTITY)"
        else
            log_error "Code signature verification failed"
            exit 1
        fi
    else
        # Ad-hoc signing (required for macOS to run unsigned binaries)
        log_info "Ad-hoc signing executable (CODESIGN_IDENTITY not set)..."
        codesign --force --sign - "$TARGET"
        if [ $? -eq 0 ]; then
            log_info "Ad-hoc code signing successful"
        else
            log_warn "Code signing failed - binary may not run on macOS"
        fi
    fi
else
    log_error "Build failed"
    exit 1
fi
