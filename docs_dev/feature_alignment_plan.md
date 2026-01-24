# FBF.SVG Player - Feature Alignment Plan

**Generated:** 2026-01-19
**Reference Implementation:** macOS Player
**Goal:** Achieve feature parity across all platforms

---

## Executive Summary

Based on comprehensive audits of all platform implementations, this plan establishes a roadmap to align Linux, Windows, and iOS versions with the macOS reference implementation.

### Current Status Overview

| Platform | Completeness | Critical Gaps |
|----------|--------------|---------------|
| **macOS** | ~90% | None (reference) |
| **Linux** | ~90% | GPU backend missing |
| **Windows** | ~88% | GPU backend missing, CLI options incomplete |
| **iOS SDK** | ~90% | Callback bridging, touch events |
| **Shared API** | 98% | 3 TODOs (non-critical) |

---

## Phase 1: Critical Performance (GPU Backends)

**Priority: HIGH**
**Impact: 2-4x performance improvement**

The single most impactful improvement is adding GPU acceleration to Linux and Windows.

### 1.1 Linux GPU Backend (OpenGL/EGL or Vulkan)

**Current State:** CPU-only rendering via SDL2 texture blit
**Target:** GPU-accelerated SVG rendering via Skia

**Options:**

| Backend | Pros | Cons | Effort |
|---------|------|------|--------|
| **OpenGL/EGL** | Widest compatibility, Skia mature support | Older API | ~1 week |
| **Vulkan** | Modern, explicit control | More complex, newer GPUs only | ~2 weeks |

**Recommendation:** OpenGL/EGL first (wider compatibility), Vulkan as optional flag later.

**Implementation Tasks:**
1. [ ] Create `src/gl_context_linux.cpp` (~300 lines)
   - Initialize EGL display/context
   - Create Skia GrDirectContext with GL backend
   - Handle surface creation/resize
2. [ ] Add `--opengl` flag to Linux player
3. [ ] Integrate with SDL2 window (EGL native window)
4. [ ] Add VSync control via EGL swap interval
5. [ ] Implement fallback to CPU on GL init failure
6. [ ] Update build script to link GL/EGL libraries
7. [ ] Add performance metrics to debug overlay

**Files to Create/Modify:**
- `src/gl_context_linux.cpp` (NEW)
- `src/gl_context_linux.h` (NEW)
- `src/svg_player_animated_linux.cpp` (add GL path)
- `scripts/build-linux.sh` (link GL/EGL)

---

### 1.2 Windows GPU Backend (Direct3D 12 or Vulkan)

**Current State:** CPU rendering, D3D11 only for texture blit
**Target:** GPU-accelerated SVG rendering via Skia

**Options:**

| Backend | Pros | Cons | Effort |
|---------|------|------|--------|
| **Direct3D 12** | Native Windows, best integration | Complex setup, Win10+ only | ~2-3 weeks |
| **Vulkan** | Cross-platform, same as Linux | Extra runtime dependency | ~2 weeks |

**Recommendation:** Vulkan first (code reuse with Linux), D3D12 as optional flag later.

**Implementation Tasks:**
1. [ ] Create `src/vulkan_context_windows.cpp` (~400 lines)
   - Vulkan instance/device creation
   - Skia GrDirectContext with Vulkan backend
   - DXGI swap chain integration (or Vulkan surface)
2. [ ] Add `--vulkan` flag to Windows player
3. [ ] Handle device enumeration (discrete GPU preference)
4. [ ] Implement VSync via swap chain
5. [ ] Fallback to CPU on Vulkan init failure
6. [ ] Update build script to link Vulkan SDK

**Files to Create/Modify:**
- `src/vulkan_context_windows.cpp` (NEW)
- `src/vulkan_context_windows.h` (NEW)
- `src/svg_player_animated_windows.cpp` (add Vulkan path)
- `scripts/build-windows.bat` (link Vulkan)

---

## Phase 2: Feature Parity (Desktop Players)

**Priority: MEDIUM**
**Impact: Consistent user experience across platforms**

### 2.1 Linux Missing Features

| Feature | Status | Effort | Notes |
|---------|--------|--------|-------|
| Remote control CLI flag | Code exists, flag missing | 1 hour | Add `--remote-control[=PORT]` |
| Benchmark mode | Missing | 4 hours | Add `--duration`, `--json` flags |
| Image sequence mode | Missing | 1 day | Folder of SVGs as animation |

**Implementation Tasks:**
1. [ ] Add `--remote-control[=PORT]` CLI flag (expose existing code)
2. [ ] Add `--duration=N` benchmark flag
3. [ ] Add `--json` stats output flag
4. [ ] Implement image sequence folder playback

**Files to Modify:**
- `src/svg_player_animated_linux.cpp` (CLI parsing, main loop)

---

### 2.2 Windows Missing Features

| Feature | Status | Effort | Notes |
|---------|--------|--------|-------|
| **M key** (maximize) | Missing | 30 min | Add SDL_MaximizeWindow toggle |
| **T key** (frame limiter) | Missing | 30 min | Add toggle logic |
| **Up/Down keys** (seek) | Missing | 1 hour | ±1 second seek |
| **L key** (loop mode) | Missing | 30 min | Toggle RepeatMode |
| `--pos=X,Y` | Missing | 2 hours | Window position flag |
| `--size=WxH` | Missing | 2 hours | Window size flag |
| `--maximize` | Missing | 1 hour | Start maximized |
| `--duration` | Missing | 2 hours | Benchmark mode |
| `--json` | Missing | 2 hours | JSON stats output |
| Image sequence mode | Missing | 1 day | Folder playback |
| Remote control server | Missing | 2 days | TCP server from macOS |

**Implementation Tasks:**
1. [ ] Add M key handler for maximize/restore toggle
2. [ ] Add T key handler for frame limiter toggle
3. [ ] Add Up/Down arrow handlers for ±1s seek
4. [ ] Add L key handler for loop mode toggle
5. [ ] Add `--pos=X,Y` CLI flag
6. [ ] Add `--size=WxH` CLI flag
7. [ ] Add `--maximize` CLI flag
8. [ ] Add `--duration=N` benchmark flag
9. [ ] Add `--json` output flag
10. [ ] Port remote control server from macOS
11. [ ] Add image sequence mode

**Files to Modify:**
- `src/svg_player_animated_windows.cpp` (keyboard, CLI, remote control)
- `src/remote_control.cpp` (may need Windows socket adaptation)

---

## Phase 3: iOS SDK Completion

**Priority: MEDIUM**
**Impact: Production-ready iOS framework**

### 3.1 Callback Bridging

**Current State:** C callbacks defined but not bridged to Obj-C delegates
**Target:** Full delegate support for all player events

**Implementation Tasks:**
1. [ ] Define `SVGPlayerDelegate` protocol in `SVGPlayerController.h`
   ```objc
   @protocol SVGPlayerDelegate <NSObject>
   @optional
   - (void)playerDidChangePlaybackState:(SVGPlaybackState)state;
   - (void)playerDidUpdateProgress:(double)progress;
   - (void)playerDidEncounterError:(NSError *)error;
   - (void)playerDidCompleteAnimation;
   @end
   ```
2. [ ] Add delegate property to `SVGPlayerController`
3. [ ] Implement C callback → delegate forwarding
4. [ ] Update demo app to use delegate pattern

**Files to Modify:**
- `ios-sdk/SVGPlayer/SVGPlayerController.h` (add protocol)
- `ios-sdk/SVGPlayer/SVGPlayerController.mm` (add forwarding)

---

### 3.2 Element Touch Events

**Current State:** Stubbed (returns false)
**Target:** Hit testing for interactive SVG elements

**Implementation Tasks:**
1. [ ] Implement `SVGPlayer_HitTest(x, y)` in shared API
2. [ ] Wire `touchesBegan:` to hit testing
3. [ ] Add `elementTapped:` delegate callback
4. [ ] Support element highlighting on touch

**Files to Modify:**
- `shared/svg_player_api.cpp` (implement hit testing)
- `ios-sdk/SVGPlayer/SVGPlayerView.mm` (touch handling)
- `ios-sdk/SVGPlayer/SVGPlayerController.mm` (delegate call)

---

### 3.3 Fullscreen Support

**Current State:** Stub (empty implementation)
**Target:** Full view controller presentation

**Implementation Tasks:**
1. [ ] Implement fullscreen presentation using `UIViewController`
2. [ ] Add dismiss gesture (swipe/tap)
3. [ ] Handle status bar hiding
4. [ ] Support both portrait and landscape

**Files to Modify:**
- `ios-sdk/SVGPlayer/SVGPlayerController.mm` (fullscreen logic)
- Add `SVGPlayerFullscreenViewController.h/.mm` (NEW)

---

## Phase 4: Shared API Completion

**Priority: LOW**
**Impact: API completeness**

### 4.1 Element Bounds Query (TODO)

**File:** `shared/svg_player_api.cpp` line ~340

**Implementation:**
```cpp
SVGRect SVGPlayer_GetElementBounds(SVGPlayerHandle handle, const char* elementId) {
    // Query Skia DOM for element bounds
    // Transform to screen coordinates
    // Return SVGRect{x, y, width, height}
}
```

### 4.2 Element Property Introspection (TODO)

**File:** `shared/svg_player_api.cpp` line ~360

**Implementation:**
```cpp
const char* SVGPlayer_GetElementProperty(SVGPlayerHandle handle,
                                         const char* elementId,
                                         const char* propertyName) {
    // Query SVG DOM for element attributes
    // Return property value as string
}
```

### 4.3 Debug Overlay Customization (TODO)

**File:** `shared/svg_player_api.cpp` line ~390

**Implementation:**
```cpp
void SVGPlayer_ConfigureDebugOverlay(SVGPlayerHandle handle,
                                      uint32_t flags) {
    // Enable/disable specific overlay components:
    // OVERLAY_FPS, OVERLAY_MEMORY, OVERLAY_FRAME_INFO, etc.
}
```

---

## Implementation Schedule

### Week 1-2: GPU Backends (Critical)

| Day | Task | Platform |
|-----|------|----------|
| 1-2 | Create `gl_context_linux.cpp` | Linux |
| 3-4 | Integrate GL backend with player | Linux |
| 5 | Test and benchmark Linux GL | Linux |
| 6-8 | Create `vulkan_context_windows.cpp` | Windows |
| 9-10 | Integrate Vulkan with Windows player | Windows |

### Week 3: Desktop Feature Parity

| Day | Task | Platform |
|-----|------|----------|
| 1 | Add missing keyboard shortcuts | Windows |
| 2 | Add CLI options (--pos, --size, etc.) | Windows |
| 3 | Add benchmark mode | Windows |
| 4 | Port remote control server | Windows |
| 5 | Add remaining Linux features | Linux |

### Week 4: iOS SDK Completion

| Day | Task | Platform |
|-----|------|----------|
| 1-2 | Implement delegate pattern | iOS |
| 3 | Implement element touch events | iOS |
| 4 | Implement fullscreen support | iOS |
| 5 | Update demo app, testing | iOS |

### Week 5: Polish and Testing

| Day | Task |
|-----|------|
| 1-2 | Cross-platform testing matrix |
| 3 | Performance benchmarks all platforms |
| 4 | Documentation updates |
| 5 | Release preparation |

---

## Feature Parity Matrix (Target State)

After completing all phases:

| Feature | macOS | Linux | Windows | iOS |
|---------|:-----:|:-----:|:-------:|:---:|
| **Core Playback** | ✅ | ✅ | ✅ | ✅ |
| **GPU Acceleration** | ✅ Metal | ✅ OpenGL | ✅ Vulkan | ✅ Metal |
| **Folder Browser** | ✅ | ✅ | ✅ | N/A |
| **Keyboard Shortcuts** | ✅ | ✅ | ✅ | N/A |
| **CLI Options** | ✅ | ✅ | ✅ | N/A |
| **Remote Control** | ✅ | ✅ | ✅ | N/A |
| **Benchmark Mode** | ✅ | ✅ | ✅ | N/A |
| **Touch Events** | N/A | N/A | N/A | ✅ |
| **Delegate Callbacks** | N/A | N/A | N/A | ✅ |
| **Fullscreen** | ✅ | ✅ | ✅ | ✅ |

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Vulkan driver issues on older Windows | Medium | Medium | Maintain CPU fallback, D3D12 alternative |
| EGL compatibility on headless Linux | Low | Low | Mesa software renderer fallback |
| iOS App Store review delays | Low | Medium | Early submission, compliance check |
| Skia GPU backend bugs | Low | High | Pin Skia version, extensive testing |

---

## Success Criteria

### Phase 1 Complete When:
- [ ] Linux player achieves 60fps on 1080p complex SVGs (GPU mode)
- [ ] Windows player achieves 60fps on 1080p complex SVGs (GPU mode)
- [ ] Both fall back gracefully to CPU mode

### Phase 2 Complete When:
- [ ] All keyboard shortcuts work identically on all desktop platforms
- [ ] All CLI options work identically on all desktop platforms
- [ ] Remote control server works on all desktop platforms

### Phase 3 Complete When:
- [ ] iOS delegate callbacks fire for all player events
- [ ] Touch events report correct element IDs
- [ ] Fullscreen works on iPhone and iPad

### Phase 4 Complete When:
- [ ] All 146 API functions fully implemented
- [ ] No TODOs remain in shared API

---

## Appendix: Audit Reports

Detailed findings for each platform are available in:

- `/docs_dev/audit_macos_player.md`
- `/docs_dev/audit_linux_player.md`
- `/docs_dev/audit_windows_player.md`
- `/docs_dev/audit_ios_sdk.md`
- `/docs_dev/audit_shared_components.md`

---

**Plan Author:** Claude Code (Orchestrator)
**Last Updated:** 2026-01-19
