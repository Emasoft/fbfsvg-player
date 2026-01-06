#!/bin/bash

# run-tests-linux.sh - Linux Test Runner for SVG Player
#
# Runs the automated test suite in a Linux environment (native or Docker).
# Outputs JSON report for cross-platform aggregation.
#
# Usage:
#   ./scripts/run-tests-linux.sh [OPTIONS]
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
elif [ "$ARCH" = "aarch64" ]; then
    ARCH_NAME="arm64"
else
    log_error "Unsupported architecture: $ARCH"
    exit 1
fi

PLATFORM="linux-${ARCH_NAME}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="${OUTPUT_DIR}/${PLATFORM}_${TIMESTAMP}.json"

log_step "Running Linux tests on ${PLATFORM}..."

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if test binary exists
TEST_BINARY="${PROJECT_ROOT}/build/run_tests_linux_${ARCH_NAME}"
if [ ! -f "$TEST_BINARY" ]; then
    # Try to build the test binary
    log_info "Test binary not found, building..."
    cd "$PROJECT_ROOT"

    # Check for Skia
    SKIA_DIR="${PROJECT_ROOT}/skia-build/src/skia"
    SKIA_OUT="${SKIA_DIR}/out/release-linux"

    if [ ! -f "${SKIA_OUT}/libskia.a" ]; then
        log_error "Skia libraries not found at ${SKIA_OUT}"
        log_info "Run './skia-build/build-linux.sh' to build Skia first"

        # Create a minimal test result indicating build failure
        cat > "$RESULT_FILE" << EOF
{
    "platform": "${PLATFORM}",
    "timestamp": "$(date -Iseconds)",
    "status": "build_failed",
    "error": "Skia libraries not found",
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

    # Compiler settings
    CXX="clang++"
    if ! command -v clang++ >/dev/null 2>&1; then
        CXX="g++"
    fi

    # Build test binary
    log_info "Compiling test suite..."

    TEST_SRC="${PROJECT_ROOT}/tests/test_folder_browser_automated.cpp"
    TEST_CXXFLAGS="-std=c++17 -DSVG_INSTRUMENTATION_ENABLED=1 -g -O1"

    # Get SDL2 flags
    SDL2_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || echo "")
    SDL2_LIBS=$(pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")

    # Get ICU flags
    ICU_CFLAGS=$(pkg-config --cflags icu-uc icu-i18n 2>/dev/null || echo "")
    ICU_LIBS=$(pkg-config --libs icu-uc icu-i18n 2>/dev/null || echo "-licuuc -licui18n -licudata")

    TEST_INCLUDES="-I${PROJECT_ROOT} -I${PROJECT_ROOT}/shared -I${PROJECT_ROOT}/tests -I${PROJECT_ROOT}/src -I${SKIA_DIR} ${SDL2_CFLAGS} ${ICU_CFLAGS}"

    # Skia libraries (order matters)
    SKIA_LIBS="${SKIA_OUT}/libsvg.a \
        ${SKIA_OUT}/libskresources.a \
        ${SKIA_OUT}/libskshaper.a \
        ${SKIA_OUT}/libskunicode_core.a \
        ${SKIA_OUT}/libskunicode_icu.a \
        ${SKIA_OUT}/libharfbuzz.a \
        ${SKIA_OUT}/libskparagraph.a \
        ${SKIA_OUT}/libskia.a \
        ${SKIA_OUT}/libexpat.a \
        ${SKIA_OUT}/libpng.a \
        ${SKIA_OUT}/libjpeg.a \
        ${SKIA_OUT}/libwebp.a \
        ${SKIA_OUT}/libzlib.a \
        ${SKIA_OUT}/libwuffs.a"

    # Linux system libraries
    LINUX_LIBS="-lGL -lEGL -lX11 -lXext -lpthread -ldl -lm -lfontconfig -lfreetype"

    mkdir -p "${PROJECT_ROOT}/build"

    $CXX $TEST_CXXFLAGS $TEST_INCLUDES \
        "$TEST_SRC" \
        -o "$TEST_BINARY" \
        $SKIA_LIBS \
        $ICU_LIBS \
        $SDL2_LIBS \
        $LINUX_LIBS \
        -lstdc++ || {
            cat > "$RESULT_FILE" << EOF
{
    "platform": "${PLATFORM}",
    "timestamp": "$(date -Iseconds)",
    "status": "build_failed",
    "error": "Test compilation failed",
    "tests": [],
    "summary": {
        "total": 0,
        "passed": 0,
        "failed": 0,
        "skipped": 0
    }
}
EOF
            log_error "Test compilation failed. Results saved to: $RESULT_FILE"
            exit 1
        }

    log_info "Test binary built: $TEST_BINARY"
fi

# Run the tests and capture output
log_step "Executing tests..."

# Create temp file for raw output
RAW_OUTPUT=$(mktemp)
trap "rm -f $RAW_OUTPUT" EXIT

# Run tests with timeout (5 minutes max)
START_TIME=$(date +%s.%N)

if timeout 300 "$TEST_BINARY" --json > "$RAW_OUTPUT" 2>&1; then
    TEST_EXIT_CODE=0
else
    TEST_EXIT_CODE=$?
fi

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc)

# Process results
if [ -s "$RAW_OUTPUT" ] && head -1 "$RAW_OUTPUT" | grep -q '^{'; then
    # Output is valid JSON, use it directly but add platform info
    # Use jq if available, otherwise use simple sed
    if command -v jq >/dev/null 2>&1; then
        jq --arg platform "$PLATFORM" \
           --arg timestamp "$(date -Iseconds)" \
           --arg duration "$DURATION" \
           '. + {platform: $platform, timestamp: $timestamp, duration_seconds: ($duration | tonumber)}' \
           "$RAW_OUTPUT" > "$RESULT_FILE"
    else
        # Fallback: wrap the output
        {
            echo "{"
            echo "  \"platform\": \"${PLATFORM}\","
            echo "  \"timestamp\": \"$(date -Iseconds)\","
            echo "  \"duration_seconds\": ${DURATION},"
            echo "  \"raw_output\": true,"
            cat "$RAW_OUTPUT"
            echo "}"
        } > "$RESULT_FILE"
    fi
else
    # Parse text output to JSON format
    PASSED=$(grep -c "PASS" "$RAW_OUTPUT" 2>/dev/null || echo "0")
    FAILED=$(grep -c "FAIL" "$RAW_OUTPUT" 2>/dev/null || echo "0")
    TOTAL=$((PASSED + FAILED))

    # Escape output for JSON
    ESCAPED_OUTPUT=$(cat "$RAW_OUTPUT" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g' | tr '\n' ' ')

    cat > "$RESULT_FILE" << EOF
{
    "platform": "${PLATFORM}",
    "timestamp": "$(date -Iseconds)",
    "status": "$([ $TEST_EXIT_CODE -eq 0 ] && echo "passed" || echo "failed")",
    "exit_code": ${TEST_EXIT_CODE},
    "duration_seconds": ${DURATION},
    "summary": {
        "total": ${TOTAL},
        "passed": ${PASSED},
        "failed": ${FAILED},
        "skipped": 0
    },
    "raw_output": "${ESCAPED_OUTPUT}"
}
EOF
fi

# Display summary
if [ "$VERBOSE" = true ]; then
    cat "$RAW_OUTPUT"
fi

if [ $TEST_EXIT_CODE -eq 0 ]; then
    log_info "All tests passed on ${PLATFORM}"
else
    log_error "Some tests failed on ${PLATFORM} (exit code: $TEST_EXIT_CODE)"
fi

log_info "Results saved to: $RESULT_FILE"

exit $TEST_EXIT_CODE
