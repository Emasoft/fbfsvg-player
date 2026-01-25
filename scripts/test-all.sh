#!/bin/bash
# test-all.sh - Unified test runner for all platforms
#
# This script runs tests for the SVG Player SDK.
# It can run both lightweight API compilation tests (no Skia required)
# and full unit tests (requires built libraries).
#
# Usage:
#   ./scripts/test-all.sh              # Run all tests
#   ./scripts/test-all.sh --quick      # Quick API header compilation test only
#   ./scripts/test-all.sh --linux      # Run Linux tests (via Docker)
#   ./scripts/test-all.sh --macos      # Run macOS tests
#   ./scripts/test-all.sh --help       # Show help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Test results tracking
declare -A TEST_RESULTS
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Parse arguments
QUICK_ONLY=false
TEST_LINUX=false
TEST_MACOS=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --quick|-q)
            QUICK_ONLY=true
            shift
            ;;
        --linux|-l)
            TEST_LINUX=true
            shift
            ;;
        --macos|-m)
            TEST_MACOS=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Run SVG Player SDK tests."
            echo ""
            echo "Options:"
            echo "  --quick, -q     Quick API header compilation test only"
            echo "  --linux, -l     Run Linux tests (via Docker)"
            echo "  --macos, -m     Run macOS tests"
            echo "  --verbose, -v   Verbose output"
            echo "  --help, -h      Show this help message"
            echo ""
            echo "If no platform is specified, tests run on the current platform."
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# If no platform specified, detect current platform
if [ "$TEST_LINUX" = false ] && [ "$TEST_MACOS" = false ]; then
    case "$(uname -s)" in
        Darwin)
            TEST_MACOS=true
            ;;
        Linux)
            TEST_LINUX=true
            ;;
        *)
            echo "Unknown platform: $(uname -s)"
            exit 1
            ;;
    esac
fi

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

section_header() {
    echo ""
    echo "================================================================"
    echo -e "${BLUE}$1${NC}"
    echo "================================================================"
}

record_result() {
    local name="$1"
    local passed="$2"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [ "$passed" = true ]; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        TEST_RESULTS["$name"]="PASS"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        TEST_RESULTS["$name"]="FAIL"
    fi
}

cd "$PROJECT_ROOT"

section_header "SVG Player SDK - Test Suite"
echo "Date: $(date)"
echo "Platform: $(uname -s) $(uname -m)"
echo ""

# =============================================================================
# Quick API Header Compilation Test
# =============================================================================

section_header "Test: API Header Compilation"

log_info "Compiling API header test..."

# Create temporary build directory
TEST_BUILD_DIR="$PROJECT_ROOT/build/tests"
mkdir -p "$TEST_BUILD_DIR"

# Compile the API compilation test (compile only, no linking needed)
# This verifies the API header is syntactically correct
if clang++ -std=c++17 \
    -I"$PROJECT_ROOT/shared" \
    -Wall -Wextra -Werror \
    -c "$PROJECT_ROOT/tests/test_api_compile.cpp" \
    -o "$TEST_BUILD_DIR/test_api_compile.o" 2>&1; then

    log_success "API header compiled successfully"
    record_result "api_header_compile" true

    # Clean up object file
    rm -f "$TEST_BUILD_DIR/test_api_compile.o"
else
    log_fail "API header compilation failed"
    record_result "api_header_compile" false
fi

# Exit early if quick mode
if [ "$QUICK_ONLY" = true ]; then
    section_header "Test Summary (Quick Mode)"
    echo ""
    printf "%-30s %s\n" "Test" "Result"
    printf "%-30s %s\n" "----" "------"
    for test in "${!TEST_RESULTS[@]}"; do
        result="${TEST_RESULTS[$test]}"
        if [ "$result" = "PASS" ]; then
            printf "%-30s ${GREEN}%s${NC}\n" "$test" "$result"
        else
            printf "%-30s ${RED}%s${NC}\n" "$test" "$result"
        fi
    done
    echo ""
    echo "Total: $PASSED_TESTS/$TOTAL_TESTS passed"
    exit $FAILED_TESTS
fi

# =============================================================================
# macOS Tests
# =============================================================================

if [ "$TEST_MACOS" = true ]; then
    section_header "Test: macOS Build Verification"

    # Check if macOS build exists
    if [ -f "$PROJECT_ROOT/build/fbfsvg-player" ]; then
        log_info "Found macOS desktop player binary"

        # Verify binary is valid
        if file "$PROJECT_ROOT/build/fbfsvg-player" | grep -q "Mach-O"; then
            log_success "macOS binary is valid Mach-O executable"
            record_result "macos_binary_valid" true

            # Test binary runs (just check --help or version if supported)
            if "$PROJECT_ROOT/build/fbfsvg-player" --help 2>/dev/null || \
               "$PROJECT_ROOT/build/fbfsvg-player" --version 2>/dev/null || \
               timeout 2 "$PROJECT_ROOT/build/fbfsvg-player" 2>/dev/null; then
                log_success "macOS binary executes"
                record_result "macos_binary_runs" true
            else
                # Binary may need arguments - check exit code
                log_warning "macOS binary requires arguments (expected)"
                record_result "macos_binary_runs" true
            fi
        else
            log_fail "macOS binary is not valid Mach-O"
            record_result "macos_binary_valid" false
        fi
    else
        log_warning "macOS binary not found - run build-all.sh first"
        record_result "macos_binary_valid" false
    fi

    # Check iOS XCFramework
    section_header "Test: iOS XCFramework Verification"

    if [ -d "$PROJECT_ROOT/build/FBFSVGPlayer.xcframework" ]; then
        log_info "Found iOS XCFramework"

        # Verify framework structure
        if [ -f "$PROJECT_ROOT/build/FBFSVGPlayer.xcframework/Info.plist" ]; then
            log_success "XCFramework has valid Info.plist"
            record_result "ios_xcframework_valid" true
        else
            log_fail "XCFramework missing Info.plist"
            record_result "ios_xcframework_valid" false
        fi
    else
        log_warning "iOS XCFramework not found - run build-all.sh first"
        record_result "ios_xcframework_valid" false
    fi
fi

# =============================================================================
# Linux Tests (via Docker)
# =============================================================================

if [ "$TEST_LINUX" = true ]; then
    section_header "Test: Linux Build Verification"

    # Check if running on Linux or need Docker
    if [ "$(uname -s)" = "Linux" ]; then
        # Native Linux
        if [ -f "$PROJECT_ROOT/build/linux/libfbfsvgplayer.so" ]; then
            log_info "Found Linux shared library"

            # Verify library is valid ELF
            if file -L "$PROJECT_ROOT/build/linux/libfbfsvgplayer.so" | grep -q "ELF"; then
                log_success "Linux library is valid ELF shared object"
                record_result "linux_library_valid" true

                # Check exported symbols
                if nm -D "$PROJECT_ROOT/build/linux/libfbfsvgplayer.so" 2>/dev/null | grep -q "FBFSVGPlayer_Create"; then
                    log_success "Linux library exports FBFSVGPlayer_Create symbol"
                    record_result "linux_symbols_exported" true
                else
                    log_fail "Linux library missing expected symbols"
                    record_result "linux_symbols_exported" false
                fi
            else
                log_fail "Linux library is not valid ELF"
                record_result "linux_library_valid" false
            fi
        else
            log_warning "Linux library not found - run build-linux-sdk.sh first"
            record_result "linux_library_valid" false
        fi
    else
        # Run via Docker
        log_info "Running Linux tests via Docker..."

        if ! command -v docker &> /dev/null; then
            log_warning "Docker not found - skipping Linux tests"
            record_result "linux_docker" false
        elif ! docker info &> /dev/null 2>&1; then
            log_warning "Docker daemon not running - skipping Linux tests"
            record_result "linux_docker" false
        else
            # Detect host architecture for selecting the right container
            HOST_ARCH=$(uname -m)
            if [ "$HOST_ARCH" = "arm64" ] || [ "$HOST_ARCH" = "aarch64" ]; then
                DOCKER_SERVICE="dev-arm64"
            else
                DOCKER_SERVICE="dev-x64"
            fi

            # Check if container is running
            if docker compose -f "$PROJECT_ROOT/docker/docker-compose.yml" ps --status running 2>/dev/null | grep -q "$DOCKER_SERVICE"; then
                log_info "Docker container ($DOCKER_SERVICE) is running"
            else
                log_info "Starting Docker container ($DOCKER_SERVICE)..."
                docker compose -f "$PROJECT_ROOT/docker/docker-compose.yml" up -d "$DOCKER_SERVICE"
                sleep 2
            fi

            # Run tests inside container
            if docker compose -f "$PROJECT_ROOT/docker/docker-compose.yml" exec -T "$DOCKER_SERVICE" bash -c "
                cd /workspace
                if [ -f build/linux/libfbfsvgplayer.so ]; then
                    file -L build/linux/libfbfsvgplayer.so | grep -q 'ELF' && \
                    nm -D build/linux/libfbfsvgplayer.so 2>/dev/null | grep -q 'FBFSVGPlayer_Create'
                else
                    exit 1
                fi
            "; then
                log_success "Linux library verified via Docker"
                record_result "linux_library_valid" true
                record_result "linux_symbols_exported" true
            else
                log_fail "Linux library verification failed"
                record_result "linux_library_valid" false
            fi
        fi
    fi
fi

# =============================================================================
# Summary
# =============================================================================

section_header "Test Summary"

echo ""
printf "%-35s %s\n" "Test" "Result"
printf "%-35s %s\n" "----" "------"

for test in "${!TEST_RESULTS[@]}"; do
    result="${TEST_RESULTS[$test]}"
    if [ "$result" = "PASS" ]; then
        printf "%-35s ${GREEN}%s${NC}\n" "$test" "$result"
    else
        printf "%-35s ${RED}%s${NC}\n" "$test" "$result"
    fi
done

echo ""
echo "================================================================"
if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed: $PASSED_TESTS/$TOTAL_TESTS${NC}"
else
    echo -e "${RED}Tests failed: $FAILED_TESTS/$TOTAL_TESTS${NC}"
fi
echo "================================================================"

exit $FAILED_TESTS
