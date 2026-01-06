#!/bin/bash

# test_all.sh - Unified Cross-Platform Parallel Test Runner
#
# Runs tests on all available platforms in parallel:
# - macOS (native, if on macOS host)
# - Linux ARM64 (Docker container)
# - Linux x64 (Docker container, QEMU emulated on Apple Silicon)
# - iOS (stub - simulation only)
# - Windows (stub - not implemented)
#
# Usage:
#   ./scripts/test_all.sh [OPTIONS]
#
# Options:
#   --macos-only        Only run macOS tests
#   --linux-only        Only run Linux tests (both ARM64 and x64)
#   --arm64-only        Only run ARM64 tests (macOS native + Linux ARM64)
#   --x64-only          Only run x64 tests (Linux x64)
#   --no-parallel       Run tests sequentially instead of parallel
#   --skip-build        Skip building test binaries
#   --output-dir DIR    Output directory for results (default: build/test-results)
#   --timeout SECONDS   Per-container timeout (default: 600)
#   -v, --verbose       Verbose output
#   -h, --help          Show this help
#
# Exit codes:
#   0 - All tests passed
#   1 - Some tests failed
#   2 - Build or infrastructure error

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DOCKER_DIR="${PROJECT_ROOT}/docker"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }
log_platform() { echo -e "${MAGENTA}[$1]${NC} $2"; }

# Default options
OUTPUT_DIR="${PROJECT_ROOT}/build/test-results"
PARALLEL=true
SKIP_BUILD=false
VERBOSE=false
TIMEOUT=600
RUN_MACOS=true
RUN_LINUX_ARM64=true
RUN_LINUX_X64=true
RUN_IOS=false      # Stub only
RUN_WINDOWS=false  # Stub only

# Detect host platform
HOST_OS=$(uname -s)
HOST_ARCH=$(uname -m)

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --macos-only)
            RUN_LINUX_ARM64=false
            RUN_LINUX_X64=false
            shift
            ;;
        --linux-only)
            RUN_MACOS=false
            shift
            ;;
        --arm64-only)
            RUN_LINUX_X64=false
            if [ "$HOST_OS" != "Darwin" ] || [ "$HOST_ARCH" != "arm64" ]; then
                RUN_MACOS=false
            fi
            shift
            ;;
        --x64-only)
            RUN_MACOS=false
            RUN_LINUX_ARM64=false
            shift
            ;;
        --no-parallel)
            PARALLEL=false
            shift
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Cross-platform parallel test runner for SVG Player"
            echo ""
            echo "Options:"
            echo "  --macos-only        Only run macOS tests"
            echo "  --linux-only        Only run Linux tests (ARM64 + x64)"
            echo "  --arm64-only        Only run ARM64 tests"
            echo "  --x64-only          Only run x64 tests"
            echo "  --no-parallel       Run tests sequentially"
            echo "  --skip-build        Skip building test binaries"
            echo "  --output-dir DIR    Output directory (default: build/test-results)"
            echo "  --timeout SECONDS   Per-test timeout (default: 600)"
            echo "  -v, --verbose       Verbose output"
            echo "  -h, --help          Show this help"
            echo ""
            echo "Platforms:"
            echo "  macOS:        Native execution (if on macOS host)"
            echo "  Linux ARM64:  Docker container (native on Apple Silicon)"
            echo "  Linux x64:    Docker container (QEMU emulated on Apple Silicon)"
            echo "  iOS:          Stub only (XCTest planned)"
            echo "  Windows:      Stub only (not implemented)"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 2
            ;;
    esac
done

# Adjust based on host platform
if [ "$HOST_OS" != "Darwin" ]; then
    RUN_MACOS=false
    log_info "Not on macOS, skipping macOS tests"
fi

# Check for Docker if running Linux tests
if [ "$RUN_LINUX_ARM64" = true ] || [ "$RUN_LINUX_X64" = true ]; then
    if ! command -v docker >/dev/null 2>&1; then
        log_warn "Docker not found, skipping Linux tests"
        RUN_LINUX_ARM64=false
        RUN_LINUX_X64=false
    elif ! docker info >/dev/null 2>&1; then
        log_warn "Docker daemon not running, skipping Linux tests"
        RUN_LINUX_ARM64=false
        RUN_LINUX_X64=false
    fi
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
AGGREGATE_RESULT="${OUTPUT_DIR}/aggregate_${TIMESTAMP}.json"

echo ""
echo -e "${BOLD}============================================${NC}"
echo -e "${BOLD}  SVG Player Cross-Platform Test Runner${NC}"
echo -e "${BOLD}============================================${NC}"
echo ""
log_info "Host: ${HOST_OS} ${HOST_ARCH}"
log_info "Output: ${OUTPUT_DIR}"
log_info "Parallel: ${PARALLEL}"
echo ""

# Track PIDs for parallel execution
declare -A PIDS
declare -A RESULTS
PLATFORMS_RUN=0
PLATFORMS_PASSED=0
PLATFORMS_FAILED=0

# Function to run macOS tests
run_macos_tests() {
    log_platform "macOS" "Starting tests..."

    local result_file="${OUTPUT_DIR}/macos-${HOST_ARCH}_${TIMESTAMP}.json"
    local log_file="${OUTPUT_DIR}/macos-${HOST_ARCH}_${TIMESTAMP}.log"

    if [ "$SKIP_BUILD" = false ]; then
        log_platform "macOS" "Building test binary..."
        cd "$PROJECT_ROOT"
        make test-build > "$log_file" 2>&1 || {
            log_platform "macOS" "Build failed"
            echo '{"platform":"macos-'${HOST_ARCH}'","status":"build_failed","summary":{"total":0,"passed":0,"failed":0}}' > "$result_file"
            return 1
        }
    fi

    log_platform "macOS" "Running tests..."
    "${SCRIPT_DIR}/run-tests-macos.sh" --output-dir "$OUTPUT_DIR" >> "$log_file" 2>&1
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_platform "macOS" "Tests PASSED"
    else
        log_platform "macOS" "Tests FAILED (exit: $exit_code)"
    fi

    return $exit_code
}

# Function to run Linux ARM64 tests in Docker
run_linux_arm64_tests() {
    log_platform "Linux-ARM64" "Starting Docker container..."

    local result_file="${OUTPUT_DIR}/linux-arm64_${TIMESTAMP}.json"
    local log_file="${OUTPUT_DIR}/linux-arm64_${TIMESTAMP}.log"

    cd "$DOCKER_DIR"

    # Ensure container is running
    docker-compose up -d dev-arm64 >> "$log_file" 2>&1 || {
        log_platform "Linux-ARM64" "Failed to start container"
        echo '{"platform":"linux-arm64","status":"container_failed","summary":{"total":0,"passed":0,"failed":0}}' > "$result_file"
        return 1
    }

    # Wait for container to be ready
    sleep 2

    log_platform "Linux-ARM64" "Running tests in container..."

    # Run tests inside container
    docker-compose exec -T dev-arm64 bash -c "
        cd /workspace
        export OUTPUT_DIR=/workspace/build/test-results
        mkdir -p \$OUTPUT_DIR
        ./scripts/run-tests-linux.sh --output-dir \$OUTPUT_DIR
    " >> "$log_file" 2>&1
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_platform "Linux-ARM64" "Tests PASSED"
    else
        log_platform "Linux-ARM64" "Tests FAILED (exit: $exit_code)"
    fi

    return $exit_code
}

# Function to run Linux x64 tests in Docker (QEMU emulation)
run_linux_x64_tests() {
    log_platform "Linux-x64" "Starting Docker container (QEMU)..."

    local result_file="${OUTPUT_DIR}/linux-x64_${TIMESTAMP}.json"
    local log_file="${OUTPUT_DIR}/linux-x64_${TIMESTAMP}.log"

    cd "$DOCKER_DIR"

    # Ensure container is running (this may take a while with QEMU)
    docker-compose up -d dev-x64 >> "$log_file" 2>&1 || {
        log_platform "Linux-x64" "Failed to start container"
        echo '{"platform":"linux-x64","status":"container_failed","summary":{"total":0,"passed":0,"failed":0}}' > "$result_file"
        return 1
    }

    # Wait for container to be ready (QEMU takes longer)
    sleep 5

    log_platform "Linux-x64" "Running tests in container (slower due to QEMU)..."

    # Run tests inside container
    docker-compose exec -T dev-x64 bash -c "
        cd /workspace
        export OUTPUT_DIR=/workspace/build/test-results
        mkdir -p \$OUTPUT_DIR
        ./scripts/run-tests-linux.sh --output-dir \$OUTPUT_DIR
    " >> "$log_file" 2>&1
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_platform "Linux-x64" "Tests PASSED"
    else
        log_platform "Linux-x64" "Tests FAILED (exit: $exit_code)"
    fi

    return $exit_code
}

# Function to create iOS stub result
run_ios_tests_stub() {
    log_platform "iOS" "Creating stub result (XCTest not implemented)..."

    local result_file="${OUTPUT_DIR}/ios-arm64_${TIMESTAMP}.json"

    cat > "$result_file" << EOF
{
    "platform": "ios-arm64",
    "timestamp": "$(date -Iseconds 2>/dev/null || date +%Y-%m-%dT%H:%M:%S)",
    "status": "stub",
    "message": "iOS XCTest integration pending. Run tests via Xcode.",
    "summary": {
        "total": 0,
        "passed": 0,
        "failed": 0,
        "skipped": 0
    }
}
EOF

    log_platform "iOS" "Stub created (not a real test)"
    return 0
}

# Function to create Windows stub result
run_windows_tests_stub() {
    log_platform "Windows" "Creating stub result (not implemented)..."

    local result_file="${OUTPUT_DIR}/windows-x64_${TIMESTAMP}.json"

    cat > "$result_file" << EOF
{
    "platform": "windows-x64",
    "timestamp": "$(date -Iseconds 2>/dev/null || date +%Y-%m-%dT%H:%M:%S)",
    "status": "stub",
    "message": "Windows SDK not yet implemented.",
    "summary": {
        "total": 0,
        "passed": 0,
        "failed": 0,
        "skipped": 0
    }
}
EOF

    log_platform "Windows" "Stub created (not a real test)"
    return 0
}

# Run tests based on configuration
log_step "Starting test execution..."
echo ""

if [ "$PARALLEL" = true ]; then
    # Run tests in parallel using background processes

    if [ "$RUN_MACOS" = true ]; then
        run_macos_tests &
        PIDS["macos"]=$!
        PLATFORMS_RUN=$((PLATFORMS_RUN + 1))
    fi

    if [ "$RUN_LINUX_ARM64" = true ]; then
        run_linux_arm64_tests &
        PIDS["linux-arm64"]=$!
        PLATFORMS_RUN=$((PLATFORMS_RUN + 1))
    fi

    if [ "$RUN_LINUX_X64" = true ]; then
        run_linux_x64_tests &
        PIDS["linux-x64"]=$!
        PLATFORMS_RUN=$((PLATFORMS_RUN + 1))
    fi

    if [ "$RUN_IOS" = true ]; then
        run_ios_tests_stub &
        PIDS["ios"]=$!
    fi

    if [ "$RUN_WINDOWS" = true ]; then
        run_windows_tests_stub &
        PIDS["windows"]=$!
    fi

    # Wait for all background processes and collect results
    log_info "Waiting for parallel tests to complete..."
    echo ""

    for platform in "${!PIDS[@]}"; do
        pid=${PIDS[$platform]}
        if wait $pid; then
            RESULTS[$platform]="passed"
            PLATFORMS_PASSED=$((PLATFORMS_PASSED + 1))
        else
            RESULTS[$platform]="failed"
            PLATFORMS_FAILED=$((PLATFORMS_FAILED + 1))
        fi
    done

else
    # Run tests sequentially

    if [ "$RUN_MACOS" = true ]; then
        PLATFORMS_RUN=$((PLATFORMS_RUN + 1))
        if run_macos_tests; then
            RESULTS["macos"]="passed"
            PLATFORMS_PASSED=$((PLATFORMS_PASSED + 1))
        else
            RESULTS["macos"]="failed"
            PLATFORMS_FAILED=$((PLATFORMS_FAILED + 1))
        fi
    fi

    if [ "$RUN_LINUX_ARM64" = true ]; then
        PLATFORMS_RUN=$((PLATFORMS_RUN + 1))
        if run_linux_arm64_tests; then
            RESULTS["linux-arm64"]="passed"
            PLATFORMS_PASSED=$((PLATFORMS_PASSED + 1))
        else
            RESULTS["linux-arm64"]="failed"
            PLATFORMS_FAILED=$((PLATFORMS_FAILED + 1))
        fi
    fi

    if [ "$RUN_LINUX_X64" = true ]; then
        PLATFORMS_RUN=$((PLATFORMS_RUN + 1))
        if run_linux_x64_tests; then
            RESULTS["linux-x64"]="passed"
            PLATFORMS_PASSED=$((PLATFORMS_PASSED + 1))
        else
            RESULTS["linux-x64"]="failed"
            PLATFORMS_FAILED=$((PLATFORMS_FAILED + 1))
        fi
    fi

    if [ "$RUN_IOS" = true ]; then
        run_ios_tests_stub
    fi

    if [ "$RUN_WINDOWS" = true ]; then
        run_windows_tests_stub
    fi
fi

# Generate aggregate result
echo ""
log_step "Generating aggregate results..."

# Find all result files
RESULT_FILES=$(find "$OUTPUT_DIR" -name "*_${TIMESTAMP}.json" -type f 2>/dev/null | sort)

# Create aggregate JSON
{
    echo "{"
    echo "  \"timestamp\": \"$(date -Iseconds 2>/dev/null || date +%Y-%m-%dT%H:%M:%S)\","
    echo "  \"host\": \"${HOST_OS} ${HOST_ARCH}\","
    echo "  \"parallel\": ${PARALLEL},"
    echo "  \"summary\": {"
    echo "    \"platforms_run\": ${PLATFORMS_RUN},"
    echo "    \"platforms_passed\": ${PLATFORMS_PASSED},"
    echo "    \"platforms_failed\": ${PLATFORMS_FAILED}"
    echo "  },"
    echo "  \"platforms\": {"

    first=true
    for result_file in $RESULT_FILES; do
        platform=$(basename "$result_file" | sed 's/_[0-9]*\.json$//')
        if [ "$first" = true ]; then
            first=false
        else
            echo ","
        fi
        echo -n "    \"${platform}\": "
        cat "$result_file"
    done
    echo ""
    echo "  }"
    echo "}"
} > "$AGGREGATE_RESULT"

# Print summary
echo ""
echo -e "${BOLD}============================================${NC}"
echo -e "${BOLD}           TEST RESULTS SUMMARY${NC}"
echo -e "${BOLD}============================================${NC}"
echo ""

# Table header
printf "${BOLD}%-20s %-15s %-10s${NC}\n" "Platform" "Status" "Tests"
echo "--------------------------------------------"

for platform in "${!RESULTS[@]}"; do
    status=${RESULTS[$platform]}
    if [ "$status" = "passed" ]; then
        status_color="${GREEN}PASSED${NC}"
    else
        status_color="${RED}FAILED${NC}"
    fi
    printf "%-20s ${status_color}%-15s${NC}\n" "$platform" ""
done

echo "--------------------------------------------"
echo ""
log_info "Platforms tested: ${PLATFORMS_RUN}"
log_info "Passed: ${PLATFORMS_PASSED}"
if [ $PLATFORMS_FAILED -gt 0 ]; then
    log_error "Failed: ${PLATFORMS_FAILED}"
else
    log_info "Failed: 0"
fi
echo ""
log_info "Aggregate results: ${AGGREGATE_RESULT}"
log_info "Individual results: ${OUTPUT_DIR}/"
echo ""

# Exit with appropriate code
if [ $PLATFORMS_FAILED -gt 0 ]; then
    exit 1
else
    exit 0
fi
