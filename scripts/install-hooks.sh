#!/bin/bash
# install-hooks.sh - Install git hooks for automated regression testing
# Run this script once after cloning the repository

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOOKS_DIR="$PROJECT_ROOT/.git/hooks"

echo -e "${YELLOW}Installing git hooks for SVG Player...${NC}"

# Ensure .git/hooks directory exists
if [[ ! -d "$HOOKS_DIR" ]]; then
    echo -e "${RED}Error: .git/hooks directory not found.${NC}"
    echo "Are you running this from within the git repository?"
    exit 1
fi

# Install pre-commit hook
PRE_COMMIT_SRC="$SCRIPT_DIR/pre-commit-hook.sh"
PRE_COMMIT_DST="$HOOKS_DIR/pre-commit"

if [[ -f "$PRE_COMMIT_SRC" ]]; then
    cp "$PRE_COMMIT_SRC" "$PRE_COMMIT_DST"
    chmod +x "$PRE_COMMIT_DST"
    echo -e "${GREEN}Installed pre-commit hook${NC}"
else
    echo -e "${RED}Error: pre-commit-hook.sh not found at $PRE_COMMIT_SRC${NC}"
    exit 1
fi

# Verify installation
if [[ -x "$PRE_COMMIT_DST" ]]; then
    echo ""
    echo -e "${GREEN}Git hooks installed successfully!${NC}"
    echo ""
    echo "Installed hooks:"
    echo "  - pre-commit: Runs regression tests before each commit"
    echo ""
    echo "To bypass hooks (not recommended):"
    echo "  git commit --no-verify"
    echo ""
    echo "To uninstall:"
    echo "  rm $PRE_COMMIT_DST"
else
    echo -e "${RED}Failed to install hooks${NC}"
    exit 1
fi
