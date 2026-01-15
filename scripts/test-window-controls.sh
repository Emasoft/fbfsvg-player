#!/bin/bash
# test-window-controls.sh - Automated tests for window control features
# Tests: position, size, maximize, M key, F key
#
# SAFEGUARDS:
# - Maximum script runtime: 60 seconds
# - Per-test timeout: 5 seconds for background processes
# - Cleanup trap kills all child processes on exit
# - Watchdog kills any stray player processes
#
# TIP: To run tests on a specific Desktop (macOS):
#   1. Start the player manually once
#   2. Right-click player icon in Dock > Options > Assign To > Desktop 2
#   3. Now tests will run on Desktop 2, not interfering with your work

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PLAYER="$PROJECT_ROOT/build/svg_player_animated"
SVG_FILE="$PROJECT_ROOT/svg_input_samples/panther_bird.fbf.svg"

# Maximum runtime for entire test suite (seconds)
MAX_RUNTIME=60
# Maximum runtime for a single background test (seconds)
TEST_TIMEOUT=5

# Track all background PIDs for cleanup
declare -a BG_PIDS=()

# Cleanup function - kills all background processes
cleanup() {
    local exit_code=$?
    # Kill all tracked background processes
    for pid in "${BG_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    # Kill any stray player processes from this test run
    pkill -f "svg_player_animated.*panther_bird" 2>/dev/null || true
    exit $exit_code
}

# Set trap for cleanup on any exit
trap cleanup EXIT INT TERM

# Watchdog: kill script if it runs too long
(
    sleep $MAX_RUNTIME
    echo ""
    echo "[ERROR] Test suite exceeded ${MAX_RUNTIME}s timeout - killing"
    # Kill parent and all children
    kill -TERM -$$ 2>/dev/null || kill -TERM $$ 2>/dev/null
) &
WATCHDOG_PID=$!
# Don't track watchdog in BG_PIDS (it should exit when we do)
disown $WATCHDOG_PID 2>/dev/null || true

# Ensure logs directory exists
mkdir -p "$PROJECT_ROOT/tests/logs"
TEST_LOG="$PROJECT_ROOT/tests/logs/window-controls-$(date +%Y%m%d_%H%M%S).log"

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo "[PASS] $1"
    echo "[PASS] $1" >> "$TEST_LOG"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo "[FAIL] $1"
    echo "[FAIL] $1" >> "$TEST_LOG"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

info() {
    echo "[INFO] $1"
    echo "[INFO] $1" >> "$TEST_LOG"
}

# Check if player exists
if [[ ! -x "$PLAYER" ]]; then
    echo "ERROR: Player not found at $PLAYER"
    echo "Run 'make macos' first"
    exit 1
fi

echo "========================================"
echo "Window Controls Test Suite"
echo "Started: $(date)"
echo "========================================"
echo ""

# Test 1: Help shows new options
info "Test 1: Help shows new options"
HELP_OUTPUT=$("$PLAYER" --help 2>&1)
if echo "$HELP_OUTPUT" | grep -q "\-\-pos=X,Y" && \
   echo "$HELP_OUTPUT" | grep -q "\-\-size=WxH" && \
   echo "$HELP_OUTPUT" | grep -q "\-\-maximize"; then
    pass "Help shows all new window control options"
else
    fail "Help missing some window control options"
fi

# Test 2: Custom position
info "Test 2: Window with custom position (--pos=200,150)"
OUTPUT=$(timeout 2 "$PLAYER" "$SVG_FILE" --windowed --pos=200,150 2>&1 || true)
if echo "$OUTPUT" | grep -q "Rendering..."; then
    pass "Player started with custom position"
else
    fail "Player failed to start with custom position"
fi

# Test 3: Custom size
info "Test 3: Window with custom size (--size=640x480)"
OUTPUT=$(timeout 2 "$PLAYER" "$SVG_FILE" --windowed --size=640x480 2>&1 || true)
if echo "$OUTPUT" | grep -q "Rendering..."; then
    pass "Player started with custom size"
else
    fail "Player failed to start with custom size"
fi

# Test 4: Maximize option
info "Test 4: Start maximized (--maximize)"
OUTPUT=$(timeout 2 "$PLAYER" "$SVG_FILE" --maximize 2>&1 || true)
if echo "$OUTPUT" | grep -q "Started maximized"; then
    pass "Player started maximized"
else
    fail "Player failed to start maximized"
fi

# Test 5: Combined position and size
info "Test 5: Combined position and size"
OUTPUT=$(timeout 2 "$PLAYER" "$SVG_FILE" --windowed --pos=100,100 --size=800x600 2>&1 || true)
if echo "$OUTPUT" | grep -q "Rendering..."; then
    pass "Player started with combined position and size"
else
    fail "Player failed with combined position and size"
fi

# Test 6: M key for maximize toggle (uses osascript for reliable key sending)
info "Test 6: M key toggles maximize"
# Start player in background at known position with hard timeout
timeout $TEST_TIMEOUT "$PLAYER" "$SVG_FILE" --windowed --pos=100,100 --size=640x480 > /tmp/player_output.txt 2>&1 &
PLAYER_PID=$!
BG_PIDS+=($PLAYER_PID)
sleep 1.5

# Check if player is still running before sending keys
if kill -0 $PLAYER_PID 2>/dev/null; then
    # Use osascript to activate window and send keys (more reliable than cliclick)
    osascript <<'APPLESCRIPT' 2>/dev/null || true
tell application "System Events"
    set targetProcess to first process whose name contains "svg_player"
    set frontmost of targetProcess to true
    delay 0.3
    keystroke "m"
    delay 0.5
    keystroke "m"
    delay 0.3
    keystroke "q"
end tell
APPLESCRIPT
    sleep 0.5
fi

# Kill if still running (should have timed out or quit)
kill $PLAYER_PID 2>/dev/null || true
wait $PLAYER_PID 2>/dev/null || true

OUTPUT=$(cat /tmp/player_output.txt 2>/dev/null || echo "")
if echo "$OUTPUT" | grep -q "MAXIMIZED" && echo "$OUTPUT" | grep -q "RESTORED"; then
    pass "M key toggles maximize/restore"
else
    fail "M key maximize toggle not confirmed"
fi

# Test 7: F key for fullscreen toggle (uses osascript for reliable key sending)
info "Test 7: F key toggles fullscreen"
# Start player in background at known position with hard timeout
timeout $TEST_TIMEOUT "$PLAYER" "$SVG_FILE" --windowed --pos=100,100 --size=640x480 > /tmp/player_output.txt 2>&1 &
PLAYER_PID=$!
BG_PIDS+=($PLAYER_PID)
sleep 1.5

# Check if player is still running before sending keys
if kill -0 $PLAYER_PID 2>/dev/null; then
    # Use osascript to activate window and send keys (more reliable than cliclick)
    osascript <<'APPLESCRIPT' 2>/dev/null || true
tell application "System Events"
    set targetProcess to first process whose name contains "svg_player"
    set frontmost of targetProcess to true
    delay 0.3
    keystroke "f"
    delay 0.5
    keystroke "f"
    delay 0.3
    keystroke "q"
end tell
APPLESCRIPT
    sleep 0.5
fi

# Kill if still running (should have timed out or quit)
kill $PLAYER_PID 2>/dev/null || true
wait $PLAYER_PID 2>/dev/null || true

OUTPUT=$(cat /tmp/player_output.txt 2>/dev/null || echo "")
if echo "$OUTPUT" | grep -q "Fullscreen: ON" && echo "$OUTPUT" | grep -q "Fullscreen: OFF"; then
    pass "F key toggles fullscreen"
else
    fail "F key fullscreen toggle not confirmed"
fi

# Test 8: Invalid position format error
info "Test 8: Invalid position format error"
OUTPUT=$("$PLAYER" "$SVG_FILE" --pos=invalid 2>&1 || true)
if echo "$OUTPUT" | grep -q "Invalid position format"; then
    pass "Invalid position format detected"
else
    fail "Invalid position format not detected"
fi

# Test 9: Invalid size format error
info "Test 9: Invalid size format error"
OUTPUT=$("$PLAYER" "$SVG_FILE" --size=invalid 2>&1 || true)
if echo "$OUTPUT" | grep -q "Invalid size format"; then
    pass "Invalid size format detected"
else
    fail "Invalid size format not detected"
fi

echo ""
echo "========================================"
echo "Test Results"
echo "========================================"
echo "PASSED: $PASS_COUNT"
echo "FAILED: $FAIL_COUNT"
echo "Log: $TEST_LOG"
echo ""

# Kill the watchdog before exiting
kill $WATCHDOG_PID 2>/dev/null || true

if [[ $FAIL_COUNT -gt 0 ]]; then
    echo "SOME TESTS FAILED"
    exit 1
else
    echo "ALL TESTS PASSED"
    exit 0
fi
