# Windows Player Feature List

**Generated:** 2026-01-22  
**Source:** `src/svg_player_animated_windows.cpp`

---

## Command-Line Flags

### Core Options
| Flag | Short | Description |
|------|-------|-------------|
| `--help` | `-h` | Show help message and exit |
| `--version` | `-v` | Show version information and exit |
| `--windowed` | `-w` | Start in windowed mode (default is fullscreen) |
| `--fullscreen` | `-f` | Start in fullscreen mode (default) |
| `--maximize` | `-m` | Start in maximized (zoomed) windowed mode |
| `--pos=X,Y` | - | Set initial window position (e.g., `--pos=100,200`) |
| `--size=WxH` | - | Set initial window size (e.g., `--size=800x600`) |

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
| `--cpu` | Disable GPU rendering and use CPU raster mode (Graphite is default) |

### Remote Control
| Flag | Description |
|------|-------------|
| `--remote-control[=PORT]` | Enable remote control server (default port: 9999) |

### Usage Examples

```bash
svg_player_animated animation.svg
svg_player_animated animation.svg --fullscreen
svg_player_animated animation.svg --windowed --size=1280x720
svg_player_animated animation.svg --cpu
svg_player_animated animation.svg --sequential --duration=10 --json
svg_player_animated animation.svg --remote-control
svg_player_animated animation.svg --remote-control=8080
svg_player_animated --version
```

---

## Keyboard Shortcuts

### Navigation & Control

| Key | Action | Notes |
|-----|--------|-------|
| **ESCAPE** | Exit browser / Quit | Exits browser mode if active, otherwise quits application |
| **Q** | Quit | Always quits, even in browser mode |
| **LEFT** | Previous page | Browser mode only |
| **RIGHT** | Next page | Browser mode only |

### Playback Control

| Key | Action | Notes |
|-----|--------|-------|
| **SPACE** | Play / Pause | Toggle animation playback |
| **R** | Reset | Reset animation and statistics |
| **L** | Toggle loop mode | Cycle through repeat modes |

### Display Modes

| Key | Action | Notes |
|-----|--------|-------|
| **T** | Toggle frame limiter | Soft FPS cap at display refresh rate (when VSync off) |
| **F** / **G** | Toggle fullscreen | Exclusive fullscreen mode |
| **M** | Toggle maximize | Maximize/restore window (windowed mode only) |
| **V** | Toggle VSync | Runtime VSync enable/disable |
| **D** | Toggle debug overlay | Show/hide performance statistics |
| **B** | Toggle browser mode | Open/close visual folder browser |

### Performance & Rendering

| Key | Action | Notes |
|-----|--------|-------|
| **P** | Toggle parallel mode | PreBuffer (parallel) vs Off (single-threaded) |
| **S** | Toggle stress test | Artificial 50ms delay to test frame skipping |
| **C** | Capture screenshot | Save as PPM format (uncompressed) |

### File Operations

| Key | Action | Notes |
|-----|--------|-------|
| **O** | Open file dialog | Hot-reload a new SVG file |

---

## GPU Backends

### Graphite Backend (Default)
- **Default:** Enabled by default on Windows
- **Disabled with:** `--cpu` flag
- **Technology:** Vulkan on Windows
- **Features:**
  - Next-generation GPU rendering pipeline
  - Direct Vulkan swapchain presentation
  - No CPU-GPU copy overhead (GPU-only path)
  - Falls back to CPU raster if initialization fails
  - Separate surface creation and frame submission

**Status Messages:**
- Success: `[Graphite] Next-gen GPU backend enabled - Vulkan rendering active`
- Failure: `[Graphite] Failed to initialize Graphite context (Vulkan), falling back to CPU raster`

### CPU Raster (Fallback/Opt-In)
- **Enabled with:** `--cpu` flag or automatic fallback if Graphite fails
- **Technology:** Software rendering via Skia
- **Features:**
  - Direct2D renderer integration (SDL2)
  - Works on all Windows systems
  - No GPU dependencies

---

## Special Features

### 1. Folder Browser Mode
- **Activation:** Press `B` key
- **Purpose:** Visual SVG file navigation with live animated thumbnails
- **Architecture:** Composite animated SVG with all cells playing simultaneously
- **Features:**
  - Multi-threaded directory scanning with progress bar
  - Background thumbnail loading (async, non-blocking)
  - Cell-based grid layout with animations
  - Navigation controls (back, forward, sort, pagination)
  - Breadcrumb path display
  - File type detection (SVG, FBF.SVG, folders, frame folders)
  - Selection highlighting with hover states
  - Cancel and Load buttons
  - Sort modes: Alphabetical (A-Z) ↔ Date

**Browser Hit Test Zones:**
- Entry cells (clickable)
- Cancel button
- Load button
- Back/Forward buttons
- Sort button
- Previous/Next page buttons
- Breadcrumb path
- Play arrow (for frame folders)

**Supported Entry Types:**
- Parent directory (`..`)
- Volumes/drives
- Regular folders
- SVG files
- FBF.SVG files (Frame-by-Frame SVG)
- Frame folders (image sequences)

**Note:** Frame folder/image sequence playback is NOT YET IMPLEMENTED on Windows (shows message: "Frame sequence folders not yet supported. Use macOS player.")

### 2. Parallel Rendering Modes
- **Toggle:** Press `P` key
- **Modes:**
  - **Off:** Single-threaded rendering
  - **PreBuffer:** Pre-render animation frames ahead for smooth playback
- **Worker Configuration:**
  - Default: 8 worker threads
  - Frame cache: 120 frames (2 seconds at 60fps)
  - Priority queue for future frames
  - Non-blocking cache access

### 3. Frame Limiter
- **Toggle:** Press `F` key
- **Purpose:** Soft frame rate cap (target display refresh rate)
- **Behavior:**
  - Only active when VSync is OFF
  - Uses sleep-based pacing
  - Targets display refresh rate (default: 60 Hz)
  - Can be bypassed by stress test mode

### 4. Audio Sync Guarantee
- **Principle:** Frame shown = f(current_time), NOT f(frame_count)
- **Behavior:**
  - If rendering is slow, frames are skipped but CORRECT frame for current time is always shown
  - Guarantees audio sync even if frame rate drops to 1 FPS
- **Stress Test:** Press `S` to add 50ms artificial delay and verify sync works

### 5. Screenshot Capture
- **Key:** Press `C`
- **Format:** PPM (Portable Pixmap) - uncompressed
- **Filename Pattern:** `screenshot_YYYYMMDD_HHMMSS.ppm`
- **Max Resolution:** 32768x32768 pixels (1 gigapixel)
- **Location:** Current working directory

### 6. Hot-Reload
- **Key:** Press `O`
- **Features:**
  - Native Windows file dialog
  - Load new SVG without restarting player
  - Safely stops renderers before loading
  - Resets all stats and timers
  - Updates window title with new filename

### 7. VSync Control
- **Toggle:** Press `V` key
- **Behavior:**
  - Recreates SDL renderer to apply setting
  - Resets all performance statistics
  - Changes swap behavior (tearing vs. synchronized)

### 8. Fullscreen Mode
- **Keys:** `F` or `G`
- **Type:** Exclusive fullscreen (SDL_WINDOW_FULLSCREEN)
- **Features:**
  - Direct display access (no compositor)
  - Uses native display resolution
  - Seamless toggle (preserves state)

### 9. Remote Control Server
- **Activation:** `--remote-control[=PORT]` flag (default port 9999)
- **Protocol:** TCP/JSON, newline-delimited messages
- **Purpose:** Programmatic control of the player from external tools

**Features:**
- Non-blocking TCP server running in background thread
- Multiple simultaneous client connections
- JSON request/response protocol
- Auto-cleanup on client disconnect

**Supported Commands:**

| Command | Description |
|---------|-------------|
| `ping` | Health check, returns `{"status":"ok"}` |
| `play` | Start/resume animation playback |
| `pause` | Pause animation playback |
| `stop` | Stop animation and reset to beginning |
| `toggle_play` | Toggle between play and pause states |
| `seek` | Seek to specific time (params: `time` in seconds) |
| `fullscreen` | Toggle fullscreen mode |
| `maximize` | Toggle maximized window mode |
| `set_position` | Set window position (params: `x`, `y`) |
| `set_size` | Set window size (params: `width`, `height`) |
| `get_state` | Get current player state (playing, frame, time, etc.) |
| `get_stats` | Get performance statistics (FPS, render times, etc.) |
| `screenshot` | Capture screenshot (params: `path` optional) |
| `quit` | Exit the player |

**Example Usage:**

```bash
# Start player with remote control
svg_player_animated animation.svg --remote-control

# Start with custom port
svg_player_animated animation.svg --remote-control=8080

# Send commands using netcat (PowerShell)
echo '{"cmd":"get_state"}' | nc localhost 9999

# Python controller script
python scripts/svg_player_controller.py --port 9999
```

**JSON Request Format:**
```json
{"cmd": "seek", "time": 2.5}
{"cmd": "set_position", "x": 100, "y": 200}
{"cmd": "screenshot", "path": "capture.ppm"}
```

**JSON Response Format:**
```json
{"status": "ok", "state": {"playing": true, "current_frame": 42, ...}}
{"status": "error", "message": "Invalid command"}
```

### 10. Image Sequence Mode
- **Activation:** Load a folder containing numbered SVG files (via browser or command-line)
- **Purpose:** Play a sequence of static SVG files as animation frames

**Features:**
- Automatic frame number detection from filenames
- Pre-loads all frames into memory for fast playback
- Sequential rendering (frames 0, 1, 2, 3... in order)
- Supports naming patterns: `frame_0001.svg`, `0001.svg`, `frame0001.svg`

**Loading Methods:**
1. **Command-line:** Pass a folder path instead of an SVG file
   ```bash
   svg_player_animated ./my_frames_folder/
   ```
2. **Browser mode:** Double-click or use play arrow on FrameFolder entries

**Frame Detection:**
- Scans folder for `.svg` files
- Extracts frame numbers from filenames
- Sorts frames numerically
- Falls back to alphabetical sort for unnumbered files

**Usage Example:**
```bash
# Folder structure:
# my_animation/
#   frame_0001.svg
#   frame_0002.svg
#   frame_0003.svg
#   ...

# Play as image sequence
svg_player_animated my_animation/

# With benchmark mode
svg_player_animated my_animation/ --sequential --duration=10 --json
```

---

## Debug Overlay Content

**Toggle with:** `D` key  
**Default State:** ON  
**Update Frequency:** Every frame (60fps typical)

### Performance Metrics

| Metric | Description |
|--------|-------------|
| **FPS (avg)** | Average frames per second (highlighted) |
| **FPS (instant)** | Instantaneous FPS from rolling average |
| **Skia FPS** | Frame delivery rate from Skia worker thread with hit rate % |
| **Frame Time (avg)** | Average time per frame in milliseconds |

### Pipeline Timing Phases

All phases sum to total frame time. Order: Event → Anim → Fetch → Overlay → Copy → Present

| Phase | Description | Percentage |
|-------|-------------|------------|
| **Event** | SDL event processing | % of frame time |
| **Anim** | Animation state updates | % of frame time |
| **Fetch** | Frame fetch from renderer | % of frame time |
| **Overlay** | Drawing debug overlay | % of frame time |
| **Copy** | CPU-GPU memory copy | % of frame time |
| **Present** | Display presentation | % of frame time |

**Culprit Detection:** Slowest phase is highlighted when total frame time exceeds 30ms (unless stress test is active)

### Animation Information

| Item | Description |
|------|-------------|
| **Frame** | Current frame / Total frames |
| **Time** | Current animation time in seconds |
| **Duration** | Total animation duration in seconds |
| **Mode** | Animation repeat mode (once, loop, pingpong, etc.) |
| **Speed** | Playback speed multiplier |
| **Author FPS** | Original animation frame rate |
| **Size** | SVG document dimensions |

### System Information

| Item | Description |
|------|-------------|
| **Frames shown** | Total frames presented to display |
| **Display cycles** | Total main loop iterations |
| **Hit rate** | Frame delivery success rate (%) |

### Render Mode Status

| Mode | Display |
|------|---------|
| **Parallel** | Shows mode (Off, PreBuffer) and active workers |
| **VSync** | ON / OFF |
| **Limiter** | ON (XX FPS) / OFF |

### CPU Statistics (Windows APIs)

| Stat | Description |
|------|-------------|
| **Threads** | Active threads / Total threads |
| **CPU Usage** | Process CPU usage percentage |

### Control Hints

Bottom of overlay shows key bindings:
```
[SPACE] Animation: PLAYING / PAUSED
[S] Stress test: ON (50ms delay) / OFF
[R] Reset stats  [D] Toggle overlay  [G] Fullscreen
```

---

## Playback Modes

### Animation States
- **Playing:** Animation actively progressing
- **Paused:** Animation frozen at current time
- **Stopped:** Reset to beginning

### Repeat Modes
**Toggle with:** `L` key

| Mode | Behavior |
|------|----------|
| **None** | Play once and stop |
| **Loop** | Repeat indefinitely from start |
| **Reverse** | Play backwards |
| **Pingpong** | Alternate forward/backward |

*(Additional modes exist - total count: SVGRepeatMode_Count)*

---

## File Format Support

### Directly Supported
- **SVG 1.1/2.0** - Standard vector graphics
- **FBF.SVG** - Frame-by-Frame SVG (discrete animations)
- **SMIL Animations** - Declarative animation tags
- **Frame Folders** - Image sequence directories (folders containing numbered SVG files)

### Naming Patterns Recognized (Image Sequences)
- `frame_####.svg` (zero-padded with underscore)
- `####.svg` (numbers only)
- `frame####.svg` (no underscore)

### Screenshot Export Format
- **PPM (Portable Pixmap)** - Uncompressed raster format

---

## Window Management

### Title Bar Display
- **Format:** `filename - XX.X FPS - SVG Player`
- **Update Frequency:** Every 500ms
- **FPS Source:** Rolling average of recent frame times (instantaneous)
- **Always visible:** Even when debug overlay is hidden

### HiDPI Support
- **Enabled:** `SDL_WINDOW_ALLOW_HIGHDPI` flag
- **Automatic scaling:** UI elements scale with display DPI
- **Debug font:** 10pt base, scaled for HiDPI

### Renderer Configuration
- **Backend:** Direct3D (SDL2 hardware renderer)
- **Batching:** Enabled for better throughput
- **Resizable:** Window can be resized dynamically

---

## Performance Features

### Statistics Tracking
- **Window Size:** 30 frames (~0.5 seconds at 60fps)
- **Rolling Averages:** All timing metrics use rolling windows
- **Reset Conditions:** Stats reset on mode changes, resets, file loads
- **Skip Mechanism:** `skipStatsThisFrame` flag excludes disruptive events from averages

### Thread Safety
- **Browser Mutexes:**
  - `g_browserDomParseMutex` - DOM parsing state
  - `g_browserPendingAnimMutex` - Pending animations
  - `g_browserAnimMutex` - Active animations
  - `g_browserDomMutex` - Pending DOM
  - `g_browserScanMessageMutex` - Scan progress messages

### Memory Management
- Pre-allocated frame buffers (parallel mode)
- Automatic cache eviction
- GPU resource cleanup (Graphite)
- Proper renderer shutdown sequence

---

## Limitations & Caveats

1. **PPM Screenshots Only:** No PNG/JPEG export yet
2. **Windows-Specific:** Uses Direct3D renderer, not portable
3. **Vulkan Requirement:** Graphite (default GPU backend) requires Vulkan runtime. Use `--cpu` flag on systems without Vulkan support.

---

## Technical Details

### Build Configuration
- **UTF-8 Console:** Automatically enabled on Windows
- **Version Info:** Accessible via `--version` flag
- **Startup Banner:** Always shown on execution

### Cleanup Sequence
1. Stop threaded renderer
2. Stop parallel renderer (if active)
3. Destroy Graphite context (if active)
4. SDL renderer cleanup
5. SDL window cleanup
6. SDL subsystem shutdown

### Error Handling
- **Fail-Fast:** No error handling - failures propagate
- **No Fallbacks:** Code works as intended or exits
- **Verbose Logging:** All errors printed to stderr

---

## Version Information

**Access with:** `--version` or `-v` flag

**Displays:**
- Version banner (from `SVGPlayerVersion::getVersionBanner()`)
- Build info (from `SVG_PLAYER_BUILD_INFO` constant)

---

## Related Components

### Headers & Dependencies
- `shared/SVGAnimationController.h` - Core animation logic
- `folder_browser.h` - Visual folder browser
- `thumbnail_cache.h` - Background thumbnail loading
- `graphite_context.h` - Vulkan GPU backend
- `file_dialog_windows.h` - Native file dialogs
- `remote_control.h` - Remote control API (if enabled)

### Cross-Platform Alignment
This feature list mirrors capabilities in:
- `src/svg_player_animated.cpp` (macOS)
- `src/svg_player_animated_linux.cpp` (Linux)

**Major Difference:** Windows player lacks image sequence/frame folder playback support.
