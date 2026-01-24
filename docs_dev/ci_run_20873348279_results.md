# CI Run 20873348279 Results

**Repository:** Emasoft/fbfsvg-player
**Triggered:** 2026-01-10
**Trigger:** push to main
**Duration:** ~18 minutes
**Overall Status:** FAILURE

## Job Summary

| Job | Status | Duration |
|-----|--------|----------|
| API Header Test | PASS | 12s |
| Linux Build | PASS | 45s |
| macOS Build | PASS | 39s |
| iOS Build | PASS | 34s |
| Windows Build | FAIL | 17m41s |
| Build Summary | FAIL | 3s |

## Artifacts Generated

- ios-xcframework
- macos-player
- linux-sdk

## Failed Job Details

### Windows Build

**Failed Step:** Build Windows Player

**Error:**
```
LINK : fatal error LNK1181: cannot open input file 'skunicode.lib'
```

**Root Cause:** The Skia build for Windows is missing the `skunicode.lib` library. This library is required for Unicode text handling in Skia but may not be built by default with the current Windows Skia build configuration.

**Possible Solutions:**
1. Update Windows Skia build args to include `skia_use_icu=true` and `skia_use_client_icu=false`
2. Ensure skunicode module is enabled in Skia GN args
3. Update build script to conditionally link skunicode only if present
4. Build skunicode as a separate library if Skia modularizes it differently on Windows

### Compilation Warnings (Non-fatal)

Several warnings were generated during compilation:
- C4244: conversion from int to float/SkScalar (possible loss of data)
- C4267: conversion from size_t to int (possible loss of data)
- C4996: 'localtime' deprecated, consider localtime_s
- C5030: attribute [[clang::reinitializes]] not recognized

These are non-fatal but should be addressed for clean builds.

## Annotations

```
! pkgconf 2.5.1 is already installed and up-to-date.
! ninja 1.13.2 is already installed and up-to-date.
X Process completed with exit code 1. (Windows Build)
X Process completed with exit code 1. (Build Summary)
```

## Run URL

https://github.com/Emasoft/fbfsvg-player/actions/runs/20873348279
