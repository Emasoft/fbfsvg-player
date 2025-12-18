# Makefile for SVG Video Player
# Builds the animated SVG player using Skia and SDL2

# Compiler settings
CXX := clang++
CXXFLAGS := -std=c++17 -O2
CXXFLAGS_DEBUG := -std=c++17 -g -O0 -DDEBUG

# Project structure
SRC_DIR := src
BUILD_DIR := build
SKIA_DIR := skia-build/src/skia

# Output binary
TARGET := $(BUILD_DIR)/svg_player_animated
TARGET_DEBUG := $(BUILD_DIR)/svg_player_animated_debug

# Source files
MAIN_SRC := $(SRC_DIR)/svg_player_animated.cpp

# ICU path (Homebrew keg-only package)
ICU_PREFIX := $(shell brew --prefix icu4c 2>/dev/null || echo "/opt/homebrew/opt/icu4c")

# Include paths
INCLUDES := -I$(SKIA_DIR) \
            -I$(SKIA_DIR)/include \
            -I$(SKIA_DIR)/modules \
            $(shell pkg-config --cflags sdl2)

# Skia static libraries (order matters for linking)
SKIA_LIBS := $(SKIA_DIR)/out/release-macos/libsvg.a \
             $(SKIA_DIR)/out/release-macos/libskia.a \
             $(SKIA_DIR)/out/release-macos/libskresources.a \
             $(SKIA_DIR)/out/release-macos/libskshaper.a \
             $(SKIA_DIR)/out/release-macos/libharfbuzz.a \
             $(SKIA_DIR)/out/release-macos/libskunicode_core.a \
             $(SKIA_DIR)/out/release-macos/libskunicode_icu.a \
             $(SKIA_DIR)/out/release-macos/libexpat.a \
             $(SKIA_DIR)/out/release-macos/libpng.a \
             $(SKIA_DIR)/out/release-macos/libzlib.a \
             $(SKIA_DIR)/out/release-macos/libjpeg.a \
             $(SKIA_DIR)/out/release-macos/libwebp.a \
             $(SKIA_DIR)/out/release-macos/libwuffs.a

# External libraries
LDFLAGS := $(shell pkg-config --libs sdl2) \
           -L$(ICU_PREFIX)/lib -licuuc -licui18n -licudata \
           -liconv

# macOS frameworks
FRAMEWORKS := -framework CoreGraphics \
              -framework CoreText \
              -framework CoreFoundation \
              -framework ApplicationServices \
              -framework Metal \
              -framework MetalKit \
              -framework Cocoa \
              -framework IOKit \
              -framework IOSurface \
              -framework OpenGL \
              -framework QuartzCore

# Default target
.PHONY: all
all: $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build release binary
$(TARGET): $(MAIN_SRC) $(SKIA_LIBS) | $(BUILD_DIR)
	@echo "Building SVG Video Player (release)..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(SKIA_LIBS) $(LDFLAGS) $(FRAMEWORKS)
	@echo "Build complete: $@"

# Build debug binary
.PHONY: debug
debug: $(TARGET_DEBUG)

$(TARGET_DEBUG): $(MAIN_SRC) $(SKIA_LIBS) | $(BUILD_DIR)
	@echo "Building SVG Video Player (debug)..."
	$(CXX) $(CXXFLAGS_DEBUG) $(INCLUDES) $< -o $@ $(SKIA_LIBS) $(LDFLAGS) $(FRAMEWORKS)
	@echo "Debug build complete: $@"

# Check if Skia is built
$(SKIA_LIBS):
	@echo "ERROR: Skia libraries not found."
	@echo "Run './scripts/build-skia.sh' to build Skia first."
	@exit 1

# Install dependencies
.PHONY: deps
deps:
	./scripts/install-deps.sh

# Build Skia
.PHONY: skia
skia:
	./scripts/build-skia.sh

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned"

# Clean everything (including Skia build)
.PHONY: distclean
distclean: clean
	rm -rf skia-build/src/skia/out/release-macos
	rm -rf skia-build/src/skia/out/release-macos-*
	@echo "All build artifacts cleaned"

# Run the player with a test file
.PHONY: run
run: $(TARGET)
	$(TARGET) svg_input_samples/girl_hair.fbf.svg

# Run in fullscreen
.PHONY: run-fullscreen
run-fullscreen: $(TARGET)
	$(TARGET) svg_input_samples/girl_hair.fbf.svg --fullscreen

# Show help
.PHONY: help
help:
	@echo "SVG Video Player - Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build release binary (default)"
	@echo "  debug        Build debug binary with symbols"
	@echo "  deps         Install system dependencies (Homebrew)"
	@echo "  skia         Build Skia library (takes 30-60 min)"
	@echo "  clean        Remove build artifacts"
	@echo "  distclean    Remove all artifacts including Skia"
	@echo "  run          Build and run with test SVG"
	@echo "  run-fullscreen  Build and run in fullscreen mode"
	@echo "  help         Show this help"
	@echo ""
	@echo "First time setup:"
	@echo "  1. make deps    # Install SDL2, ICU, pkg-config"
	@echo "  2. make skia    # Build Skia (one-time, ~30-60 min)"
	@echo "  3. make         # Build SVG player"
	@echo "  4. make run     # Test with sample SVG"
