#!/bin/bash
# Launch Skia folder player for benchmarking
# Pin to Desktop 2 for isolated testing

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

FRAMES_DIR="${PROJECT_DIR}/svg_input_samples/stress_frames"
SKIA_PLAYER="${PROJECT_DIR}/build/skia_folder_player"

if [[ ! -x "$SKIA_PLAYER" ]]; then
    echo "Error: Skia folder player not found at $SKIA_PLAYER"
    exit 1
fi

if [[ ! -d "$FRAMES_DIR" ]]; then
    echo "Error: Stress frames not found at $FRAMES_DIR"
    exit 1
fi

echo "=== Skia Player Benchmark ==="
echo "Frames: $FRAMES_DIR"
echo "FPS displayed in window title"
echo ""

exec "$SKIA_PLAYER" "$FRAMES_DIR" --loop
