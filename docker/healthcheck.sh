#!/bin/bash
# healthcheck.sh - Verify build environment is correctly configured

set -e

echo "=== SVGPlayer Linux Dev Environment Health Check ==="
echo ""

errors=0

# Check compilers
check_tool() {
    local tool=$1
    local name=$2
    if command -v $tool >/dev/null 2>&1; then
        echo "[OK] $name: $(command -v $tool)"
        if [ "$tool" = "clang" ] || [ "$tool" = "gcc" ]; then
            echo "     Version: $($tool --version | head -1)"
        fi
    else
        echo "[FAIL] $name not found"
        ((errors++))
    fi
}

check_lib() {
    local lib=$1
    local name=$2
    if pkg-config --exists $lib 2>/dev/null; then
        echo "[OK] $name: $(pkg-config --modversion $lib 2>/dev/null)"
    else
        echo "[WARN] $name not found via pkg-config"
    fi
}

echo "=== Compilers ==="
check_tool clang "Clang C Compiler"
check_tool clang++ "Clang C++ Compiler"
check_tool gcc "GCC C Compiler"
check_tool g++ "GCC C++ Compiler"

echo ""
echo "=== Build Tools ==="
check_tool make "Make"
check_tool ninja "Ninja"
check_tool cmake "CMake"
check_tool pkg-config "pkg-config"
check_tool python3 "Python 3"
check_tool git "Git"

echo ""
echo "=== Graphics Libraries ==="
check_lib egl "EGL"
check_lib glesv2 "OpenGL ES 2"
check_lib gl "OpenGL"

echo ""
echo "=== Font Libraries ==="
check_lib freetype2 "FreeType"
check_lib fontconfig "FontConfig"
check_lib harfbuzz "HarfBuzz"

echo ""
echo "=== Image Libraries ==="
check_lib libpng "libpng"
check_lib libjpeg "libjpeg"
check_lib libwebp "libwebp"

echo ""
echo "=== Other Libraries ==="
check_lib zlib "zlib"
check_lib expat "expat"
check_lib icu-uc "ICU"

echo ""
echo "=== Environment ==="
echo "CC=$CC"
echo "CXX=$CXX"
echo "PATH includes depot_tools: $(echo $PATH | grep -q depot_tools && echo 'yes' || echo 'no')"

echo ""
if [ $errors -gt 0 ]; then
    echo "=== RESULT: $errors errors found ==="
    exit 1
else
    echo "=== RESULT: All checks passed ==="
    exit 0
fi
