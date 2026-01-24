# GitHub Actions CI Run Results

**Run ID:** 20873624478
**Repository:** Emasoft/fbfsvg-player
**Workflow:** main CI
**Trigger:** push
**Status:** SUCCESS
**Run URL:** https://github.com/Emasoft/fbfsvg-player/actions/runs/20873624478

## Job Results

| Job | Status | Duration |
|-----|--------|----------|
| API Header Test | PASS | 16s |
| macOS Build | PASS | 31s |
| Linux Build | PASS | 56s |
| Windows Build | PASS | 15m59s |
| iOS Build | PASS | 35s |
| Build Summary | PASS | 2s |

## Artifacts Produced

- macos-player
- ios-xcframework
- linux-sdk
- windows-player

## Annotations (Warnings)

### macOS Build
- `pkgconf 2.5.1 is already installed and up-to-date.` (.github#9)
- `ninja 1.13.2 is already installed and up-to-date.` (.github#5)

## Job IDs

| Job | Job ID |
|-----|--------|
| API Header Test | 59979568818 |
| macOS Build | 59979578448 |
| Linux Build | 59979578449 |
| Windows Build | 59979578450 |
| iOS Build | 59979578457 |
| Build Summary | 59980023487 |

## Summary

All 6 jobs completed successfully. The Windows Build took the longest at ~16 minutes (likely due to vcpkg dependency installation), while other builds completed in under a minute.

---
*Report generated: 2026-01-10*
