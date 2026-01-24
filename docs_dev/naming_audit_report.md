# Naming Consistency Audit Report

**Generated**: 2026-01-25  
**Purpose**: Identify all inconsistencies between "SVGPlayer" and "FBFSVGPlayer/fbfsvg-player" naming

---

## Executive Summary

The codebase shows partial migration from "SVGPlayer" to "FBFSVGPlayer/fbfsvg-player":

- **✓ Completed**: macOS SDK directory renamed to `FBFSVGPlayer`
- **✗ Incomplete**: iOS, Linux, Windows SDKs still use `SVGPlayer`
- **✗ Incomplete**: All C API functions still use `SVGPlayer_` prefix
- **✗ Incomplete**: All header guards and defines use `SVG_PLAYER_`
- **✗ Incomplete**: iOS framework output is still `SVGPlayer.xcframework`

**Impact**: ~7,800 lines of SDK code, 60+ files, 80+ API functions

---

## Current State Matrix

| Component | Current Name | Target Name | Status |
|-----------|--------------|-------------|--------|
| Binary | fbfsvg-player | fbfsvg-player | ✓ Done |
| macOS SDK Directory | FBFSVGPlayer/ | FBFSVGPlayer/ | ✓ Done |
| iOS SDK Directory | SVGPlayer/ | FBFSVGPlayer/ | ✗ Needs rename |
| Linux SDK Directory | SVGPlayer/ | FBFSVGPlayer/ | ✗ Needs rename |
| Windows SDK Directory | SVGPlayer/ | FBFSVGPlayer/ | ✗ Needs rename |
| iOS Framework Output | SVGPlayer.xcframework | FBFSVGPlayer.xcframework | ✗ Needs rename |
| C API Prefix | SVGPlayer_ | FBFSVG_ or FBFSVGPlayer_ | ✗ Needs rename |
| Header Guards | SVG_PLAYER_ | FBFSVG_PLAYER_ | ✗ Needs rename |
| Bundle Identifier | com.svgplayer.SVGPlayer | com.fbfsvg.FBFSVGPlayer | ✗ Needs rename |

---

## 1. Directories to Rename

### SDK Directories

| Current Path | Target Path | Files |
|--------------|-------------|-------|
| `ios-sdk/SVGPlayer/` | `ios-sdk/FBFSVGPlayer/` | 9 files |
| `linux-sdk/SVGPlayer/` | `linux-sdk/FBFSVGPlayer/` | 3 files |
| `windows-sdk/SVGPlayer/` | `windows-sdk/FBFSVGPlayer/` | 1 file (stub) |

### Build Output

| Current | Target | Impact |
|---------|--------|--------|
| `build/SVGPlayer.xcframework/` | `build/FBFSVGPlayer.xcframework/` | iOS framework artifact |

---

## 2. Files to Rename

### iOS SDK (ios-sdk/SVGPlayer/)

| Current | Target | Type |
|---------|--------|------|
| `SVGPlayer.h` | `FBFSVGPlayer.h` | Umbrella header |
| `SVGPlayerController.h` | `FBFSVGPlayerController.h` | Controller header |
| `SVGPlayerController.mm` | `FBFSVGPlayerController.mm` | Controller impl |
| `SVGPlayerView.h` | `FBFSVGPlayerView.h` | View header |
| `SVGPlayerView.mm` | `FBFSVGPlayerView.mm` | View impl |
| `SVGPlayerMetalRenderer.h` | `FBFSVGPlayerMetalRenderer.h` | Renderer header |
| `SVGPlayerMetalRenderer.mm` | `FBFSVGPlayerMetalRenderer.mm` | Renderer impl |
| `module.modulemap` | (content update) | Module definition |
| `Info.plist` | (content update) | Bundle config |

### macOS SDK (macos-sdk/FBFSVGPlayer/)

**Status**: Directory renamed, but files still use SVGPlayer prefix

| Current | Target | Type |
|---------|--------|------|
| `SVGPlayerController.h` | `FBFSVGPlayerController.h` | Controller header |
| `SVGPlayerController.mm` | `FBFSVGPlayerController.mm` | Controller impl |
| `svg_player.h` | `fbfsvg_player.h` | C API forwarder |

### Linux SDK (linux-sdk/SVGPlayer/)

| Current | Target | Type |
|---------|--------|------|
| `svg_player.h` | `fbfsvg_player.h` | Header |
| `svg_player.cpp` | `fbfsvg_player.cpp` | Implementation |
| `libsvgplayer.map` | `libfbfsvgplayer.map` | Symbol map |

### Shared Components

| File | Action | Reason |
|------|--------|--------|
| `shared/svg_player_api.h` | Rename functions inside | Master API definition |
| `shared/svg_player_api.cpp` | Rename functions inside | Master implementation |
| `shared/SVGTypes.h` | Update header guard | Consistency |
| `shared/version.h` | Update defines | Consistency |

---

## 3. Code Symbols to Rename

### C API Functions (80+ functions)

All functions in `shared/svg_player_api.h` use the `SVGPlayer_` prefix:

**Category: Lifecycle (3 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_Create()` | `FBFSVGPlayer_Create()` |
| `SVGPlayer_Destroy()` | `FBFSVGPlayer_Destroy()` |
| `SVGPlayer_GetVersion()` | `FBFSVGPlayer_GetVersion()` |

**Category: Loading (5 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_LoadSVG()` | `FBFSVGPlayer_LoadSVG()` |
| `SVGPlayer_LoadSVGData()` | `FBFSVGPlayer_LoadSVGData()` |
| `SVGPlayer_Unload()` | `FBFSVGPlayer_Unload()` |
| `SVGPlayer_IsLoaded()` | `FBFSVGPlayer_IsLoaded()` |
| `SVGPlayer_HasAnimations()` | `FBFSVGPlayer_HasAnimations()` |

**Category: Size/Dimension (4 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_GetSize()` | `FBFSVGPlayer_GetSize()` |
| `SVGPlayer_GetSizeInfo()` | `FBFSVGPlayer_GetSizeInfo()` |
| `SVGPlayer_SetViewport()` | `FBFSVGPlayer_SetViewport()` |
| `SVGPlayer_GetViewport()` | `FBFSVGPlayer_GetViewport()` |

**Category: Playback Control (13 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_Play()` | `FBFSVGPlayer_Play()` |
| `SVGPlayer_Pause()` | `FBFSVGPlayer_Pause()` |
| `SVGPlayer_Stop()` | `FBFSVGPlayer_Stop()` |
| `SVGPlayer_TogglePlayback()` | `FBFSVGPlayer_TogglePlayback()` |
| `SVGPlayer_SetPlaybackState()` | `FBFSVGPlayer_SetPlaybackState()` |
| `SVGPlayer_GetPlaybackState()` | `FBFSVGPlayer_GetPlaybackState()` |
| `SVGPlayer_IsPlaying()` | `FBFSVGPlayer_IsPlaying()` |
| `SVGPlayer_IsPaused()` | `FBFSVGPlayer_IsPaused()` |
| `SVGPlayer_IsStopped()` | `FBFSVGPlayer_IsStopped()` |
| `SVGPlayer_SetPlaybackRate()` | `FBFSVGPlayer_SetPlaybackRate()` |
| `SVGPlayer_GetPlaybackRate()` | `FBFSVGPlayer_GetPlaybackRate()` |
| `SVGPlayer_IsPlayingForward()` | `FBFSVGPlayer_IsPlayingForward()` |
| `SVGPlayer_IsLooping()` | `FBFSVGPlayer_IsLooping()` |

**Category: Repeat Modes (6 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_SetRepeatMode()` | `FBFSVGPlayer_SetRepeatMode()` |
| `SVGPlayer_GetRepeatMode()` | `FBFSVGPlayer_GetRepeatMode()` |
| `SVGPlayer_SetRepeatCount()` | `FBFSVGPlayer_SetRepeatCount()` |
| `SVGPlayer_GetRepeatCount()` | `FBFSVGPlayer_GetRepeatCount()` |
| `SVGPlayer_GetCompletedLoops()` | `FBFSVGPlayer_GetCompletedLoops()` |
| `SVGPlayer_SetLooping()` | `FBFSVGPlayer_SetLooping()` |

**Category: Timeline (12 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_Update()` | `FBFSVGPlayer_Update()` |
| `SVGPlayer_SeekTo()` | `FBFSVGPlayer_SeekTo()` |
| `SVGPlayer_SeekToFrame()` | `FBFSVGPlayer_SeekToFrame()` |
| `SVGPlayer_SeekToProgress()` | `FBFSVGPlayer_SeekToProgress()` |
| `SVGPlayer_GetDuration()` | `FBFSVGPlayer_GetDuration()` |
| `SVGPlayer_GetCurrentTime()` | `FBFSVGPlayer_GetCurrentTime()` |
| `SVGPlayer_GetProgress()` | `FBFSVGPlayer_GetProgress()` |
| `SVGPlayer_GetCurrentFrame()` | `FBFSVGPlayer_GetCurrentFrame()` |
| `SVGPlayer_GetTotalFrames()` | `FBFSVGPlayer_GetTotalFrames()` |
| `SVGPlayer_GetFrameRate()` | `FBFSVGPlayer_GetFrameRate()` |
| `SVGPlayer_StepForward()` | `FBFSVGPlayer_StepForward()` |
| `SVGPlayer_StepBackward()` | `FBFSVGPlayer_StepBackward()` |

**Category: Rendering (2 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_Render()` | `FBFSVGPlayer_Render()` |
| `SVGPlayer_RenderAtTime()` | `FBFSVGPlayer_RenderAtTime()` |

**Category: Statistics (3 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_GetStats()` | `FBFSVGPlayer_GetStats()` |
| `SVGPlayer_GetLastError()` | `FBFSVGPlayer_GetLastError()` |
| `SVGPlayer_ClearError()` | `FBFSVGPlayer_ClearError()` |

**Category: Coordinate Conversion (2 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_ViewToSVG()` | `FBFSVGPlayer_ViewToSVG()` |
| `SVGPlayer_SVGToView()` | `FBFSVGPlayer_SVGToView()` |

**Category: Element Interaction (5 functions)**
| Current | Target |
|---------|--------|
| `SVGPlayer_SubscribeToElement()` | `FBFSVGPlayer_SubscribeToElement()` |
| `SVGPlayer_UnsubscribeFromElement()` | `FBFSVGPlayer_UnsubscribeFromElement()` |
| `SVGPlayer_UnsubscribeFromAllElements()` | `FBFSVGPlayer_UnsubscribeFromAllElements()` |
| `SVGPlayer_HitTest()` | `FBFSVGPlayer_HitTest()` |
| `SVGPlayer_GetElementBounds()` | `FBFSVGPlayer_GetElementBounds()` |

**Total**: ~80 C API functions

### Objective-C Classes

| Current | Target | Files |
|---------|--------|-------|
| `SVGPlayerController` | `FBFSVGPlayerController` | iOS/macOS |
| `SVGPlayerView` | `FBFSVGPlayerView` | iOS |
| `SVGPlayerLayer` | `FBFSVGPlayerLayer` | iOS/macOS |
| `SVGPlayerMetalRenderer` | `FBFSVGPlayerMetalRenderer` | iOS |

### Type Definitions

| Current | Target | File |
|---------|--------|------|
| `typedef struct SVGPlayer* SVGPlayerRef` | `typedef struct FBFSVGPlayer* FBFSVGPlayerRef` | shared/svg_player_api.h |
| `struct SVGPlayer` | `struct FBFSVGPlayer` | Implementation files |
| `SVGPlayerControllerErrorCode` | `FBFSVGPlayerControllerErrorCode` | iOS/macOS controllers |
| `SVGPlayerLayerBlendMode` | `FBFSVGPlayerLayerBlendMode` | iOS/macOS controllers |

---

## 4. Header Guards and Defines

### Header Guards

| File | Current | Target |
|------|---------|--------|
| `shared/SVGTypes.h` | `SVGPLAYER_TYPES_H` | `FBFSVGPLAYER_TYPES_H` |
| `shared/svg_player_api.h` | `SVG_PLAYER_API_H` | `FBFSVG_PLAYER_API_H` |
| `shared/version.h` | `SVG_PLAYER_VERSION_H` | `FBFSVG_PLAYER_VERSION_H` |
| `tests/test_environment.h` | `SVG_PLAYER_TEST_ENVIRONMENT_H` | `FBFSVG_PLAYER_TEST_ENVIRONMENT_H` |

### Macro Definitions

**Version Macros** (shared/version.h):
| Current | Target |
|---------|--------|
| `SVG_PLAYER_VERSION_MAJOR` | `FBFSVG_PLAYER_VERSION_MAJOR` |
| `SVG_PLAYER_VERSION_MINOR` | `FBFSVG_PLAYER_VERSION_MINOR` |
| `SVG_PLAYER_VERSION_PATCH` | `FBFSVG_PLAYER_VERSION_PATCH` |
| `SVG_PLAYER_HAS_PRERELEASE` | `FBFSVG_PLAYER_HAS_PRERELEASE` |
| `SVG_PLAYER_VERSION_PRERELEASE` | `FBFSVG_PLAYER_VERSION_PRERELEASE` |
| `SVG_PLAYER_BUILD_ID` | `FBFSVG_PLAYER_BUILD_ID` |
| `SVG_PLAYER_VERSION_STRING` | `FBFSVG_PLAYER_VERSION_STRING` |
| `SVG_PLAYER_VERSION` | `FBFSVG_PLAYER_VERSION` |

**Platform Macros**:
| Current | Target |
|---------|--------|
| `SVG_PLAYER_PLATFORM` | `FBFSVG_PLAYER_PLATFORM` |
| `SVG_PLAYER_PLATFORM_IOS` | `FBFSVG_PLAYER_PLATFORM_IOS` |
| `SVG_PLAYER_PLATFORM_MACOS` | `FBFSVG_PLAYER_PLATFORM_MACOS` |
| `SVG_PLAYER_PLATFORM_LINUX` | `FBFSVG_PLAYER_PLATFORM_LINUX` |
| `SVG_PLAYER_PLATFORM_WINDOWS` | `FBFSVG_PLAYER_PLATFORM_WINDOWS` |
| `SVG_PLAYER_ARCH` | `FBFSVG_PLAYER_ARCH` |
| `SVG_PLAYER_COMPILER` | `FBFSVG_PLAYER_COMPILER` |
| `SVG_PLAYER_BUILD_TYPE` | `FBFSVG_PLAYER_BUILD_TYPE` |
| `SVG_PLAYER_BUILD_INFO` | `FBFSVG_PLAYER_BUILD_INFO` |

**Metadata Macros**:
| Current | Target |
|---------|--------|
| `SVG_PLAYER_NAME` | `FBFSVG_PLAYER_NAME` |
| `SVG_PLAYER_DESCRIPTION` | `FBFSVG_PLAYER_DESCRIPTION` |
| `SVG_PLAYER_COPYRIGHT` | `FBFSVG_PLAYER_COPYRIGHT` |
| `SVG_PLAYER_LICENSE` | `FBFSVG_PLAYER_LICENSE` |
| `SVG_PLAYER_URL` | `FBFSVG_PLAYER_URL` |

**API Export Macros**:
| Current | Target |
|---------|--------|
| `SVG_PLAYER_API` | `FBFSVG_PLAYER_API` |
| `SVG_PLAYER_API_VERSION_MAJOR` | `FBFSVG_PLAYER_API_VERSION_MAJOR` |
| `SVG_PLAYER_API_VERSION_MINOR` | `FBFSVG_PLAYER_API_VERSION_MINOR` |
| `SVG_PLAYER_API_VERSION_PATCH` | `FBFSVG_PLAYER_API_VERSION_PATCH` |
| `SVG_PLAYER_WINDOWS_IMPLEMENTED` | `FBFSVG_PLAYER_WINDOWS_IMPLEMENTED` |
| `SVG_PLAYER_BUILDING_DLL` | `FBFSVG_PLAYER_BUILDING_DLL` |

---

## 5. Package Configuration

### iOS Info.plist

**File**: `ios-sdk/SVGPlayer/Info.plist`

| Key | Current | Target |
|-----|---------|--------|
| CFBundleExecutable | SVGPlayer | FBFSVGPlayer |
| CFBundleIdentifier | com.svgplayer.SVGPlayer | com.fbfsvg.FBFSVGPlayer |
| CFBundleName | SVGPlayer | FBFSVGPlayer |

### iOS Module Map

**File**: `ios-sdk/SVGPlayer/module.modulemap`

| Current | Target |
|---------|--------|
| `framework module SVGPlayer {` | `framework module FBFSVGPlayer {` |
| `umbrella header "SVGPlayer.h"` | `umbrella header "FBFSVGPlayer.h"` |

### Linux Symbol Map

**File**: `linux-sdk/SVGPlayer/libsvgplayer.map`

| Current | Target |
|---------|--------|
| Version namespace: `SVGPLAYER_1.0` | `FBFSVGPLAYER_1.0` |
| All function names: `SVGPlayer_*` | `FBFSVGPlayer_*` |

### Build Scripts

Multiple scripts reference `SVGPlayer`:

| Script | References |
|--------|------------|
| `scripts/build-ios-framework.sh` | SDK_NAME="SVGPlayer", IOS_SDK_DIR="ios-sdk/SVGPlayer" |
| `scripts/build-linux-sdk.sh` | SDK_DIR="linux-sdk/SVGPlayer", output="libsvgplayer.so" |
| `scripts/build-all.sh` | Framework path references |
| `Makefile` | Build targets and paths |

---

## 6. Documentation References

### Files with SVGPlayer References

**Count**: 60+ files contain "SVGPlayer" references

**Key Documentation Files**:
- `README.md` - API examples use SVGPlayer_* functions
- `CLAUDE.md` - Project documentation
- `docs_dev/*.md` - All audit and planning documents
- `release*/README.md` - Release notes

**Example Code Impact**:
```c
// Current (in README.md and examples)
SVGPlayerHandle player = SVGPlayer_Create();
SVGPlayer_LoadSVG(player, "animation.fbf.svg");
SVGPlayer_SetPlaybackState(player, SVGPlaybackState_Playing);
SVGPlayer_Update(player, deltaTime);
SVGPlayer_Render(player, pixelBuffer, width, height, scale);
SVGPlayer_Destroy(player);

// Target
FBFSVGPlayerHandle player = FBFSVGPlayer_Create();
FBFSVGPlayer_LoadSVG(player, "animation.fbf.svg");
// ... etc
```

---

## 7. Estimated Impact Analysis

### Code Volume

| Component | Files | Approx. Lines |
|-----------|-------|---------------|
| iOS SDK | 9 | ~3,500 |
| macOS SDK | 3 | ~2,000 |
| Linux SDK | 3 | ~500 |
| Windows SDK | 1 | ~200 (stub) |
| Shared API | 4 | ~1,500 |
| Tests | 5 | ~300 |
| **Total** | **~25** | **~8,000** |

### Symbol Counts

| Category | Count |
|----------|-------|
| C API Functions | ~80 |
| Objective-C Classes | 4 |
| Type Definitions | 8 |
| Macros/Defines | 35+ |
| Header Guards | 4 |
| Build Targets | 12+ |

### Build Impact

| Component | Impact |
|-----------|--------|
| iOS Framework | Must rebuild and re-sign |
| Linux Shared Library | Must rebuild with new symbols |
| macOS Binary | Must rebuild |
| All Tests | Must update imports and function calls |
| CI/CD Pipelines | Must update artifact names |

---

## 8. Migration Strategy Recommendations

### Phase 1: Preparation
1. Create feature branch: `feature/rename-to-fbfsvgplayer`
2. Commit all current work
3. Document all external references (if any public releases exist)

### Phase 2: Core Renaming (High Risk)
1. Rename shared API functions (80+ functions)
2. Update header guards and macros (35+ defines)
3. Rename type definitions (8 types)
4. Update symbol map (Linux)

### Phase 3: Platform SDKs
1. Rename iOS SDK directory and files
2. Rename Linux SDK directory and files
3. Rename Windows SDK directory and files
4. Update macOS SDK files (directory already renamed)
5. Update module maps and Info.plist

### Phase 4: Objective-C Wrappers
1. Rename classes (4 classes)
2. Update all references in .mm/.h files
3. Update @interface declarations

### Phase 5: Build System
1. Update build scripts (5+ scripts)
2. Update Makefile targets
3. Update CI/CD workflows
4. Update package configurations

### Phase 6: Documentation
1. Update README.md examples
2. Update CLAUDE.md references
3. Update all docs_dev/ files
4. Update release notes

### Phase 7: Testing
1. Run full test suite on all platforms
2. Verify framework builds correctly
3. Verify shared library exports correct symbols
4. Test example applications

### Phase 8: Cleanup
1. Search for any remaining "SVGPlayer" references
2. Verify no broken references
3. Update version number (breaking change)
4. Tag release

---

## 9. Risk Assessment

### High Risk Areas

| Area | Risk | Mitigation |
|------|------|------------|
| C API Functions | Breaking change for any existing users | This is pre-alpha, no public users |
| Symbol Map | Linux library won't be compatible | Update version namespace |
| Framework Name | iOS apps won't find framework | Update import statements |
| Build Scripts | CI/CD will fail | Update all paths in one commit |

### Medium Risk Areas

| Area | Risk | Mitigation |
|------|------|------------|
| Header Guards | Subtle compilation issues | Comprehensive grep before/after |
| Macros | Code using macros will break | Update all references |
| Module Maps | Swift imports will fail | Update module definitions |

### Low Risk Areas

| Area | Risk | Mitigation |
|------|------|------------|
| Documentation | Outdated examples | Easy to fix post-rename |
| Comments | Stale references | Can be fixed gradually |

---

## 10. Automation Opportunities

### Batch Rename Operations

**File Renames**:
```bash
# iOS SDK
mv ios-sdk/SVGPlayer ios-sdk/FBFSVGPlayer
for file in ios-sdk/FBFSVGPlayer/SVGPlayer*; do
    mv "$file" "${file/SVGPlayer/FBFSVGPlayer}"
done

# Linux SDK
mv linux-sdk/SVGPlayer linux-sdk/FBFSVGPlayer
# ... similar for other platforms
```

**Content Replacements** (use sed with caution):
```bash
# Function names
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.mm" \) \
    -exec sed -i '' 's/SVGPlayer_/FBFSVGPlayer_/g' {} +

# Macros
find . -type f \( -name "*.h" \) \
    -exec sed -i '' 's/SVG_PLAYER_/FBFSVG_PLAYER_/g' {} +

# Type names
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.mm" \) \
    -exec sed -i '' 's/SVGPlayerRef/FBFSVGPlayerRef/g' {} +
```

**WARNING**: Automated replacements can cause issues. Manual review required.

---

## 11. Post-Migration Verification

### Checklist

- [ ] All files renamed
- [ ] All directories renamed
- [ ] All function names updated
- [ ] All type names updated
- [ ] All macros updated
- [ ] All header guards updated
- [ ] iOS framework builds
- [ ] Linux library builds
- [ ] macOS binary builds
- [ ] Symbol map exports correct symbols
- [ ] Module map has correct framework name
- [ ] Info.plist has correct bundle ID
- [ ] README examples updated
- [ ] CLAUDE.md updated
- [ ] All tests pass
- [ ] CI/CD passes
- [ ] No remaining "SVGPlayer" references (except in comments/history)

### Grep Verification Commands

```bash
# Find any remaining SVGPlayer references (excluding expected ones)
grep -r "SVGPlayer" --exclude-dir=.git --exclude-dir=build \
    --exclude="*.md" --exclude="CHANGELOG*"

# Find any remaining SVG_PLAYER macros
grep -r "SVG_PLAYER_" --include="*.h" --include="*.cpp"

# Verify new naming
grep -r "FBFSVGPlayer" --include="*.h" --include="*.cpp" | wc -l
```

---

## 12. Conclusion

**Scope**: Large-scale renaming affecting ~25 core files, 80+ API functions, 35+ macros

**Recommendation**: Due to pre-alpha status, proceed with rename. No backward compatibility needed.

**Timeline Estimate**:
- Preparation: 1 hour
- Core renaming: 3-4 hours
- Platform updates: 2-3 hours
- Build system: 1-2 hours
- Testing: 2-3 hours
- Documentation: 1-2 hours
- **Total**: ~10-15 hours

**Priority**: Medium-High (do before public release, not urgent for internal development)

---

**Report End**
