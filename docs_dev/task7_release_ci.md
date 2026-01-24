# Task 7: Add --ci mode to release.py

**Date:** 2026-01-24
**Status:** DONE

## Summary

Added `--ci` flag to `scripts/release.py` for GitHub Actions integration.

## Changes Made

1. **Added `--ci` argument** to argparse (line 1859-1862)
2. **Added `CI_MODE` global** and `set_ci_mode()` function (lines 125-131)
3. **Updated `log()` function** to suppress colors when CI_MODE is True (lines 147-168)
4. **Updated `run_release()` signature** to accept `ci_mode: bool = False` (line 1527)
5. **Added `write_ci_summary()` function** to write JSON to `release/release-summary.json` (lines 1491-1512)
6. **Updated all exit points** to use return codes and write CI summary
7. **Updated `main()`** to pass `ci_mode=args.ci` and handle return codes (lines 1875-1884)
8. **Updated docstring examples** to show `--ci` usage

## CI Mode Behavior

When `--ci` is set:
- `no_confirm=True` automatically (skips interactive publish confirmation)
- Exit codes: 0=success, 1=error, 2=partial (currently 0/1 used)
- JSON summary written to `release/release-summary.json`:
  ```json
  {
    "version": "0.2.0",
    "tag": "v0.2.0",
    "success": true,
    "packages": [{"name": "...", "sha256": "...", "size": "..."}],
    "errors": []
  }
  ```
- Color codes suppressed in output (all ANSI codes set to empty strings)

## File Modified

`/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/scripts/release.py`
