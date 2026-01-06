#!/bin/bash

# run-tests-macos.sh - macOS Test Runner for SVG Player
#
# Runs the automated test suite on macOS.
# Outputs JSON report for cross-platform aggregation.
#
# Usage:
#   ./scripts/run-tests-macos.sh [OPTIONS]
#
# Options:
#   --output-dir DIR    Output directory for test results (default: build/test-results)
#   --json              Output JSON format (default)
#   --verbose           Verbose output
#   -h, --help          Show this help

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# Default options
OUTPUT_DIR="${PROJECT_ROOT}/build/test-results"
OUTPUT_FORMAT="json"
VERBOSE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --json)
            OUTPUT_FORMAT="json"
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --output-dir DIR    Output directory for test results"
            echo "  --json              Output JSON format (default)"
            echo "  --verbose           Verbose output"
            echo "  -h, --help          Show this help"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    ARCH_NAME="x64"
elif [ "$ARCH" = "arm64" ]; then
    ARCH_NAME="arm64"
else
    log_error "Unsupported architecture: $ARCH"
    exit 1
fi

PLATFORM="macos-${ARCH_NAME}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="${OUTPUT_DIR}/${PLATFORM}_${TIMESTAMP}.json"

log_step "Running macOS tests on ${PLATFORM}..."

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if test binary exists (use the Makefile target)
TEST_BINARY="${PROJECT_ROOT}/build/run_tests"

if [ ! -f "$TEST_BINARY" ]; then
    log_info "Test binary not found, building with make..."
    cd "$PROJECT_ROOT"

    # Build using make
    if make test-build 2>&1; then
        log_info "Test binary built successfully"
    else
        cat > "$RESULT_FILE" << EOF
{
    "platform": "${PLATFORM}",
    "timestamp": "$(date -Iseconds)",
    "status": "build_failed",
    "error": "Test compilation failed via make test-build",
    "tests": [],
    "summary": {
        "total": 0,
        "passed": 0,
        "failed": 0,
        "skipped": 0
    }
}
EOF
        log_error "Test build failed. Results saved to: $RESULT_FILE"
        exit 1
    fi
fi

# Run the tests and capture output
log_step "Executing tests..."

# Create temp file for raw output
RAW_OUTPUT=$(mktemp)
trap "rm -f $RAW_OUTPUT" EXIT

# Run tests with timeout (5 minutes max)
START_TIME=$(date +%s.%N 2>/dev/null || gdate +%s.%N 2>/dev/null || date +%s)

if gtimeout 300 "$TEST_BINARY" > "$RAW_OUTPUT" 2>&1 || timeout 300 "$TEST_BINARY" > "$RAW_OUTPUT" 2>&1 || "$TEST_BINARY" > "$RAW_OUTPUT" 2>&1; then
    TEST_EXIT_CODE=$?
else
    TEST_EXIT_CODE=$?
fi

END_TIME=$(date +%s.%N 2>/dev/null || gdate +%s.%N 2>/dev/null || date +%s)

# Calculate duration (handle cases where bc might not give decimals)
if command -v bc >/dev/null 2>&1; then
    DURATION=$(echo "$END_TIME - $START_TIME" | bc 2>/dev/null || echo "0")
else
    DURATION=$((${END_TIME%.*} - ${START_TIME%.*}))
fi

# Parse test output to extract counts
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

# Look for "Total: N" and "Passed: N" format from our test harness
if grep -q "^Total:" "$RAW_OUTPUT" 2>/dev/null; then
    TOTAL=$(grep "^Total:" "$RAW_OUTPUT" | grep -o "[0-9]*" | head -1)
    PASSED=$(grep "^Passed:" "$RAW_OUTPUT" | grep -o "[0-9]*" | head -1)
    FAILED=$(grep "^Failed:" "$RAW_OUTPUT" | grep -o "[0-9]*" | head -1)
    SKIPPED=0
else
    # Fallback: count PASS/FAIL lines
    PASSED=$(grep -c "\[PASS\]" "$RAW_OUTPUT" 2>/dev/null || echo "0")
    FAILED=$(grep -c "\[FAIL\]" "$RAW_OUTPUT" 2>/dev/null || echo "0")
    SKIPPED=$(grep -c "\[SKIP\]" "$RAW_OUTPUT" 2>/dev/null || echo "0")
    TOTAL=$((PASSED + FAILED + SKIPPED))
fi

# Ensure values are integers
TOTAL=${TOTAL:-0}
PASSED=${PASSED:-0}
FAILED=${FAILED:-0}
SKIPPED=${SKIPPED:-0}

# Create JSON result file
# Escape special characters in output for JSON
OUTPUT_LINES=""
while IFS= read -r line; do
    # Escape backslashes, quotes, and control characters
    escaped=$(echo "$line" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g' | tr -d '\r')
    if [ -n "$OUTPUT_LINES" ]; then
        OUTPUT_LINES="${OUTPUT_LINES}, \"${escaped}\""
    else
        OUTPUT_LINES="\"${escaped}\""
    fi
done < "$RAW_OUTPUT"

cat > "$RESULT_FILE" << EOF
{
    "platform": "${PLATFORM}",
    "timestamp": "$(date -Iseconds 2>/dev/null || date +%Y-%m-%dT%H:%M:%S)",
    "status": "$([ $TEST_EXIT_CODE -eq 0 ] && echo "passed" || echo "failed")",
    "exit_code": ${TEST_EXIT_CODE},
    "duration_seconds": ${DURATION:-0},
    "summary": {
        "total": ${TOTAL},
        "passed": ${PASSED},
        "failed": ${FAILED},
        "skipped": ${SKIPPED}
    },
    "output_lines": [${OUTPUT_LINES}]
}
EOF

# Display summary
if [ "$VERBOSE" = true ]; then
    cat "$RAW_OUTPUT"
fi

echo ""
log_info "=========================================="
log_info "Test Results for ${PLATFORM}"
log_info "=========================================="
log_info "Total:   ${TOTAL}"
log_info "Passed:  ${PASSED}"
log_info "Failed:  ${FAILED}"
log_info "Skipped: ${SKIPPED}"
log_info "Duration: ${DURATION}s"
log_info "=========================================="

if [ $TEST_EXIT_CODE -eq 0 ]; then
    log_info "All tests passed on ${PLATFORM}"
else
    log_error "Some tests failed on ${PLATFORM} (exit code: $TEST_EXIT_CODE)"
fi

log_info "Results saved to: $RESULT_FILE"

exit $TEST_EXIT_CODE
