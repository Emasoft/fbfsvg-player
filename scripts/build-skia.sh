#!/bin/bash
# build-skia.sh - Build Skia dependency (universal binary for macOS)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== Building Skia Dependency ==="

cd "$PROJECT_ROOT/skia-build"

# Check if already built
if [ -f "src/skia/out/release-macos/libskia.a" ]; then
    echo "Skia libraries already exist."
    echo "To rebuild, run: rm -rf skia-build/src/skia/out/release-macos"

    # Verify architecture
    ARCH=$(lipo -info src/skia/out/release-macos/libskia.a 2>/dev/null | grep -o "arm64\|x86_64" | tr '\n' ' ')
    echo "Current architectures: $ARCH"
    exit 0
fi

echo "Building Skia (this may take 30-60 minutes)..."
echo "Building universal binary (x64 + arm64)..."

./build-macos-universal.sh

echo ""
echo "Skia build complete!"
echo "Libraries: skia-build/src/skia/out/release-macos/"
