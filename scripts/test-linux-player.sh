#!/bin/bash

# test-linux-player.sh - Automated tests for Linux SVG player
# Runs in Docker container with Xvfb for headless testing

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_error() { echo -e "${RED}[FAIL]${NC} $1"; }
log_test() { echo -e "${CYAN}[TEST]${NC} $1"; }

PLAYER="$PROJECT_ROOT/build/fbfsvg-player-linux-x64"
TEST_SVG="$PROJECT_ROOT/svg_input_samples/seagull.fbf.svg"
LOG_DIR="$PROJECT_ROOT/tests/logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/linux_player_test_$TIMESTAMP.log"

# Create log directory
mkdir -p "$LOG_DIR"

# Initialize test counters
TESTS_PASSED=0
TESTS_FAILED=0

pass_test() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    log_info "$1"
}

fail_test() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    log_error "$1"
}

echo "=============================================="
echo "Linux SVG Player - Automated Test Suite"
echo "=============================================="
echo ""
echo "Log file: $LOG_FILE"
echo ""

# Test 1: Binary exists
log_test "Test 1: Binary exists"
if [ -f "$PLAYER" ]; then
    pass_test "Binary found at $PLAYER"
else
    fail_test "Binary not found at $PLAYER"
    exit 1
fi

# Test 2: Binary is ELF x64
log_test "Test 2: Binary architecture"
ARCH_INFO=$(file "$PLAYER")
if echo "$ARCH_INFO" | grep -q "ELF 64-bit.*x86-64"; then
    pass_test "Binary is ELF 64-bit x86-64"
else
    fail_test "Unexpected binary format: $ARCH_INFO"
fi

# Test 3: Help flag works
log_test "Test 3: --help flag"
if "$PLAYER" --help 2>&1 | grep -q "SVG Player"; then
    pass_test "--help works"
else
    fail_test "--help failed"
fi

# Test 4: Version flag works
log_test "Test 4: --version flag"
if "$PLAYER" --version 2>&1 | grep -q "v0\\."; then
    pass_test "--version works"
else
    fail_test "--version failed"
fi

# Test 5: Missing file error handling
log_test "Test 5: Missing file error"
OUTPUT=$("$PLAYER" /nonexistent/file.svg 2>&1) || true
if echo "$OUTPUT" | grep -qi "not found\|error"; then
    pass_test "Missing file returns error"
else
    fail_test "Missing file should return error"
fi

# Test 6: Run with Xvfb (load and render)
log_test "Test 6: Xvfb rendering test (3 seconds)"
if [ -f "$TEST_SVG" ]; then
    timeout 3s xvfb-run -a --server-args="-screen 0 1920x1080x24" \
        "$PLAYER" "$TEST_SVG" > "$LOG_FILE" 2>&1 || true

    # Check if animation was loaded (indicates successful rendering)
    if grep -q "SVGAnimationController: Loaded" "$LOG_FILE"; then
        pass_test "Player rendered without crash"
    else
        fail_test "Player failed to render"
    fi
else
    echo -e "${YELLOW}[SKIP]${NC} Test SVG not found at $TEST_SVG"
fi

# Test 7: Verify animation parsing (check log for animation info)
log_test "Test 7: Animation parsing"
if grep -q "animations, duration" "$LOG_FILE"; then
    pass_test "Animation parsed correctly"
else
    fail_test "Animation parsing failed"
fi

# Test 8: Check for memory leaks (basic check)
log_test "Test 8: No crash messages"
if ! grep -qi "segfault\|abort\|core dump\|fatal" "$LOG_FILE"; then
    pass_test "No crash messages in log"
else
    fail_test "Crash messages found in log"
fi

# Summary
echo ""
echo "=============================================="
echo "Test Summary"
echo "=============================================="
echo ""
echo -e "Tests Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests Failed: ${RED}$TESTS_FAILED${NC}"
echo ""

if [ "$TESTS_FAILED" -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}$TESTS_FAILED test(s) failed${NC}"
    exit 1
fi
