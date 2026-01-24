# Linux Player Feature List

**Generated:** 2026-01-22  
**Source:** `src/svg_player_animated_linux.cpp`

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
| `--cpu` | Disable GPU rendering and force CPU raster mode (Graphite GPU backend is default) |

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
| **F** | Toggle frame limiter | Soft FPS cap at display refresh rate (when VSync off) |
| **G** | Toggle fullscreen | Same as F key |
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
| **O** | File operations | Context-dependent file actions |

---

## GPU Backend Support

### Graphite Backend (Default)

**Default:** Enabled by default on Linux

- **API:** Vulkan (Linux)
- **Status:** Default GPU backend for optimal performance
- **Features:**
  - Direct GPU rendering (bypasses CPU raster)
  - Direct GPU presentation (no SDL texture copy)
  - Automatic fallback to CPU raster on failure
- **Performance:** Optimal - eliminates CPU→GPU copy overhead
- **Disable with:** `--cpu` flag to force CPU raster mode

### CPU Raster Backend (Fallback)

**Enable with:** `--cpu` flag

- **API:** Skia CPU rasterizer
- **Fallback:** Automatic when Graphite initialization fails
- **Path:** Render → Copy to SDL texture → Present
- **Use case:** Debugging, systems without Vulkan support

**Backend indicator shown in debug overlay**

---

## Special Features

### 1. Folder Browser Mode

**Trigger:** Press **B** key

A visual file browser with live animated SVG thumbnails.

#### Architecture

- **Composite SVG Rendering:** All visible cells are combined into a single SVG document
- **Live Animations:** All thumbnails play their animations simultaneously at 60fps
- **Asynchronous Operations:** Non-blocking directory scanning and DOM parsing
- **Background Loading:** Threaded thumbnail loader for smooth UI

#### Browser Features

| Feature | Description |
|---------|-------------|
| **Pagination** | Navigate pages with LEFT/RIGHT or on-screen buttons |
| **Live Thumbnails** | SVG animations play in real-time in the grid |
| **Hover Highlighting** | Cells highlight on mouse hover |
| **Click Feedback** | Visual feedback on click |
| **Double-Click** | Opens SVG file or enters folder |
| **Sort Modes** | Alphabetical (A-Z) or Date (newest first) |
| **Navigation History** | Back/Forward buttons with history stack |
| **Breadcrumb Navigation** | Click path segments to jump to parent directories |
| **Selection State** | Highlighted selection with Load button |
| **Async Scanning** | Progress bar during directory scan |

#### Browser Entry Types

- **Parent Directory** (`..`) - Navigate to parent
- **Folder** - Enter subdirectory
- **SVG File** - Standard SVG file
- **FBF.SVG File** - Frame-by-frame SVG animation
- **Frame Folder** - Image sequence (NOT supported on Linux yet)
- **Volume** - System volume/drive

#### Browser Thread Safety

Multiple mutexes protect concurrent operations:
- `g_browserDomParseMutex` - Protects DOM parsing state
- `g_browserAnimMutex` - Protects active animations
- `g_browserPendingAnimMutex` - Protects pending animations
- `g_browserDomMutex` - Protects pending DOM
- `g_browserScanMessageMutex` - Protects scan progress messages

---

### 2. Parallel Rendering (PreBuffer Mode)

**Trigger:** Press **P** key

#### Modes

| Mode | Description | Performance |
|------|-------------|-------------|
| **Off** | Single-threaded, direct rendering | Baseline |
| **PreBuffer** | Multi-threaded frame pre-rendering | Optimal for complex animations |

#### PreBuffer Architecture

- **Worker Pool:** Dynamic thread pool (excludes 1 CPU core for system)
- **Per-Worker Cache:** Each worker has cached DOM and surface (parse once)
- **Frame Queue:** Pre-rendered frames queued for display
- **Time-Based Sync:** Supports multiple simultaneous animations
- **Atomic Flags:** Non-blocking mode checks from main thread

#### Performance Characteristics

- **Scales with CPU cores** - More cores = more parallel workers
- **Memory trade-off** - Caches DOM per worker thread
- **Ideal for:** High frame rate animations, complex SVG documents
- **Main thread never blocks** - Rendering happens asynchronously

---

### 3. Screenshot Capture

**Trigger:** Press **C** key

Captures the exact rendered frame at current resolution.

#### Specifications

| Property | Value |
|----------|-------|
| **Format** | PPM (Portable Pixmap) - uncompressed |
| **Max Size** | 32768x32768 pixels (1 gigapixel) |
| **Filename** | `screenshot_YYYYMMDD_HHMMSS_WxH.ppm` |
| **Source** | Direct from render buffer (not scaled) |

#### Features

- **Non-blocking** - Screenshot taken from cached frame
- **Independent** - Doesn't affect render state or frameReady flag
- **Timestamped** - Automatic filename with timestamp and resolution
- **Full resolution** - Captures at actual render resolution (respects HiDPI)

---

### 4. HiDPI / Retina Display Support

Automatic detection and adaptation for high-DPI displays.

#### Features

- **Automatic Detection:** SDL_WINDOW_ALLOW_HIGHDPI flag
- **Scale Factor Calculation:** `rendererPixels / windowLogicalPixels`
- **Scaled UI Elements:**
  - Debug font size scaled by HiDPI factor
  - Debug overlay dimensions scaled (40% larger base + HiDPI)
  - Line heights, padding, spacing all scaled
- **Coordinate Mapping:** Mouse coordinates scaled for hit testing

#### Debug Overlay Scaling

Base sizes increased by 40%, then multiplied by HiDPI scale:
- Font: 10pt × HiDPI scale (was 7pt)
- Line height: 13 × HiDPI scale (was 9)
- Padding: 3 × HiDPI scale (was 2)
- Label width: 112 × HiDPI scale (was 80)

---

### 5. VSync Control

**Trigger:** Press **V** key

Runtime VSync enable/disable with automatic renderer recreation.

#### Features

- **Dynamic Toggle:** VSync can be enabled/disabled at runtime
- **Renderer Recreation:** SDL renderer destroyed and recreated with new VSync state
- **Display Detection:** Automatically detects display refresh rate
- **Statistics Reset:** All performance stats reset after VSync change (prevents skewed data)

#### Behavior

| VSync State | Frame Limiter | Behavior |
|-------------|---------------|----------|
| **ON** | Ignored | Hardware VSync limits FPS to display refresh rate |
| **OFF** | ON | Soft limiter targets display refresh rate |
| **OFF** | OFF | Unlimited FPS (max performance) |

---

### 6. Frame Limiter

**Trigger:** Press **F** key

Soft frame rate limiter (active only when VSync is OFF).

#### Features

- **Display-Adaptive:** Targets detected display refresh rate (e.g., 60 Hz)
- **Soft Limiting:** Uses sleep to reduce CPU usage without hard blocking
- **Independent of VSync:** Only active when VSync is OFF
- **Stress Test Compatible:** Disabled during stress test mode

---

### 7. SVG Text Rendering

Proper font support for SVG `<text>` elements.

#### Features

- **Font Manager:** Platform-specific font manager initialization
- **System Fonts:** Access to system font directory
- **Text Rendering:** Full SVG text element support
- **Antialiasing:** Subpixel antialiasing for smooth text

---

### 8. Image Sequence Mode (NOT SUPPORTED)

⚠️ **Linux player does not support image sequence mode yet.**

- **Entry Type:** FrameFolder detected in browser
- **Status:** Displays message: "Use macOS player for frame sequences"
- **Planned:** TODO comments indicate future implementation

---

### 9. Remote Control Server

**Activation:** `--remote-control[=PORT]` flag (default port 9999)

A TCP/JSON server for programmatic control of the player.

#### Features

- **TCP Protocol:** JSON over TCP with newline-delimited messages
- **Default Port:** 9999 (configurable via `--remote-control=PORT`)
- **Commands:** Play, pause, stop, seek, fullscreen, maximize, screenshot, quit
- **State Queries:** Get current playback state, performance statistics
- **Non-blocking:** Server runs in background thread

#### Supported Commands

| Command | Description |
|---------|-------------|
| `ping` | Health check (returns "pong") |
| `play` | Resume animation playback |
| `pause` | Pause animation playback |
| `stop` | Stop and reset to beginning |
| `toggle_play` | Toggle play/pause state |
| `seek` | Seek to specific time (requires `time` param) |
| `fullscreen` | Toggle fullscreen mode |
| `maximize` | Toggle maximize/restore window |
| `set_position` | Set window position (requires `x`, `y` params) |
| `set_size` | Set window size (requires `width`, `height` params) |
| `get_state` | Get current player state (JSON response) |
| `get_stats` | Get performance statistics (JSON response) |
| `screenshot` | Capture screenshot (optional `path` param) |
| `quit` | Quit the player |

#### Example Usage

```bash
# Start player with remote control
svg_player_animated animation.svg --remote-control

# Connect with netcat and send commands
echo '{"cmd":"get_state"}' | nc localhost 9999
echo '{"cmd":"pause"}' | nc localhost 9999
echo '{"cmd":"seek","time":2.5}' | nc localhost 9999

# Use Python controller script
python scripts/svg_player_controller.py --port 9999
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

**Toggle:** Press **D** key

### Performance Metrics

#### FPS Metrics

| Metric | Description |
|--------|-------------|
| **FPS (avg)** | Average frames per second (cumulative) |
| **FPS (instant)** | Instantaneous FPS (rolling average) |
| **Skia FPS** | Frames delivered by render thread (with % ready hit rate) |
| **Frame time** | Average time per frame (milliseconds) |

#### Pipeline Timing Breakdown

All phases sum to total frame time. Percentage shows contribution to frame time.

| Phase | Description | When Active |
|-------|-------------|-------------|
| **Event** | SDL event processing time | Always |
| **Anim** | Animation state update time | Always |
| **Fetch** | Fetch frame from render thread | Always |
| **Wait Skia** | Idle time waiting for render thread | When render slower than display |
| **Overlay** | Debug overlay drawing time | When overlay enabled |
| **Copy** | Texture upload to SDL (CPU only) | CPU raster mode only |
| **Present** | SDL_RenderPresent / GPU present | Always |

#### Render Thread Metrics

| Metric | Description |
|--------|-------------|
| **Skia work** | Async render time in worker thread (min/max/avg) |
| **Active work** | Sum of main thread active phases |

#### Display Metrics

| Metric | Description |
|--------|-------------|
| **Resolution** | Current render resolution |
| **SVG size** | Native SVG document size |
| **Scale** | Render scale factor (fit-to-window) |
| **Frames** | Total frames displayed |

#### Animation Metrics

Only shown when animation is loaded:

| Metric | Description |
|--------|-------------|
| **Anim time** | Current animation time in seconds (shows PAUSED if paused) |
| **Anim frame** | Current frame / total frames |
| **Anim duration** | Total animation duration in seconds |
| **Frame** | Current frame position / total frames |
| **Remaining** | Frames remaining until animation end |
| **Frames shown** | Count of frames successfully rendered |
| **Frames skipped** | Count of frames skipped (performance issue indicator) |
| **Skip rate** | Percentage of frames skipped (highlighted if >10%) |
| **Anim target** | Animation's target FPS (from duration ÷ frame count) |

#### System Metrics

| Metric | Description |
|--------|-------------|
| **Threads** | Active threads / total threads |
| **CPU usage** | Process CPU usage percentage |
| **Backend** | Rendering backend (CPU Raster or GPU Graphite/Vulkan) |

#### Control Status

| Control | Display | Values |
|---------|---------|--------|
| **[V] VSync** | VSync state | ON / OFF |
| **[F] Limiter** | Frame limiter | ON (60 FPS) / OFF |
| **[P] Mode** | Parallel mode | PreBuffer / Off |
| **[SPACE] Animation** | Playback state | PLAYING / PAUSED |
| **[S] Stress test** | Stress test | ON (50ms delay) / OFF |

#### Shortcuts

Bottom of overlay shows quick reference:
```
[R] Reset stats  [D] Toggle overlay  [G] Fullscreen
```

---

## Performance Statistics Output

On exit, the player prints comprehensive timing statistics to stdout.

### Summary Format

```
--- Pipeline Timing (average) ---
Display FPS: XX.XX (main loop rate)
Skia FPS: XX.XX (frame delivery rate, XX.XX% hit rate)

Event:      XX.XX ms (XX.X%)
Anim:       XX.XX ms (XX.X%)
Fetch:      XX.XX ms (XX.X%)
Overlay:    XX.XX ms (XX.X%)
Copy:       XX.XX ms (XX.X%)
Present:    XX.XX ms (XX.X%)

Skia work (render thread): XX.XX ms
Sum phases: XX.XX ms (XX.X% of frame time)

Total uptime: XX.XXs
```

### Performance Bottleneck Detection

The player tracks the slowest phase each frame and reports culprits:
- EVENT (if event processing is slow)
- FETCH (if waiting for render thread)
- OVERLAY (if debug overlay is expensive)
- COPY (if texture upload is slow)
- PRESENT (if SDL_RenderPresent is slow)

---

## Browser Configuration

### Viewport-Based Sizing

Browser UI uses viewport-height (vh) units for consistent proportions:
- Each dimension is a fixed percentage of viewport height
- Ensures consistent UI at any resolution
- Adapts to window resize in real-time

### Real-Time Responsiveness

- **Window resize:** Browser regenerates SVG with new dimensions
- **Hover updates:** Non-blocking hover state changes
- **Click feedback:** Temporary visual feedback (auto-expires)
- **Progress updates:** Atomic scan progress for smooth progress bar

---

## Threading Model

### Main Thread (Event Loop)

- SDL event processing
- Animation state updates
- Fetch rendered frames (non-blocking)
- Draw debug overlay
- Present to screen
- **NEVER blocks on rendering**

### Render Thread (Threaded Renderer)

- Async SVG rendering in background
- Queues finished frames for main thread
- Runs continuously at animation FPS
- Uses atomic flags for lock-free status checks

### Browser Threads

- **Scan Thread:** Async directory scanning with progress callbacks
- **DOM Parse Thread:** Background SVG parsing (browser composite)
- **Thumbnail Loader:** Background thumbnail loading from disk cache

### Worker Pool (PreBuffer Mode)

- Dynamic worker threads (CPU cores - 1 reserved for system)
- Per-worker DOM and surface cache
- Parallel frame pre-rendering
- Atomic worker count tracking

---

## Supported File Formats

| Format | Extension | Description | Support Level |
|--------|-----------|-------------|---------------|
| **SVG** | `.svg` | Standard SVG 1.1/2.0 | Full |
| **FBF.SVG** | `.svg` | Frame-by-frame SVG (SMIL animations) | Full |
| **Frame Sequence** | Directory | Sequential SVG files as animation | ❌ Not supported |

---

## Build Configuration

### Dependencies

- SDL2 (windowing, events, rendering)
- Skia (SVG parsing, rendering)
- Vulkan SDK (for Graphite backend)
- OpenGL/EGL (fallback)

### Compile Flags

- C++17 required
- SMIL animation support enabled
- Graphite backend enabled by default (requires Vulkan SDK)

---

## Known Limitations

1. **PPM Screenshots Only:** No PNG/JPEG export yet
2. **Vulkan Requirement:** Graphite backend (default) requires Vulkan support (automatic fallback to CPU if unavailable)
3. **File Dialog:** Uses Zenity (GTK dialog) - requires `zenity` package installed

---

## Performance Tips

### For Best Performance

1. **Use Graphite (Default):** GPU acceleration enabled by default (disable with `--cpu` only for debugging)
2. **Enable PreBuffer:** Press **P** for parallel rendering (high core count CPUs)
3. **Disable VSync:** Press **V** if input latency is critical
4. **Use Frame Limiter:** Press **F** to cap FPS and reduce CPU usage (when VSync off)
5. **Hide Debug Overlay:** Press **D** (overlay rendering has cost)

### For Debugging Performance Issues

1. **Enable Debug Overlay:** Press **D** to see pipeline timing
2. **Check Skip Rate:** High skip rate indicates render thread too slow
3. **Check Skia FPS:** If <100% hit rate, rendering slower than display
4. **Check Phase Times:** Identify bottleneck (FETCH, COPY, PRESENT)
5. **Monitor CPU Usage:** Overlay shows real-time CPU % and thread count

---

## Exit Statistics

On clean exit (Q key or close window), the player prints:

- **Uptime:** Total session duration
- **Display FPS:** Main loop iteration rate
- **Skia FPS:** Frame delivery rate and hit rate
- **Phase Averages:** Average time for each pipeline phase
- **Percentages:** Each phase's contribution to total frame time

**Note:** Press **R** to reset statistics during session.
