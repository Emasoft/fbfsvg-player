# Build Scripts Audit Report
**Generated:** 2026-01-24  
**Project:** fbfsvg-player (FBF.SVG Player)  
**Scope:** All platform build scripts in `scripts/` directory

---

## Executive Summary

The fbfsvg-player project uses a sophisticated multi-platform build system with **27 build-related scripts** spanning 4 platforms (macOS, Linux, Windows, iOS). The architecture follows a **shared component pattern** with platform-specific wrappers, all using the same core C++17 codebase (`shared/SVGAnimationController.cpp`).

**Key Findings:**
- ✅ Excellent architecture detection (arm64/x64) across all platforms
- ✅ Comprehensive error handling with colored logging
- ✅ Docker-based Linux builds for portability
- ✅ Universal binary support (macOS, iOS simulator)
- ⚠️ Some manual dependency paths (ICU on macOS)
- ⚠️ Windows build is least automated (requires manual Vulkan SDK setup)

---

## Build Scripts Inventory

### Core Platform Builds (8 scripts)

| Script | Platform | Type | Architectures | Lines |
|--------|----------|------|---------------|-------|
| `build-macos.sh` | macOS | Desktop Player | arm64, x64, universal | 153 |
| `build-macos-arch.sh` | macOS | Arch-specific | arm64 OR x64 | 268 |
| `build-linux.sh` | Linux | Desktop Player | x64, arm64 | 441 |
| `build-linux-sdk.sh` | Linux | Shared Library SDK | x64, arm64 | 444 |
| `build-windows.bat` | Windows | Desktop Player | x64 | 243 |
| `build-ios.sh` | iOS | Static Library | arm64 (device), x64+arm64 (sim) | 362 |
| `build-ios-framework.sh` | iOS | XCFramework | arm64+x64 universal | 351 |
| `build-all.sh` | All | Orchestrator | All | 303 |

### Skia Dependency Builds (2 scripts)

| Script | Purpose | Output |
|--------|---------|--------|
| `build-skia.sh` | macOS Skia | `skia-build/src/skia/out/release-macos/` |
| `build-skia-linux.sh` | Linux Skia | `skia-build/src/skia/out/release-linux/` |

### Support Scripts (3 scripts)

| Script | Purpose |
|--------|---------|
| `install-deps.sh` | Install Homebrew dependencies (SDL2, ICU, pkg-config) |
| `build.sh` | Legacy build script (deprecated?) |
| `build-skia.sh` | Skia orchestrator |

---

## Platform-by-Platform Analysis

### 1. macOS Build System

**Scripts:**
- `build-macos.sh` (orchestrator)
- `build-macos-arch.sh` (architecture-specific compiler)

**Dependencies:**
| Dependency | Detection Method | Error Handling |
|------------|------------------|----------------|
| SDL2 | `pkg-config --exists sdl2` | ✅ Suggests `brew install sdl2` |
| ICU | `brew --prefix icu4c` → checks `include/unicode` | ✅ Suggests `brew install icu4c` |
| Skia | Checks `skia-build/src/skia/out/release-macos-{arch}/libskia.a` | ✅ Suggests build command |
| Xcode | Implicit (clang++) | ⚠️ No explicit check |

**Build Flow:**
```
User: ./build-macos.sh [--universal] [--debug]
  ↓
Detect arch (arm64 or x64 from uname -m)
  ↓
If --universal:
  ├─→ build-macos-arch.sh x64 release
  ├─→ build-macos-arch.sh arm64 release
  └─→ lipo -create → fbfsvg-player (universal)
Else:
  └─→ build-macos-arch.sh {current_arch} release
  ↓
Code sign with ad-hoc signature
  ↓
Output: build/fbfsvg-player
```

**Architecture Handling:**
- ✅ Correctly translates `x86_64` → `x64` (Skia naming)
- ✅ Arch-specific Skia paths: `release-macos-arm64`, `release-macos-x64`
- ✅ Fallback to generic `release-macos` if arch-specific not found
- ✅ Validates binary arch with `lipo -info`

**Source Files Compiled:**
```cpp
src/svg_player_animated.cpp             // Main desktop player
shared/SVGAnimationController.cpp       // Core animation engine
shared/SVGGridCompositor.cpp            // Grid layout compositor
shared/svg_instrumentation.cpp          // Performance tracking
shared/DirtyRegionTracker.cpp           // Optimized rendering
shared/ElementBoundsExtractor.cpp       // SVG element bounds
src/file_dialog_macos.mm                // Native file picker (Obj-C++)
src/metal_context.mm                    // Metal GPU backend (Obj-C++)
src/graphite_context_metal.mm           // Graphite experimental backend
src/folder_browser.cpp                  // SVG folder browser
src/thumbnail_cache.cpp                 // Thumbnail cache
src/remote_control.cpp                  // Remote control server
```

**Frameworks Linked:**
```
CoreGraphics, CoreText, CoreFoundation, ApplicationServices
Metal, MetalKit, Cocoa, IOKit, IOSurface, OpenGL, QuartzCore
UniformTypeIdentifiers
```

**Error Handling:** ✅ Excellent
- Color-coded logs (RED/GREEN/YELLOW)
- Validates all dependencies before compiling
- Clear error messages with installation commands
- Architecture verification after build

**Cross-Compilation:** ✅ Full support
- Can build x64 on arm64 host and vice versa
- Uses `-arch` flags correctly
- Lipo creates universal binaries

---

### 2. Linux Build System

**Scripts:**
- `build-linux.sh` (desktop player)
- `build-linux-sdk.sh` (shared library)

**Dependencies:**
| Dependency | Detection Method | Required? |
|------------|------------------|-----------|
| Build tools | `command -v clang++` or `g++` | ✅ Required |
| pkg-config | `command -v pkg-config` | ✅ Required |
| SDL2 | `pkg-config --exists sdl2` | ✅ Required |
| EGL/OpenGL | `pkg-config --exists egl glesv2 gl` | ✅ Required |
| Vulkan | `pkg-config --exists vulkan` | ⚠️ Optional (for Graphite) |
| ICU | `pkg-config --exists icu-uc icu-i18n` | ✅ Required |
| FreeType | `pkg-config --exists freetype2` | ⚠️ Recommended |
| FontConfig | `pkg-config --exists fontconfig` | ⚠️ Recommended |
| X11 | `pkg-config --exists x11` | ✅ Required |

**Build Flow (Desktop Player):**
```
User: ./build-linux.sh [-y] [--debug]
  ↓
Check system dependencies (compiler, pkg-config, SDL2, X11)
  ↓
Check OpenGL/EGL dependencies
  ↓  [Optional - prompts if missing]
Check Vulkan (for Graphite)
  ↓  [Optional - warns if missing]
Detect ICU (pkg-config or system paths)
  ↓
Compile sources:
  ├─→ svg_player_animated_linux.cpp
  ├─→ folder_browser.cpp
  ├─→ file_dialog_linux.cpp
  ├─→ thumbnail_cache.cpp
  ├─→ remote_control.cpp
  ├─→ graphite_context_vulkan.cpp (if Vulkan available)
  ├─→ SVGAnimationController.cpp
  ├─→ SVGGridCompositor.cpp
  ├─→ svg_instrumentation.cpp
  ├─→ DirtyRegionTracker.cpp
  └─→ ElementBoundsExtractor.cpp
  ↓
Link order:
  Skia modules → Skia core → Skia deps → ICU → System libs
  ↓
Output: build/fbfsvg-player-linux-{arch}
```

**Build Flow (Linux SDK):**
```
User: ./build-linux-sdk.sh [-y] [--debug]
  ↓
Check dependencies (same as desktop)
  ↓
Compile to object files:
  ├─→ svg_player.o
  ├─→ SVGAnimationController.o
  ├─→ SVGGridCompositor.o
  └─→ svg_instrumentation.o
  ↓
Link shared library:
  $CXX -shared -Wl,--version-script=libsvgplayer.map \
       -o libsvgplayer.so.1.0.0 \
       *.o $SKIA_LIBS $ICU_LIBS $SYSTEM_LIBS
  ↓
Create symlinks:
  libsvgplayer.so.1 → libsvgplayer.so.1.0.0
  libsvgplayer.so → libsvgplayer.so.1
  ↓
Generate pkg-config file (svgplayer.pc)
  ↓
Output: build/linux/libsvgplayer.so
```

**Architecture Handling:**
- ✅ Detects `x86_64` → `x64`, `aarch64` → `arm64`
- ✅ Uses architecture-specific Skia: `release-linux-x64`, `release-linux-arm64`
- ✅ Embeds arch in binary name: `fbfsvg-player-linux-{arch}`

**Interactive Mode:**
- ✅ `-y` flag skips prompts (for CI/automation)
- ✅ Prompts when optional deps missing
- ✅ Can proceed without FreeType/FontConfig (with warning)

**Link Order (Critical):**
```
Desktop Player:
  Sources → Skia modules (svg, shaper, paragraph, resources) →
  Unicode modules → Skia core → Skia deps (expat, png, jpeg, webp, zlib, wuffs) →
  ICU libs → SDL2 → System libs (GL, EGL, X11, pthread, dl, m, fontconfig, freetype) →
  Vulkan (if available)

SDK:
  svg_player.o → SVGAnimationController.o → SVGGridCompositor.o → svg_instrumentation.o →
  Skia static libs → ICU → System libs
```

**Error Handling:** ✅ Excellent
- Staged dependency checks (system → OpenGL → ICU → Skia)
- Interactive prompts with suggestions
- Compiler preference: Clang > GCC
- Clear installation instructions for Ubuntu/Debian

**Docker Integration:**
- ✅ Both scripts work inside Docker container
- ✅ Non-interactive mode for automated builds
- ✅ Healthchecks verify environment

---

### 3. Windows Build System

**Script:** `build-windows.bat` (Windows Batch)

**Dependencies:**
| Dependency | Detection Method | Error Handling |
|------------|------------------|----------------|
| Visual Studio | `vswhere.exe` | ✅ Suggests download URL |
| C++ Tools | `vswhere -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64` | ✅ Suggests workload |
| Skia | Checks `skia-build\src\skia\out\release-windows\skia.lib` | ✅ Provides manual build instructions |
| SDL2 | Checks common paths: `C:\SDL2`, `C:\Libraries\SDL2`, `%USERPROFILE%\SDL2` | ✅ Suggests download |
| Vulkan SDK | Checks `%VULKAN_SDK%` env var, then `C:\VulkanSDK\*` | ⚠️ Optional (for Graphite) |

**Build Flow:**
```
User: build-windows.bat [/y] [/debug]
  ↓
Find Visual Studio installation via vswhere
  ↓
Initialize VS environment (vcvars64.bat)
  ↓
Check for Skia (skia.lib, not libskia.lib - Windows naming!)
  ↓
Locate SDL2 (try multiple paths + pkg-config)
  ↓
Locate Vulkan SDK (optional)
  ↓
Compile sources:
  ├─→ svg_player_animated_windows.cpp
  ├─→ file_dialog_windows.cpp
  ├─→ folder_browser.cpp
  ├─→ thumbnail_cache.cpp
  ├─→ remote_control.cpp
  ├─→ graphite_context_vulkan.cpp (if Vulkan available)
  ├─→ SVGAnimationController.cpp
  ├─→ SVGGridCompositor.cpp
  ├─→ svg_instrumentation.cpp
  ├─→ DirtyRegionTracker.cpp
  └─→ ElementBoundsExtractor.cpp
  ↓
cl.exe /Fe:fbfsvg-player.exe <sources> /link <libs>
  ↓
Copy SDL2.dll to build directory
  ↓
Output: build\windows\fbfsvg-player.exe
```

**Compiler Flags:**
```batch
/std:c++17 /EHsc /W3 
/DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /DNOMINMAX
```
- `/DNOMINMAX` prevents Windows min/max macros from conflicting with Skia

**Libraries Linked:**
```
skia.lib, svg.lib, skshaper.lib, skresources.lib
skunicode_core.lib, skunicode_icu.lib
SDL2.lib, SDL2main.lib
opengl32.lib, user32.lib, gdi32.lib, shell32.lib
comdlg32.lib, ole32.lib, shlwapi.lib, advapi32.lib, dwrite.lib
vulkan-1.lib (if Vulkan SDK available)
```

**Architecture:** ⚠️ x64 only (no arm64 support yet)

**Error Handling:** ✅ Good
- Validates Visual Studio installation
- Checks for required C++ workload
- Provides manual Skia build instructions (no automated Skia build for Windows)
- Multiple SDL2 search paths

**Weaknesses:**
- ⚠️ Skia build not automated (requires manual GN + Ninja commands)
- ⚠️ Vulkan SDK requires manual environment variable setup
- ⚠️ No arm64 Windows support (WoA)
- ⚠️ No universal binary (unlike macOS)

---

### 4. iOS Build System

**Scripts:**
- `build-ios.sh` (static library for manual integration)
- `build-ios-framework.sh` (complete XCFramework - **recommended**)

**Dependencies:**
| Dependency | Detection Method | Error Handling |
|------------|------------------|----------------|
| Xcode | `command -v xcodebuild` | ✅ Suggests `xcode-select --install` |
| Skia iOS | Checks `skia-build/src/skia/out/release-ios-{target}/libskia.a` | ✅ Suggests Skia build command |

**Build Flow (XCFramework - Recommended):**
```
User: ./build-ios-framework.sh [--clean] [-y]
  ↓
Check prerequisites (Xcode, Skia for device + simulator)
  ↓
Compile for device (arm64):
  ├─→ C++ sources (svg_player_ios.cpp, SVGAnimationController.cpp, etc.)
  ├─→ Obj-C++ sources (SVGPlayerController.mm, SVGPlayerView.mm, SVGPlayerMetalRenderer.mm)
  ├─→ ar rcs → libSVGPlayer.a
  └─→ Combine with Skia via libtool -static
  ↓
Compile for simulator (arm64 Apple Silicon):
  └─→ Same sources, different SDK (iphonesimulator)
  ↓
Compile for simulator (x86_64 Intel):
  └─→ Same sources, x86_64 arch
  ↓
Create universal simulator library:
  lipo -create arm64.a x64.a → universal.a
  ↓
Create framework bundles:
  ├─→ Device: SVGPlayer.framework (arm64)
  └─→ Simulator: SVGPlayer.framework (arm64+x64 universal)
  ↓
Create XCFramework:
  xcodebuild -create-xcframework \
    -framework device.framework \
    -framework simulator.framework \
    -output SVGPlayer.xcframework
  ↓
Output: build/SVGPlayer.xcframework/
```

**Architecture Matrix:**

| Target | Architectures | SDK | Notes |
|--------|---------------|-----|-------|
| Device | arm64 | iphoneos | Physical iPhone/iPad |
| Simulator (Apple Silicon) | arm64 | iphonesimulator | M1/M2/M3 Macs |
| Simulator (Intel) | x86_64 | iphonesimulator | Intel Macs |
| Universal Simulator | arm64 + x86_64 | iphonesimulator | **Lipo'd binary** |

**Compiler Flags:**
```bash
-std=c++17 -O2
-arch {arm64|x86_64}
-isysroot {SDK_PATH}
-mios-version-min=13.0  # Minimum iOS version
-fembed-bitcode         # For App Store submission
-fvisibility=hidden     # Hide internal symbols
-DIOS_BUILD
```

**Frameworks/Headers Included:**
```
SVGPlayer.h                  # Umbrella header
SVGPlayerView.h              # @IBDesignable UIView wrapper
SVGPlayerController.h        # Core controller
SVGPlayerMetalRenderer.h     # Metal rendering backend
svg_player_ios.h             # C API header
module.modulemap             # Swift module map
Info.plist                   # Framework metadata
```

**Integration:**
```swift
// Swift
import SVGPlayer
let player = SVGPlayerView(frame: view.bounds, svgFileName: "animation")
```

**Error Handling:** ✅ Excellent
- Validates Xcode installation
- Checks for all required Skia architectures
- Graceful degradation (arm64-only simulator if x64 Skia missing)
- Verifies architectures with `lipo -info`

**XCFramework Benefits:**
- ✅ Single artifact for device + simulator
- ✅ Xcode automatically selects correct arch
- ✅ Swift/Obj-C compatible
- ✅ @IBDesignable support (Interface Builder preview)

---

## Cross-Platform Build Orchestrator

**Script:** `build-all.sh`

**Purpose:** Build all platforms from a single macOS host

**Flow:**
```
User: ./build-all.sh [-y] [--skip-{platform}] [--skip-tests]
  ↓
Check prerequisites:
  ├─→ Xcode (for iOS/macOS)
  ├─→ Docker (for Linux)
  └─→ Skia builds (all platforms)
  ↓
Build macOS:
  ./build-macos.sh
  ↓
Build iOS XCFramework:
  ./build-ios-framework.sh
  ↓
Build Linux SDK (via Docker):
  ├─→ Detect host arch (arm64 or x64)
  ├─→ Select Docker service (dev-arm64 or dev-x64)
  ├─→ docker compose up -d {service}
  └─→ docker compose exec {service} ./build-linux-sdk.sh -y
  ↓
Run tests (if --skip-tests not set):
  bash ./test-all.sh
  ↓
Summary table:
  Platform  | Status
  ----------|--------
  macOS     | SUCCESS
  iOS       | SUCCESS
  Linux     | SUCCESS
```

**Results Tracking:**
```bash
declare -A BUILD_RESULTS
BUILD_RESULTS["macOS"]="SUCCESS|FAILED|SKIPPED"
```

**Exit Code:** Fails if ANY platform fails

**CI/CD Ready:**
- ✅ `-y` flag for non-interactive mode
- ✅ `--skip-{platform}` for selective builds
- ✅ `--skip-tests` for build-only
- ✅ Colored output with status table

---

## Dependency Management

### macOS Dependencies

**Script:** `install-deps.sh`

**Managed by:** Homebrew

**Packages:**
| Package | Purpose | Detection |
|---------|---------|-----------|
| `sdl2` | Window/input handling | `brew list sdl2` |
| `icu4c` | Unicode support (Skia requirement) | `brew list icu4c` |
| `pkg-config` | Build configuration | `brew list pkg-config` |

**Installation:**
```bash
brew install sdl2 icu4c pkg-config
```

**Limitations:**
- ⚠️ ICU path not in default search path: `/opt/homebrew/opt/icu4c/`
- ⚠️ Requires manual `ICU_ROOT` detection in build scripts

### Linux Dependencies

**Managed by:** apt-get (Ubuntu/Debian)

**Required:**
```bash
build-essential clang pkg-config
libsdl2-dev libicu-dev
libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dev
libx11-dev libfreetype6-dev
```

**Optional:**
```bash
libfontconfig1-dev libpng-dev
vulkan-tools libvulkan-dev  # For Graphite backend
```

**Detection:** ✅ Robust
- Checks `pkg-config --exists {lib}` first
- Fallback to header file existence
- Clear installation commands

### Windows Dependencies

**Managed by:** Manual installation

**Required:**
- Visual Studio 2019+ with C++ workload
- SDL2 development libraries (manual download)
- Skia (manual build with GN + Ninja)

**Optional:**
- Vulkan SDK (manual download from lunarg.com)

**Detection:** ⚠️ Less automated
- Checks common installation paths
- No automatic installation

### iOS Dependencies

**Managed by:** Xcode

**Required:**
- Xcode 15+ with iOS SDK
- Skia iOS libraries (built separately)

**Detection:** ✅ Good
- Validates Xcode installation
- Checks for Skia iOS builds

---

## Error Handling Comparison

| Platform | Logging | Dependency Checks | Interactive Prompts | CI Mode |
|----------|---------|-------------------|---------------------|---------|
| macOS | ✅ Color-coded | ✅ Comprehensive | ⚠️ No prompts | ✅ `-y` flag |
| Linux | ✅ Color-coded | ✅ Comprehensive | ✅ Prompts for optional deps | ✅ `-y` flag |
| Windows | ✅ Color-coded | ✅ Good | ⚠️ No prompts | ✅ `/y` flag |
| iOS | ✅ Color-coded | ✅ Good | ⚠️ No prompts | ✅ `-y` flag |

**Legend:**
- ✅ Excellent - ⚠️ Adequate - ❌ Missing

---

## Architecture Support Matrix

| Platform | x64 (Intel) | arm64 (Apple Silicon/ARM) | Universal |
|----------|-------------|---------------------------|-----------|
| macOS Desktop | ✅ | ✅ | ✅ (lipo) |
| macOS SDK | ⚠️ Not applicable | ⚠️ Not applicable | ⚠️ Not applicable |
| Linux Desktop | ✅ | ✅ | ❌ |
| Linux SDK | ✅ | ✅ | ❌ |
| Windows | ✅ | ❌ | ❌ |
| iOS Device | ❌ | ✅ | ❌ |
| iOS Simulator | ✅ | ✅ | ✅ (lipo) |
| iOS XCFramework | ✅ (sim) + ✅ (device) | ✅ (sim) + ✅ (device) | ✅ |

**Notes:**
- macOS/iOS support universal binaries via `lipo`
- Linux uses separate binaries per arch (no universal)
- Windows lacks arm64 support (Windows on ARM not yet supported)

---

## Integration with Skia

### Skia Build Output Paths

| Platform | Path | Library Name |
|----------|------|--------------|
| macOS (arm64) | `skia-build/src/skia/out/release-macos-arm64/` | `libskia.a` |
| macOS (x64) | `skia-build/src/skia/out/release-macos-x64/` | `libskia.a` |
| Linux (x64) | `skia-build/src/skia/out/release-linux-x64/` | `libskia.a` |
| Linux (arm64) | `skia-build/src/skia/out/release-linux-arm64/` | `libskia.a` |
| Windows | `skia-build\src\skia\out\release-windows\` | `skia.lib` ⚠️ |
| iOS Device | `skia-build/src/skia/out/release-ios-device/` | `libskia.a` |
| iOS Sim (arm64) | `skia-build/src/skia/out/release-ios-simulator-arm64/` | `libskia.a` |
| iOS Sim (x64) | `skia-build/src/skia/out/release-ios-simulator-x64/` | `libskia.a` |

⚠️ **Windows uses `skia.lib` not `libskia.lib`** (MSVC naming convention)

### Skia Modules Linked

**All platforms link these Skia modules:**
```
libsvg.a              # SVG parsing/rendering
libskia.a             # Core Skia graphics
libskshaper.a         # Text shaping (HarfBuzz wrapper)
libskparagraph.a      # Text layout
libskresources.a      # Resource management
libskunicode_core.a   # Unicode support
libskunicode_icu.a    # ICU integration
libharfbuzz.a         # Text shaping library
libexpat.a            # XML parsing
libpng.a              # PNG codec
libjpeg.a             # JPEG codec
libwebp.a             # WebP codec
libzlib.a             # Compression
libwuffs.a            # Safe codec library
```

**Link order is critical:**
```
Dependent modules first → Dependencies last
svg → shaper/paragraph/resources → unicode → harfbuzz → skia → codecs → zlib
```

---

## Shared Codebase Architecture

### Core C++ Files (All Platforms)

**Shared animation engine:**
```
shared/SVGAnimationController.cpp     # SMIL animation interpreter
shared/SVGAnimationController.h
shared/SVGGridCompositor.cpp          # Grid layout for multi-SVG
shared/SVGGridCompositor.h
shared/svg_instrumentation.cpp        # Performance metrics
shared/svg_instrumentation.h
shared/DirtyRegionTracker.cpp         # Incremental rendering optimization
shared/DirtyRegionTracker.h
shared/ElementBoundsExtractor.cpp     # SVG element bounding boxes
shared/ElementBoundsExtractor.h
shared/SVGTypes.h                     # Common type definitions
shared/platform.h                     # Platform abstractions
```

### Platform-Specific Entry Points

| Platform | Main Source | Purpose |
|----------|-------------|---------|
| macOS | `src/svg_player_animated.cpp` | SDL2-based desktop player |
| Linux | `src/svg_player_animated_linux.cpp` | SDL2-based desktop player |
| Windows | `src/svg_player_animated_windows.cpp` | SDL2-based desktop player |
| iOS | `src/svg_player_ios.cpp` | C API for UIKit integration |

**Common features across desktop platforms:**
- Folder browser (`src/folder_browser.cpp`)
- Thumbnail cache (`src/thumbnail_cache.cpp`)
- Remote control server (`src/remote_control.cpp`)

**Platform-specific UI:**
| Platform | File Dialog | GPU Context |
|----------|-------------|-------------|
| macOS | `file_dialog_macos.mm` (NSOpenPanel) | `metal_context.mm` + `graphite_context_metal.mm` |
| Linux | `file_dialog_linux.cpp` (zenity/kdialog) | `graphite_context_vulkan.cpp` |
| Windows | `file_dialog_windows.cpp` (Win32 API) | `graphite_context_vulkan.cpp` |

---

## Recommendations

### High Priority (Security/Correctness)

1. **Windows arm64 support** ⚠️  
   Windows on ARM is gaining market share. Add arm64 target to `build-windows.bat`.

2. **Vulkan detection on Windows** ⚠️  
   Automate Vulkan SDK detection (currently requires manual env var setup).

3. **Skia build automation for Windows** ⚠️  
   Consider adding `build-skia-windows.bat` similar to macOS/Linux.

### Medium Priority (Developer Experience)

4. **Unified dependency installer**  
   Create `install-deps-{platform}.sh` for all platforms (currently only macOS).

5. **Cache-friendly builds**  
   Add incremental compilation support (currently rebuilds everything).

6. **CMake integration**  
   Consider CMake for cross-platform builds instead of per-platform scripts.

### Low Priority (Nice to Have)

7. **Prebuilt Skia binaries**  
   Provide downloadable Skia binaries to skip 30-60 min build.

8. **Continuous Integration**  
   GitHub Actions workflow to build all platforms on every commit.

9. **Build time metrics**  
   Log build duration for each platform/arch combination.

---

## Build Time Estimates

| Platform | Skia Build | Player Build | Total (Clean) |
|----------|------------|--------------|---------------|
| macOS (universal) | ~45 min | ~3 min | ~48 min |
| macOS (single arch) | ~25 min | ~1.5 min | ~27 min |
| Linux (Docker) | ~40 min | ~2 min | ~42 min |
| Windows | ~50 min (manual) | ~2 min | ~52 min |
| iOS (XCFramework) | ~60 min | ~5 min | ~65 min |

**Assumptions:**
- Clean build (no cached Skia)
- 8-core CPU, 16GB RAM
- SSD storage

---

## Appendix: Build Script Dependency Graph

```
build-all.sh
  ├─→ build-macos.sh
  │     └─→ build-macos-arch.sh (x64, arm64)
  │           ├─→ Requires: skia-build/src/skia/out/release-macos-{arch}/
  │           ├─→ Requires: SDL2, ICU (via Homebrew)
  │           └─→ Compiles: svg_player_animated.cpp + shared/*.cpp + src/*.mm
  │
  ├─→ build-ios-framework.sh
  │     ├─→ Requires: skia-build/src/skia/out/release-ios-device/
  │     ├─→ Requires: skia-build/src/skia/out/release-ios-simulator-{arch}/
  │     ├─→ Compiles: svg_player_ios.cpp + shared/*.cpp + ios-sdk/*.mm
  │     └─→ Creates: SVGPlayer.xcframework
  │
  └─→ Docker → build-linux-sdk.sh
        ├─→ Requires: skia-build/src/skia/out/release-linux-{arch}/
        ├─→ Requires: libicu-dev, libegl1-mesa-dev, libsdl2-dev (apt-get)
        ├─→ Compiles: svg_player.cpp + shared/*.cpp
        └─→ Creates: libsvgplayer.so.1.0.0 + symlinks
```

---

## Conclusion

The fbfsvg-player build system is **well-architected** with:
- ✅ Consistent shared codebase across all platforms
- ✅ Comprehensive error handling and dependency checking
- ✅ Strong architecture detection (arm64/x64)
- ✅ Docker-based Linux builds for portability
- ✅ Universal binary support (macOS, iOS)

**Areas for improvement:**
- ⚠️ Windows automation lags behind (Skia build, Vulkan detection)
- ⚠️ Windows arm64 support missing
- ⚠️ No CMake/unified build system (relies on per-platform scripts)

Overall: **8/10** - Production-ready with room for Windows platform improvements.
