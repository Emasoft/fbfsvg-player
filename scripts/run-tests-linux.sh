#!/bin/bash

# run-tests-linux.sh - Linux CI/CD Smoke Tests for FBF.SVG Player
#
# Runs basic smoke tests to verify the Linux build works correctly.
# Designed to run inside Docker container with /workspace as project root.
#
# Tests performed:
#   1. Binary existence and executability
#   2. Version check (--version smoke test)
#   3. Headless render test (if sample SVG exists)
#
# Usage:
#   ./scripts/run-tests-linux.sh
#   docker-compose exec dev /workspace/scripts/run-tests-linux.sh
#
# Exit codes:
#   0 - All tests passed
#   1 - One or more tests failed

set -e

# Detect project root (works both inside Docker and standalone)
if [ -d "/workspace" ] && [ -f "/workspace/CLAUDE.md" ]; then
    PROJECT_ROOT="/workspace"
else
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
fi

# Create logs directory if it doesn't exist
LOG_DIR="${PROJECT_ROOT}/tests/logs"
mkdir -p "$LOG_DIR"

# Timestamped log file
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="${LOG_DIR}/linux_test_${TIMESTAMP}.log"

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Logging functions (write to both terminal and log file)
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a "$LOG_FILE"
}
log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a "$LOG_FILE"
}
log_error() {
    echo -e "${RED}[FAIL]${NC} $1" | tee -a "$LOG_FILE"
}
log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1" | tee -a "$LOG_FILE"
    ((TESTS_PASSED++))
}
log_fail() {
    echo -e "${RED}[FAIL]${NC} $1" | tee -a "$LOG_FILE"
    ((TESTS_FAILED++))
}
log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1" | tee -a "$LOG_FILE"
    ((TESTS_SKIPPED++))
}

# Write header to log
{
    echo "=============================================="
    echo "FBF.SVG Player - Linux CI/CD Smoke Tests"
    echo "=============================================="
    echo "Timestamp: $(date -Iseconds)"
    echo "Project Root: $PROJECT_ROOT"
    echo "Hostname: $(hostname)"
    echo "Architecture: $(uname -m)"
    echo "Kernel: $(uname -r)"
    echo "=============================================="
    echo ""
} >> "$LOG_FILE"

log_info "Starting Linux smoke tests..."
log_info "Log file: $LOG_FILE"
echo ""

# Detect architecture for binary path
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    ARCH_SUFFIX="x64"
elif [ "$ARCH" = "aarch64" ]; then
    ARCH_SUFFIX="arm64"
else
    log_warn "Unknown architecture: $ARCH, trying x64"
    ARCH_SUFFIX="x64"
fi

# Find the binary (check multiple possible locations)
BINARY=""
POSSIBLE_PATHS=(
    "${PROJECT_ROOT}/build/fbfsvg-player-linux-${ARCH_SUFFIX}"
    "${PROJECT_ROOT}/build/linux/fbfsvg-player"
    "${PROJECT_ROOT}/build/fbfsvg-player"
    "${PROJECT_ROOT}/build/svg_player_animated_linux"
)

for path in "${POSSIBLE_PATHS[@]}"; do
    if [ -f "$path" ]; then
        BINARY="$path"
        break
    fi
done

# ============================================
# TEST 1: Binary existence and executability
# ============================================
echo "--- Test 1: Binary Existence ---" >> "$LOG_FILE"

if [ -z "$BINARY" ]; then
    log_fail "Test 1: Binary not found in any expected location"
    echo "Searched paths:" >> "$LOG_FILE"
    for path in "${POSSIBLE_PATHS[@]}"; do
        echo "  - $path" >> "$LOG_FILE"
    done
    echo ""
    log_error "Build the Linux player first with: ./scripts/build-linux.sh"

    # Write final summary and exit
    {
        echo ""
        echo "=============================================="
        echo "TEST SUMMARY"
        echo "=============================================="
        echo "Passed:  $TESTS_PASSED"
        echo "Failed:  $TESTS_FAILED"
        echo "Skipped: $TESTS_SKIPPED"
        echo "Status:  FAILED"
        echo "=============================================="
    } >> "$LOG_FILE"

    exit 1
fi

log_info "Found binary at: $BINARY"

if [ -x "$BINARY" ]; then
    log_pass "Test 1: Binary exists and is executable"
else
    log_fail "Test 1: Binary exists but is not executable"
    chmod +x "$BINARY" 2>/dev/null && log_info "Fixed: Made binary executable" || true
fi

# ============================================
# TEST 2: Version smoke test (--version)
# ============================================
echo "" >> "$LOG_FILE"
echo "--- Test 2: Version Check ---" >> "$LOG_FILE"

log_info "Running: $BINARY --version"

if VERSION_OUTPUT=$("$BINARY" --version 2>&1); then
    log_pass "Test 2: --version returned successfully"
    echo "Output: $VERSION_OUTPUT" >> "$LOG_FILE"
    log_info "Version output: $VERSION_OUTPUT"
else
    EXIT_CODE=$?
    # Some programs return non-zero for --version but still work
    if [ -n "$VERSION_OUTPUT" ]; then
        log_warn "Test 2: --version returned exit code $EXIT_CODE but produced output"
        echo "Output: $VERSION_OUTPUT" >> "$LOG_FILE"
        log_pass "Test 2: --version produced output (exit code $EXIT_CODE)"
    else
        log_fail "Test 2: --version failed with exit code $EXIT_CODE"
    fi
fi

# ============================================
# TEST 3: Headless render test
# ============================================
echo "" >> "$LOG_FILE"
echo "--- Test 3: Headless Render Test ---" >> "$LOG_FILE"

# Find a test SVG file
TEST_SVG=""
SAMPLE_SVGS=(
    "${PROJECT_ROOT}/svg_input_samples/test.svg"
    "${PROJECT_ROOT}/svg_input_samples/benchmark_500.svg"
    "${PROJECT_ROOT}/svg_input_samples/simple.svg"
)

for svg in "${SAMPLE_SVGS[@]}"; do
    if [ -f "$svg" ]; then
        TEST_SVG="$svg"
        break
    fi
done

# Also check for any .svg file in samples directory
if [ -z "$TEST_SVG" ]; then
    TEST_SVG=$(find "${PROJECT_ROOT}/svg_input_samples" -name "*.svg" -type f 2>/dev/null | head -1)
fi

if [ -z "$TEST_SVG" ]; then
    log_skip "Test 3: No test SVG file found in svg_input_samples/"
else
    log_info "Using test SVG: $TEST_SVG"
    log_info "Running: $BINARY --headless --frames 10 $TEST_SVG"

    # Run headless render test with timeout
    if timeout 30 "$BINARY" --headless --frames 10 "$TEST_SVG" >> "$LOG_FILE" 2>&1; then
        log_pass "Test 3: Headless render completed successfully"
    else
        EXIT_CODE=$?
        if [ $EXIT_CODE -eq 124 ]; then
            log_fail "Test 3: Headless render timed out after 30 seconds"
        else
            log_fail "Test 3: Headless render failed with exit code $EXIT_CODE"
        fi
    fi
fi

# ============================================
# TEST SUMMARY
# ============================================
echo ""
echo "=============================================="
{
    echo ""
    echo "=============================================="
    echo "TEST SUMMARY"
    echo "=============================================="
    echo "Passed:  $TESTS_PASSED"
    echo "Failed:  $TESTS_FAILED"
    echo "Skipped: $TESTS_SKIPPED"
} >> "$LOG_FILE"

TOTAL=$((TESTS_PASSED + TESTS_FAILED))

if [ $TESTS_FAILED -eq 0 ]; then
    log_info "All tests passed ($TESTS_PASSED/$TOTAL)"
    echo "Status:  PASSED" >> "$LOG_FILE"
    echo "=============================================="
    log_info "Results saved to: $LOG_FILE"
    exit 0
else
    log_error "Some tests failed ($TESTS_FAILED/$TOTAL failed)"
    echo "Status:  FAILED" >> "$LOG_FILE"
    echo "=============================================="
    log_info "Results saved to: $LOG_FILE"
    exit 1
fi
