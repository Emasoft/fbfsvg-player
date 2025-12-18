# Code Style and Conventions

## C++ Style

### Language Standard
- C++17 (use `-std=c++17` flag)

### Naming Conventions
- **Classes/Structs**: PascalCase (`ThreadedRenderer`, `SMILAnimation`, `RollingAverage`)
- **Functions**: camelCase (`getProcessCPUStats`, `parseDuration`, `extractAttribute`)
- **Variables**: camelCase (`renderWidth`, `frameCount`, `animationStartTime`)
- **Constants**: SCREAMING_SNAKE_CASE (`MAX_BUFFER_SIZE`, `RENDER_TIMEOUT_MS`)
- **Member variables**: No prefix, camelCase (`frontBuffer`, `activeWorkers`)
- **Private members**: trailing underscore (`values_`, `maxSize_`)

### Indentation
- 4 spaces (no tabs)

### Braces
- K&R style for control structures
- Opening brace on same line

### Comments
- Use `//` for single-line comments
- Use `/* */` for section headers
- Document WHY, not just WHAT
- Include section markers: `// === SECTION NAME ===`

### Includes
Order:
1. Project headers (Skia)
2. System headers (SDL, chrono, etc.)
3. Standard library

### Threading
- Use `std::atomic` for thread-safe flags
- Use `std::mutex` with `std::lock_guard` for critical sections
- Prefer non-blocking patterns (consumer-producer)
- Always document thread safety requirements

## Architecture Patterns

### Consumer-Producer Pattern
- Render thread produces frames in background
- Main thread consumes frames when ready
- Use atomic flags for frame ready state
- Never block main thread on rendering

### Double Buffering
- Front buffer for display
- Back buffer for rendering
- Atomic swap when frame is ready

### Performance Measurement
- Use `std::chrono::steady_clock` for timing
- Track per-phase metrics (Event, Anim, Fetch, Copy, Present)
- Use rolling averages for stable display

## Error Handling
- Print errors to `std::cerr`
- Check return values from SDL/Skia functions
- Clean up resources on error (RAII where possible)
- Return error codes from main()

## Debug Overlay
- Display metrics in real-time
- Use monospace font (Menlo/Courier)
- Scale for HiDPI displays
- Color-code warnings/highlights
