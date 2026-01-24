#!/bin/bash
# test-player-automation.sh - Automated testing of SVG player on Space 2
# This script launches the player, moves it to virtual desktop 2, and runs tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PLAYER="$PROJECT_ROOT/build/svg_player_animated"
LOG_DIR="$PROJECT_ROOT/tests/logs"
LOG_FILE="$LOG_DIR/automation-test-$(date +%Y%m%d_%H%M%S).log"
TEST_SVG="$PROJECT_ROOT/svg_input_samples/seagull.fbf.svg"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create log directory
mkdir -p "$LOG_DIR"

echo "======================================"
echo "  SVG Player Automation Test"
echo "======================================"
echo "Log file: $LOG_FILE"
echo ""

# Check player exists
if [[ ! -x "$PLAYER" ]]; then
    echo -e "${RED}[ERROR]${NC} Player not found: $PLAYER"
    exit 1
fi

# Check test file exists
if [[ ! -f "$TEST_SVG" ]]; then
    echo -e "${RED}[ERROR]${NC} Test SVG not found: $TEST_SVG"
    exit 1
fi

# Check cliclick is available
if ! command -v cliclick &> /dev/null; then
    echo -e "${RED}[ERROR]${NC} cliclick not found. Install with: brew install cliclick"
    exit 1
fi

# Function to move active window to Space 2
move_to_space_2() {
    echo "[INFO] Moving player window to Space 2..."
    # Use AppleScript to simulate Control+2 (moves focused window to Desktop 2)
    osascript <<'EOF'
tell application "System Events"
    -- Give the window focus
    delay 0.5
    -- Control+2 moves to Space 2 (if Mission Control shortcuts are default)
    key code 19 using control down
    delay 0.5
end tell
EOF
    echo "[INFO] Window should now be on Space 2"
}

# Function to send key to player
send_key() {
    local key="$1"
    local desc="$2"
    echo "[TEST] Sending key: $key ($desc)"
    cliclick kp:"$key"
    sleep 0.5
}

# Function to wait for player to be ready
wait_for_player() {
    echo "[INFO] Waiting for player to start..."
    sleep 2

    # Use AppleScript to bring player to front
    osascript <<'EOF'
tell application "System Events"
    tell process "svg_player_animated"
        set frontmost to true
    end tell
end tell
EOF
    sleep 0.5
}

# Start log capture
echo "Starting automation test at $(date)" > "$LOG_FILE"
echo "Player: $PLAYER" >> "$LOG_FILE"
echo "Test SVG: $TEST_SVG" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Launch player in background with log capture
echo "[STEP 1] Launching player..."
"$PLAYER" "$TEST_SVG" >> "$LOG_FILE" 2>&1 &
PLAYER_PID=$!
echo "[INFO] Player PID: $PLAYER_PID"

# Wait for player to initialize
wait_for_player

# Move to Space 2
echo ""
echo "[STEP 2] Moving to Space 2..."
move_to_space_2

# Run tests
echo ""
echo "[STEP 3] Running keyboard tests..."

# Test 1: Space bar (play/pause)
send_key "space" "Play/Pause toggle"
sleep 1
send_key "space" "Play/Pause toggle (resume)"
sleep 1

# Test 2: Speed controls
send_key "arrow-up" "Increase speed"
sleep 0.5
send_key "arrow-down" "Decrease speed"
sleep 0.5

# Test 3: Seek controls
send_key "arrow-right" "Seek forward"
sleep 0.5
send_key "arrow-left" "Seek backward"
sleep 0.5

# Test 4: Reset
send_key "r" "Reset to beginning"
sleep 0.5

# Test 5: Browser mode (the main feature we're testing)
echo ""
echo "[STEP 4] Testing browser mode..."
send_key "b" "Open browser"
sleep 2

# Wait for thumbnails to load
echo "[INFO] Waiting for thumbnails to load (5 seconds)..."
sleep 5

# Exit browser with ESC (testing our fix)
echo "[TEST] Testing ESC key in browser mode..."
send_key "escape" "Exit browser (ESC)"
sleep 1

# Re-open browser to test Cancel button would work
echo "[TEST] Re-opening browser..."
send_key "b" "Open browser again"
sleep 3

# Close with ESC again
send_key "escape" "Exit browser (ESC again)"
sleep 1

# Test 6: Fullscreen toggle
echo ""
echo "[STEP 5] Testing fullscreen..."
send_key "f" "Toggle fullscreen"
sleep 1
send_key "f" "Exit fullscreen"
sleep 1

# Test 7: Exit with Q
echo ""
echo "[STEP 6] Exiting player..."
send_key "q" "Quit player"
sleep 1

# Check if player exited cleanly
if kill -0 $PLAYER_PID 2>/dev/null; then
    echo -e "${YELLOW}[WARN]${NC} Player still running, sending SIGTERM..."
    kill $PLAYER_PID 2>/dev/null || true
    sleep 1
fi

wait $PLAYER_PID 2>/dev/null || true

echo ""
echo "======================================"
echo "  Test Complete"
echo "======================================"
echo ""
echo "Log file: $LOG_FILE"
echo ""
echo "Checking log for errors..."
echo ""

# Analyze log for issues
if grep -i "error\|fail\|crash\|exception" "$LOG_FILE" | head -10; then
    echo -e "${YELLOW}[WARN]${NC} Potential issues found in log (see above)"
else
    echo -e "${GREEN}[OK]${NC} No obvious errors in log"
fi

echo ""
echo "Last 20 lines of log:"
echo "--------------------------------------"
tail -20 "$LOG_FILE"
echo "--------------------------------------"

echo ""
echo -e "${GREEN}[DONE]${NC} Automation test completed"
