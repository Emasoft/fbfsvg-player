#!/bin/bash
#
# test_all_platforms.sh - Run remote control tests on all platforms
#
# This script tests SVG Player remote control on multiple platforms simultaneously.
# Configure your target hosts below.
#
# Usage:
#   ./test_all_platforms.sh              # Test all configured targets
#   ./test_all_platforms.sh --local      # Test only localhost
#   ./test_all_platforms.sh --parallel   # Run tests in parallel
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEST_SCRIPT="${SCRIPT_DIR}/test_remote_control.py"

# ============================================================================
# CONFIGURATION - Edit these to match your setup
# ============================================================================

# Default targets (host:port format)
# Add your Linux VM, Windows VM, etc. here
TARGETS=(
    "localhost:9999"        # Local macOS player
    # "linux-vm:9999"       # Linux VM (uncomment when available)
    # "windows-vm:9999"     # Windows VM (uncomment when available)
    # "192.168.1.100:9999"  # Another machine by IP
)

# ============================================================================
# END CONFIGURATION
# ============================================================================

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}"
    echo "=============================================="
    echo "  SVG Player Remote Control Test Suite"
    echo "=============================================="
    echo -e "${NC}"
}

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --local       Test only localhost:9999"
    echo "  --parallel    Run tests on all targets in parallel"
    echo "  --targets     Comma-separated list of targets (overrides config)"
    echo "  --help        Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Test all configured targets"
    echo "  $0 --local                            # Test localhost only"
    echo "  $0 --parallel                         # Test all targets in parallel"
    echo "  $0 --targets localhost:9999,vm:9999   # Test specific targets"
}

# Parse arguments
LOCAL_ONLY=false
PARALLEL=false
CUSTOM_TARGETS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --local)
            LOCAL_ONLY=true
            shift
            ;;
        --parallel)
            PARALLEL=true
            shift
            ;;
        --targets)
            CUSTOM_TARGETS="$2"
            shift 2
            ;;
        --help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_usage
            exit 1
            ;;
    esac
done

# Check if test script exists
if [[ ! -f "$TEST_SCRIPT" ]]; then
    echo -e "${RED}Error: Test script not found at $TEST_SCRIPT${NC}"
    exit 1
fi

# Build target list
if [[ -n "$CUSTOM_TARGETS" ]]; then
    TARGET_LIST="$CUSTOM_TARGETS"
elif [[ "$LOCAL_ONLY" == "true" ]]; then
    TARGET_LIST="localhost:9999"
else
    # Join array elements with commas
    TARGET_LIST=$(IFS=,; echo "${TARGETS[*]}")
fi

print_header

echo -e "${YELLOW}Targets:${NC} $TARGET_LIST"
echo -e "${YELLOW}Mode:${NC} $([ "$PARALLEL" == "true" ] && echo "Parallel" || echo "Sequential")"
echo ""

# Run tests
ARGS="--targets $TARGET_LIST"
if [[ "$PARALLEL" == "true" ]]; then
    ARGS="$ARGS --parallel"
fi

python3 "$TEST_SCRIPT" $ARGS
EXIT_CODE=$?

# Summary
echo ""
if [[ $EXIT_CODE -eq 0 ]]; then
    echo -e "${GREEN}All tests passed!${NC}"
else
    echo -e "${RED}Some tests failed!${NC}"
fi

exit $EXIT_CODE
