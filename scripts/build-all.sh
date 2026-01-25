#!/bin/bash
# build-all.sh - Unified build, lint, and test script for all platforms
#
# This script builds the SVG Player SDK for all supported platforms
# from a macOS host. Linux builds are executed via Docker.
#
# Usage:
#   ./scripts/build-all.sh           # Build all platforms
#   ./scripts/build-all.sh -y        # Non-interactive mode (skip prompts)
#   ./scripts/build-all.sh --help    # Show usage
#
# Platforms built:
#   - macOS desktop player (native)
#   - iOS XCFramework (native via Xcode)
#   - Linux SDK (via Docker)
#
# Prerequisites:
#   - macOS host with Xcode installed
#   - Docker Desktop running (for Linux builds)
#   - Skia already built for all platforms

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
NON_INTERACTIVE=false
SKIP_LINUX=false
SKIP_IOS=false
SKIP_MACOS=false
SKIP_TESTS=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -y|--yes)
            NON_INTERACTIVE=true
            shift
            ;;
        --skip-linux)
            SKIP_LINUX=true
            shift
            ;;
        --skip-ios)
            SKIP_IOS=true
            shift
            ;;
        --skip-macos)
            SKIP_MACOS=true
            shift
            ;;
        --skip-tests)
            SKIP_TESTS=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Build SVG Player SDK for all platforms."
            echo ""
            echo "Options:"
            echo "  -y, --yes        Non-interactive mode (skip prompts)"
            echo "  --skip-linux     Skip Linux SDK build"
            echo "  --skip-ios       Skip iOS XCFramework build"
            echo "  --skip-macos     Skip macOS desktop player build"
            echo "  --skip-tests     Skip running tests after build"
            echo "  -v, --verbose    Verbose output"
            echo "  -h, --help       Show this help message"
            echo ""
            echo "Prerequisites:"
            echo "  - macOS host with Xcode installed"
            echo "  - Docker Desktop running (for Linux builds)"
            echo "  - Skia already built for all platforms"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

section_header() {
    echo ""
    echo "================================================================"
    echo -e "${BLUE}$1${NC}"
    echo "================================================================"
}

# Track build results
declare -A BUILD_RESULTS

cd "$PROJECT_ROOT"

section_header "SVG Player - Unified Build System"
echo "Project root: $PROJECT_ROOT"
echo "Date: $(date)"
echo ""

# Check prerequisites
log_info "Checking prerequisites..."

# Check for Xcode (required for iOS/macOS)
if ! command -v xcodebuild &> /dev/null; then
    log_warning "Xcode not found - iOS and macOS builds will be skipped"
    SKIP_IOS=true
    SKIP_MACOS=true
fi

# Check for Docker (required for Linux)
if ! command -v docker &> /dev/null; then
    log_warning "Docker not found - Linux build will be skipped"
    SKIP_LINUX=true
elif ! docker info &> /dev/null 2>&1; then
    log_warning "Docker daemon not running - Linux build will be skipped"
    SKIP_LINUX=true
fi

# Check for Skia builds
if [ ! -d "skia-build/src/skia/out/release-macos" ] && [ "$SKIP_MACOS" = false ]; then
    log_warning "macOS Skia not found at skia-build/src/skia/out/release-macos"
    log_warning "Run 'make skia-macos' first or use --skip-macos"
    SKIP_MACOS=true
fi

# Build macOS
if [ "$SKIP_MACOS" = false ]; then
    section_header "Building macOS Desktop Player"

    if [ -f "$SCRIPT_DIR/build-macos.sh" ]; then
        if "$SCRIPT_DIR/build-macos.sh" ${NON_INTERACTIVE:+-y}; then
            BUILD_RESULTS["macOS"]="SUCCESS"
            log_success "macOS build completed"
        else
            BUILD_RESULTS["macOS"]="FAILED"
            log_error "macOS build failed"
        fi
    else
        log_error "build-macos.sh not found"
        BUILD_RESULTS["macOS"]="SKIPPED"
    fi
else
    BUILD_RESULTS["macOS"]="SKIPPED"
    log_info "Skipping macOS build"
fi

# Build iOS XCFramework
if [ "$SKIP_IOS" = false ]; then
    section_header "Building iOS XCFramework"

    if [ -f "$SCRIPT_DIR/build-ios-framework.sh" ]; then
        if "$SCRIPT_DIR/build-ios-framework.sh" ${NON_INTERACTIVE:+-y}; then
            BUILD_RESULTS["iOS"]="SUCCESS"
            log_success "iOS XCFramework build completed"
        else
            BUILD_RESULTS["iOS"]="FAILED"
            log_error "iOS XCFramework build failed"
        fi
    else
        log_error "build-ios-framework.sh not found"
        BUILD_RESULTS["iOS"]="SKIPPED"
    fi
else
    BUILD_RESULTS["iOS"]="SKIPPED"
    log_info "Skipping iOS build"
fi

# Build Linux SDK via Docker
if [ "$SKIP_LINUX" = false ]; then
    section_header "Building Linux SDK (via Docker)"

    # Detect host architecture for selecting the right container
    HOST_ARCH=$(uname -m)
    if [ "$HOST_ARCH" = "arm64" ] || [ "$HOST_ARCH" = "aarch64" ]; then
        DOCKER_SERVICE="dev-arm64"
    else
        DOCKER_SERVICE="dev-x64"
    fi

    # Check if Docker container is running
    if docker compose -f "$PROJECT_ROOT/docker/docker-compose.yml" ps --status running 2>/dev/null | grep -q "$DOCKER_SERVICE"; then
        log_info "Docker container ($DOCKER_SERVICE) already running"
    else
        log_info "Starting Docker container ($DOCKER_SERVICE)..."
        docker compose -f "$PROJECT_ROOT/docker/docker-compose.yml" up -d "$DOCKER_SERVICE"
        sleep 2
    fi

    # Run Linux SDK build inside container
    if docker compose -f "$PROJECT_ROOT/docker/docker-compose.yml" exec -T "$DOCKER_SERVICE" bash -c "cd /workspace && ./scripts/build-linux-sdk.sh -y"; then
        BUILD_RESULTS["Linux"]="SUCCESS"
        log_success "Linux SDK build completed"
    else
        BUILD_RESULTS["Linux"]="FAILED"
        log_error "Linux SDK build failed"
    fi
else
    BUILD_RESULTS["Linux"]="SKIPPED"
    log_info "Skipping Linux build"
fi

# Summary
section_header "Build Summary"

echo ""
printf "%-15s %s\n" "Platform" "Status"
printf "%-15s %s\n" "--------" "------"

for platform in "macOS" "iOS" "Linux"; do
    status="${BUILD_RESULTS[$platform]:-UNKNOWN}"
    case $status in
        SUCCESS)
            printf "%-15s ${GREEN}%s${NC}\n" "$platform" "$status"
            ;;
        FAILED)
            printf "%-15s ${RED}%s${NC}\n" "$platform" "$status"
            ;;
        SKIPPED)
            printf "%-15s ${YELLOW}%s${NC}\n" "$platform" "$status"
            ;;
        *)
            printf "%-15s %s\n" "$platform" "$status"
            ;;
    esac
done

echo ""

# Check for any failures
FAILED=false
for platform in "macOS" "iOS" "Linux"; do
    if [ "${BUILD_RESULTS[$platform]}" = "FAILED" ]; then
        FAILED=true
        break
    fi
done

if [ "$FAILED" = true ]; then
    log_error "Some builds failed. Check the logs above for details."
    exit 1
else
    log_success "All requested builds completed successfully!"
    echo ""
    echo "Build artifacts:"
    [ "${BUILD_RESULTS[macOS]}" = "SUCCESS" ] && echo "  - macOS: build/fbfsvg-player"
    [ "${BUILD_RESULTS[iOS]}" = "SUCCESS" ] && echo "  - iOS:   build/FBFSVGPlayer.xcframework/"
    [ "${BUILD_RESULTS[Linux]}" = "SUCCESS" ] && echo "  - Linux: build/linux/libfbfsvgplayer.so"
fi

# Run tests if not skipped
if [ "$SKIP_TESTS" = false ]; then
    section_header "Running Tests"

    TEST_ARGS=""
    [ "$SKIP_MACOS" = true ] && TEST_ARGS="$TEST_ARGS --skip-macos"
    [ "$SKIP_LINUX" = true ] && TEST_ARGS="$TEST_ARGS --skip-linux"

    if [ -f "$SCRIPT_DIR/test-all.sh" ]; then
        # Explicitly use bash for associative array support
        if bash "$SCRIPT_DIR/test-all.sh" $TEST_ARGS; then
            log_success "All tests passed!"
        else
            log_error "Some tests failed"
            exit 1
        fi
    else
        log_warning "test-all.sh not found - skipping tests"
    fi
else
    log_info "Skipping tests (--skip-tests)"
fi

exit 0
