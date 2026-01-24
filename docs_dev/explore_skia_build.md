# Skia Build Configuration Report

**Generated:** 2026-01-19  
**Purpose:** Document current Skia build configuration across all platforms

---

## Overview

This project builds Skia separately for each platform with platform-specific GN arguments. All platforms currently use **Ganesh** (legacy GPU backend), NOT **Graphite** (next-gen).

---

## Build Scripts Architecture

```
skia-build/
├── build-macos.sh           # Wrapper for macOS (calls build-macos-arch.sh)
├── build-macos-arch.sh      # macOS arch-specific build (arm64/x64)
├── build-linux.sh           # Linux build (auto-detects arch)
├── build-ios.sh             # Wrapper for iOS (calls build-ios-arch.sh)
└── build-ios-arch.sh        # iOS arch-specific build (device/sim)
```

### How They Work

| Script | Role | Output |
|--------|------|--------|
| `build-macos.sh` | Detects arch, calls `build-macos-arch.sh`, optionally creates universal binary | `src/skia/out/release-macos/` |
| `build-macos-arch.sh` | Sets GN args, runs gn + ninja for specific arch | `src/skia/out/release-macos-{arch}/` |
| `build-linux.sh` | Checks deps, sets GN args inline, runs gn + ninja | `src/skia/out/release-linux-{arch}/` |
| `build-ios.sh` | Calls `build-ios-arch.sh` for device/sim, optionally creates XCFramework | `src/skia/out/release-ios-{target}-{arch}/` |
| `build-ios-arch.sh` | Sets GN args for iOS device/simulator, runs gn + ninja | `src/skia/out/release-ios-{target}-{arch}/` |

---

## GN Arguments by Platform

### macOS (build-macos-arch.sh)

```python
# Build type
is_official_build = true
target_cpu = "arm64"  # or "x64"

# System libraries (all bundled)
skia_use_system_expat = false
skia_use_system_libjpeg_turbo = false
skia_use_system_libpng = false
skia_use_system_libwebp = false
skia_use_system_zlib = false
skia_use_system_harfbuzz = false

# ICU (system if available, else disabled)
skia_use_icu = true                # If system ICU found
skia_use_system_icu = true         # If system ICU found
# OR
skia_use_icu = false               # If no system ICU

# Text shaping
skia_use_harfbuzz = true

# GPU backend (GANESH with Metal, NOT Graphite)
skia_use_metal = true

# Text rendering
skia_enable_skshaper = true

# C++ features
extra_cflags_cc = ["-fexceptions", "-frtti"]

# ICU paths (if Homebrew ICU)
extra_cflags = ["-I/opt/homebrew/opt/icu4c/include"]
extra_ldflags = ["-L/opt/homebrew/opt/icu4c/lib"]
```

**Notes:**
- **Metal enabled** → Uses Ganesh Metal backend (legacy)
- **Graphite NOT enabled** → Missing `skia_use_graphite = true`
- ICU auto-detected from Homebrew or system paths

---

### Linux (build-linux.sh)

```python
# Build type
is_official_build = true
target_cpu = "x64"  # or "arm64" (auto-detected)

# Compiler (prefers Clang)
cc = "clang"       # If available
cxx = "clang++"    # If available

# System libraries (all bundled)
skia_use_system_expat = false
skia_use_system_libjpeg_turbo = false
skia_use_system_libpng = false
skia_use_system_libwebp = false
skia_use_system_zlib = false
skia_use_system_harfbuzz = false

# ICU (system if available, else disabled)
skia_use_icu = true                # If system ICU found
skia_use_system_icu = true         # If system ICU found
# OR
skia_use_icu = false               # If no system ICU

# GPU backend (GANESH with OpenGL, NOT Graphite)
skia_use_gl = true
skia_use_egl = true

# C++ features
extra_cflags_cc = ["-fexceptions", "-frtti"]
```

**Notes:**
- **OpenGL/EGL enabled** → Uses Ganesh OpenGL backend (legacy)
- **Graphite NOT enabled** → Missing `skia_use_graphite = true`
- **Vulkan NOT enabled** → Missing `skia_use_vulkan = true`
- Clang strongly preferred over GCC for performance

---

### iOS (build-ios-arch.sh)

```python
# Build type
is_official_build = true
target_os = "ios"
target_cpu = "arm64"  # or "x64" (simulator only)

# iOS-specific
ios_use_simulator = true   # For simulator builds
ios_use_simulator = false  # For device builds
ios_min_target = "12.0"

# System libraries (all bundled)
skia_use_system_expat = false
skia_use_system_libjpeg_turbo = false
skia_use_system_libpng = false
skia_use_system_libwebp = false
skia_use_system_zlib = false
skia_use_system_icu = false
skia_use_system_harfbuzz = false
skia_use_system_freetype2 = false

# Text shaping
skia_use_harfbuzz = true
skia_use_icu = true
skia_use_freetype = true

# GPU backend (GANESH with Metal, NOT Graphite)
skia_use_metal = true
skia_enable_gpu = true
skia_use_gl = false  # Metal only on iOS

# Optional features
skia_enable_skottie = true
skia_enable_pdf = true
skia_enable_skshaper = true

# Platform exclusions
skia_use_x11 = false
skia_use_fontconfig = false

# C++ features
extra_cflags_cc = ["-fexceptions", "-frtti"]
```

**Notes:**
- **Metal enabled** → Uses Ganesh Metal backend (legacy)
- **OpenGL disabled** → iOS uses Metal exclusively
- **Graphite NOT enabled** → Missing `skia_use_graphite = true`
- All system libraries bundled (no pkg-config on iOS)

---

## Skia Libraries Linked

From `scripts/build-macos-arch.sh` (lines 172-184):

```bash
SKIA_LIBS="
    libsvg.a              # SVG parsing and rendering
    libskia.a             # Core Skia library
    libskresources.a      # Resource management
    libskshaper.a         # Text shaping wrapper
    libharfbuzz.a         # HarfBuzz text shaping
    libskunicode_core.a   # Unicode support (core)
    libskunicode_icu.a    # Unicode support (ICU)
    libexpat.a            # XML parsing
    libpng.a              # PNG codec
    libzlib.a             # Compression
    libjpeg.a             # JPEG codec
    libwebp.a             # WebP codec
    libwuffs.a            # Wuffs (safe decoders)
"
```

**Note:** Linux and iOS use the same libraries (platform-specific paths).

---

## Graphite Status

### Current State: **NOT ENABLED**

| Platform | GPU Backend | API | Graphite Enabled? |
|----------|-------------|-----|-------------------|
| macOS | Ganesh | Metal | ❌ NO |
| iOS | Ganesh | Metal | ❌ NO |
| Linux | Ganesh | OpenGL/EGL | ❌ NO |
| Windows | N/A (CPU only) | N/A | ❌ NO |

### What's Missing?

To enable Graphite, the following GN args would need to be added:

```python
# Enable Graphite
skia_use_graphite = true

# Platform-specific Graphite backends
skia_use_graphite_mtl = true      # macOS/iOS
skia_use_graphite_vulkan = true   # Linux/Windows

# Optional: Disable Ganesh (reduces binary size)
skia_enable_ganesh = false
```

### Why Graphite Matters

From `/docs_dev/skia_graphite_report.md`:

1. **Ganesh is deprecated** - Graphite is Skia's future
2. **Performance:** 15-200% improvements across platforms
3. **Multi-threading:** Built-in parallel recording
4. **Production-ready:** Shipping in Chrome on macOS since July 2025

**See:** `docs_dev/skia_graphite_report.md` for full migration plan

---

## ICU Configuration

### macOS

```bash
# Auto-detected via Homebrew
brew --prefix icu4c
# → /opt/homebrew/opt/icu4c (Apple Silicon)
# → /usr/local/opt/icu4c (Intel)

# If found:
skia_use_icu = true
skia_use_system_icu = true
extra_cflags = ["-I{ICU_ROOT}/include"]
extra_ldflags = ["-L{ICU_ROOT}/lib"]

# If not found:
skia_use_icu = false  # Avoids V8 conflicts
```

### Linux

```bash
# Auto-detected via pkg-config
pkg-config --exists icu-uc icu-i18n

# If found:
skia_use_icu = true
skia_use_system_icu = true

# If not found:
skia_use_icu = false  # Avoids V8 conflicts
```

### iOS

```python
# iOS always uses bundled ICU
skia_use_icu = true
skia_use_system_icu = false
```

**Rationale:** System ICU avoids conflicts with V8's bundled ICU in the Chromium codebase.

---

## Compiler Settings

### macOS

```bash
CXX="clang++"
CXXFLAGS="-std=c++17 -O2 -DNDEBUG"  # Release
CXXFLAGS="-std=c++17 -g -O0 -DDEBUG"  # Debug
ARCH_FLAG="-arch arm64"  # or "-arch x86_64"
```

### Linux

```bash
# Prefers Clang (5-15% faster than GCC for Skia)
cc = "clang"
cxx = "clang++"

# Falls back to GCC if Clang unavailable
cc = "gcc"    # With warning about performance
cxx = "g++"
```

### iOS

```bash
# Always uses Xcode toolchain
# Configured via GN args (target_os = "ios")
# No explicit compiler flags needed
```

---

## Build Outputs

| Platform | Output Directory | Files |
|----------|------------------|-------|
| macOS (arm64) | `skia-build/src/skia/out/release-macos-arm64/` | *.a libraries |
| macOS (x64) | `skia-build/src/skia/out/release-macos-x64/` | *.a libraries |
| macOS (universal) | `skia-build/src/skia/out/release-macos/` | Universal *.a (via lipo) |
| Linux (x64) | `skia-build/src/skia/out/release-linux-x64/` | *.a libraries |
| Linux (arm64) | `skia-build/src/skia/out/release-linux-arm64/` | *.a libraries |
| iOS (device) | `skia-build/src/skia/out/release-ios-device-arm64/` | *.a libraries |
| iOS (sim x64) | `skia-build/src/skia/out/release-ios-simulator-x64/` | *.a libraries |
| iOS (sim arm64) | `skia-build/src/skia/out/release-ios-simulator-arm64/` | *.a libraries |
| iOS (XCFramework) | `skia-build/src/skia/out/xcframeworks/skia.xcframework/` | Framework bundle |

---

## Comparison: Current vs. Graphite

| Aspect | Current (Ganesh) | With Graphite |
|--------|------------------|---------------|
| **macOS GPU** | Metal (Ganesh) | Metal (Graphite) |
| **iOS GPU** | Metal (Ganesh) | Metal (Graphite) |
| **Linux GPU** | OpenGL/EGL (Ganesh) | Vulkan (Graphite) |
| **Windows GPU** | None (CPU only) | Vulkan (Graphite) |
| **Multi-threading** | Single-threaded | Multi-threaded by default |
| **Shader compilation** | Runtime (jank) | Pre-compilation support |
| **Binary size** | ~25MB (both backends) | ~20MB (Graphite only) |
| **Future support** | **Deprecated** | Sole GPU backend |

---

## Recommendations

### Immediate Actions

1. **Enable Graphite** - Ganesh is deprecated and will be removed
2. **Add Vulkan for Linux** - OpenGL is legacy; Vulkan is modern
3. **Consider Vulkan for Windows** - Enables GPU acceleration

### Migration Path

See `docs_dev/skia_graphite_report.md` for detailed migration plan:

1. **Week 1:** Rebuild Skia with Graphite GN args
2. **Week 2:** Migrate macOS to Graphite Metal backend
3. **Week 3:** Add Linux/Windows Vulkan backend
4. **Week 4:** Migrate iOS to Graphite

### Performance Expected

Based on Chrome and React Native data:

- **macOS:** +15% rendering performance (M3 MacBook Pro)
- **iOS:** +50% animation performance
- **Android/Linux:** +200% animation performance, +200% faster startup

---

## Build Commands Reference

```bash
# macOS (current architecture)
cd skia-build && ./build-macos.sh

# macOS (universal binary)
cd skia-build && ./build-macos.sh --universal

# Linux (inside Docker)
cd skia-build && ./build-linux.sh -y

# iOS (device)
cd skia-build && ./build-ios.sh --device

# iOS (XCFramework)
cd skia-build && ./build-ios.sh --xcframework
```

---

## Related Files

| File | Purpose |
|------|---------|
| `skia-build/build-macos-arch.sh` | macOS Skia build (arch-specific) |
| `skia-build/build-linux.sh` | Linux Skia build |
| `skia-build/build-ios-arch.sh` | iOS Skia build (arch-specific) |
| `scripts/build-macos-arch.sh` | macOS player build (links Skia) |
| `docs_dev/skia_graphite_report.md` | Graphite migration research |
| `shared/SVGAnimationController.cpp` | Uses Skia API (backend-agnostic) |

---

**End of Report**
