#!/bin/bash
#
# SVGPlayer.sh - SVG Player Launcher
#
# This script can be pinned to macOS Desktop 2 via Dock:
# 1. Drag this script to Dock (it will show as Terminal icon)
# 2. Right-click the icon -> Options -> Assign To -> Desktop 2
#
# The script always launches the latest built player binary.
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLAYER="${SCRIPT_DIR}/build/fbfsvg-player"
DEFAULT_SVG="${SCRIPT_DIR}/svg_input_samples/seagull.fbf.svg"

# Use provided SVG file or default
SVG_FILE="${1:-$DEFAULT_SVG}"

# Check if player exists
if [[ ! -x "$PLAYER" ]]; then
    echo "ERROR: Player not found at:"
    echo "  $PLAYER"
    echo ""
    echo "Build it first with: make macos"
    echo ""
    read -p "Press Enter to close..."
    exit 1
fi

# Check if SVG file exists
if [[ ! -f "$SVG_FILE" ]]; then
    echo "ERROR: SVG file not found:"
    echo "  $SVG_FILE"
    echo ""
    read -p "Press Enter to close..."
    exit 1
fi

echo "=========================================="
echo "  SVG Player Launcher"
echo "=========================================="
echo ""
echo "Player:  $PLAYER"
echo "SVG:     $SVG_FILE"
echo "Remote:  port 9999"
echo ""
echo "Keys: F/G=Fullscreen, M=Maximize, Q=Quit"
echo ""

# Launch the player with remote control enabled
exec "$PLAYER" --remote-control "$SVG_FILE"
