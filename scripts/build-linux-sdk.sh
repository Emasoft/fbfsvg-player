#!/bin/bash

# build-linux-sdk.sh - Build FBFSVGPlayer SDK for Linux
# Usage: ./build-linux-sdk.sh [options]
# Options:
#   -y, --non-interactive    Skip confirmation prompts (useful for CI/automation)
#   --debug                   Build debug version
#   -h, --help               Show this help message

set -e

# Parse command line arguments
non_interactive=false
build_type="release"
for arg in "$@"; do
    case $arg in
        -y|--non-interactive)
            non_interactive=true
            shift
            ;;
        --debug)
            build_type="debug"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -y, --non-interactive    Skip confirmation prompts (useful for CI/automation)"
            echo "  --debug                   Build debug version"
            echo "  -h, --help               Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

# Confirmation function for interactive mode
confirm_continue() {
    if [ "$non_interactive" = true ]; then
        return 0  # Always continue in non-interactive mode
    fi

    local prompt="${1:-Continue anyway?}"
    read -p "$prompt [Y/n] " -n 1 -r
    echo    # Move to a new line
    if [[ ! $REPLY =~ ^[Yy]$ ]] && [[ ! -z $REPLY ]]; then
        return 1  # User said no
    fi
    return 0  # User said yes or just pressed enter
}

# Check for required OpenGL/EGL libraries
check_gl_dependencies() {
    echo "Checking for OpenGL/EGL dependencies..."

    local missing_deps=()

    # Check for EGL headers
    if ! pkg-config --exists egl 2>/dev/null && ! [ -f /usr/include/EGL/egl.h ]; then
        missing_deps+=("libegl1-mesa-dev")
    fi

    # Check for OpenGL ES headers
    if ! pkg-config --exists glesv2 2>/dev/null && ! [ -f /usr/include/GLES2/gl2.h ]; then
        missing_deps+=("libgles2-mesa-dev")
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        echo "Missing OpenGL/EGL dependencies: ${missing_deps[*]}"
        echo "To install: sudo apt-get install ${missing_deps[*]}"
        return 1
    else
        echo "OpenGL/EGL dependencies found"
        return 0
    fi
}

# Check for system library dependencies
check_system_dependencies() {
    echo "Checking for system library dependencies..."

    local missing_deps=()
    local warnings=()

    # Check for build essentials and compiler
    local compiler_found=false

    if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
        compiler_found=true
        echo "Clang found (recommended)"
    elif command -v gcc >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1; then
        compiler_found=true
        echo "GCC found - Warning: Clang is recommended for better performance"
        echo "  To install Clang: sudo apt-get install clang"
    fi

    if [ "$compiler_found" = false ]; then
        missing_deps+=("build-essential")
    fi

    # Check for FreeType
    if ! pkg-config --exists freetype2 2>/dev/null && ! [ -f /usr/include/freetype2/ft2build.h ]; then
        missing_deps+=("libfreetype6-dev")
    else
        echo "FreeType found"
    fi

    # Check for FontConfig
    if ! pkg-config --exists fontconfig 2>/dev/null && ! [ -f /usr/include/fontconfig/fontconfig.h ]; then
        warnings+=("libfontconfig1-dev")
    else
        echo "FontConfig found"
    fi

    # Check for libpng
    if ! pkg-config --exists libpng 2>/dev/null && ! [ -f /usr/include/png.h ]; then
        warnings+=("libpng-dev")
    else
        echo "libpng found"
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        echo "Missing required dependencies: ${missing_deps[*]}"
        echo "To install: sudo apt-get install ${missing_deps[*]}"
        return 1
    fi

    if [ ${#warnings[@]} -gt 0 ]; then
        echo "Optional system libraries not found: ${warnings[*]}"
        echo "To install all: sudo apt-get install ${warnings[*]}"
        echo ""

        if ! confirm_continue "Do you want to continue without optional libraries?"; then
            echo "Build cancelled by user."
            exit 1
        fi
    fi

    return 0
}

# Detect current architecture
detect_architecture() {
    current_arch=$(uname -m)
    if [ "$current_arch" = "x86_64" ]; then
        target_cpu="x64"
    elif [ "$current_arch" = "aarch64" ]; then
        target_cpu="arm64"
    else
        echo "Unsupported architecture: $current_arch"
        exit 1
    fi
    echo "Detected architecture: $current_arch ($target_cpu)"
}

# Get compiler to use
get_compiler() {
    if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
        CC="clang"
        CXX="clang++"
        echo "Using Clang compiler"
    elif command -v gcc >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1; then
        CC="gcc"
        CXX="g++"
        echo "Using GCC compiler (warning: Clang is recommended)"
    else
        echo "No compiler found!"
        exit 1
    fi
}

# Main script starts here
echo "=============================================="
echo "FBFSVGPlayer SDK for Linux - Build Script"
echo "=============================================="
echo ""

# Detect architecture early so we can select the correct Skia build
detect_architecture

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_DIR="$PROJECT_ROOT/linux-sdk/FBFSVGPlayer"
SHARED_DIR="$PROJECT_ROOT/shared"
BUILD_DIR="$PROJECT_ROOT/build/linux"
SKIA_DIR="$PROJECT_ROOT/skia-build/src/skia"
# Select architecture-specific Skia build (release-linux-arm64 or release-linux-x64)
SKIA_OUT="$SKIA_DIR/out/release-linux-${target_cpu}"

echo "Project root: $PROJECT_ROOT"
echo "SDK source:   $SDK_DIR"
echo "Shared src:   $SHARED_DIR"
echo "Build output: $BUILD_DIR"
echo "Skia path:    $SKIA_OUT"
echo ""

# Check if SDK source exists
if [ ! -d "$SDK_DIR" ]; then
    echo "Error: SDK source directory not found: $SDK_DIR"
    exit 1
fi

# Check if shared source exists
if [ ! -d "$SHARED_DIR" ]; then
    echo "Error: Shared source directory not found: $SHARED_DIR"
    exit 1
fi

if [ ! -f "$SHARED_DIR/SVGAnimationController.cpp" ]; then
    echo "Error: Shared animation controller not found: $SHARED_DIR/SVGAnimationController.cpp"
    exit 1
fi

echo "Shared animation controller found"

# Check if Skia is built
if [ ! -d "$SKIA_OUT" ]; then
    echo "Error: Skia for Linux not built yet."
    echo "Please run: cd skia-build && ./build-linux.sh"
    exit 1
fi

if [ ! -f "$SKIA_OUT/libskia.a" ]; then
    echo "Error: Skia static library not found: $SKIA_OUT/libskia.a"
    echo "Please rebuild Skia for Linux."
    exit 1
fi

echo "Skia static library found"

# Check dependencies
echo ""
if ! check_system_dependencies; then
    echo ""
    echo "Missing required dependencies. Please install them before continuing."
    exit 1
fi

echo ""
if ! check_gl_dependencies; then
    echo ""
    echo "Missing OpenGL/EGL dependencies."

    if ! confirm_continue "Do you want to try building anyway?"; then
        echo "Build cancelled by user."
        exit 1
    fi
    echo "Proceeding without OpenGL/EGL dependencies - build may fail!"
fi

echo ""
echo "All required dependencies found. Proceeding with build..."
echo ""

# Get compiler (architecture already detected early)
get_compiler

# Create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Compiler flags
CXXFLAGS="-std=c++17 -fPIC -fvisibility=hidden -fexceptions -frtti"
if [ "$build_type" = "debug" ]; then
    CXXFLAGS="$CXXFLAGS -g -O0 -DDEBUG"
    echo "Building DEBUG version"
else
    CXXFLAGS="$CXXFLAGS -O2 -DNDEBUG"
    echo "Building RELEASE version"
fi

# Include paths (includes project root for shared/ directory, and Skia modules for SVG support)
INCLUDES="-I$SDK_DIR -I$PROJECT_ROOT -I$SKIA_DIR -I$SKIA_DIR/include -I$SKIA_DIR/modules"

# Link flags for shared library
LDFLAGS="-shared -Wl,--version-script=$SDK_DIR/libfbfsvgplayer.map"
if [ -f "$SDK_DIR/libfbfsvgplayer.map" ]; then
    echo "Using symbol version script"
else
    LDFLAGS="-shared"
    echo "No version script found, exporting all symbols"
fi

# Libraries to link - Skia and its modules
# Order matters: link dependent libraries first, dependencies after
LIBS="-L$SKIA_OUT"

# SVG module and its dependencies
LIBS="$LIBS -lsvg"

# Text shaping and paragraph support for SVG <text> elements
LIBS="$LIBS -lskshaper -lskparagraph"

# Unicode support for text handling
LIBS="$LIBS -lskunicode_icu -lskunicode_core"

# HarfBuzz for text shaping (bundled with Skia)
LIBS="$LIBS -lharfbuzz"

# Main Skia library (must come after modules that depend on it)
LIBS="$LIBS -lskia"

# Additional Skia dependencies
LIBS="$LIBS -lexpat -lpng -ljpeg -lwebp -lzlib -lwuffs"

# Add ICU library for Unicode support (system installed)
if pkg-config --exists icu-uc 2>/dev/null; then
    LIBS="$LIBS $(pkg-config --libs icu-uc icu-i18n)"
else
    # Fallback to direct linking if pkg-config doesn't work
    LIBS="$LIBS -licuuc -licui18n -licudata"
fi

# Add system libraries last
LIBS="$LIBS -lpthread -ldl -lm -lstdc++"

# Add OpenGL/EGL if available
if pkg-config --exists egl 2>/dev/null; then
    LIBS="$LIBS $(pkg-config --libs egl)"
fi
if pkg-config --exists glesv2 2>/dev/null; then
    LIBS="$LIBS $(pkg-config --libs glesv2)"
fi

# Add FreeType if available
if pkg-config --exists freetype2 2>/dev/null; then
    LIBS="$LIBS $(pkg-config --libs freetype2)"
    CXXFLAGS="$CXXFLAGS $(pkg-config --cflags freetype2)"
fi

# Add FontConfig if available
if pkg-config --exists fontconfig 2>/dev/null; then
    LIBS="$LIBS $(pkg-config --libs fontconfig)"
    CXXFLAGS="$CXXFLAGS $(pkg-config --cflags fontconfig)"
fi

echo ""
echo "Compiling FBFSVGPlayer with shared animation controller..."
echo "Compiler: $CXX"
echo "Flags:    $CXXFLAGS"
echo "Sources:  $SDK_DIR/fbfsvg_player.cpp"
echo "          $SHARED_DIR/SVGAnimationController.cpp"
echo "          $SHARED_DIR/SVGGridCompositor.cpp"
echo "          $SHARED_DIR/svg_instrumentation.cpp"
echo "          $SHARED_DIR/ElementBoundsExtractor.cpp"
echo "          $SHARED_DIR/DirtyRegionTracker.cpp"
echo ""

# Compile SDK source file to object
echo "Compiling fbfsvg_player.cpp..."
$CXX $CXXFLAGS $INCLUDES -c "$SDK_DIR/fbfsvg_player.cpp" -o "$BUILD_DIR/fbfsvg_player.o"

if [ $? -ne 0 ]; then
    echo "Compilation of fbfsvg_player.cpp failed!"
    exit 1
fi

# Compile shared animation controller to object
echo "Compiling SVGAnimationController.cpp..."
$CXX $CXXFLAGS $INCLUDES -c "$SHARED_DIR/SVGAnimationController.cpp" -o "$BUILD_DIR/SVGAnimationController.o"

if [ $? -ne 0 ]; then
    echo "Compilation of SVGAnimationController.cpp failed!"
    exit 1
fi

# Compile shared grid compositor to object
echo "Compiling SVGGridCompositor.cpp..."
$CXX $CXXFLAGS $INCLUDES -c "$SHARED_DIR/SVGGridCompositor.cpp" -o "$BUILD_DIR/SVGGridCompositor.o"

if [ $? -ne 0 ]; then
    echo "Compilation of SVGGridCompositor.cpp failed!"
    exit 1
fi

# Compile shared instrumentation to object (enabled in debug builds for testing)
echo "Compiling svg_instrumentation.cpp..."
$CXX $CXXFLAGS $INCLUDES -c "$SHARED_DIR/svg_instrumentation.cpp" -o "$BUILD_DIR/svg_instrumentation.o"

if [ $? -ne 0 ]; then
    echo "Compilation of svg_instrumentation.cpp failed!"
    exit 1
fi

# Compile ElementBoundsExtractor to object (required for SVGPlayer_GetElementBounds API)
echo "Compiling ElementBoundsExtractor.cpp..."
$CXX $CXXFLAGS $INCLUDES -c "$SHARED_DIR/ElementBoundsExtractor.cpp" -o "$BUILD_DIR/ElementBoundsExtractor.o"

if [ $? -ne 0 ]; then
    echo "Compilation of ElementBoundsExtractor.cpp failed!"
    exit 1
fi

# Compile DirtyRegionTracker to object (performance optimization for partial re-rendering)
echo "Compiling DirtyRegionTracker.cpp..."
$CXX $CXXFLAGS $INCLUDES -c "$SHARED_DIR/DirtyRegionTracker.cpp" -o "$BUILD_DIR/DirtyRegionTracker.o"

if [ $? -ne 0 ]; then
    echo "Compilation of DirtyRegionTracker.cpp failed!"
    exit 1
fi

echo "Linking shared library..."

# Link shared library with all object files
$CXX $LDFLAGS -o "$BUILD_DIR/libfbfsvgplayer.so.1.0.0" "$BUILD_DIR/fbfsvg_player.o" "$BUILD_DIR/SVGAnimationController.o" "$BUILD_DIR/SVGGridCompositor.o" "$BUILD_DIR/svg_instrumentation.o" "$BUILD_DIR/ElementBoundsExtractor.o" "$BUILD_DIR/DirtyRegionTracker.o" $LIBS

if [ $? -ne 0 ]; then
    echo "Linking failed!"
    exit 1
fi

# Create symlinks
cd "$BUILD_DIR"
ln -sf libfbfsvgplayer.so.1.0.0 libfbfsvgplayer.so.1
ln -sf libfbfsvgplayer.so.1 libfbfsvgplayer.so
cd - > /dev/null

# Copy header file
cp "$SDK_DIR/fbfsvg_player.h" "$BUILD_DIR/"

# Create pkg-config file
cat > "$BUILD_DIR/fbfsvgplayer.pc" << EOF
prefix=/usr/local
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: FBFSVGPlayer
Description: Cross-platform FBF.SVG Player library with SMIL animation support
Version: 1.0.0
Requires:
Libs: -L\${libdir} -lfbfsvgplayer
Cflags: -I\${includedir}
EOF

echo ""
echo "=============================================="
echo "Build complete!"
echo "=============================================="
echo ""
echo "Output files:"
echo "  Library:     $BUILD_DIR/libfbfsvgplayer.so"
echo "  Header:      $BUILD_DIR/fbfsvg_player.h"
echo "  pkg-config:  $BUILD_DIR/fbfsvgplayer.pc"
echo ""
echo "To install system-wide:"
echo "  sudo cp $BUILD_DIR/libfbfsvgplayer.so.1.0.0 /usr/local/lib/"
echo "  sudo ln -sf /usr/local/lib/libfbfsvgplayer.so.1.0.0 /usr/local/lib/libfbfsvgplayer.so.1"
echo "  sudo ln -sf /usr/local/lib/libfbfsvgplayer.so.1 /usr/local/lib/libfbfsvgplayer.so"
echo "  sudo cp $BUILD_DIR/fbfsvg_player.h /usr/local/include/"
echo "  sudo cp $BUILD_DIR/fbfsvgplayer.pc /usr/local/lib/pkgconfig/"
echo "  sudo ldconfig"
echo ""
echo "To use in your project:"
echo "  #include <fbfsvg_player.h>"
echo "  Link with: -lfbfsvgplayer"
echo ""
