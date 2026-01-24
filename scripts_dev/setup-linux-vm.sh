#!/bin/bash

# setup-linux-vm.sh - Setup script to run INSIDE the Ubuntu VM after installation
# This installs all dependencies needed for building and running the SVG player

set -e

echo "=== SVG Player Linux VM Setup ==="
echo ""

# Update system
echo "[1/5] Updating system packages..."
sudo apt update && sudo apt upgrade -y

# Install build dependencies
echo "[2/5] Installing build dependencies..."
sudo apt install -y \
    build-essential \
    clang \
    cmake \
    ninja-build \
    pkg-config \
    git \
    curl \
    wget

# Install SDL2 and graphics dependencies
echo "[3/5] Installing SDL2 and graphics libraries..."
sudo apt install -y \
    libsdl2-dev \
    libgl1-mesa-dev \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    mesa-utils \
    libdrm-dev

# Install font and text dependencies
echo "[4/5] Installing font and text libraries..."
sudo apt install -y \
    libfontconfig1-dev \
    libfreetype6-dev \
    libicu-dev \
    fonts-dejavu-core

# Install additional tools
echo "[5/5] Installing additional tools..."
sudo apt install -y \
    htop \
    neofetch \
    vim

# Verify installations
echo ""
echo "=== Verification ==="
echo "Clang version: $(clang --version | head -1)"
echo "SDL2 version: $(pkg-config --modversion sdl2)"
echo "ICU version: $(pkg-config --modversion icu-uc)"
echo "OpenGL info:"
glxinfo | grep "OpenGL version" || echo "  (Run in GUI session to see OpenGL info)"

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "1. Clone or copy the project to this VM"
echo "2. Build Skia for Linux: cd skia-build && ./build-linux.sh -y"
echo "3. Build the player: ./scripts/build-linux.sh"
echo "4. Run: ./build/svg_player_animated <svg_file>"
echo ""
