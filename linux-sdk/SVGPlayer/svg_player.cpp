// svg_player.cpp - Linux SVG Player SDK Implementation
//
// This file serves as a compilation unit for the Linux SDK build.
// The actual implementation is in shared/svg_player_api.cpp
//
// Build systems should compile shared/svg_player_api.cpp directly,
// or include this file which simply re-exports the unified implementation.

// Enable DLL export for the unified API
#define SVG_PLAYER_BUILDING_DLL

// Include the unified implementation
// This makes all the API functions available when this file is compiled
#include "../../shared/svg_player_api.cpp"

// Note: By including the .cpp file directly, we get all the unified
// implementation compiled into the Linux shared library (.so).
//
// Alternative approach for larger projects:
//   - Add shared/svg_player_api.cpp to your build system directly
//   - Link against the shared library (libsvgplayer.so)
//
// This file exists for backward compatibility with build systems
// that expect a svg_player.cpp in the linux-sdk directory.
