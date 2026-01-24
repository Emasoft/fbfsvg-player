#!/bin/bash
# Launch ThorVG player for benchmarking
# Pin to Desktop 2 for isolated testing

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

FRAMES_DIR="${PROJECT_DIR}/svg_input_samples/stress_frames"
THORVG_PLAYER="${PROJECT_DIR}/builds_dev/thorvg/thorvg_player"

if [[ ! -x "$THORVG_PLAYER" ]]; then
    echo "Error: ThorVG player not found at $THORVG_PLAYER"
    exit 1
fi

if [[ ! -d "$FRAMES_DIR" ]]; then
    echo "Error: Stress frames not found at $FRAMES_DIR"
    exit 1
fi

echo "=== ThorVG Player Benchmark ==="
echo "Frames: $FRAMES_DIR"
echo "FPS displayed in window title"
echo ""

exec "$THORVG_PLAYER" "$FRAMES_DIR" --loop
