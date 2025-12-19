# Makefile for SVG Video Player
# Multi-platform build system for macOS, Linux, and iOS
#
# Supported platforms:
#   - macOS (x64, arm64, universal)
#   - Linux (x64, arm64)
#   - iOS (device, simulator, XCFramework)

# Detect host OS
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Project structure
SRC_DIR := src
BUILD_DIR := build
SCRIPTS_DIR := scripts
SKIA_DIR := skia-build/src/skia

# Source files
MAIN_SRC := $(SRC_DIR)/svg_player_animated.cpp

# Output binary (platform-specific)
TARGET := $(BUILD_DIR)/svg_player_animated

#==============================================================================
# Platform Detection
#==============================================================================

ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    ifeq ($(UNAME_M),arm64)
        ARCH := arm64
    else
        ARCH := x64
    endif
else ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    ifeq ($(UNAME_M),aarch64)
        ARCH := arm64
    else
        ARCH := x64
    endif
else
    PLATFORM := unknown
    ARCH := unknown
endif

#==============================================================================
# Default Target
#==============================================================================

.PHONY: all
all:
	@echo "Building for $(PLATFORM) ($(ARCH))..."
	@./$(SCRIPTS_DIR)/build.sh

#==============================================================================
# Multi-Platform Targets
#==============================================================================

# Build all Apple platforms (macOS + iOS) - requires macOS host
.PHONY: all-apple
all-apple:
ifeq ($(PLATFORM),macos)
	@echo "=== Building all Apple platforms ==="
	@echo ""
	@echo "[1/3] Building macOS universal binary..."
	@./$(SCRIPTS_DIR)/build-macos.sh --universal
	@echo ""
	@echo "[2/3] Building iOS XCFramework..."
	@./$(SCRIPTS_DIR)/build-ios-framework.sh
	@echo ""
	@echo "[3/3] Build complete!"
	@echo ""
	@echo "Outputs:"
	@echo "  macOS: $(BUILD_DIR)/svg_player_animated"
	@echo "  iOS:   $(BUILD_DIR)/SVGPlayer.xcframework/"
else
	@echo "Error: Apple platform builds require macOS host"
	@exit 1
endif

# Build all Linux targets - requires Linux host or Docker
.PHONY: all-linux
all-linux:
ifeq ($(PLATFORM),linux)
	@echo "=== Building all Linux targets ==="
	@echo ""
	@echo "[1/2] Building Linux desktop player..."
	@./$(SCRIPTS_DIR)/build-linux.sh -y
	@echo ""
	@echo "[2/2] Building Linux SDK..."
	@./$(SCRIPTS_DIR)/build-linux-sdk.sh -y
	@echo ""
	@echo "Outputs:"
	@echo "  Desktop: $(BUILD_DIR)/svg_player_animated"
	@echo "  SDK:     $(BUILD_DIR)/linux/libsvgplayer.so"
else
	@echo "Linux builds require Linux host. Use Docker:"
	@echo "  cd docker && docker-compose up -d"
	@echo "  docker-compose exec dev make all-linux"
endif

# Build everything possible from current host
.PHONY: all-platforms
all-platforms:
	@echo "=== Building all platforms from $(PLATFORM) host ==="
	@echo ""
ifeq ($(PLATFORM),macos)
	@$(MAKE) all-apple
	@echo ""
	@echo "=== Linux builds require Docker ==="
	@echo "To build Linux targets, run:"
	@echo "  cd docker && docker-compose up -d"
	@echo "  docker-compose exec dev make all-linux"
else ifeq ($(PLATFORM),linux)
	@$(MAKE) all-linux
	@echo ""
	@echo "Note: iOS/macOS builds require a macOS host"
endif

# Build everything via Docker (for CI/automation on any host with Docker)
.PHONY: all-docker
all-docker:
	@echo "=== Building via Docker ==="
	@echo "This builds Linux targets in Docker container"
	@cd docker && docker-compose up -d && docker-compose exec -T dev make all-linux
ifeq ($(PLATFORM),macos)
	@echo ""
	@echo "Also building Apple platforms natively..."
	@$(MAKE) all-apple
endif

#==============================================================================
# macOS Targets
#==============================================================================

.PHONY: macos
macos:
	@./$(SCRIPTS_DIR)/build-macos.sh

.PHONY: macos-universal
macos-universal:
	@./$(SCRIPTS_DIR)/build-macos.sh --universal

.PHONY: macos-arm64
macos-arm64:
	@./$(SCRIPTS_DIR)/build-macos-arch.sh arm64

.PHONY: macos-x64
macos-x64:
	@./$(SCRIPTS_DIR)/build-macos-arch.sh x64

.PHONY: macos-debug
macos-debug:
	@./$(SCRIPTS_DIR)/build-macos.sh --debug

#==============================================================================
# Linux Targets
#==============================================================================

.PHONY: linux
linux:
	@./$(SCRIPTS_DIR)/build-linux.sh

.PHONY: linux-debug
linux-debug:
	@./$(SCRIPTS_DIR)/build-linux.sh --debug

.PHONY: linux-ci
linux-ci:
	@./$(SCRIPTS_DIR)/build-linux.sh -y

#==============================================================================
# iOS Targets
#==============================================================================

.PHONY: ios
ios: ios-device

.PHONY: ios-device
ios-device:
	@./$(SCRIPTS_DIR)/build-ios.sh --device

.PHONY: ios-simulator
ios-simulator:
	@./$(SCRIPTS_DIR)/build-ios.sh --simulator

.PHONY: ios-simulator-universal
ios-simulator-universal:
	@./$(SCRIPTS_DIR)/build-ios.sh --simulator --universal

.PHONY: ios-xcframework
ios-xcframework:
	@./$(SCRIPTS_DIR)/build-ios.sh --xcframework

.PHONY: ios-framework
ios-framework:
	@./$(SCRIPTS_DIR)/build-ios-framework.sh

.PHONY: ios-framework-clean
ios-framework-clean:
	@./$(SCRIPTS_DIR)/build-ios-framework.sh --clean

#==============================================================================
# Linux SDK Targets
#==============================================================================

.PHONY: linux-sdk
linux-sdk:
	@./$(SCRIPTS_DIR)/build-linux-sdk.sh

.PHONY: linux-sdk-debug
linux-sdk-debug:
	@./$(SCRIPTS_DIR)/build-linux-sdk.sh --debug

.PHONY: linux-sdk-ci
linux-sdk-ci:
	@./$(SCRIPTS_DIR)/build-linux-sdk.sh -y

#==============================================================================
# Skia Build Targets
#==============================================================================

.PHONY: skia
skia:
ifeq ($(PLATFORM),macos)
	@./$(SCRIPTS_DIR)/build-skia.sh
else ifeq ($(PLATFORM),linux)
	@./$(SCRIPTS_DIR)/build-skia-linux.sh
else
	@echo "Skia build not supported on $(PLATFORM)"
	@exit 1
endif

.PHONY: skia-macos
skia-macos:
	@./$(SCRIPTS_DIR)/build-skia.sh

.PHONY: skia-linux
skia-linux:
	@./$(SCRIPTS_DIR)/build-skia-linux.sh

.PHONY: skia-ios
skia-ios:
	@cd skia-build && ./build-ios.sh --xcframework

.PHONY: skia-ios-device
skia-ios-device:
	@cd skia-build && ./build-ios.sh --device

.PHONY: skia-ios-simulator
skia-ios-simulator:
	@cd skia-build && ./build-ios.sh --simulator --universal

#==============================================================================
# Dependencies
#==============================================================================

.PHONY: deps
deps:
ifeq ($(PLATFORM),macos)
	@./$(SCRIPTS_DIR)/install-deps.sh
else ifeq ($(PLATFORM),linux)
	@echo "Installing Linux dependencies..."
	@echo "Run: sudo apt-get install build-essential clang pkg-config libsdl2-dev libicu-dev"
	@echo "     sudo apt-get install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev"
	@echo "     sudo apt-get install libfreetype6-dev libfontconfig1-dev"
else
	@echo "Dependency installation not supported on $(PLATFORM)"
endif

.PHONY: deps-linux
deps-linux:
	@echo "=== Installing Linux Dependencies ==="
	@echo "Run these commands:"
	@echo "  sudo apt-get update"
	@echo "  sudo apt-get install build-essential clang pkg-config"
	@echo "  sudo apt-get install libsdl2-dev libicu-dev"
	@echo "  sudo apt-get install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev"
	@echo "  sudo apt-get install libfreetype6-dev libfontconfig1-dev libx11-dev"

#==============================================================================
# Build Directory Management
#==============================================================================

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned"

.PHONY: distclean
distclean: clean
	@rm -rf skia-build/src/skia/out/release-macos*
	@rm -rf skia-build/src/skia/out/release-linux*
	@rm -rf skia-build/src/skia/out/release-ios*
	@rm -rf skia-build/src/skia/out/xcframeworks
	@echo "All build artifacts cleaned (including Skia)"

#==============================================================================
# Run Targets
#==============================================================================

.PHONY: run
run: all
	@$(TARGET) svg_input_samples/girl_hair.fbf.svg

.PHONY: run-fullscreen
run-fullscreen: all
	@$(TARGET) svg_input_samples/girl_hair.fbf.svg --fullscreen

.PHONY: run-debug
run-debug:
	@./$(SCRIPTS_DIR)/build.sh --debug
	@$(BUILD_DIR)/svg_player_animated svg_input_samples/girl_hair.fbf.svg

#==============================================================================
# Information
#==============================================================================

.PHONY: info
info:
	@echo "=== Build Environment ==="
	@echo "Host OS:        $(UNAME_S)"
	@echo "Host Arch:      $(UNAME_M)"
	@echo "Platform:       $(PLATFORM)"
	@echo "Architecture:   $(ARCH)"
	@echo ""
	@echo "=== Directories ==="
	@echo "Source:         $(SRC_DIR)"
	@echo "Build:          $(BUILD_DIR)"
	@echo "Scripts:        $(SCRIPTS_DIR)"
	@echo "Skia:           $(SKIA_DIR)"
	@echo ""
	@echo "=== Skia Libraries ==="
ifeq ($(PLATFORM),macos)
	@ls -la $(SKIA_DIR)/out/release-macos/*.a 2>/dev/null | head -5 || echo "Not built"
else ifeq ($(PLATFORM),linux)
	@ls -la $(SKIA_DIR)/out/release-linux/*.a 2>/dev/null | head -5 || echo "Not built"
endif

#==============================================================================
# Help
#==============================================================================

.PHONY: help
help:
	@echo "SVG Video Player - Multi-Platform Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "=== Quick Start ==="
	@echo "  make deps           Install dependencies for current platform"
	@echo "  make skia           Build Skia for current platform"
	@echo "  make                Build SVG player for current platform"
	@echo "  make run            Build and run with test SVG"
	@echo ""
	@echo "=== Multi-Platform Builds ==="
	@echo "  make all-platforms  Build everything possible from current host"
	@echo "  make all-apple      Build macOS + iOS (requires macOS host)"
	@echo "  make all-linux      Build Linux player + SDK (requires Linux/Docker)"
	@echo "  make all-docker     Build Linux via Docker + Apple natively"
	@echo ""
	@echo "=== macOS Targets ==="
	@echo "  make macos          Build for current macOS architecture"
	@echo "  make macos-universal  Build universal binary (x64 + arm64)"
	@echo "  make macos-arm64    Build for Apple Silicon"
	@echo "  make macos-x64      Build for Intel"
	@echo "  make macos-debug    Build with debug symbols"
	@echo ""
	@echo "=== Linux Targets ==="
	@echo "  make linux          Build for current Linux architecture"
	@echo "  make linux-debug    Build with debug symbols"
	@echo "  make linux-ci       Build non-interactively (for CI)"
	@echo "  make linux-sdk      Build SVGPlayer shared library for Linux"
	@echo "  make linux-sdk-debug  Build Linux SDK with debug symbols"
	@echo "  make linux-sdk-ci   Build Linux SDK non-interactively"
	@echo ""
	@echo "=== iOS Targets ==="
	@echo "  make ios            Build for iOS device (arm64)"
	@echo "  make ios-device     Build for iOS device (arm64)"
	@echo "  make ios-simulator  Build for iOS simulator"
	@echo "  make ios-simulator-universal  Build universal simulator"
	@echo "  make ios-xcframework  Build XCFramework (device + simulator)"
	@echo "  make ios-framework  Build SVGPlayer.xcframework SDK"
	@echo "  make ios-framework-clean  Clean and rebuild iOS framework"
	@echo ""
	@echo "=== Skia Targets ==="
	@echo "  make skia           Build Skia for current platform"
	@echo "  make skia-macos     Build Skia for macOS (universal)"
	@echo "  make skia-linux     Build Skia for Linux"
	@echo "  make skia-ios       Build Skia XCFramework for iOS"
	@echo ""
	@echo "=== Maintenance ==="
	@echo "  make clean          Remove build artifacts"
	@echo "  make distclean      Remove all artifacts including Skia"
	@echo "  make info           Show build environment info"
	@echo "  make help           Show this help"
	@echo ""
	@echo "=== First Time Setup ==="
	@echo "  1. make deps        # Install platform dependencies"
	@echo "  2. make skia        # Build Skia (one-time, ~30-60 min)"
	@echo "  3. make             # Build SVG player"
	@echo "  4. make run         # Test with sample SVG"
	@echo ""
	@echo "Current platform: $(PLATFORM) ($(ARCH))"
