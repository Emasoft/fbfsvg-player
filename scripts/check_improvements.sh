#!/bin/bash
# check_improvements.sh - Detect if current run improved over baseline
#
# Returns 0 if performance improved by >5%, 1 otherwise
# Used by run_test_cycle.sh to decide whether to update baselines
#
# Usage: ./scripts/check_improvements.sh test-report.json

set -e

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <test-report.json>"
    exit 1
fi

REPORT=$1

# Get platform
PLATFORM=$(uname -m)
case "$(uname -s)" in
    Darwin*) PLATFORM="macos_${PLATFORM}" ;;
    Linux*)  PLATFORM="linux_${PLATFORM}" ;;
esac

BASELINE="tests/baselines/${PLATFORM}/performance.json"

# Check if baseline exists
if [[ ! -f "${BASELINE}" ]]; then
    echo "No baseline found at ${BASELINE}"
    echo "Run with --update-baseline to create initial baseline"
    exit 1
fi

# Check if report exists
if [[ ! -f "${REPORT}" ]]; then
    echo "Report not found: ${REPORT}"
    exit 1
fi

# Minimum improvement threshold (percent)
THRESHOLD=5.0

# Compare key metrics
# FPS improvement (higher is better)
CURRENT_FPS=$(jq -r '.metrics.measuredFPS // .metrics.fps // 0' "${REPORT}")
BASELINE_FPS=$(jq -r '.measuredFPS // .fps // 0' "${BASELINE}")

# Render time improvement (lower is better)
CURRENT_RENDER=$(jq -r '.metrics.avgRenderTimeMs // .metrics.renderTimeMs // 0' "${REPORT}")
BASELINE_RENDER=$(jq -r '.avgRenderTimeMs // .renderTimeMs // 0' "${BASELINE}")

# Memory improvement (lower is better)
CURRENT_MEMORY=$(jq -r '.metrics.peakCacheBytes // .metrics.peakMemoryBytes // 0' "${REPORT}")
BASELINE_MEMORY=$(jq -r '.peakCacheBytes // .peakMemoryBytes // 0' "${BASELINE}")

# Calculate improvements
if [[ "${BASELINE_FPS}" != "0" ]] && [[ "${BASELINE_FPS}" != "null" ]]; then
    FPS_IMPROVEMENT=$(echo "scale=2; (${CURRENT_FPS} - ${BASELINE_FPS}) / ${BASELINE_FPS} * 100" | bc)
else
    FPS_IMPROVEMENT=0
fi

if [[ "${BASELINE_RENDER}" != "0" ]] && [[ "${BASELINE_RENDER}" != "null" ]]; then
    RENDER_IMPROVEMENT=$(echo "scale=2; (${BASELINE_RENDER} - ${CURRENT_RENDER}) / ${BASELINE_RENDER} * 100" | bc)
else
    RENDER_IMPROVEMENT=0
fi

if [[ "${BASELINE_MEMORY}" != "0" ]] && [[ "${BASELINE_MEMORY}" != "null" ]]; then
    MEMORY_IMPROVEMENT=$(echo "scale=2; (${BASELINE_MEMORY} - ${CURRENT_MEMORY}) / ${BASELINE_MEMORY} * 100" | bc)
else
    MEMORY_IMPROVEMENT=0
fi

echo "Performance comparison:"
echo "  FPS:    ${BASELINE_FPS} -> ${CURRENT_FPS} (${FPS_IMPROVEMENT}%)"
echo "  Render: ${BASELINE_RENDER}ms -> ${CURRENT_RENDER}ms (${RENDER_IMPROVEMENT}%)"
echo "  Memory: ${BASELINE_MEMORY} -> ${CURRENT_MEMORY} bytes (${MEMORY_IMPROVEMENT}%)"

# Check if any metric improved significantly
IMPROVED=false

if (( $(echo "${FPS_IMPROVEMENT} > ${THRESHOLD}" | bc -l) )); then
    echo "FPS improved by ${FPS_IMPROVEMENT}% (threshold: ${THRESHOLD}%)"
    IMPROVED=true
fi

if (( $(echo "${RENDER_IMPROVEMENT} > ${THRESHOLD}" | bc -l) )); then
    echo "Render time improved by ${RENDER_IMPROVEMENT}% (threshold: ${THRESHOLD}%)"
    IMPROVED=true
fi

if (( $(echo "${MEMORY_IMPROVEMENT} > ${THRESHOLD}" | bc -l) )); then
    echo "Memory usage improved by ${MEMORY_IMPROVEMENT}% (threshold: ${THRESHOLD}%)"
    IMPROVED=true
fi

if [[ "${IMPROVED}" == "true" ]]; then
    echo "IMPROVEMENT DETECTED - baseline should be updated"
    exit 0
else
    echo "No significant improvement (threshold: ${THRESHOLD}%)"
    exit 1
fi
