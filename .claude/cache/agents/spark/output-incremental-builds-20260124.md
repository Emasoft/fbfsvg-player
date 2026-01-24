# Quick Fix: Enable Incremental Builds for Skia iOS
Generated: 2026-01-24 22:52

## Change Made

**Problem**: Build scripts deleted build outputs on every run, preventing incremental builds and wasting 3+ minutes on unnecessary recompilation.

**Solution**: Added `--clean` flag to conditionally remove build outputs. Incremental builds now enabled by default.

## Files Modified

1. `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/skia-build/build-ios.sh`
   - Added `--clean` flag to argument parsing
   - Made all `rm -rf` conditional on `clean_build=true` (except temp files)
   - Added build timing (shows duration at end of each build path)
   - Passes `--clean` flag to child script when set

2. `/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/skia-build/build-ios-arch.sh`
   - Accepts `--clean` as third argument
   - Made `rm -rf out/$release_name` conditional on `--clean`
   - Added ccache detection and automatic configuration
   - Provides helpful message if ccache not installed

## Verification

- Syntax: PASS (bash scripts validated)
- Pattern: Follows conditional cleanup pattern
- ccache: Integrated with GN build system

## Usage

```bash
# Incremental build (default - FAST)
./build-ios.sh --device

# Clean build (when needed)
./build-ios.sh --device --clean

# Install ccache for maximum speed
brew install ccache
```

## Expected Performance

| Build Type | First Build | Incremental |
|------------|-------------|-------------|
| Without ccache | ~3-5 min | ~30-90s |
| With ccache | ~3-5 min | ~10-30s |

## Notes

- Temporary intermediate directories (lipo outputs, combined libs) are always cleaned as they're regeneration artifacts
- Final output directories (release-ios-device, release-ios-simulator, xcframeworks) are preserved unless `--clean` is used
- ccache auto-detected but not required - builds work without it
