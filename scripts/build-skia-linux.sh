#!/bin/bash

# build-skia-linux.sh - Build Skia dependency for Linux
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

echo "=== Building Skia Dependency for Linux ==="

cd "$PROJECT_ROOT/skia-build"

# Check if already built
if [ -f "src/skia/out/release-linux/libskia.a" ]; then
    log_info "Skia Linux libraries already exist."
    log_info "To rebuild, run: rm -rf skia-build/src/skia/out/release-linux"

    # Verify architecture
    ARCH=$(file src/skia/out/release-linux/libskia.a 2>/dev/null | grep -o "x86-64\|aarch64\|ARM" | head -1)
    log_info "Current architecture: $ARCH"
    exit 0
fi

log_info "Building Skia for Linux (this may take 30-60 minutes)..."

./build-linux.sh -y

echo ""
log_info "Skia Linux build complete!"
log_info "Libraries: skia-build/src/skia/out/release-linux/"
