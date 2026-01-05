#!/bin/bash
# pre-commit-hook.sh - Run regression tests before allowing commits
# This script is installed to .git/hooks/pre-commit by install-hooks.sh

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Running pre-commit regression tests...${NC}"

# Get the project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == *".git/hooks"* ]]; then
    # Running as git hook
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    # Running from scripts directory
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

cd "$PROJECT_ROOT"

# Check if test binary exists
if [[ ! -x "build/run_tests" ]]; then
    echo -e "${YELLOW}Test binary not found. Building...${NC}"
    if ! make test-build 2>/dev/null; then
        echo -e "${RED}Failed to build test suite. Skipping pre-commit tests.${NC}"
        echo -e "${YELLOW}Run 'make test-build' manually to enable pre-commit testing.${NC}"
        exit 0  # Allow commit but warn
    fi
fi

# Run tests with minimal output
echo -e "${YELLOW}Running tests...${NC}"
TEST_OUTPUT=$(mktemp)
TEST_EXIT_CODE=0

if ./build/run_tests --report-format=json --report-output=pre-commit-report 2>"$TEST_OUTPUT"; then
    # Check the JSON report for results
    if [[ -f "pre-commit-report.json" ]]; then
        FAILED=$(jq -r '.summary.failed // 0' pre-commit-report.json 2>/dev/null || echo "0")
        CRITICAL=$(jq -r '.summary.critical // 0' pre-commit-report.json 2>/dev/null || echo "0")
        PASSED=$(jq -r '.summary.passed // 0' pre-commit-report.json 2>/dev/null || echo "0")
        TOTAL=$(jq -r '.summary.total // 0' pre-commit-report.json 2>/dev/null || echo "0")

        if [[ "$FAILED" -gt 0 ]] || [[ "$CRITICAL" -gt 0 ]]; then
            echo -e "${RED}REGRESSION DETECTED${NC}"
            echo -e "Tests: $PASSED/$TOTAL passed, ${RED}$FAILED failed${NC}, ${RED}$CRITICAL critical${NC}"
            echo ""
            echo "Failed tests:"
            jq -r '.regressions[]? | "  - \(.test)"' pre-commit-report.json 2>/dev/null || true
            echo ""
            echo -e "${RED}Commit blocked. Fix the regressions before committing.${NC}"
            echo -e "Run './scripts/run_test_cycle.sh' for details."
            rm -f pre-commit-report.json "$TEST_OUTPUT"
            exit 1
        else
            echo -e "${GREEN}All $TOTAL tests passed${NC}"
        fi
        rm -f pre-commit-report.json
    else
        echo -e "${YELLOW}Warning: Could not parse test report${NC}"
    fi
else
    TEST_EXIT_CODE=$?
    echo -e "${RED}Tests failed with exit code $TEST_EXIT_CODE${NC}"
    cat "$TEST_OUTPUT"
    rm -f pre-commit-report.json "$TEST_OUTPUT"
    exit 1
fi

rm -f "$TEST_OUTPUT"
echo -e "${GREEN}Pre-commit checks passed${NC}"
exit 0
