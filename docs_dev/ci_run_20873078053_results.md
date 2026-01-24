# CI Run 20873078053 Results

**Repository:** Emasoft/fbfsvg-player
**Workflow:** main CI
**Trigger:** push
**Date:** 2026-01-10
**URL:** https://github.com/Emasoft/fbfsvg-player/actions/runs/20873078053

## Summary

| Job | Status | Duration |
|-----|--------|----------|
| API Header Test | PASS | 8s |
| Linux Build | PASS | 1m8s |
| macOS Build | PASS | 34s |
| iOS Build | PASS | 33s |
| Windows Build | FAIL | 17m17s |

**Overall Status: FAILURE**

## Artifacts Generated

- ios-xcframework
- macos-player
- linux-sdk

## Failure Details

### Windows Build - Build Windows Player Step

**Error Type:** Linker Error (LNK2019, LNK1120)

**Error Message:**
```
skshaper.lib(skshaper.SkShaper_harfbuzz.obj) : error LNK2019: unresolved external symbol
"class sk_sp<class SkUnicode> __cdecl SkUnicodes::ICU::Make(void)"
(?Make@ICU@SkUnicodes@@YA?AV?$sk_sp@VSkUnicode@@@@XZ)
referenced in function "public: static class std::unique_ptr<class SkShaper,struct std::default_delete<class SkShaper> > __cdecl SkShaper::MakeShapeThenWrap(class sk_sp<class SkFontMgr>)"

skshaper.lib(skshaper.SkShaper_skunicode.obj) : error LNK2001: unresolved external symbol
"class sk_sp<class SkUnicode> __cdecl SkUnicodes::ICU::Make(void)"

D:\a\fbfsvg-player\fbfsvg-player\scripts\..\build\windows\svg_player_animated.exe :
fatal error LNK1120: 1 unresolved externals
```

**Root Cause:**
The Windows build is missing the `skunicode.lib` library in the linker command. The skshaper library depends on SkUnicode (specifically ICU implementation) but the library providing `SkUnicodes::ICU::Make()` is not being linked.

**Fix Required:**
Add `skunicode.lib` to the Windows build script (`scripts/build-windows.bat`) in the linker libraries list.

## Compiler Warnings (Non-blocking)

The following warnings were observed but did not cause build failure:

1. **SkRect.h** - int32_t to float conversion warnings (C4244)
2. **SkMatrix.h** - int32_t to SkScalar conversion warnings (C4244)
3. **SkTArray.h** - size_t to int conversion warning (C4267)
4. **SkTemplates.h** - Unrecognized [[clang::reinitializes]] attribute (C5030)
5. **platform.h:262** - ULONGLONG to double conversion (C4244)
6. **svg_player_animated_windows.cpp** - int to SkScalar conversions (C4244)
7. **svg_player_animated_windows.cpp:1306** - localtime unsafe warning (C4996)
8. **folder_browser.cpp:52** - double to unsigned __int64 conversion (C4244)
9. **SVGAnimationController.cpp:400** - size_t to int conversion (C4267)
10. **SVGGridCompositor.cpp:429** - double to unsigned __int64 conversion (C4244)

## macOS Build Annotations (Info Only)

- pkgconf 2.5.1 already installed
- ninja 1.13.2 already installed
