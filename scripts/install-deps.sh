#!/bin/bash
# install-deps.sh - Install required dependencies for SVG Video Player
set -e

echo "=== SVG Video Player - Dependency Installation ==="

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    echo "ERROR: Homebrew not found. Install from https://brew.sh"
    exit 1
fi

echo "Installing required packages..."

# SDL2 - Window/input handling
if ! brew list sdl2 &> /dev/null; then
    echo "Installing SDL2..."
    brew install sdl2
else
    echo "SDL2 already installed"
fi

# ICU - Unicode support (required by Skia)
if ! brew list icu4c &> /dev/null; then
    echo "Installing ICU..."
    brew install icu4c
else
    echo "ICU already installed"
fi

# pkg-config - Build configuration
if ! brew list pkg-config &> /dev/null; then
    echo "Installing pkg-config..."
    brew install pkg-config
else
    echo "pkg-config already installed"
fi

echo ""
echo "=== Dependency Check ==="
echo "SDL2: $(brew list --versions sdl2)"
echo "ICU:  $(brew list --versions icu4c)"
echo "pkg-config: $(brew list --versions pkg-config)"

echo ""
echo "All dependencies installed successfully!"
