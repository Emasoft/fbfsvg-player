# macOS Player Feature List

**Source:** `src/svg_player_animated.cpp`  
**Generated:** 2026-01-22

---

## Command-Line Flags

### Core Options
| Flag | Description |
|------|-------------|
| `-h`, `--help` | Show help message and exit |
| `-v`, `--version` | Show version information and exit |
| `-w`, `--windowed` | Start in windowed mode (default is fullscreen) |
| `-f`, `--fullscreen` | Start in fullscreen mode (default) |
| `-m`, `--maximize` | Start in maximized (zoomed) windowed mode |
| `--pos=X,Y` | Set initial window position (e.g., `--pos=100,200`) |
| `--size=WxH` | Set initial window size (e.g., `--size=800x600`) |

### Performance & Benchmarking
| Flag | Description |
|------|-------------|
| `--sequential` | Sequential frame mode: render frames 0,1,2,3... as fast as possible (ignores SMIL timing) |
| `--duration=SECS` | Benchmark mode: run for N seconds then exit |
| `--json` | Output benchmark stats as JSON (for scripting) |
| `--screenshot=PATH` | Capture screenshot to file (e.g., `--screenshot=output.ppm`) |

### GPU Backends
| Flag | Description |
|------|-------------|
| `--metal` | Enable Metal GPU backend (Ganesh) |
| `--cpu` | Disable GPU rendering, use software raster backend |

### Remote Control
| Flag | Description |
|------|-------------|
| `--remote-control[=PORT]` | Enable remote control server (default port: 9999) |

---

## Keyboard Shortcuts

### Playback Controls
| Key | Action | Notes |
|-----|--------|-------|
| **SPACE** | Pause/Resume animation | Toggles playback state |
| **R** | Restart animation from beginning | Resets timing state |
| **ESC** | Exit browser mode / Quit player | Context-dependent |
| **Q** | Quit player | Immediate exit |

### Display & Window Management
| Key | Action | Notes |
|-----|--------|-------|
| **F** / **G** | Toggle fullscreen mode | Exclusive fullscreen mode |
| **M** | Toggle maximize/restore (zoom) | Only works in windowed mode |
| **D** | Toggle debug overlay | Shows performance metrics |

### Performance & Rendering
| Key | Action | Notes |
|-----|--------|-------|
| **V** | Toggle VSync | Recreates renderer (CPU mode); controls CAMetalLayer in GPU mode |
| **T** | Toggle frame limiter | Limits to display refresh rate when enabled |
| **P** | Toggle parallel rendering mode | Off ↔ PreBuffer (not available in Metal mode) |
| **S** | Toggle stress test | Adds artificial 50ms delay per frame |

### Debugging & Utilities
| Key | Action | Notes |
|-----|--------|-------|
| **C** | Capture screenshot | Saves to timestamped file (format: `screenshot_YYYY-MM-DD_HH-MM-SS.ppm`) |
| **O** | Open file dialog | Hot-reload a new SVG file |
| **R** (again) | Reset statistics | Clears all performance counters |
| **B** | Toggle folder browser mode | Opens visual SVG browser |

### Browser Mode Navigation
| Key | Action | Notes |
|-----|--------|-------|
| **LEFT** | Previous page | Only in browser mode |
| **RIGHT** | Next page | Only in browser mode |
| **ESC** | Exit browser mode | Returns to player |

### Not Yet Implemented
| Key | Planned Action | Status |
|-----|----------------|--------|
| **UP** / **DOWN** | Speed up/slow down playback | Mentioned in help text, not implemented (TODO) |
| **L** | Toggle loop mode | Mentioned in help text, not implemented |
| **LEFT** / **RIGHT** | Seek backward/forward 1 second | Mentioned in help text (player mode) |

---

## GPU Backends

### Available Backends
| Backend | Technology | Flag | Notes |
|---------|------------|------|-------|
| **Graphite** | GPU-accelerated (next-gen) | (default) | Uses Skia's Graphite backend; falls back to Metal Ganesh if unavailable |
| **Metal (Ganesh)** | GPU-accelerated (current-gen) | `--metal` | Uses Skia's Ganesh backend with Metal |
| **CPU (Raster)** | Software rendering | `--cpu` | Uses Skia's raster backend |

### Backend Selection Logic
1. If `--cpu` is specified:
   - Use CPU raster backend
2. If `--metal` is specified:
   - Initialize Metal Ganesh context
   - If fails, fallback to CPU raster
3. Default: Graphite GPU backend
   - Try to initialize Graphite context
   - If fails, fallback to Metal (Ganesh)
   - If Metal fails, fallback to CPU raster

### VSync Control by Backend
| Backend | VSync Control Method |
|---------|---------------------|
| CPU | SDL renderer flags (recreate renderer to toggle) |
| Metal (Ganesh) | `MetalContext::setVSyncEnabled()` → `CAMetalLayer.displaySyncEnabled` |
| Graphite | `GraphiteContext::setVSyncEnabled()` → `CAMetalLayer.displaySyncEnabled` |

---

## Special Features

### 1. Folder Browser Mode
**Activation:** Press **B** key or command-line argument  
**Description:** Visual SVG file browser with live animated thumbnails

**Features:**
- Grid layout with animated SVG previews (all cells animate simultaneously)
- Breadcrumb navigation
- Back/forward history
- Sort by name or date
- File type detection (SVG, FBF.SVG, frame folders)
- Double-click to open files or enter folders
- Single-click to select
- Play arrow for frame sequence folders
- Hover highlighting
- Asynchronous scanning with progress indicator
- Thumbnail caching (background thread)

**Navigation:**
- **LEFT/RIGHT:** Previous/next page
- **Mouse:** Click buttons, breadcrumbs, or entries
- **ESC:** Exit browser mode

### 2. Image Sequence Mode
**Activation:** Load a folder containing SVG frames (via browser or command-line)  
**Description:** Plays a sequence of static SVG files as animation

**Features:**
- Automatic frame detection (naming patterns: `frame_0001.svg`, `001.svg`, etc.)
- Pre-loads all frames into memory for fast playback
- Sequential rendering (frames 0,1,2,3... in order)
- No SMIL animation processing (static frames only)

### 3. Remote Control Server
**Activation:** `--remote-control[=PORT]` (default port 9999)  
**Description:** TCP/JSON API for programmatic control

**Endpoints:**
- State query (playback state, frame info, dimensions)
- Statistics (render times, FPS, memory usage)
- Screenshot capture
- Error responses

**Usage:**
```bash
python scripts/svg_player_controller.py --port 9999
```

### 4. Benchmark Mode
**Activation:** `--duration=SECS --json`  
**Description:** Runs for specified duration then outputs JSON stats

**Output Includes:**
- Average/min/max FPS
- Frame times
- Render pipeline breakdown
- Frame skip statistics
- Hit rate (percentage of frames delivered on time)

### 5. Hot-Reload
**Activation:** Press **O** key  
**Description:** Opens file dialog to load a new SVG without restarting

**Features:**
- Preserves window state
- Resets animation timing
- Configures renderers automatically
- Validates SVG before loading

### 6. Stress Test
**Activation:** Press **S** key  
**Description:** Adds artificial 50ms delay to test sync behavior

**Purpose:**
- Verify animation stays time-synchronized under load
- Test frame skipping logic
- Validate freeze detection

### 7. Parallel Rendering Modes
**Modes:**
- **Off:** Single-threaded rendering
- **PreBuffer:** Multi-threaded with frame pre-buffering
- **Metal/Graphite:** GPU-accelerated (always active when enabled)

**Toggle:** Press **P** key (not available in GPU modes)

---

## Debug Overlay Content

**Toggle:** Press **D** key  
**Location:** Top-left corner  
**Scaling:** 1.4x base size to match font

### Performance Metrics
| Section | Metrics Displayed |
|---------|-------------------|
| **FPS** | Average FPS, Instant FPS, Skia worker FPS, Frame time |
| **Pipeline Timing** | Event, Anim, Fetch, Wait Skia (with hit rate), Overlay, Copy, Present |
| **Skia Work** | Worker thread render time (min/max/avg) |
| **Active Work** | Sum of all active phases (excludes idle time) |

### Rendering Info
| Section | Metrics Displayed |
|---------|-------------------|
| **Resolution** | Current render resolution (with HiDPI indicator) |
| **SVG Size** | Original SVG dimensions |
| **Scale** | Scaling factor applied |
| **Frames** | Total animation frames |

### Animation Info
| Section | Metrics Displayed |
|---------|-------------------|
| **Animation Mode** | Once / Loop / PingPong / Count |
| **Remaining** | Frames and time until cycle completes |
| **Frames Shown** | Count of delivered frames |
| **Frames Skipped** | Count and percentage |
| **Skip Rate** | Frames per second being skipped |
| **Anim Target** | Target FPS for animation |
| **Screen** | Display refresh rate (with VRR indicator) |

### System Info
| Section | Metrics Displayed |
|---------|-------------------|
| **VSync** | ON / OFF |
| **Fullscreen** | ON / OFF |
| **Mode** | Off / PreBuffer / Metal / Graphite |
| **Threads** | CPU core count |
| **CPU Usage** | Percentage (estimate based on active work) |

### Runtime State
| Section | Metrics Displayed |
|---------|-------------------|
| **Animation** | PLAYING / PAUSED |
| **Stress Test** | ON (50ms delay) / OFF |

### Keyboard Hints
| Section | Content |
|---------|---------|
| **Controls** | `[R] Reset stats  [D] Toggle overlay  [G] Fullscreen` |

### Visual Design
- **Background:** Black semi-transparent rectangle
- **Text Colors:**
  - White: Labels
  - Cyan: Highlighted metrics (FPS avg, Wait Skia, frames skipped)
  - Gray: Secondary info
- **Layout:** Two-column format (label: value)
- **Font:** Menlo (monospace) with fallbacks to Monaco, Courier New, Courier

---

## Environment Variables

### Debug Flags
| Variable | Effect |
|----------|--------|
| `RENDER_DEBUG` | Enables verbose rendering debug output |

---

## Supported Input Formats

### SVG Files
| Format | Description |
|--------|-------------|
| **FBF.SVG** | Frame-by-frame SVG with SMIL animations (animated vector video format) |
| **SVG 1.1/2.0** | Standard SVG with or without SMIL animations |

### Image Sequences
| Format | Description |
|--------|-------------|
| **Frame Folders** | Directory containing numbered SVG files (e.g., `frame_0001.svg`) |

### Naming Patterns Recognized
- `frame_####.svg` (zero-padded)
- `####.svg` (numbers only)
- `frame####.svg` (no underscore)

---

## Limitations & Known Issues

### Not Yet Implemented
1. **Playback speed control** (UP/DOWN keys planned)
2. **Loop mode toggle** (L key planned)
3. **Seek backward/forward** (LEFT/RIGHT keys in player mode)
4. **Screenshot in Metal mode** (requires GPU pixel readback)

### Platform-Specific Notes
- **macOS only:** Metal and Graphite backends
- **HiDPI scaling:** Automatically detected via `SDL_Metal_GetDrawableSize()`
- **Fullscreen:** Exclusive fullscreen mode (takes over display)

### Performance Considerations
- **GPU modes:** No threaded renderer (GPU is always active)
- **Browser mode:** All visible thumbnails animate simultaneously
- **Image sequences:** Pre-loads all frames into RAM (can be memory-intensive)

---

## Build Configuration

### Compiler Flags Required
- C++17 or later
- Skia headers and libraries
- SDL2 with Metal support (macOS)
- Objective-C++ support (`-x objective-c++`)

### Optional Dependencies
- Graphite backend (requires Skia built with Graphite support)
- Metal backend (requires macOS 10.14+)

---

## Related Components

### Shared Libraries
| Component | File | Purpose |
|-----------|------|---------|
| SVG Animation Controller | `shared/SVGAnimationController.cpp` | Core animation logic |
| Folder Browser | `src/folder_browser.cpp` | Visual browser implementation |
| Remote Control | `src/remote_control.cpp` | TCP/JSON API server |
| Thumbnail Cache | `src/thumbnail_cache.cpp` | Background SVG loading |
| Metal Context | `src/graphite_context_metal.mm` | Metal GPU setup (Ganesh) |
| Graphite Context | `src/graphite_context.h` | Graphite GPU setup |

### Python Controllers
| Script | Purpose |
|--------|---------|
| `scripts/svg_player_controller.py` | Remote control client |
| `scripts/svg_id_prefixer.py` | SVG ID prefixing for compositing |

---

## Statistics & Profiling

### Frame Pipeline Breakdown
| Phase | Description |
|-------|-------------|
| **Event** | SDL event processing time |
| **Anim** | Animation state update time |
| **Fetch** | Time to retrieve frame from threaded renderer |
| **Wait Skia** | Idle time waiting for worker thread |
| **Overlay** | Debug overlay rendering time |
| **Copy** | Pixel copy to SDL texture time (CPU mode only) |
| **Present** | SDL_RenderPresent time (display submission) |
| **Skia work** | Worker thread SVG rendering time |

### Black Screen Detection
- Samples every 100th pixel
- Excludes debug overlay area (300x400 at HiDPI)
- Logs warning if frame is entirely black (likely rendering issue)

### Freeze Detection
- **Warning:** After 2 seconds of stuck frame
- **Fatal Exit:** After 5 seconds (with stack trace)
- Skipped when animation is paused
- Skipped in Metal mode (no threaded renderer)

---

## JSON API Schema

### State Endpoint
```json
{
  "isPlaying": true,
  "isPaused": false,
  "currentFrame": 42,
  "totalFrames": 120,
  "totalDuration": 4.0,
  "playbackSpeed": 1.0,
  "svgWidth": 1920,
  "svgHeight": 1080,
  "loadedFile": "/path/to/file.svg"
}
```

### Statistics Endpoint
```json
{
  "renderTimeMs": 8.5,
  "updateTimeMs": 0.2,
  "animationTimeMs": 0.1,
  "currentFrame": 42,
  "totalFrames": 120,
  "fps": 60.0,
  "peakMemoryBytes": 52428800,
  "elementsRendered": 0
}
```

### Screenshot Endpoint
```json
{
  "success": true,
  "path": "/path/to/screenshot.ppm",
  "width": 1920,
  "height": 1080
}
```

### Error Response
```json
{
  "error": "Error message here"
}
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success (normal exit or benchmark complete) |
| 1 | Error (file not found, invalid format, initialization failure) |
| 2 | Fatal freeze detected (animation stuck for >5s) |

---

## Memory Management

### Auto-Cleanup
- SDL resources destroyed on exit
- GPU contexts properly released (Metal/Graphite)
- Threaded renderer shutdown with timeout
- Browser thread pool cleanup

### Signal Handlers
- `SIGTERM` → graceful shutdown
- `SIGINT` → graceful shutdown
- Atomic shutdown flag prevents race conditions
- Re-installs handlers after Metal context creation

---

## Version Information

**Version String Format:**
```
fbfsvg-player v1.0.0
Skia version: <skia_version>
SDL version: <sdl_version>
```

---

## Future Roadmap

### Planned Features
1. Playback speed control (0.25x, 0.5x, 1x, 2x, 4x)
2. Loop mode toggle (Once, Loop, PingPong)
3. Timeline scrubbing (seek to arbitrary time)
4. Export to video formats (MP4, WebM)
5. Layer visibility controls
6. Element inspection mode
7. Performance profiler overlay

### Experimental Features
- Graphite backend (next-gen GPU)
- Remote control API extensions
- Multi-file comparison view
- Frame diff visualization

---

**Last Updated:** 2026-01-22
