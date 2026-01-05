#!/bin/bash
# run_test_cycle.sh - Complete autonomous test cycle
#
# This script runs the full test suite, compares against baselines,
# detects regressions, and triggers auto-revert if needed.
#
# Usage:
#   ./scripts/run_test_cycle.sh [--update-baseline] [--skip-build]

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse arguments
UPDATE_BASELINE=false
SKIP_BUILD=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --update-baseline) UPDATE_BASELINE=true; shift ;;
        --skip-build) SKIP_BUILD=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Determine directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
cd "${PROJECT_ROOT}"

# Get platform
PLATFORM=$(uname -m)
case "$(uname -s)" in
    Darwin*) PLATFORM="macos_${PLATFORM}" ;;
    Linux*)  PLATFORM="linux_${PLATFORM}" ;;
esac

BASELINE_DIR="tests/baselines/${PLATFORM}"
REPORT_BASE="test-report"      # Base name without extension
REPORT_FILE="${REPORT_BASE}.json"  # Full filename for jq parsing

echo -e "${BLUE}=== SVG Player Automated Test Cycle ===${NC}"
echo -e "Platform: ${PLATFORM}"
echo -e "Baseline: ${BASELINE_DIR}"
echo -e ""

# Step 1: Build test suite
if [[ "${SKIP_BUILD}" == "false" ]]; then
    echo -e "${YELLOW}[1/5] Building test suite...${NC}"
    make test-build || {
        echo -e "${RED}Build failed${NC}"
        exit 1
    }
else
    echo -e "${YELLOW}[1/5] Skipping build (--skip-build)${NC}"
fi

# Step 2: Run tests
echo -e "${YELLOW}[2/5] Running automated tests...${NC}"
TEST_EXIT_CODE=0
./build/run_tests \
    --baseline-dir="${BASELINE_DIR}" \
    --report-format=json \
    --report-output="${REPORT_BASE}" \
    --deterministic \
    2>&1 | tee test-output.log || TEST_EXIT_CODE=$?

# Step 3: Check results
echo -e "${YELLOW}[3/5] Analyzing results...${NC}"

if [[ ! -f "${REPORT_FILE}" ]]; then
    echo -e "${RED}Error: Test report not generated${NC}"
    exit 1
fi

# Parse summary from report
TOTAL=$(jq -r '.summary.total // 0' "${REPORT_FILE}")
PASSED=$(jq -r '.summary.passed // 0' "${REPORT_FILE}")
FAILED=$(jq -r '.summary.failed // 0' "${REPORT_FILE}")
CRITICAL=$(jq -r '.summary.critical // 0' "${REPORT_FILE}")

echo -e "Results: ${PASSED}/${TOTAL} passed, ${FAILED} failed, ${CRITICAL} critical"

# Step 4: Handle results
if [[ ${CRITICAL} -gt 0 ]] || [[ ${TEST_EXIT_CODE} -ne 0 ]]; then
    echo -e "${RED}[4/5] REGRESSION DETECTED${NC}"

    # Check for regressions in report
    REGRESSIONS=$(jq -r '.regressions | length // 0' "${REPORT_FILE}")
    if [[ ${REGRESSIONS} -gt 0 ]]; then
        echo -e "${RED}Found ${REGRESSIONS} regression(s):${NC}"
        jq -r '.regressions[] | "  - \(.test): \(.delta)% (threshold: \(.threshold)%)"' "${REPORT_FILE}"
    fi

    # Trigger auto-revert
    echo -e "${YELLOW}Triggering auto-revert...${NC}"
    "${SCRIPT_DIR}/auto_revert.sh" || exit 1

elif [[ ${FAILED} -gt 0 ]]; then
    echo -e "${YELLOW}[4/5] Tests failed but no regression detected${NC}"
    echo -e "Review test-report.json for details"
    exit 1

else
    echo -e "${GREEN}[4/5] All tests passed!${NC}"
fi

# Step 5: Update baseline if requested or improved
echo -e "${YELLOW}[5/5] Checking for improvements...${NC}"

if [[ "${UPDATE_BASELINE}" == "true" ]]; then
    echo -e "${GREEN}Updating baselines (--update-baseline)${NC}"
    mkdir -p "${BASELINE_DIR}"

    # Save current metrics as baseline
    jq '.metrics' "${REPORT_FILE}" > "${BASELINE_DIR}/performance.json"

    # Save current commit as last known good
    git rev-parse HEAD > "${BASELINE_DIR}/commit_hash.txt"

    echo -e "${GREEN}Baselines updated${NC}"

elif "${SCRIPT_DIR}/check_improvements.sh" "${REPORT_FILE}" 2>/dev/null; then
    echo -e "${GREEN}Performance improved! Updating baselines...${NC}"
    mkdir -p "${BASELINE_DIR}"
    jq '.metrics' "${REPORT_FILE}" > "${BASELINE_DIR}/performance.json"
    git rev-parse HEAD > "${BASELINE_DIR}/commit_hash.txt"

    # Commit baseline updates
    git add "${BASELINE_DIR}/"
    git commit -m "Update baselines: performance improved [skip ci]" || true
else
    echo -e "${BLUE}No significant improvement, baselines unchanged${NC}"
fi

echo -e ""
echo -e "${GREEN}=== Test Cycle Complete ===${NC}"
exit 0
