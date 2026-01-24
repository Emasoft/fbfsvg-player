# Quick Fix: Add Linux regression testing to CI
Generated: 2026-01-24

## Change Made
- File: `.github/workflows/regression-guard.yml`
- Lines: 9-11 (concurrency), 18-28 (matrix), 37-46 (platform-aware deps), 67 (artifact), 91, 139, 143 (baselines)
- Change: Converted single macOS job to matrix strategy with macOS + Linux

## Verification
- Syntax check: PASS (valid YAML)
- Pattern followed: GitHub Actions matrix strategy

## Files Modified
1. `.github/workflows/regression-guard.yml` - Added Linux regression testing with matrix strategy

## Notes
- Both platforms now run tests in parallel
- Platform-specific baselines: `tests/baselines/{macos,linux}_{arch}/`
- Artifacts include platform prefix: `test-report-{platform}-{sha}`
- Concurrency control prevents duplicate runs per platform
