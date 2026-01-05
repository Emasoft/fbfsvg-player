#!/bin/bash
# auto_revert.sh - Automatic git revert on regression detection
# Called by run_test_cycle.sh when tests detect regression
#
# Workflow:
# 1. Create regression branch for investigation
# 2. Document regression info
# 3. Revert main to last known good commit
# 4. Add git note marking bad commit

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Get platform-specific baseline directory
PLATFORM=$(uname -m)
case "$(uname -s)" in
    Darwin*) PLATFORM="macos_${PLATFORM}" ;;
    Linux*)  PLATFORM="linux_${PLATFORM}" ;;
esac

BASELINE_DIR="tests/baselines/${PLATFORM}"
COMMIT_HASH_FILE="${BASELINE_DIR}/commit_hash.txt"

# Verify we're in a git repo
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${RED}Error: Not in a git repository${NC}"
    exit 1
fi

# Get current and last good commits
CURRENT=$(git rev-parse HEAD)
CURRENT_SHORT=$(git rev-parse --short HEAD)

if [[ ! -f "${COMMIT_HASH_FILE}" ]]; then
    echo -e "${RED}Error: No baseline commit hash found at ${COMMIT_HASH_FILE}${NC}"
    echo -e "${YELLOW}Cannot auto-revert without known good commit${NC}"
    exit 1
fi

LAST_GOOD=$(cat "${COMMIT_HASH_FILE}")
LAST_GOOD_SHORT=$(git rev-parse --short "${LAST_GOOD}")

echo -e "${YELLOW}=== REGRESSION DETECTED ===${NC}"
echo -e "Current commit: ${CURRENT_SHORT}"
echo -e "Last good:      ${LAST_GOOD_SHORT}"

# Check if we're already at the last good commit
if [[ "${CURRENT}" == "${LAST_GOOD}" ]]; then
    echo -e "${GREEN}Already at last known good commit${NC}"
    exit 0
fi

# Create regression branch for investigation
BRANCH_NAME="regression/$(date +%Y%m%d-%H%M%S)-${CURRENT_SHORT}"
echo -e "${YELLOW}Creating regression branch: ${BRANCH_NAME}${NC}"
git checkout -b "${BRANCH_NAME}"

# Document regression
cat > REGRESSION_INFO.md << EOF
# Regression Detected

**Timestamp:** $(date -u +"%Y-%m-%dT%H:%M:%SZ")
**Bad Commit:** ${CURRENT}
**Last Good Commit:** ${LAST_GOOD}

## Actions Taken
1. Created this regression branch for investigation
2. Reverted main to last known good commit

## Investigation Checklist
- [ ] Review test-report.json for failed tests
- [ ] Identify regression cause
- [ ] Fix and submit new PR

## Test Report
See: test-report.json (if exists)
EOF

git add REGRESSION_INFO.md
git commit -m "Document regression in ${CURRENT_SHORT}"

# Switch back to main
git checkout main

# Revert to last good commit
echo -e "${YELLOW}Reverting main to ${LAST_GOOD_SHORT}${NC}"
git reset --hard "${LAST_GOOD}"

# Add git note marking the bad commit
echo -e "${YELLOW}Marking ${CURRENT_SHORT} as regression${NC}"
git notes add -f -m "REGRESSION: Auto-reverted $(date -u +%Y-%m-%d)" "${CURRENT}" 2>/dev/null || true

echo -e "${RED}=== AUTO-REVERT COMPLETE ===${NC}"
echo -e "Main reverted to: ${LAST_GOOD_SHORT}"
echo -e "Regression branch: ${BRANCH_NAME}"
echo -e ""
echo -e "To investigate, run:"
echo -e "  git checkout ${BRANCH_NAME}"

exit 1  # Exit with error to signal CI failure
