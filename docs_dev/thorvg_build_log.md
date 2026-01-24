# ThorVG Build Error Log

**Date**: 2026-01-17
**Script**: `./scripts/benchmark-native.sh --setup`

## Error Summary

Meson build failed due to unknown option "examples".

## Full Error Output

```
The Meson build system
Version: 1.10.0
Source dir: /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/builds_dev/thorvg/thorvg
Build dir: /Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/builds_dev/thorvg/thorvg/build
Build type: native build

meson.build:1:0: ERROR: Unknown option: "examples".
```

## Cause

The ThorVG meson build options have changed. The `-Dexamples=false` flag is no longer valid.

## Fix Required

Update `scripts/benchmark-native.sh` to use correct meson options for current ThorVG version. Check available options with:
```bash
meson configure builds_dev/thorvg/thorvg
```
