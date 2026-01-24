# Dockerfile for ThorVG vs fbfsvg-player GPU Benchmark
# Requires Linux host with GPU passthrough (NVIDIA Container Toolkit or /dev/dri mount)
#
# Build: docker build -t svg-benchmark .
# Run:   docker run --gpus all -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix svg-benchmark

FROM ubuntu:24.04

LABEL maintainer="fbfsvg-player benchmark"
LABEL description="GPU benchmark comparing ThorVG and fbfsvg-player performance"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install base dependencies
RUN apt-get update && apt-get install -y \
    # Build essentials
    build-essential \
    clang \
    cmake \
    ninja-build \
    pkg-config \
    git \
    curl \
    wget \
    python3 \
    python3-pip \
    # Graphics libraries
    libgl1-mesa-dev \
    libgles2-mesa-dev \
    libegl1-mesa-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxxf86vm-dev \
    # SDL2 for fbfsvg-player
    libsdl2-dev \
    # Font support
    libfreetype6-dev \
    libfontconfig1-dev \
    # ICU for Skia unicode
    libicu-dev \
    # ThorVG dependencies
    meson \
    libpng-dev \
    libjpeg-dev \
    libwebp-dev \
    # Benchmark utilities
    hyperfine \
    time \
    linux-tools-generic \
    # X11 utilities for fullscreen
    x11-utils \
    xdotool \
    wmctrl \
    # Video capture for visual comparison
    ffmpeg \
    # JSON processing for results
    jq \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for builds
RUN useradd -m -s /bin/bash benchuser && \
    mkdir -p /workspace /results && \
    chown -R benchuser:benchuser /workspace /results

WORKDIR /workspace

# ============================================================
# STAGE 1: Build ThorVG
# ============================================================
RUN git clone --depth 1 https://github.com/thorvg/thorvg.git /workspace/thorvg

WORKDIR /workspace/thorvg

# Build ThorVG with all backends and tools
RUN meson setup builddir \
    -Ddefault_library=shared \
    -Dloaders="svg,png,jpg,webp,lottie,ttf" \
    -Dsavers="tvg,gif" \
    -Dbindings="capi" \
    -Dtools="svg2tvg,svg2png,lottie2gif" \
    -Dexamples=true \
    -Dtests=false \
    -Dlog=true \
    --prefix=/usr/local && \
    ninja -C builddir && \
    ninja -C builddir install

# Create ThorVG benchmark player (uses ThorVG's example viewer as base)
RUN cat > /workspace/thorvg_player.cpp << 'THORVG_PLAYER_EOF'
// ThorVG SVG Player for Benchmarking
// Plays SVG/Lottie animations with frame timing stats

#include <thorvg.h>
#include <SDL2/SDL.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>
#include <fstream>
#include <cstring>

using Clock = std::chrono::steady_clock;

// Rolling average for frame times
class FrameStats {
    std::vector<double> times;
    size_t maxSamples = 1000;
public:
    void add(double ms) {
        if (times.size() >= maxSamples) times.erase(times.begin());
        times.push_back(ms);
    }
    double average() const {
        if (times.empty()) return 0;
        return std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    }
    double min() const {
        if (times.empty()) return 0;
        return *std::min_element(times.begin(), times.end());
    }
    double max() const {
        if (times.empty()) return 0;
        return *std::max_element(times.begin(), times.end());
    }
    size_t count() const { return times.size(); }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <svg_file> [duration_seconds] [--fullscreen] [--json output.json]" << std::endl;
        return 1;
    }

    const char* svgFile = argv[1];
    double duration = 10.0;  // Default 10 seconds
    bool fullscreen = false;
    const char* jsonOutput = nullptr;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--fullscreen") == 0) {
            fullscreen = true;
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            jsonOutput = argv[++i];
        } else {
            duration = std::stod(argv[i]);
        }
    }

    // Initialize ThorVG
    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        std::cerr << "ThorVG initialization failed" << std::endl;
        return 1;
    }

    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Get display info for fullscreen
    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    int width = fullscreen ? displayMode.w : 1280;
    int height = fullscreen ? displayMode.h : 720;

    Uint32 windowFlags = SDL_WINDOW_SHOWN;
    if (fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    }

    SDL_Window* window = SDL_CreateWindow(
        "ThorVG Benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, windowFlags
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create ThorVG canvas with SDL surface
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, width, height);

    auto canvas = tvg::SwCanvas::gen();

    // Allocate buffer for software rendering
    std::vector<uint32_t> buffer(width * height);
    canvas->target(buffer.data(), width, width, height, tvg::SwCanvas::ARGB8888);

    // Load SVG
    auto picture = tvg::Picture::gen();
    if (picture->load(svgFile) != tvg::Result::Success) {
        std::cerr << "Failed to load SVG: " << svgFile << std::endl;
        return 1;
    }

    // Scale to fit window
    float svgW, svgH;
    picture->size(&svgW, &svgH);
    float scale = std::min(width / svgW, height / svgH);
    picture->scale(scale);

    // Center in window
    float offsetX = (width - svgW * scale) / 2;
    float offsetY = (height - svgH * scale) / 2;
    picture->translate(offsetX, offsetY);

    canvas->push(std::move(picture));

    std::cout << "ThorVG Benchmark Player" << std::endl;
    std::cout << "File: " << svgFile << std::endl;
    std::cout << "Resolution: " << width << "x" << height << std::endl;
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << "Fullscreen: " << (fullscreen ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    FrameStats frameStats;
    FrameStats renderStats;
    FrameStats presentStats;

    auto startTime = Clock::now();
    auto lastFrameTime = startTime;
    bool running = true;
    int frameCount = 0;

    while (running) {
        auto frameStart = Clock::now();

        // Handle events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        // Check duration
        double elapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
        if (elapsed >= duration) {
            running = false;
            continue;
        }

        // Render with ThorVG
        auto renderStart = Clock::now();

        // Clear buffer
        std::fill(buffer.begin(), buffer.end(), 0xFF000000);  // Black background

        // Update and draw canvas
        canvas->update();
        canvas->draw();
        canvas->sync();

        auto renderEnd = Clock::now();
        double renderMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
        renderStats.add(renderMs);

        // Copy to SDL texture
        auto presentStart = Clock::now();
        void* pixels;
        int pitch;
        SDL_LockTexture(texture, nullptr, &pixels, &pitch);
        memcpy(pixels, buffer.data(), width * height * 4);
        SDL_UnlockTexture(texture);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        auto presentEnd = Clock::now();
        double presentMs = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
        presentStats.add(presentMs);

        // Frame timing
        auto frameEnd = Clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        frameStats.add(frameMs);

        frameCount++;
        lastFrameTime = frameEnd;
    }

    auto totalTime = std::chrono::duration<double>(Clock::now() - startTime).count();

    // Print results
    std::cout << "\n=== ThorVG Benchmark Results ===" << std::endl;
    std::cout << "Total frames: " << frameCount << std::endl;
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << totalTime << "s" << std::endl;
    std::cout << "Average FPS: " << std::setprecision(2) << (frameCount / totalTime) << std::endl;
    std::cout << std::endl;
    std::cout << "Frame time (ms): avg=" << std::setprecision(2) << frameStats.average()
              << ", min=" << frameStats.min() << ", max=" << frameStats.max() << std::endl;
    std::cout << "Render time (ms): avg=" << std::setprecision(2) << renderStats.average()
              << ", min=" << renderStats.min() << ", max=" << renderStats.max() << std::endl;
    std::cout << "Present time (ms): avg=" << std::setprecision(2) << presentStats.average()
              << ", min=" << presentStats.min() << ", max=" << presentStats.max() << std::endl;

    // Write JSON output if requested
    if (jsonOutput) {
        std::ofstream json(jsonOutput);
        json << "{\n";
        json << "  \"player\": \"thorvg\",\n";
        json << "  \"file\": \"" << svgFile << "\",\n";
        json << "  \"resolution\": \"" << width << "x" << height << "\",\n";
        json << "  \"fullscreen\": " << (fullscreen ? "true" : "false") << ",\n";
        json << "  \"total_frames\": " << frameCount << ",\n";
        json << "  \"total_time_s\": " << std::setprecision(4) << totalTime << ",\n";
        json << "  \"avg_fps\": " << std::setprecision(2) << (frameCount / totalTime) << ",\n";
        json << "  \"frame_time_ms\": {\n";
        json << "    \"avg\": " << std::setprecision(4) << frameStats.average() << ",\n";
        json << "    \"min\": " << frameStats.min() << ",\n";
        json << "    \"max\": " << frameStats.max() << "\n";
        json << "  },\n";
        json << "  \"render_time_ms\": {\n";
        json << "    \"avg\": " << std::setprecision(4) << renderStats.average() << ",\n";
        json << "    \"min\": " << renderStats.min() << ",\n";
        json << "    \"max\": " << renderStats.max() << "\n";
        json << "  },\n";
        json << "  \"present_time_ms\": {\n";
        json << "    \"avg\": " << std::setprecision(4) << presentStats.average() << ",\n";
        json << "    \"min\": " << presentStats.min() << ",\n";
        json << "    \"max\": " << presentStats.max() << "\n";
        json << "  }\n";
        json << "}\n";
        json.close();
        std::cout << "\nResults written to: " << jsonOutput << std::endl;
    }

    // Cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    tvg::Initializer::term();

    return 0;
}
THORVG_PLAYER_EOF

# Compile ThorVG benchmark player
RUN g++ -std=c++17 -O2 \
    /workspace/thorvg_player.cpp \
    -o /usr/local/bin/thorvg_player \
    $(pkg-config --cflags --libs thorvg sdl2) \
    -lpthread

# ============================================================
# STAGE 2: Build fbfsvg-player (Skia-based)
# ============================================================
WORKDIR /workspace

# Install depot_tools for Skia build
RUN git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git /workspace/depot_tools
ENV PATH="/workspace/depot_tools:${PATH}"

# Copy fbfsvg-player source (mounted at build time or copied)
# For now, create placeholder - actual source will be mounted
RUN mkdir -p /workspace/fbfsvg-player

# ============================================================
# STAGE 3: Benchmark Scripts
# ============================================================

# Create main benchmark script
RUN cat > /usr/local/bin/run-benchmark << 'BENCHMARK_EOF'
#!/bin/bash
# SVG Player Benchmark Script
# Compares ThorVG vs fbfsvg-player performance

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# Default parameters
DURATION=30
FULLSCREEN=false
RESULTS_DIR="/results"
SVG_FILES=()

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --fullscreen)
            FULLSCREEN=true
            shift
            ;;
        --results)
            RESULTS_DIR="$2"
            shift 2
            ;;
        --help)
            echo "Usage: run-benchmark [options] <svg_files...>"
            echo ""
            echo "Options:"
            echo "  --duration N     Run each test for N seconds (default: 30)"
            echo "  --fullscreen     Run in fullscreen mode"
            echo "  --results DIR    Output directory for results (default: /results)"
            echo ""
            exit 0
            ;;
        *)
            SVG_FILES+=("$1")
            shift
            ;;
    esac
done

if [ ${#SVG_FILES[@]} -eq 0 ]; then
    log_error "No SVG files specified"
    echo "Usage: run-benchmark [options] <svg_files...>"
    exit 1
fi

# Create results directory
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RUN_DIR="$RESULTS_DIR/benchmark_$TIMESTAMP"
mkdir -p "$RUN_DIR"

log_info "=== SVG Player Benchmark ==="
log_info "Duration: ${DURATION}s per test"
log_info "Fullscreen: $FULLSCREEN"
log_info "Results: $RUN_DIR"
log_info "SVG files: ${#SVG_FILES[@]}"
echo ""

# Check for GPU
if [ -d /dev/dri ]; then
    log_info "GPU detected: $(ls /dev/dri/)"
else
    log_warn "No GPU detected - using software rendering"
fi

# System info
log_step "Collecting system information..."
cat > "$RUN_DIR/system_info.json" << EOF
{
  "timestamp": "$TIMESTAMP",
  "hostname": "$(hostname)",
  "kernel": "$(uname -r)",
  "cpu": "$(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)",
  "cpu_cores": $(nproc),
  "memory_gb": $(free -g | awk '/Mem:/ {print $2}'),
  "gpu": "$(lspci 2>/dev/null | grep -i vga | cut -d: -f3 | xargs || echo 'unknown')"
}
EOF

# Build fullscreen flag
FS_FLAG=""
if [ "$FULLSCREEN" = true ]; then
    FS_FLAG="--fullscreen"
fi

# Run benchmarks for each SVG file
for svg_file in "${SVG_FILES[@]}"; do
    if [ ! -f "$svg_file" ]; then
        log_warn "File not found: $svg_file"
        continue
    fi

    svg_name=$(basename "$svg_file" .svg)
    svg_name=$(basename "$svg_name" .fbf)
    log_step "Benchmarking: $svg_name"

    # ThorVG benchmark
    log_info "  Running ThorVG..."
    thorvg_json="$RUN_DIR/${svg_name}_thorvg.json"
    if command -v thorvg_player &> /dev/null; then
        thorvg_player "$svg_file" "$DURATION" $FS_FLAG --json "$thorvg_json" 2>&1 | tee "$RUN_DIR/${svg_name}_thorvg.log"
    else
        log_warn "  ThorVG player not found, skipping"
        echo '{"error": "player not found"}' > "$thorvg_json"
    fi

    # fbfsvg-player benchmark
    log_info "  Running fbfsvg-player..."
    fbfsvg_json="$RUN_DIR/${svg_name}_fbfsvg.json"
    if command -v svg_player_animated_linux &> /dev/null; then
        # Run with timeout and capture stats
        timeout "${DURATION}s" svg_player_animated_linux "$svg_file" $FS_FLAG 2>&1 | tee "$RUN_DIR/${svg_name}_fbfsvg.log" || true
        # Parse stats from log (fbfsvg-player outputs stats on exit)
        # For now, create placeholder JSON - will be parsed from log
        echo '{"player": "fbfsvg", "file": "'"$svg_file"'"}' > "$fbfsvg_json"
    else
        log_warn "  fbfsvg-player not found, skipping"
        echo '{"error": "player not found"}' > "$fbfsvg_json"
    fi

    echo ""
done

# Generate comparison report
log_step "Generating comparison report..."

cat > "$RUN_DIR/report.md" << 'REPORT_HEADER'
# SVG Player Benchmark Report

## Test Configuration
REPORT_HEADER

echo "- **Date**: $(date)" >> "$RUN_DIR/report.md"
echo "- **Duration**: ${DURATION}s per test" >> "$RUN_DIR/report.md"
echo "- **Fullscreen**: $FULLSCREEN" >> "$RUN_DIR/report.md"
echo "" >> "$RUN_DIR/report.md"

echo "## Results" >> "$RUN_DIR/report.md"
echo "" >> "$RUN_DIR/report.md"
echo "| File | Player | Avg FPS | Avg Frame (ms) | Min Frame (ms) | Max Frame (ms) |" >> "$RUN_DIR/report.md"
echo "|------|--------|---------|----------------|----------------|----------------|" >> "$RUN_DIR/report.md"

for svg_file in "${SVG_FILES[@]}"; do
    svg_name=$(basename "$svg_file" .svg)
    svg_name=$(basename "$svg_name" .fbf)

    # Parse ThorVG results
    thorvg_json="$RUN_DIR/${svg_name}_thorvg.json"
    if [ -f "$thorvg_json" ] && ! grep -q "error" "$thorvg_json"; then
        fps=$(jq -r '.avg_fps // "N/A"' "$thorvg_json")
        avg=$(jq -r '.frame_time_ms.avg // "N/A"' "$thorvg_json")
        min=$(jq -r '.frame_time_ms.min // "N/A"' "$thorvg_json")
        max=$(jq -r '.frame_time_ms.max // "N/A"' "$thorvg_json")
        echo "| $svg_name | ThorVG | $fps | $avg | $min | $max |" >> "$RUN_DIR/report.md"
    fi

    # Parse fbfsvg results (from log file)
    fbfsvg_log="$RUN_DIR/${svg_name}_fbfsvg.log"
    if [ -f "$fbfsvg_log" ]; then
        fps=$(grep "Skia FPS:" "$fbfsvg_log" | awk '{print $3}' || echo "N/A")
        avg=$(grep "Average frame time:" "$fbfsvg_log" | awk '{print $4}' || echo "N/A")
        echo "| $svg_name | fbfsvg-player | $fps | $avg | - | - |" >> "$RUN_DIR/report.md"
    fi
done

echo "" >> "$RUN_DIR/report.md"
echo "## Dirty Region Tracking (fbfsvg-player only)" >> "$RUN_DIR/report.md"
echo "" >> "$RUN_DIR/report.md"

for svg_file in "${SVG_FILES[@]}"; do
    svg_name=$(basename "$svg_file" .svg)
    svg_name=$(basename "$svg_name" .fbf)
    fbfsvg_log="$RUN_DIR/${svg_name}_fbfsvg.log"

    if [ -f "$fbfsvg_log" ]; then
        echo "### $svg_name" >> "$RUN_DIR/report.md"
        grep -A5 "Dirty Region Tracking" "$fbfsvg_log" >> "$RUN_DIR/report.md" 2>/dev/null || echo "No dirty tracking data" >> "$RUN_DIR/report.md"
        echo "" >> "$RUN_DIR/report.md"
    fi
done

log_info "=== Benchmark Complete ==="
log_info "Results saved to: $RUN_DIR"
log_info "Report: $RUN_DIR/report.md"

# Print summary
echo ""
cat "$RUN_DIR/report.md"
BENCHMARK_EOF

chmod +x /usr/local/bin/run-benchmark

# Create quick comparison script
RUN cat > /usr/local/bin/quick-compare << 'QUICK_COMPARE_EOF'
#!/bin/bash
# Quick comparison of a single SVG file between both players

SVG_FILE="$1"
DURATION="${2:-10}"

if [ -z "$SVG_FILE" ]; then
    echo "Usage: quick-compare <svg_file> [duration_seconds]"
    exit 1
fi

echo "=== Quick Comparison: $(basename "$SVG_FILE") ==="
echo "Duration: ${DURATION}s each"
echo ""

echo "--- ThorVG ---"
thorvg_player "$SVG_FILE" "$DURATION" 2>&1 | grep -E "(FPS|Frame time|Render time)"

echo ""
echo "--- fbfsvg-player ---"
timeout "${DURATION}s" svg_player_animated_linux "$SVG_FILE" 2>&1 | grep -E "(FPS|frame time|Dirty Region)" || true
QUICK_COMPARE_EOF

chmod +x /usr/local/bin/quick-compare

# Set up environment
ENV LD_LIBRARY_PATH="/usr/local/lib:${LD_LIBRARY_PATH}"

# Switch to benchuser
USER benchuser
WORKDIR /workspace

# Entry point
CMD ["/bin/bash"]
