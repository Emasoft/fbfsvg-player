# SKIA-BUILD-ARM64 Project Overview

## Purpose
This project contains a real-time SVG player with SMIL animation support, built using Google's Skia graphics library and SDL2. It's designed for playing frame-by-frame animations (FBF format) commonly used in SVG sprite sheet animations.

## Tech Stack
- **Language**: C++17
- **Graphics Library**: Google Skia (built from source, Chrome m140 branch)
- **Window/Input**: SDL2
- **Platform**: macOS (ARM64 and x64)
- **Build System**: Shell scripts + clang++

## Key Features
- SMIL animation parsing and playback (discrete frame animations)
- Threaded rendering with consumer-producer pattern
- Pre-buffering for smooth animation playback
- Debug overlay with real-time performance metrics
- HiDPI/Retina display support
- Fullscreen mode (exclusive)
- Screenshot capture (PPM format)

## Directory Structure
```
SKIA-BUILD-ARM64/
├── skia-build/
│   ├── examples/                    # Custom applications
│   │   ├── svg_player_animated.cpp  # Main animated SVG player
│   │   ├── svg_player.cpp           # Simple SVG viewer
│   │   ├── svg_render.cpp           # SVG to PNG renderer
│   │   └── *.cpp                    # Other examples
│   ├── src/skia/                    # Skia library source
│   │   ├── include/                 # Skia headers
│   │   ├── modules/svg/             # SVG module
│   │   └── out/release-macos/       # Built static libraries
│   ├── depot_tools/                 # Google's build tools
│   ├── build-macos*.sh              # Build scripts
│   └── README.md
└── svg_input_samples/               # Test SVG files
    ├── girl_hair/
    ├── seagull/
    └── *.fbf.svg                    # FBF animation files
```

## Main Components (svg_player_animated.cpp)
- `SMILAnimation`: SMIL animation data structure
- `SkiaParallelRenderer`: Multi-threaded frame pre-buffering
- `ThreadedRenderer`: Background rendering with double-buffering
- `RollingAverage`: Performance metric calculator
- `main()`: Event loop, animation timing, debug overlay

## Keyboard Controls
- `V`: Toggle VSync
- `F`: Toggle frame rate limiter
- `P`: Toggle parallel pre-buffer mode
- `D`: Toggle debug overlay
- `Space`: Pause/resume animation
- `R`: Reset animation
- `S`: Toggle stress test
- `Enter`: Toggle fullscreen
- `C`: Take screenshot
- `Q`/`Escape`: Quit
