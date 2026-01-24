#!/bin/bash
# benchmark-native.sh - Native macOS/Linux benchmark comparing ThorVG vs fbfsvg-player
#
# Both players run natively WITHOUT Docker for fair GPU comparison.
#
# REQUIREMENTS (macOS):
#   brew install meson ninja sdl2 pkg-config
#
# USAGE:
#   ./scripts/benchmark-native.sh --quick          # Quick 5s benchmark on default file
#   ./scripts/benchmark-native.sh --all            # Benchmark all SVG samples (5s each)
#   ./scripts/benchmark-native.sh file.svg 10      # Benchmark specific file for 10s
#   ./scripts/benchmark-native.sh --setup          # First-time setup only

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
THORVG_DIR="$PROJECT_ROOT/builds_dev/thorvg"
RESULTS_DIR="$PROJECT_ROOT/builds_dev/benchmark_results"
SAMPLES_DIR="$PROJECT_ROOT/svg_input_samples"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# Detect platform
if [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macos"
    THORVG_PLAYER="$THORVG_DIR/build/thorvg_player_macos"
    FBFSVG_PLAYER="$PROJECT_ROOT/build/fbfsvg-player"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="linux"
    ARCH=$(uname -m)
    if [ "$ARCH" = "x86_64" ]; then
        ARCH_SUFFIX="x64"
    else
        ARCH_SUFFIX="arm64"
    fi
    THORVG_PLAYER="$THORVG_DIR/build/thorvg_player_linux"
    FBFSVG_PLAYER="$PROJECT_ROOT/build/fbfsvg-player-linux-$ARCH_SUFFIX"
else
    log_error "Unsupported platform: $OSTYPE"
    exit 1
fi

setup_thorvg() {
    log_step "Setting up ThorVG..."

    mkdir -p "$THORVG_DIR"
    cd "$THORVG_DIR"

    # Clone if not exists
    if [ ! -d "thorvg" ]; then
        log_info "Cloning ThorVG..."
        git clone --depth 1 https://github.com/thorvg/thorvg.git
    fi

    cd thorvg

    # Build ThorVG library
    log_info "Building ThorVG library..."
    # ThorVG meson options (as of 2025): engines, loaders, savers, bindings, tools, tests
    # Note: examples option was removed in newer ThorVG versions
    if [ "$PLATFORM" = "macos" ]; then
        # macOS: use default SW backend - ThorVG doesn't have Metal backend yet
        meson setup build -Ddefault_library=static -Dloaders="['svg']" -Dsavers="['']" \
            -Dbindings="['']" -Dtools="['']" -Dtests=false \
            --buildtype=release --wipe 2>/dev/null || \
        meson setup build -Ddefault_library=static -Dloaders="['svg']" -Dsavers="['']" \
            -Dbindings="['']" -Dtools="['']" -Dtests=false \
            --buildtype=release
    else
        # Linux: try GL backend for GPU acceleration
        meson setup build -Ddefault_library=static -Dloaders="['svg']" -Dsavers="['']" \
            -Dbindings="['']" -Dtools="['']" -Dtests=false \
            -Dengines="['sw','gl']" --buildtype=release --wipe 2>/dev/null || \
        meson setup build -Ddefault_library=static -Dloaders="['svg']" -Dsavers="['']" \
            -Dbindings="['']" -Dtools="['']" -Dtests=false \
            --buildtype=release
    fi

    ninja -C build

    # Create minimal benchmark player
    log_info "Building ThorVG benchmark player..."
    mkdir -p "$THORVG_DIR/build"

    cat > "$THORVG_DIR/thorvg_player.cpp" << 'PLAYER_EOF'
// Minimal ThorVG player for benchmarking - outputs JSON stats
// Updated for ThorVG 1.0 API - measures raw throughput (no vsync)
#include <thorvg.h>
#include <SDL.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <svg_file> [duration_seconds] [--json]" << std::endl;
        return 1;
    }

    const char* svgPath = argv[1];
    int duration = (argc > 2 && argv[2][0] != '-') ? std::atoi(argv[2]) : 10;
    bool jsonOutput = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    // Load SVG content
    std::ifstream file(svgPath);
    if (!file) {
        std::cerr << "Cannot open: " << svgPath << std::endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string svgData = buffer.str();

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create fullscreen window
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    int width = dm.w, height = dm.h;

    SDL_Window* window = SDL_CreateWindow("ThorVG Benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create renderer WITHOUT vsync for raw throughput measurement
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width, height);

    // Initialize ThorVG
    tvg::Initializer::init(0);

    // ThorVG 1.0 returns raw pointers
    tvg::Picture* picture = tvg::Picture::gen();
    // ThorVG 1.0 API: load(data, size, mimeType, rpath, copy)
    if (picture->load(svgData.data(), svgData.size(), "svg", nullptr, false) != tvg::Result::Success) {
        std::cerr << "Failed to load SVG" << std::endl;
        tvg::Initializer::term();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Scale to fit
    float pw, ph;
    picture->size(&pw, &ph);
    float scale = std::min((float)width / pw, (float)height / ph);
    picture->scale(scale);
    picture->translate((width - pw * scale) / 2, (height - ph * scale) / 2);

    tvg::SwCanvas* canvas = tvg::SwCanvas::gen();
    std::vector<uint32_t> pixels(width * height);
    // ThorVG 1.0 API: target(buffer, stride, w, h, ColorSpace)
    canvas->target(pixels.data(), width, width, height, tvg::ColorSpace::ARGB8888);
    // ThorVG 1.0 API: add() takes ownership
    canvas->add(picture);

    // Benchmark loop - measuring raw throughput without vsync
    std::vector<double> frameTimes;
    auto startTime = std::chrono::high_resolution_clock::now();
    auto endTime = startTime + std::chrono::seconds(duration);

    SDL_Event event;
    bool running = true;

    while (running && std::chrono::high_resolution_clock::now() < endTime) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        auto frameStart = std::chrono::high_resolution_clock::now();

        // Clear and render
        std::fill(pixels.begin(), pixels.end(), 0);
        canvas->update();
        canvas->draw();
        canvas->sync();

        // Update texture
        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        frameTimes.push_back(frameMs);
    }

    // Calculate stats
    double totalTime = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - startTime).count();

    double avgFrameTime = 0, minFrameTime = 1e9, maxFrameTime = 0;
    for (double t : frameTimes) {
        avgFrameTime += t;
        minFrameTime = std::min(minFrameTime, t);
        maxFrameTime = std::max(maxFrameTime, t);
    }
    avgFrameTime /= frameTimes.size();

    double avgFps = frameTimes.size() / totalTime;
    double minFps = 1000.0 / maxFrameTime;
    double maxFps = 1000.0 / minFrameTime;

    // Cleanup (canvas owns picture)
    delete canvas;
    tvg::Initializer::term();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    // Output
    if (jsonOutput) {
        std::cout << "{";
        std::cout << "\"player\":\"thorvg\",";
        std::cout << "\"file\":\"" << svgPath << "\",";
        std::cout << "\"duration_seconds\":" << totalTime << ",";
        std::cout << "\"total_frames\":" << frameTimes.size() << ",";
        std::cout << "\"avg_fps\":" << avgFps << ",";
        std::cout << "\"avg_frame_time_ms\":" << avgFrameTime << ",";
        std::cout << "\"min_fps\":" << minFps << ",";
        std::cout << "\"max_fps\":" << maxFps;
        std::cout << "}" << std::endl;
    } else {
        std::cout << "\n=== ThorVG Benchmark Results ===" << std::endl;
        std::cout << "File: " << svgPath << std::endl;
        std::cout << "Duration: " << totalTime << "s" << std::endl;
        std::cout << "Frames: " << frameTimes.size() << std::endl;
        std::cout << "Average FPS: " << avgFps << std::endl;
        std::cout << "Average frame time: " << avgFrameTime << " ms" << std::endl;
        std::cout << "FPS range: " << minFps << " - " << maxFps << std::endl;
    }

    return 0;
}
PLAYER_EOF

    # Compile the player (library may be libthorvg.a or libthorvg-1.a depending on version)
    THORVG_LIB=$(ls "$THORVG_DIR/thorvg/build/src"/libthorvg*.a 2>/dev/null | head -1)
    if [ -z "$THORVG_LIB" ]; then
        log_error "ThorVG library not found in build directory"
        exit 1
    fi
    log_info "Using ThorVG library: $THORVG_LIB"

    if [ "$PLATFORM" = "macos" ]; then
        # Link with OpenMP (required since ThorVG is built with OpenMP support)
        clang++ -std=c++17 -O2 \
            -I"$THORVG_DIR/thorvg/inc" \
            $(pkg-config --cflags sdl2) \
            "$THORVG_DIR/thorvg_player.cpp" \
            -o "$THORVG_PLAYER" \
            "$THORVG_LIB" \
            $(pkg-config --libs sdl2) \
            -framework Cocoa -framework IOKit -framework CoreVideo \
            -L/opt/homebrew/opt/libomp/lib -lomp
    else
        clang++ -std=c++17 -O2 \
            -I"$THORVG_DIR/thorvg/inc" \
            $(pkg-config --cflags sdl2) \
            "$THORVG_DIR/thorvg_player.cpp" \
            -o "$THORVG_PLAYER" \
            "$THORVG_LIB" \
            $(pkg-config --libs sdl2) \
            -lpthread -ldl -lgomp
    fi

    log_info "ThorVG player built: $THORVG_PLAYER"
}

# Run benchmark on a single file and return JSON results
run_single_benchmark() {
    local svg_file="$1"
    local duration="$2"
    local thorvg_json=""
    local fbfsvg_json=""

    # Run ThorVG (suppress all output except JSON)
    thorvg_json=$("$THORVG_PLAYER" "$svg_file" "$duration" --json 2>/dev/null || echo '{"error":"failed","avg_fps":0}')

    sleep 0.5

    # Run fbfsvg-player (suppress all output except JSON)
    fbfsvg_json=$("$FBFSVG_PLAYER" "$svg_file" --duration "$duration" --fullscreen --json 2>/dev/null || echo '{"error":"failed","avg_fps":0}')

    echo "$thorvg_json"
    echo "$fbfsvg_json"
}

# Print results as a nice table
print_results_table() {
    local results_json="$1"

    python3 << TABLEEOF
import json
import sys

# ANSI colors
BOLD = '\033[1m'
GREEN = '\033[0;32m'
YELLOW = '\033[1;33m'
CYAN = '\033[0;36m'
RED = '\033[0;31m'
DIM = '\033[2m'
NC = '\033[0m'

results = json.loads('''$results_json''')

# Table characters (Unicode box drawing)
TL, TR, BL, BR = '┌', '┐', '└', '┘'
H, V = '─', '│'
LT, RT, TT, BT, X = '├', '┤', '┬', '┴', '┼'

# Column widths
col_file = 28
col_thorvg = 12
col_fbfsvg = 12
col_ratio = 10
col_winner = 16
total_width = col_file + col_thorvg + col_fbfsvg + col_ratio + col_winner + 6

def hrule(left, mid, right):
    return f"{left}{H*col_file}{mid}{H*col_thorvg}{mid}{H*col_fbfsvg}{mid}{H*col_ratio}{mid}{H*col_winner}{right}"

# Header
print()
print(f"{BOLD}{CYAN}{'ThorVG vs fbfsvg-player Benchmark Results':^{total_width}}{NC}")
print()
print(hrule(TL, TT, TR))
print(f"{V}{BOLD}{'SVG File':<{col_file}}{NC}{V}{BOLD}{'ThorVG FPS':>{col_thorvg}}{NC}{V}{BOLD}{'fbfsvg FPS':>{col_fbfsvg}}{NC}{V}{BOLD}{'Ratio':>{col_ratio}}{NC}{V}{BOLD}{'Winner':<{col_winner}}{NC}{V}")
print(hrule(LT, X, RT))

total_thorvg = 0
total_fbfsvg = 0
count = 0

for entry in results:
    filename = entry.get('file', 'unknown')
    # Truncate filename if too long
    if len(filename) > col_file - 2:
        filename = '...' + filename[-(col_file-5):]

    t_fps = entry.get('thorvg_fps', 0)
    f_fps = entry.get('fbfsvg_fps', 0)

    if t_fps > 0 and f_fps > 0:
        ratio = f_fps / t_fps
        total_thorvg += t_fps
        total_fbfsvg += f_fps
        count += 1

        if ratio > 1.05:
            winner = f"{GREEN}fbfsvg-player{NC}"
            ratio_str = f"{GREEN}{ratio:.2f}x{NC}"
        elif ratio < 0.95:
            winner = f"{YELLOW}ThorVG{NC}"
            ratio_str = f"{YELLOW}{ratio:.2f}x{NC}"
        else:
            winner = f"{DIM}Tie{NC}"
            ratio_str = f"{DIM}{ratio:.2f}x{NC}"
    else:
        ratio_str = f"{RED}N/A{NC}"
        winner = f"{RED}Error{NC}"

    # Format FPS with color
    t_str = f"{t_fps:.1f}" if t_fps > 0 else f"{RED}ERR{NC}"
    f_str = f"{f_fps:.1f}" if f_fps > 0 else f"{RED}ERR{NC}"

    print(f"{V}{filename:<{col_file}}{V}{t_str:>{col_thorvg}}{V}{f_str:>{col_fbfsvg}}{V}{ratio_str:>{col_ratio+9}}{V}{winner:<{col_winner+9}}{V}")

print(hrule(BL, BT, BR))

# Summary
if count > 0:
    avg_thorvg = total_thorvg / count
    avg_fbfsvg = total_fbfsvg / count
    avg_ratio = avg_fbfsvg / avg_thorvg if avg_thorvg > 0 else 0

    print()
    print(f"{BOLD}Summary:{NC}")
    print(f"  Files benchmarked: {count}")
    print(f"  Average ThorVG:    {avg_thorvg:.1f} FPS")
    print(f"  Average fbfsvg:    {avg_fbfsvg:.1f} FPS")
    print(f"  Average ratio:     {avg_ratio:.2f}x")

    if avg_ratio > 1.05:
        pct = (avg_ratio - 1) * 100
        print(f"  {GREEN}Overall: fbfsvg-player is {pct:.0f}% faster on average{NC}")
    elif avg_ratio < 0.95:
        pct = (1 - avg_ratio) * 100
        print(f"  {YELLOW}Overall: ThorVG is {pct:.0f}% faster on average{NC}")
    else:
        print(f"  {DIM}Overall: Performance is comparable (within 5%){NC}")

print()
TABLEEOF
}

run_benchmark() {
    local svg_file="$1"
    local duration="${2:-10}"

    if [ ! -f "$svg_file" ]; then
        log_error "SVG file not found: $svg_file"
        exit 1
    fi

    check_players

    mkdir -p "$RESULTS_DIR"
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local basename=$(basename "$svg_file" .svg)

    echo ""
    log_step "Benchmarking: $svg_file"
    log_info "Duration: ${duration}s per player"
    echo ""

    # Run benchmark
    log_info "Running ThorVG..."
    local thorvg_result=$("$THORVG_PLAYER" "$svg_file" "$duration" --json 2>/dev/null || echo '{"error":"failed","avg_fps":0}')
    local t_fps=$(echo "$thorvg_result" | python3 -c "import sys,json; print(json.load(sys.stdin).get('avg_fps',0))" 2>/dev/null || echo "0")

    sleep 1

    log_info "Running fbfsvg-player..."
    local fbfsvg_result=$("$FBFSVG_PLAYER" "$svg_file" --duration "$duration" --fullscreen --json 2>/dev/null || echo '{"error":"failed","avg_fps":0}')
    local f_fps=$(echo "$fbfsvg_result" | python3 -c "import sys,json; print(json.load(sys.stdin).get('avg_fps',0))" 2>/dev/null || echo "0")

    # Create combined results JSON
    local results_json="[{\"file\":\"$basename\",\"thorvg_fps\":$t_fps,\"fbfsvg_fps\":$f_fps}]"

    # Print table
    print_results_table "$results_json"

    # Save detailed results
    local result_file="$RESULTS_DIR/benchmark_${basename}_${timestamp}.json"
    echo "[" > "$result_file"
    echo "$thorvg_result," >> "$result_file"
    echo "$fbfsvg_result" >> "$result_file"
    echo "]" >> "$result_file"
    log_info "Detailed results saved: $result_file"
}

run_all_benchmarks() {
    local duration="${1:-5}"

    check_players

    # Find all SVG files
    local svg_files=($(find "$SAMPLES_DIR" -maxdepth 1 -name "*.svg" -type f | sort))
    local count=${#svg_files[@]}

    if [ "$count" -eq 0 ]; then
        log_error "No SVG files found in: $SAMPLES_DIR"
        exit 1
    fi

    echo ""
    log_step "Benchmarking $count SVG files (${duration}s each)"
    log_info "This will take approximately $((count * duration * 2 + count))s"
    echo ""

    mkdir -p "$RESULTS_DIR"
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local all_results="["
    local first=true

    for svg_file in "${svg_files[@]}"; do
        local basename=$(basename "$svg_file" .svg)
        echo -ne "\r${CYAN}[BENCH]${NC} Testing: $basename...                    "

        # Run ThorVG
        local thorvg_result=$("$THORVG_PLAYER" "$svg_file" "$duration" --json 2>/dev/null || echo '{"error":"failed","avg_fps":0}')
        local t_fps=$(echo "$thorvg_result" | python3 -c "import sys,json; print(json.load(sys.stdin).get('avg_fps',0))" 2>/dev/null || echo "0")

        sleep 0.5

        # Run fbfsvg-player
        local fbfsvg_result=$("$FBFSVG_PLAYER" "$svg_file" --duration "$duration" --fullscreen --json 2>/dev/null || echo '{"error":"failed","avg_fps":0}')
        local f_fps=$(echo "$fbfsvg_result" | python3 -c "import sys,json; print(json.load(sys.stdin).get('avg_fps',0))" 2>/dev/null || echo "0")

        # Add to results
        if [ "$first" = true ]; then
            first=false
        else
            all_results+=","
        fi
        all_results+="{\"file\":\"$basename\",\"thorvg_fps\":$t_fps,\"fbfsvg_fps\":$f_fps}"
    done

    all_results+="]"
    echo -e "\r${GREEN}[DONE]${NC} All benchmarks complete!                    "
    echo ""

    # Print table
    print_results_table "$all_results"

    # Save results
    local result_file="$RESULTS_DIR/benchmark_all_${timestamp}.json"
    echo "$all_results" | python3 -m json.tool > "$result_file" 2>/dev/null || echo "$all_results" > "$result_file"
    log_info "Results saved: $result_file"
}

check_players() {
    if [ ! -f "$THORVG_PLAYER" ]; then
        log_error "ThorVG player not found. Run: $0 --setup"
        exit 1
    fi

    if [ ! -f "$FBFSVG_PLAYER" ]; then
        log_error "fbfsvg-player not found at: $FBFSVG_PLAYER"
        log_info "Build it with: make macos (or ./scripts/build-linux.sh)"
        exit 1
    fi
}

run_quick_benchmark() {
    local default_file=""

    # Find a good default file
    if [ -f "$SAMPLES_DIR/splat_button.fbf.svg" ]; then
        default_file="$SAMPLES_DIR/splat_button.fbf.svg"
    elif [ -f "$SAMPLES_DIR/seagull.fbf.svg" ]; then
        default_file="$SAMPLES_DIR/seagull.fbf.svg"
    else
        # Use first SVG found
        default_file=$(find "$SAMPLES_DIR" -maxdepth 1 -name "*.svg" -type f | head -1)
    fi

    if [ -z "$default_file" ] || [ ! -f "$default_file" ]; then
        log_error "No SVG files found in: $SAMPLES_DIR"
        exit 1
    fi

    run_benchmark "$default_file" 5
}

# Main
case "${1:-}" in
    --setup)
        setup_thorvg
        log_info "Setup complete!"
        log_info "Now run: $0 --quick"
        ;;
    --quick|-q)
        run_quick_benchmark
        ;;
    --all|-a)
        run_all_benchmarks "${2:-5}"
        ;;
    --help|-h)
        echo "Usage: $0 [options] [svg_file] [duration_seconds]"
        echo ""
        echo "Options:"
        echo "  --setup, -s     Build ThorVG benchmark player (first time only)"
        echo "  --quick, -q     Quick 5s benchmark on default sample file"
        echo "  --all, -a       Benchmark all SVG files in samples directory"
        echo "  --help, -h      Show this help"
        echo ""
        echo "Examples:"
        echo "  $0 --setup                    # First-time setup"
        echo "  $0 --quick                    # Quick test"
        echo "  $0 --all                      # All samples (5s each)"
        echo "  $0 --all 10                   # All samples (10s each)"
        echo "  $0 file.svg 10                # Specific file for 10s"
        ;;
    "")
        # No arguments - show help
        echo "Usage: $0 [--quick|--all|<svg_file>] [duration]"
        echo ""
        echo "Quick start:"
        echo "  $0 --setup   # First time only - builds ThorVG"
        echo "  $0 --quick   # Quick 5s benchmark"
        echo "  $0 --all     # All samples"
        echo ""
        echo "Run '$0 --help' for more options."
        exit 0
        ;;
    *)
        run_benchmark "$1" "${2:-10}"
        ;;
esac
