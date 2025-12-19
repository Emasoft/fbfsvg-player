#!/bin/bash

# build-linux.sh - Build SVG player for Linux
# Usage: ./build-linux.sh [options]
# Options:
#   -y, --non-interactive    Skip confirmation prompts (useful for CI/automation)
#   --debug                  Build with debug symbols
#   -h, --help              Show this help message

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
            echo "  --debug                  Build with debug symbols"
            echo "  -h, --help              Show this help message"
            exit 0
            ;;
        *)
            log_error "Unknown option: $arg"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

# Confirmation function for interactive mode
confirm_continue() {
    if [ "$non_interactive" = true ]; then
        return 0
    fi

    local prompt="${1:-Continue anyway?}"
    read -p "$prompt [Y/n] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]] && [[ ! -z $REPLY ]]; then
        return 1
    fi
    return 0
}

# Check for OpenGL/EGL dependencies
check_gl_dependencies() {
    log_info "Checking for OpenGL/EGL dependencies..."

    local missing_deps=()

    # Check for EGL headers
    if ! pkg-config --exists egl 2>/dev/null && ! [ -f /usr/include/EGL/egl.h ]; then
        missing_deps+=("libegl1-mesa-dev")
    fi

    # Check for OpenGL ES headers
    if ! pkg-config --exists glesv2 2>/dev/null && ! [ -f /usr/include/GLES2/gl2.h ]; then
        missing_deps+=("libgles2-mesa-dev")
    fi

    # Check for OpenGL (GLX)
    if ! pkg-config --exists gl 2>/dev/null && ! [ -f /usr/include/GL/gl.h ]; then
        missing_deps+=("libgl1-mesa-dev")
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_warn "Missing OpenGL/EGL dependencies: ${missing_deps[*]}"
        log_info "To install: sudo apt-get install ${missing_deps[*]}"
        return 1
    else
        log_info "OpenGL/EGL dependencies found"
        return 0
    fi
}

# Check for system library dependencies
check_system_dependencies() {
    log_info "Checking for system library dependencies..."

    local missing_deps=()
    local warnings=()

    # Check for compiler
    local compiler_found=false

    if command -v clang++ >/dev/null 2>&1; then
        compiler_found=true
        log_info "Clang found (recommended)"
    elif command -v g++ >/dev/null 2>&1; then
        compiler_found=true
        log_warn "GCC found - Clang is recommended for better performance"
        log_info "To install Clang: sudo apt-get install clang"
    fi

    if [ "$compiler_found" = false ]; then
        missing_deps+=("build-essential" "clang")
    fi

    # Check for pkg-config
    if ! command -v pkg-config >/dev/null 2>&1; then
        missing_deps+=("pkg-config")
    else
        log_info "pkg-config found"
    fi

    # Check for SDL2
    if ! pkg-config --exists sdl2 2>/dev/null; then
        missing_deps+=("libsdl2-dev")
    else
        sdl_version=$(pkg-config --modversion sdl2 2>/dev/null)
        log_info "SDL2 found (version: $sdl_version)"
    fi

    # Check for FreeType
    if ! pkg-config --exists freetype2 2>/dev/null && ! [ -f /usr/include/freetype2/ft2build.h ]; then
        warnings+=("libfreetype6-dev")
    else
        log_info "FreeType found"
    fi

    # Check for FontConfig
    if ! pkg-config --exists fontconfig 2>/dev/null && ! [ -f /usr/include/fontconfig/fontconfig.h ]; then
        warnings+=("libfontconfig1-dev")
    else
        log_info "FontConfig found"
    fi

    # Check for X11
    if ! pkg-config --exists x11 2>/dev/null && ! [ -f /usr/include/X11/Xlib.h ]; then
        missing_deps+=("libx11-dev")
    else
        log_info "X11 found"
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "Missing required dependencies: ${missing_deps[*]}"
        log_info "To install: sudo apt-get install ${missing_deps[*]}"
        return 1
    fi

    if [ ${#warnings[@]} -gt 0 ]; then
        log_warn "Optional libraries not found: ${warnings[*]}"
        log_info "These libraries are optional but recommended"
        log_info "To install: sudo apt-get install ${warnings[*]}"
        echo ""

        if ! confirm_continue "Do you want to continue without these libraries?"; then
            log_info "Build cancelled by user."
            exit 1
        fi
    fi

    return 0
}

# Detect ICU installation
detect_icu() {
    log_info "Detecting ICU installation..."

    # Check pkg-config for ICU (icu-data is not a valid module, libs are included via icu-uc)
    if command -v pkg-config >/dev/null 2>&1; then
        if pkg-config --exists icu-uc icu-i18n; then
            icu_version=$(pkg-config --modversion icu-uc 2>/dev/null)
            export ICU_CFLAGS=$(pkg-config --cflags icu-uc icu-i18n 2>/dev/null)
            # Note: icu-uc --libs already includes -licudata, do not use icu-data module
            export ICU_LIBS=$(pkg-config --libs icu-uc icu-i18n 2>/dev/null)
            log_info "Found ICU via pkg-config"
            log_info "ICU_LIBS: $ICU_LIBS"
            if [ -n "$icu_version" ]; then
                log_info "ICU version: $icu_version"
            fi
            return 0
        fi
    fi

    # Check standard Linux locations
    for path in /usr /usr/local; do
        if [ -d "$path/include/unicode" ] && [ -d "$path/lib" ]; then
            if ls "$path/lib/"*icu* >/dev/null 2>&1 || ls "$path/lib/x86_64-linux-gnu/"*icu* >/dev/null 2>&1; then
                export ICU_ROOT="$path"
                export ICU_CFLAGS="-I$path/include"
                export ICU_LIBS="-licuuc -licui18n -licudata"
                log_info "Found ICU at: $ICU_ROOT"
                log_info "ICU_LIBS: $ICU_LIBS"
                return 0
            fi
        fi
    done

    log_error "No compatible system ICU found"
    log_info "To install ICU: sudo apt-get install libicu-dev"
    return 1
}

# Check for Skia libraries
check_skia() {
    log_info "Checking for Skia libraries..."

    local skia_dir="$PROJECT_ROOT/skia-build/src/skia/out/release-linux"

    if [ ! -f "$skia_dir/libskia.a" ]; then
        log_error "Skia libraries not found at: $skia_dir"
        log_info "Run './scripts/build-skia-linux.sh' to build Skia for Linux first"
        return 1
    fi

    log_info "Skia libraries found"
    return 0
}

# Detect current architecture
current_arch=$(uname -m)
if [ "$current_arch" = "x86_64" ]; then
    target_cpu="x64"
elif [ "$current_arch" = "aarch64" ]; then
    target_cpu="arm64"
else
    log_error "Unsupported architecture: $current_arch"
    exit 1
fi

log_step "Building SVG Player for Linux ($target_cpu)..."

# Check for required dependencies
echo ""
if ! check_system_dependencies; then
    echo ""
    log_error "Missing required dependencies. Please install them before continuing."
    exit 1
fi

echo ""
if ! check_gl_dependencies; then
    echo ""
    log_warn "Missing OpenGL/EGL dependencies. Build may fail."

    if ! confirm_continue "Do you want to try building anyway?"; then
        log_info "Build cancelled by user."
        exit 1
    fi
    log_warn "Proceeding without OpenGL/EGL dependencies - build may fail!"
fi

echo ""
if ! detect_icu; then
    exit 1
fi

echo ""
if ! check_skia; then
    exit 1
fi

echo ""
log_info "All required dependencies found. Proceeding with build..."
echo ""

# Compiler settings
if command -v clang++ >/dev/null 2>&1; then
    CXX="clang++"
else
    CXX="g++"
fi

if [ "$build_type" = "debug" ]; then
    CXXFLAGS="-std=c++17 -g -O0 -DDEBUG"
else
    CXXFLAGS="-std=c++17 -O2"
fi

# Directories
SKIA_DIR="$PROJECT_ROOT/skia-build/src/skia"
SRC_DIR="$PROJECT_ROOT/src"
BUILD_DIR="$PROJECT_ROOT/build"

# Create build directory
mkdir -p "$BUILD_DIR"

# Output binary name
TARGET="$BUILD_DIR/svg_player_animated"

# Include paths
INCLUDES="-I$SKIA_DIR -I$SKIA_DIR/include -I$SKIA_DIR/modules $(pkg-config --cflags sdl2) $ICU_CFLAGS"

# Skia static libraries (order matters: dependents first, dependencies last)
# SVG module and text support modules depend on Skia core
# Unicode modules depend on ICU (system library linked separately)
SKIA_LIBS="$SKIA_DIR/out/release-linux/libsvg.a \
           $SKIA_DIR/out/release-linux/libskshaper.a \
           $SKIA_DIR/out/release-linux/libskparagraph.a \
           $SKIA_DIR/out/release-linux/libskresources.a \
           $SKIA_DIR/out/release-linux/libskunicode_icu.a \
           $SKIA_DIR/out/release-linux/libskunicode_core.a \
           $SKIA_DIR/out/release-linux/libharfbuzz.a \
           $SKIA_DIR/out/release-linux/libskia.a \
           $SKIA_DIR/out/release-linux/libexpat.a \
           $SKIA_DIR/out/release-linux/libpng.a \
           $SKIA_DIR/out/release-linux/libjpeg.a \
           $SKIA_DIR/out/release-linux/libwebp.a \
           $SKIA_DIR/out/release-linux/libzlib.a \
           $SKIA_DIR/out/release-linux/libwuffs.a"

# External libraries (SDL2 first, then ICU after Skia unicode modules)
LDFLAGS="$(pkg-config --libs sdl2)"

# ICU libraries must come after skunicode modules that depend on them
ICU_LINK_FLAGS="$ICU_LIBS"

# Linux-specific libraries
LINUX_LIBS="-lGL -lEGL -lX11 -lXext -lpthread -ldl -lm -lfontconfig -lfreetype"

log_info "Compiling $SRC_DIR/svg_player_animated_linux.cpp..."
log_info "Compiler: $CXX"
log_info "Target: $TARGET"

# Build command - use Linux-specific source file
# Link order: source -> Skia modules -> Skia core -> Skia deps -> ICU -> system libs
$CXX $CXXFLAGS $INCLUDES \
    "$SRC_DIR/svg_player_animated_linux.cpp" \
    -o "$TARGET" \
    $SKIA_LIBS \
    $ICU_LINK_FLAGS \
    $LDFLAGS \
    $LINUX_LIBS

if [ $? -eq 0 ]; then
    echo ""
    log_info "=== Build Summary ==="
    log_info "Platform: Linux"
    log_info "Architecture: $target_cpu"
    log_info "Build type: $build_type"
    log_info "Output: $TARGET"
    echo ""

    # Show binary info
    file "$TARGET"
    echo ""
    log_info "Run with: $TARGET <svg_file>"
else
    log_error "Build failed"
    exit 1
fi
