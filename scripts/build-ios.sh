#!/bin/bash

# build-ios.sh - Build SVG player for iOS
# Usage: ./build-ios.sh [OPTIONS]
#
# This script builds the SVG player as a static library for iOS integration.
# The library can be linked into an Xcode project with a UIKit-based wrapper.
#
# Note: iOS apps require UIKit for windowing. SDL2 can be used on iOS but
# requires additional setup. This script produces a static library that can
# be integrated into native iOS apps.

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
build_type="device"
build_universal=false
build_xcframework=false

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  --device        Build for iOS device (arm64) [default]"
    echo "  --simulator     Build for iOS simulator (current host architecture)"
    echo "  --universal     Build universal simulator binary (x64 + arm64)"
    echo "  --xcframework   Build XCFramework with device and universal simulator"
    echo "  -h, --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                            # Build for device (arm64)"
    echo "  $0 --simulator                # Build for simulator"
    echo "  $0 --simulator --universal    # Build universal simulator"
    echo "  $0 --xcframework              # Build XCFramework"
    echo ""
    echo "Output:"
    echo "  Device:     build/ios-device/libsvg_player.a"
    echo "  Simulator:  build/ios-simulator/libsvg_player.a"
    echo "  XCFramework: build/svg_player.xcframework"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --device)
            build_type="device"
            shift
            ;;
        --simulator)
            build_type="simulator"
            shift
            ;;
        --universal)
            build_universal=true
            shift
            ;;
        --xcframework)
            build_xcframework=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Detect current host architecture for simulator builds
current_arch=$(uname -m)
if [ "$current_arch" = "x86_64" ]; then
    current_arch="x64"
elif [ "$current_arch" = "arm64" ]; then
    current_arch="arm64"
else
    log_error "Unsupported host architecture: $current_arch"
    exit 1
fi

# Check for Xcode
check_xcode() {
    log_info "Checking for Xcode..."

    if ! command -v xcodebuild >/dev/null 2>&1; then
        log_error "Xcode command line tools not found"
        log_info "Install with: xcode-select --install"
        return 1
    fi

    xcode_version=$(xcodebuild -version | head -1)
    log_info "Found $xcode_version"
    return 0
}

# Check for Skia iOS libraries
check_skia_ios() {
    local target=$1
    log_info "Checking for Skia iOS libraries ($target)..."

    local skia_dir="$PROJECT_ROOT/skia-build/src/skia/out/release-ios-$target"

    if [ ! -f "$skia_dir/libskia.a" ]; then
        log_error "Skia iOS libraries not found at: $skia_dir"
        log_info "Run 'cd skia-build && ./build-ios.sh --$target' to build Skia for iOS first"
        return 1
    fi

    log_info "Skia iOS libraries found"
    return 0
}

# Build function for specific target/architecture
build_ios_arch() {
    local target=$1  # device or simulator
    local arch=$2    # arm64 or x64

    log_step "Building for iOS $target ($arch)..."

    # Validate combination
    if [ "$target" = "device" ] && [ "$arch" = "x64" ]; then
        log_error "iOS devices do not support x64 architecture"
        return 1
    fi

    # Set SDK and platform
    if [ "$target" = "device" ]; then
        SDK="iphoneos"
        PLATFORM="iOS"
    else
        SDK="iphonesimulator"
        PLATFORM="iOS Simulator"
    fi

    SDK_PATH=$(xcrun --sdk $SDK --show-sdk-path)
    MIN_IOS_VERSION="12.0"

    # Directories
    SKIA_DIR="$PROJECT_ROOT/skia-build/src/skia"
    SRC_DIR="$PROJECT_ROOT/src"
    SHARED_DIR="$PROJECT_ROOT/shared"
    BUILD_DIR="$PROJECT_ROOT/build/ios-$target-$arch"

    mkdir -p "$BUILD_DIR"

    # Compiler settings
    CXX="clang++"

    # Architecture flag
    if [ "$arch" = "arm64" ]; then
        ARCH_FLAG="-arch arm64"
    else
        ARCH_FLAG="-arch x86_64"
    fi

    CXXFLAGS="-std=c++17 -O2 \
              $ARCH_FLAG \
              -isysroot $SDK_PATH \
              -mios-version-min=$MIN_IOS_VERSION \
              -fembed-bitcode \
              -fvisibility=hidden \
              -DIOS_BUILD"

    # Include paths (includes project root for shared/ directory)
    INCLUDES="-I$SKIA_DIR -I$SKIA_DIR/include -I$SKIA_DIR/modules -I$PROJECT_ROOT"

    # Output object files (we'll create a static library)
    OBJ_FILE="$BUILD_DIR/svg_player_ios.o"
    SHARED_OBJ_FILE="$BUILD_DIR/SVGAnimationController.o"
    COMPOSITOR_OBJ_FILE="$BUILD_DIR/SVGGridCompositor.o"
    LIB_FILE="$BUILD_DIR/libsvg_player.a"

    log_info "Compiling for $PLATFORM ($arch)..."
    log_info "Sources: $SRC_DIR/svg_player_ios.cpp"
    log_info "         $SHARED_DIR/SVGAnimationController.cpp"
    log_info "         $SHARED_DIR/SVGGridCompositor.cpp"

    # Compile the iOS-specific source file
    # This file provides a C-compatible API for UIKit integration
    # It does NOT include SDL2 - iOS uses UIKit for windowing
    $CXX $CXXFLAGS $INCLUDES \
        -c "$SRC_DIR/svg_player_ios.cpp" \
        -o "$OBJ_FILE" \
        -DIOS_BUILD

    # Compile the shared animation controller
    $CXX $CXXFLAGS $INCLUDES \
        -c "$SHARED_DIR/SVGAnimationController.cpp" \
        -o "$SHARED_OBJ_FILE" \
        -DIOS_BUILD

    # Compile the shared grid compositor
    $CXX $CXXFLAGS $INCLUDES \
        -c "$SHARED_DIR/SVGGridCompositor.cpp" \
        -o "$COMPOSITOR_OBJ_FILE" \
        -DIOS_BUILD

    # Create static library with all object files
    ar rcs "$LIB_FILE" "$OBJ_FILE" "$SHARED_OBJ_FILE" "$COMPOSITOR_OBJ_FILE"
    rm -f "$OBJ_FILE" "$SHARED_OBJ_FILE" "$COMPOSITOR_OBJ_FILE"

    # Copy header file for integration
    cp "$SRC_DIR/svg_player_ios.h" "$BUILD_DIR/"

    log_info "Created: $LIB_FILE"

    # Verify architecture
    lipo -info "$LIB_FILE"
}

# Main build logic
if ! check_xcode; then
    exit 1
fi

BUILD_DIR="$PROJECT_ROOT/build"
mkdir -p "$BUILD_DIR"

if [ "$build_xcframework" = true ]; then
    log_step "Building XCFramework (device + universal simulator)..."

    # Check for Skia iOS libraries
    if ! check_skia_ios "device"; then
        exit 1
    fi
    if ! check_skia_ios "simulator"; then
        exit 1
    fi

    # Build device
    build_ios_arch device arm64

    # Build simulator architectures
    build_ios_arch simulator x64
    build_ios_arch simulator arm64

    # Create universal simulator library
    log_step "Creating universal simulator library..."
    mkdir -p "$BUILD_DIR/ios-simulator-universal"
    lipo -create \
        "$BUILD_DIR/ios-simulator-x64/libsvg_player.a" \
        "$BUILD_DIR/ios-simulator-arm64/libsvg_player.a" \
        -output "$BUILD_DIR/ios-simulator-universal/libsvg_player.a"

    # Create XCFramework
    log_step "Creating XCFramework..."
    rm -rf "$BUILD_DIR/svg_player.xcframework"
    xcodebuild -create-xcframework \
        -library "$BUILD_DIR/ios-device-arm64/libsvg_player.a" \
        -library "$BUILD_DIR/ios-simulator-universal/libsvg_player.a" \
        -output "$BUILD_DIR/svg_player.xcframework"

    # Clean up intermediate directories
    rm -rf "$BUILD_DIR/ios-device-arm64"
    rm -rf "$BUILD_DIR/ios-simulator-x64"
    rm -rf "$BUILD_DIR/ios-simulator-arm64"
    rm -rf "$BUILD_DIR/ios-simulator-universal"

    echo ""
    log_info "=== Build Summary ==="
    log_info "XCFramework created: $BUILD_DIR/svg_player.xcframework"
    log_info "Architectures:"
    log_info "  - iOS device (arm64)"
    log_info "  - iOS simulator (x64 + arm64 universal)"
    echo ""
    log_info "To use in Xcode, drag svg_player.xcframework into your project."

elif [ "$build_type" = "device" ]; then
    if [ "$build_universal" = true ]; then
        log_error "Universal build is only supported for simulators"
        log_info "iOS devices only support arm64 architecture"
        exit 1
    fi

    if ! check_skia_ios "device"; then
        exit 1
    fi

    build_ios_arch device arm64

    # Copy to generic location
    mkdir -p "$BUILD_DIR/ios-device"
    cp "$BUILD_DIR/ios-device-arm64/libsvg_player.a" "$BUILD_DIR/ios-device/"
    rm -rf "$BUILD_DIR/ios-device-arm64"

    echo ""
    log_info "=== Build Summary ==="
    log_info "Platform: iOS device"
    log_info "Architecture: arm64"
    log_info "Output: $BUILD_DIR/ios-device/libsvg_player.a"

elif [ "$build_type" = "simulator" ]; then
    if ! check_skia_ios "simulator"; then
        exit 1
    fi

    if [ "$build_universal" = true ]; then
        log_step "Building universal iOS simulator binaries (x64 + arm64)..."

        build_ios_arch simulator x64
        build_ios_arch simulator arm64

        # Create universal binary
        mkdir -p "$BUILD_DIR/ios-simulator"
        lipo -create \
            "$BUILD_DIR/ios-simulator-x64/libsvg_player.a" \
            "$BUILD_DIR/ios-simulator-arm64/libsvg_player.a" \
            -output "$BUILD_DIR/ios-simulator/libsvg_player.a"

        # Clean up
        rm -rf "$BUILD_DIR/ios-simulator-x64"
        rm -rf "$BUILD_DIR/ios-simulator-arm64"

        echo ""
        log_info "=== Build Summary ==="
        log_info "Platform: iOS simulator (universal)"
        log_info "Architectures: x64 + arm64"
        log_info "Output: $BUILD_DIR/ios-simulator/libsvg_player.a"
    else
        build_ios_arch simulator $current_arch

        # Copy to generic location
        mkdir -p "$BUILD_DIR/ios-simulator"
        cp "$BUILD_DIR/ios-simulator-$current_arch/libsvg_player.a" "$BUILD_DIR/ios-simulator/"
        rm -rf "$BUILD_DIR/ios-simulator-$current_arch"

        echo ""
        log_info "=== Build Summary ==="
        log_info "Platform: iOS simulator"
        log_info "Architecture: $current_arch"
        log_info "Output: $BUILD_DIR/ios-simulator/libsvg_player.a"
    fi
fi

echo ""
log_info "Note: To use the SVG player on iOS, you need a UIKit wrapper."
log_info "See docs/ios-integration.md for integration guide."
