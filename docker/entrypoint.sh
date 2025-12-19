#!/bin/bash
# entrypoint.sh - Docker container entry point for SVGPlayer development

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  SVGPlayer Linux Development Environment${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if workspace is mounted
if [ ! -d "/workspace" ]; then
    echo -e "${RED}Error: /workspace not found${NC}"
    echo "Please mount the project directory to /workspace"
    echo "Example: docker run -v /path/to/project:/workspace ..."
    exit 1
fi

# Check for depot_tools
if [ -d "/workspace/skia-build/depot_tools" ]; then
    echo -e "${GREEN}depot_tools found in workspace${NC}"
    export PATH="/workspace/skia-build/depot_tools:$PATH"
elif [ -d "/opt/depot_tools" ]; then
    echo -e "${GREEN}depot_tools found in /opt${NC}"
    export PATH="/opt/depot_tools:$PATH"
else
    echo -e "${YELLOW}Note: depot_tools not found${NC}"
    echo "Run: cd /workspace/skia-build && ./get_depot_tools.sh"
fi

# Add Skia bin to PATH if it exists
if [ -d "/workspace/skia-build/src/skia/bin" ]; then
    export PATH="/workspace/skia-build/src/skia/bin:$PATH"
fi

# Display environment info
echo ""
echo -e "${BLUE}Environment:${NC}"
echo "  User:     $(whoami)"
echo "  Workdir:  $(pwd)"
echo "  Clang:    $(clang --version 2>/dev/null | head -1 || echo 'not found')"
echo "  Python:   $(python3 --version 2>/dev/null || echo 'not found')"
echo ""

# Show quick help
echo -e "${BLUE}Quick Start:${NC}"
echo "  1. Build Skia:     cd skia-build && ./build-linux.sh -y"
echo "  2. Build SDK:      make linux-sdk"
echo "  3. Run tests:      cd build/linux && ./test_program"
echo "  4. Health check:   healthcheck.sh"
echo ""
echo -e "${BLUE}Entering shell...${NC}"
echo ""

# Execute the command passed to docker run
exec "$@"
