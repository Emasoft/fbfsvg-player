#!/bin/bash

# build-ios-framework.sh - Build FBFSVGPlayer.xcframework for iOS
#
# This script builds a complete iOS framework with:
# - Static library combining C++ core and Objective-C wrapper
# - Public headers for Swift/Objective-C integration
# - Module map for Swift imports
# - Universal XCFramework for device + simulator
#
# Usage:
#   ./scripts/build-ios-framework.sh [--clean]
#
# Output: build/FBFSVGPlayer.xcframework

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Log to stderr so output doesn't get captured by $() in function calls
log_info() { echo -e "${GREEN}[INFO]${NC} $1" >&2; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1" >&2; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1" >&2; }
log_error() { echo -e "${RED}[ERROR]${NC} $1" >&2; }

# Configuration
SDK_NAME="FBFSVGPlayer"
SDK_VERSION="1.0.0"
MIN_IOS_VERSION="13.0"
BUILD_DIR="$PROJECT_ROOT/build"
FRAMEWORK_BUILD_DIR="$BUILD_DIR/framework-build"
IOS_SDK_DIR="$PROJECT_ROOT/ios-sdk/FBFSVGPlayer"
SKIA_DIR="$PROJECT_ROOT/skia-build/src/skia"

# Source files (includes shared animation controller, grid compositor, and instrumentation)
CPP_SOURCES=(
    "$PROJECT_ROOT/src/svg_player_ios.cpp"
    "$PROJECT_ROOT/shared/SVGAnimationController.cpp"
    "$PROJECT_ROOT/shared/SVGGridCompositor.cpp"
    "$PROJECT_ROOT/shared/svg_instrumentation.cpp"
    "$PROJECT_ROOT/shared/DirtyRegionTracker.cpp"
    "$PROJECT_ROOT/shared/ElementBoundsExtractor.cpp"
)

OBJC_SOURCES=(
    "$IOS_SDK_DIR/FBFSVGPlayerController.mm"
    "$IOS_SDK_DIR/FBFSVGPlayerMetalRenderer.mm"
    "$IOS_SDK_DIR/FBFSVGPlayerView.mm"
)

PUBLIC_HEADERS=(
    "$IOS_SDK_DIR/FBFSVGPlayer.h"
    "$IOS_SDK_DIR/FBFSVGPlayerView.h"
    "$IOS_SDK_DIR/FBFSVGPlayerController.h"
    "$IOS_SDK_DIR/FBFSVGPlayerMetalRenderer.h"
    "$PROJECT_ROOT/src/svg_player_ios.h"
)

# Parse arguments
CLEAN=false
NON_INTERACTIVE=false
for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=true
            ;;
        -y|--non-interactive)
            NON_INTERACTIVE=true
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --clean               Clean previous builds before building"
            echo "  -y, --non-interactive Run without prompts (for CI/CD)"
            echo "  -h, --help            Show this help message"
            exit 0
            ;;
    esac
done

# Clean if requested
if [ "$CLEAN" = true ]; then
    log_step "Cleaning previous builds..."
    rm -rf "$FRAMEWORK_BUILD_DIR"
    rm -rf "$BUILD_DIR/$SDK_NAME.xcframework"
fi

# Check prerequisites
check_prerequisites() {
    log_step "Checking prerequisites..."

    # Check for Xcode
    if ! command -v xcodebuild &> /dev/null; then
        log_error "Xcode command line tools not found. Install with: xcode-select --install"
        exit 1
    fi

    # Check for Skia
    local skia_device="$SKIA_DIR/out/release-ios-device"
    local skia_sim_arm="$SKIA_DIR/out/release-ios-simulator-arm64"
    local skia_sim_x64="$SKIA_DIR/out/release-ios-simulator-x64"
    local skia_sim_generic="$SKIA_DIR/out/release-ios-simulator"

    if [ ! -f "$skia_device/libskia.a" ]; then
        log_error "Skia iOS device library not found at: $skia_device/libskia.a"
        log_info "Run: make skia-ios-device"
        exit 1
    fi

    # Check for at least one simulator architecture (includes generic path as fallback)
    if [ ! -f "$skia_sim_arm/libskia.a" ] && [ ! -f "$skia_sim_x64/libskia.a" ] && [ ! -f "$skia_sim_generic/libskia.a" ]; then
        log_error "Skia iOS simulator library not found"
        log_info "Run: make skia-ios-simulator"
        exit 1
    fi

    # Check for SDK source files
    for src in "${OBJC_SOURCES[@]}"; do
        if [ ! -f "$src" ]; then
            log_error "SDK source file not found: $src"
            exit 1
        fi
    done

    log_info "All prerequisites satisfied"
}

# Compile sources for a specific target
compile_sources() {
    local target=$1      # device or simulator
    local arch=$2        # arm64 or x86_64
    local output_dir=$3

    log_step "Compiling for $target ($arch)..."

    local sdk_name="iphoneos"
    local target_triple="arm64-apple-ios${MIN_IOS_VERSION}"
    if [ "$target" = "simulator" ]; then
        sdk_name="iphonesimulator"
        if [ "$arch" = "x86_64" ]; then
            target_triple="x86_64-apple-ios${MIN_IOS_VERSION}-simulator"
        else
            target_triple="arm64-apple-ios${MIN_IOS_VERSION}-simulator"
        fi
    fi

    local sdk_path=$(xcrun --sdk $sdk_name --show-sdk-path)
    local clang=$(xcrun --sdk $sdk_name --find clang++)

    mkdir -p "$output_dir"

    local arch_flag="-arch $arch"

    # Skia library path
    local skia_lib_dir="$SKIA_DIR/out/release-ios-device"
    if [ "$target" = "simulator" ]; then
        if [ "$arch" = "x86_64" ]; then
            skia_lib_dir="$SKIA_DIR/out/release-ios-simulator-x64"
        else
            # Try architecture-specific path first, fall back to generic
            if [ -f "$SKIA_DIR/out/release-ios-simulator-arm64/libskia.a" ]; then
                skia_lib_dir="$SKIA_DIR/out/release-ios-simulator-arm64"
            else
                skia_lib_dir="$SKIA_DIR/out/release-ios-simulator"
            fi
        fi
    fi

    # Common compiler flags (includes project root for shared/ directory)
    local common_flags=(
        -std=c++17
        -O2
        $arch_flag
        -target "$target_triple"
        -isysroot "$sdk_path"
        -mios-version-min=$MIN_IOS_VERSION
        -fvisibility=hidden
        -fPIC
        -I"$SKIA_DIR"
        -I"$SKIA_DIR/include"
        -I"$PROJECT_ROOT"
        -I"$PROJECT_ROOT/src"
        -I"$IOS_SDK_DIR"
    )

    # Objective-C specific flags
    local objc_flags=(
        "${common_flags[@]}"
        -fobjc-arc
        -fmodules
    )

    # Compile C++ sources
    local obj_files=()
    for src in "${CPP_SOURCES[@]}"; do
        local basename=$(basename "$src" .cpp)
        local obj="$output_dir/${basename}.o"
        log_info "  Compiling: $basename.cpp"
        "$clang" "${common_flags[@]}" -c "$src" -o "$obj"
        obj_files+=("$obj")
    done

    # Compile Objective-C++ sources
    for src in "${OBJC_SOURCES[@]}"; do
        local basename=$(basename "$src" .mm)
        local obj="$output_dir/${basename}.o"
        log_info "  Compiling: $basename.mm"
        "$clang" "${objc_flags[@]}" -c "$src" -o "$obj"
        obj_files+=("$obj")
    done

    # Create static library
    log_info "  Creating static library..."
    local lib_output="$output_dir/lib${SDK_NAME}.a"

    # First, extract Skia objects and combine with our objects
    local skia_lib="$skia_lib_dir/libskia.a"
    if [ -f "$skia_lib" ]; then
        # Create combined library by merging Skia and our objects
        libtool -static -o "$lib_output" "${obj_files[@]}" "$skia_lib"
    else
        # Just create library from our objects
        ar rcs "$lib_output" "${obj_files[@]}"
    fi

    # Cleanup object files
    rm -f "${obj_files[@]}"

    log_info "  Created: $lib_output"
}

# Create framework bundle
create_framework() {
    local target=$1      # device or simulator
    local lib_path=$2
    local output_dir=$3

    log_step "Creating framework bundle for $target..."

    local fw_dir="$output_dir/$SDK_NAME.framework"
    mkdir -p "$fw_dir/Headers"
    mkdir -p "$fw_dir/Modules"

    # Copy binary
    cp "$lib_path" "$fw_dir/$SDK_NAME"

    # Copy headers
    for header in "${PUBLIC_HEADERS[@]}"; do
        cp "$header" "$fw_dir/Headers/"
    done

    # Copy module map
    cp "$IOS_SDK_DIR/module.modulemap" "$fw_dir/Modules/"

    # Copy Info.plist
    cp "$IOS_SDK_DIR/Info.plist" "$fw_dir/"

    log_info "  Created: $fw_dir"
    echo "$fw_dir"
}

# Main build process
main() {
    echo ""
    echo "=================================================="
    echo "  Building $SDK_NAME.xcframework v$SDK_VERSION"
    echo "=================================================="
    echo ""

    check_prerequisites

    mkdir -p "$FRAMEWORK_BUILD_DIR"

    # Build for iOS device (arm64)
    log_step "Building for iOS device..."
    compile_sources "device" "arm64" "$FRAMEWORK_BUILD_DIR/device-arm64"

    # Build for iOS simulator (arm64 - Apple Silicon)
    log_step "Building for iOS simulator (arm64)..."
    compile_sources "simulator" "arm64" "$FRAMEWORK_BUILD_DIR/simulator-arm64"

    # Build for iOS simulator (x86_64 - Intel)
    # Check if Skia x64 simulator library exists
    if [ -f "$SKIA_DIR/out/release-ios-simulator-x64/libskia.a" ]; then
        log_step "Building for iOS simulator (x86_64)..."
        compile_sources "simulator" "x86_64" "$FRAMEWORK_BUILD_DIR/simulator-x64"

        # Create universal simulator library
        log_step "Creating universal simulator library..."
        mkdir -p "$FRAMEWORK_BUILD_DIR/simulator-universal"
        lipo -create \
            "$FRAMEWORK_BUILD_DIR/simulator-arm64/lib${SDK_NAME}.a" \
            "$FRAMEWORK_BUILD_DIR/simulator-x64/lib${SDK_NAME}.a" \
            -output "$FRAMEWORK_BUILD_DIR/simulator-universal/lib${SDK_NAME}.a"

        SIMULATOR_LIB="$FRAMEWORK_BUILD_DIR/simulator-universal/lib${SDK_NAME}.a"
    else
        log_warn "Skia x64 simulator library not found, building arm64 only"
        SIMULATOR_LIB="$FRAMEWORK_BUILD_DIR/simulator-arm64/lib${SDK_NAME}.a"
    fi

    # Create framework bundles
    log_step "Creating framework bundles..."
    DEVICE_FW=$(create_framework "device" "$FRAMEWORK_BUILD_DIR/device-arm64/lib${SDK_NAME}.a" "$FRAMEWORK_BUILD_DIR/frameworks-device")
    SIMULATOR_FW=$(create_framework "simulator" "$SIMULATOR_LIB" "$FRAMEWORK_BUILD_DIR/frameworks-simulator")

    # Create XCFramework
    log_step "Creating XCFramework..."
    rm -rf "$BUILD_DIR/$SDK_NAME.xcframework"

    xcodebuild -create-xcframework \
        -framework "$DEVICE_FW" \
        -framework "$SIMULATOR_FW" \
        -output "$BUILD_DIR/$SDK_NAME.xcframework"

    # Cleanup intermediate files
    log_step "Cleaning up..."
    rm -rf "$FRAMEWORK_BUILD_DIR"

    # Summary
    echo ""
    echo "=================================================="
    log_info "Build Complete!"
    echo "=================================================="
    echo ""
    log_info "XCFramework: $BUILD_DIR/$SDK_NAME.xcframework"
    echo ""
    log_info "To integrate into your Xcode project:"
    echo "  1. Drag $SDK_NAME.xcframework into your project"
    echo "  2. Ensure it's added to 'Frameworks, Libraries, and Embedded Content'"
    echo "  3. Set 'Embed' to 'Do Not Embed' (it's a static framework)"
    echo ""
    log_info "Usage in Swift:"
    echo "  import FBFSVGPlayer"
    echo "  let player = SVGPlayerView(frame: view.bounds, svgFileName: \"animation\")"
    echo ""
    log_info "Usage in Objective-C:"
    echo "  #import <FBFSVGPlayer/FBFSVGPlayer.h>"
    echo "  SVGPlayerView *player = [[SVGPlayerView alloc] initWithFrame:self.view.bounds"
    echo "                                                   svgFileName:@\"animation\"];"
    echo ""
}

main "$@"
