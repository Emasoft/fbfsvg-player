# Automated Testing Protocol

## Overview

This document describes the fully autonomous testing system for the SVG Player project. The protocol enables:

- **Zero human intervention** testing cycles
- **Deterministic, reproducible** results
- **Automatic regression detection** with auto-revert capability
- **Iterative TDD** improvement cycles
- **CI/CD integration** with GitHub Actions

---

## Architecture

```
+------------------+    +------------------+    +------------------+
| Instrumentation  | -> | Test Harness     | -> | Regression       |
| Layer            |    | (Deterministic)  |    | Detection        |
+------------------+    +------------------+    +------------------+
        |                       |                       |
        v                       v                       v
+------------------+    +------------------+    +------------------+
| Metrics          |    | Baseline         |    | Auto-Revert      |
| Collector        |    | Comparison       |    | Controller       |
+------------------+    +------------------+    +------------------+
```

### Component Descriptions

| Component | Purpose | Key Files |
|-----------|---------|-----------|
| Instrumentation Layer | Compile-time controlled hooks for runtime validation | `shared/svg_instrumentation.h/.cpp` |
| Test Harness | Unified framework with deterministic clock/scheduler | `tests/test_harness.h`, `tests/test_environment.h` |
| Metrics Collector | Aggregates performance, memory, and correctness metrics | `tests/metrics_collector.h` |
| Baseline Provider | Stores and compares against known-good baselines | `tests/baseline_provider.h` |
| Regression Detector | Threshold-based regression identification | `tests/regression_detector.h` |
| Auto-Revert Controller | Git-based automatic revert on regression | `scripts/auto_revert.sh` |

---

## Quick Start

### Prerequisites

- macOS with Xcode Command Line Tools
- Skia libraries built (`make skia-macos`)
- SDL2 installed (`brew install sdl2`)
- ICU installed (`brew install icu4c@78`)
- jq installed (`brew install jq`)

### Running the Full Test Cycle

```bash
# From project root
./scripts/run_test_cycle.sh
```

### Expected Output (Success)

```
=== SVG Player Automated Test Cycle ===
Platform: macos_arm64
Baseline: tests/baselines/macos_arm64

[1/5] Building test suite...
[2/5] Running automated tests...
=== Test Results ===
[PASS] infrastructure::deterministic_clock_works
[PASS] infrastructure::deterministic_scheduler_queues_operations
... (all tests)

=== Summary ===
Total:    17
Passed:   17
Warnings: 0
Failed:   0
Critical: 0

[3/5] Analyzing results...
Results: 17/17 passed, 0 failed, 0 critical
[4/5] All tests passed!
[5/5] Checking for improvements...
No significant improvement, baselines unchanged

=== Test Cycle Complete ===
```

### Expected Output (Regression)

```
[3/5] Analyzing results...
Results: 15/17 passed, 2 failed, 1 critical
[4/5] REGRESSION DETECTED
Found 2 regression(s):
  - performance::render_time: 25% (threshold: 20%)
  - memory::cache_usage: 30% (threshold: 25%)
Triggering auto-revert...

=== REGRESSION DETECTED ===
Current commit: abc1234
Last good:      def5678
Creating regression branch: regression/20260105-140000-abc1234
Reverting main to def5678

=== AUTO-REVERT COMPLETE ===
Main reverted to: def5678
Regression branch: regression/20260105-140000-abc1234
```

---

## Step-by-Step Instructions

### Step 1: Build the Test Suite

```bash
make test-build
```

**Success criteria**: Binary `build/run_tests` exists and is executable.

**Failure indicators**:
- Missing Skia libraries: `ld: library not found for -lskia`
- Missing SDL2: `sdl2-config: command not found`
- Missing ICU: `Undefined symbols: _u_errorName_78`

### Step 2: Run Tests Manually

```bash
./build/run_tests --report-format=json --report-output=test-report
```

**Success criteria**:
- All tests show `[PASS]`
- File `test-report.json` is generated
- Exit code is 0

**Failure indicators**:
- Exit code 1: Test failures
- Exit code 2: Regressions detected
- No report file: Argument parsing issue

### Step 3: Run Full Automation

```bash
./scripts/run_test_cycle.sh
```

**Success criteria**:
- All 5 steps complete
- "Test Cycle Complete" message appears
- Exit code is 0

**Failure indicators**:
- Exit code 1: Regression detected, auto-revert triggered
- Missing baseline files: First run needs `--update-baseline`

### Step 4: Update Baselines (When Intended)

```bash
./scripts/run_test_cycle.sh --update-baseline
```

**Success criteria**:
- New baseline files in `tests/baselines/<platform>/`
- `commit_hash.txt` contains current HEAD
- `performance.json` and `correctness.json` updated

---

## Test Categories

| Category | Tests | Description |
|----------|-------|-------------|
| `infrastructure` | 4 | Deterministic clock, scheduler, environment, metrics |
| `instrumentation` | 2 | Hook installation and RAII restoration |
| `regression` | 4 | Baseline comparison and detection |
| `performance` | 2 | Render time and frame drop tracking |
| `memory` | 1 | Cache metrics tracking |
| `correctness` | 2 | State transitions and ID prefixing |
| `serialization` | 1 | JSON metrics output |
| `integration` | 1 | Full test cycle simulation |

---

## Regression Thresholds

| Metric | Threshold | Action |
|--------|-----------|--------|
| Render time increase | > 20% | REGRESSION |
| FPS drop | > 10% | REGRESSION |
| Memory increase | > 25% | REGRESSION |
| Cache miss rate increase | > 15% | REGRESSION |
| Any frame error | > 0 | REGRESSION |
| Any invalid state transition | > 0 | REGRESSION |

---

## File Structure

```
tests/
├── test_harness.h              # Test framework core
├── test_environment.h          # Controlled SVG fixtures
├── metrics_collector.h         # Metric aggregation
├── baseline_provider.h         # Baseline storage
├── regression_detector.h       # Threshold checking
├── test_folder_browser_automated.cpp  # Main test file
└── baselines/
    ├── macos_arm64/
    │   ├── commit_hash.txt     # Last known good commit
    │   ├── performance.json    # Performance baseline
    │   └── correctness.json    # Correctness baseline
    └── macos_x64/
        └── ...

scripts/
├── run_test_cycle.sh           # Main automation script
├── auto_revert.sh              # Git revert on regression
└── check_improvements.sh       # Improvement detection

shared/
├── svg_instrumentation.h       # Hook definitions
└── svg_instrumentation.cpp     # Hook implementations
```

---

## Instrumentation Hooks

The instrumentation system provides compile-time controlled hooks for runtime validation:

### Available Hooks

| Category | Hook | Signature |
|----------|------|-----------|
| ThumbnailCache | `onThumbnailStateChange` | `void(ThumbnailState, string path)` |
| ThumbnailCache | `onRequestQueued` | `void(size_t queueSize)` |
| ThumbnailCache | `onRequestDequeued` | `void(size_t queueSize)` |
| ThumbnailCache | `onLRUEviction` | `void(int count)` |
| FolderBrowser | `onBrowserSVGRegenerated` | `void()` |
| FolderBrowser | `onPageChange` | `void(int page)` |
| FolderBrowser | `onSelectionChange` | `void(int index)` |
| Animation | `onFrameRendered` | `void(SVGRenderStats&)` |
| Animation | `onFrameSkipped` | `void(int frameIndex)` |
| Animation | `onAnimationLoop` | `void()` |
| Animation | `onAnimationEnd` | `void()` |

### Usage in Tests

```cpp
#include "svg_instrumentation.h"

TEST_CASE(my_category, my_test) {
    // Install hooks with RAII cleanup
    instrumentation::HookInstaller hooks;

    int stateChanges = 0;
    hooks.onThumbnailStateChange([&](ThumbnailState state, const std::string& path) {
        stateChanges++;
    });

    // ... test code that triggers state changes ...

    ASSERT_EQ(stateChanges, expectedCount);
}  // Hooks automatically restored
```

### Compile-Time Control

```cpp
// Enabled by default in debug builds
#ifndef SVG_INSTRUMENTATION_ENABLED
    #if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
        #define SVG_INSTRUMENTATION_ENABLED 1
    #else
        #define SVG_INSTRUMENTATION_ENABLED 0
    #endif
#endif
```

---

## CI/CD Integration

### GitHub Actions Workflow

Create `.github/workflows/regression-guard.yml`:

```yaml
name: Regression Guard

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  regression-test:
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Install dependencies
        run: |
          brew install sdl2 icu4c@78 jq

      - name: Build Skia (cached)
        uses: actions/cache@v4
        with:
          path: skia-build/src/skia/out/release-macos
          key: skia-macos-${{ hashFiles('skia-build/*.sh') }}

      - name: Build Test Suite
        run: make test-build

      - name: Run Automated Tests
        run: ./scripts/run_test_cycle.sh

      - name: Upload Report
        uses: actions/upload-artifact@v4
        if: always()
        with:
          name: test-report-${{ github.sha }}
          path: test-report.json
```

---

## Checklist: Compile and Run

### First-Time Setup

- [ ] Clone repository with `git clone --recursive`
- [ ] Build Skia: `make skia-macos`
- [ ] Install dependencies: `brew install sdl2 icu4c@78 jq`
- [ ] Build test suite: `make test-build`
- [ ] Create initial baselines: `./scripts/run_test_cycle.sh --update-baseline`
- [ ] Commit baselines: `git add tests/baselines/ && git commit -m "Add initial baselines"`

### Daily Development

- [ ] Run tests before committing: `./scripts/run_test_cycle.sh`
- [ ] All tests pass (exit code 0)
- [ ] No regressions detected
- [ ] Commit changes

### After Major Changes

- [ ] Run full test cycle: `./scripts/run_test_cycle.sh`
- [ ] Review test-report.json for details
- [ ] If performance improved >5%, update baselines with `--update-baseline`
- [ ] Commit baseline updates if applicable

### On Regression

- [ ] Check regression branch: `git branch -a | grep regression`
- [ ] Review REGRESSION_INFO.md in regression branch
- [ ] Analyze test-report.json for specific failures
- [ ] Fix issue and create new PR
- [ ] Verify fix with `./scripts/run_test_cycle.sh`

---

## Long-Term TODO Template

### Phase 1: Foundation (COMPLETED)

- [x] Create instrumentation layer (`svg_instrumentation.h/.cpp`)
- [x] Create deterministic clock/scheduler (`test_harness.h`)
- [x] Create test environment with controlled SVGs (`test_environment.h`)
- [x] Create metrics collector (`metrics_collector.h`)
- [x] Create baseline provider (`baseline_provider.h`)
- [x] Create regression detector (`regression_detector.h`)

### Phase 2: Automation (COMPLETED)

- [x] Create main test cycle script (`run_test_cycle.sh`)
- [x] Create auto-revert script (`auto_revert.sh`)
- [x] Create improvement detection (`check_improvements.sh`)
- [x] JSON report generation with summary/regressions/metrics
- [x] Argument parsing with both `--arg value` and `--arg=value` formats

### Phase 3: Integration (COMPLETED)

- [x] Add instrumentation hooks to `thumbnail_cache.cpp`
- [x] Add instrumentation hooks to `folder_browser.cpp`
- [x] Add instrumentation hooks to `SVGAnimationController.cpp`
- [x] Create GitHub Actions workflow (`regression-guard.yml`)
- [x] Add pre-commit hook for local regression checks (`scripts/install-hooks.sh`)

### Phase 4: Advanced Features (PLANNED)

- [ ] Visual regression testing (screenshot comparison)
- [ ] Performance benchmarking with statistical analysis
- [ ] Memory leak detection integration
- [ ] Code coverage reporting
- [ ] Fuzz testing integration

### Phase 5: Cross-Platform (PLANNED)

- [ ] Linux baseline generation (via Docker)
- [ ] iOS simulator testing
- [ ] Windows testing (when SDK ready)
- [ ] Cross-platform baseline comparison

---

## Troubleshooting

### Build Errors

**Problem**: `sdl2-config: command not found`
```bash
brew install sdl2
```

**Problem**: `Undefined symbols: _u_errorName_78`
```bash
brew install icu4c@78
```

**Problem**: `library not found for -lskia`
```bash
make skia-macos
```

### Runtime Errors

**Problem**: "Test report not generated"
- Verify `--report-format` and `--report-output` arguments
- Check file permissions in project directory

**Problem**: "No baseline commit hash found"
```bash
./scripts/run_test_cycle.sh --update-baseline
```

### Auto-Revert Issues

**Problem**: "Cannot auto-revert without known good commit"
- Run `--update-baseline` to establish initial baseline
- Ensure `tests/baselines/<platform>/commit_hash.txt` exists

**Problem**: Auto-revert triggered unexpectedly
- Check `REGRESSION_INFO.md` in the created regression branch
- Review `test-report.json` for threshold violations
- Adjust thresholds in `regression_detector.h` if needed

---

## Command Reference

| Command | Description |
|---------|-------------|
| `make test-build` | Build test suite |
| `./build/run_tests` | Run tests (console output) |
| `./build/run_tests --report-format=json --report-output=test-report` | Run with JSON report |
| `./build/run_tests --deterministic` | Enable deterministic mode |
| `./build/run_tests --update-baseline` | Update baselines |
| `./scripts/run_test_cycle.sh` | Full automation cycle |
| `./scripts/run_test_cycle.sh --skip-build` | Skip build step |
| `./scripts/run_test_cycle.sh --update-baseline` | Force baseline update |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-05 | Initial protocol documentation |

---

## Contributing

When adding new tests:

1. Use the `TEST_CASE(category, name)` macro
2. Add to appropriate category or create new one
3. Use `ASSERT_*` macros for validation
4. Add metrics with `ADD_METRIC(name, value)` if applicable
5. Update this documentation if adding new categories

When modifying thresholds:

1. Edit `RegressionThresholds` in `regression_detector.h`
2. Document rationale in commit message
3. Update threshold table in this document

---

*This protocol is designed to be adaptable for other test automation needs in the project. Replace component-specific references while maintaining the overall structure and workflow.*
