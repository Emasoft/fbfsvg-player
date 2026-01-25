// svg_player.h - Windows SVG Player SDK (Stub)
//
// This header provides the Windows SDK for the SVG animation player.
// It forwards to the unified cross-platform API defined in shared/fbfsvg_player_api.h
//
// WINDOWS IMPLEMENTATION STATUS: STUB ONLY
// =========================================
// This header provides all API declarations for Windows compatibility,
// but the actual implementation requires Windows-specific setup:
// - DirectWrite for font rendering
// - D3D11 or D3D12 for GPU acceleration
// - Skia Windows build
//
// For now, this serves as:
// 1. Documentation of the full API surface
// 2. Compile-time reference for Windows development
// 3. Future implementation template
//
// Usage (when implemented):
//   1. Create a player: FBFSVGPlayer_Create()
//   2. Load an SVG file: FBFSVGPlayer_LoadSVG() or FBFSVGPlayer_LoadSVGData()
//   3. In your render loop:
//      - FBFSVGPlayer_Update() to advance animation time
//      - FBFSVGPlayer_Render() to render to a pixel buffer
//   4. Display the pixel buffer using your GUI toolkit (WPF, Win32, etc.)
//   5. Cleanup: FBFSVGPlayer_Destroy()
//
// Thread Safety:
//   - Each FBFSVGPlayerRef should only be used from one thread at a time
//   - Multiple FBFSVGPlayerRef instances can be used from different threads
//
// Memory:
//   - The caller is responsible for allocating/freeing the pixel buffer
//   - The pixel buffer must be width * height * 4 bytes (RGBA, 8-bit per channel)
//
// Copyright (c) 2024. MIT License.

#pragma once

// Include the unified cross-platform API
#include "../../shared/fbfsvg_player_api.h"

// ============================================================================
// Windows Platform Status
// ============================================================================

// Define this to 1 when the Windows implementation is complete
#define FBFSVG_PLAYER_WINDOWS_IMPLEMENTED 0

#if !FBFSVG_PLAYER_WINDOWS_IMPLEMENTED

// Compile-time warning for stub status
#ifdef _MSC_VER
    #pragma message("FBFSVGPlayer Windows SDK: Stub only - implementation pending")
#else
    #warning "FBFSVGPlayer Windows SDK: Stub only - implementation pending"
#endif

#endif // !FBFSVG_PLAYER_WINDOWS_IMPLEMENTED

// ============================================================================
// Windows-Specific Extensions (planned)
// ============================================================================
//
// These functions will be specific to the Windows SDK when implemented.
// They are declared here for API planning purposes.

#if 0 // Future Windows extensions - uncomment when implementing

/// Create an SVG player with Direct3D 11 rendering
/// @param device ID3D11Device pointer
/// @return Handle to the player, or NULL on failure
FBFSVG_PLAYER_API FBFSVGPlayerRef FBFSVGPlayer_CreateD3D11(void* device);

/// Create an SVG player with Direct3D 12 rendering
/// @param device ID3D12Device pointer
/// @return Handle to the player, or NULL on failure
FBFSVG_PLAYER_API FBFSVGPlayerRef FBFSVGPlayer_CreateD3D12(void* device);

/// Render directly to a D3D11 texture
/// @param player Handle to the player
/// @param texture ID3D11Texture2D pointer
/// @param scale HiDPI scale factor
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_RenderToD3D11Texture(FBFSVGPlayerRef player, void* texture, float scale);

/// Render directly to a D3D12 resource
/// @param player Handle to the player
/// @param resource ID3D12Resource pointer
/// @param scale HiDPI scale factor
/// @return true on success
FBFSVG_PLAYER_API bool FBFSVGPlayer_RenderToD3D12Resource(FBFSVGPlayerRef player, void* resource, float scale);

/// Set DirectWrite font fallback (for custom fonts)
/// @param player Handle to the player
/// @param fontCollection IDWriteFontCollection pointer
FBFSVG_PLAYER_API void FBFSVGPlayer_SetDWriteFontCollection(FBFSVGPlayerRef player, void* fontCollection);

#endif // Future Windows extensions

// ============================================================================
// Implementation Notes for Windows
// ============================================================================
//
// To implement the Windows SDK:
//
// 1. Build Skia for Windows:
//    - Use GN with Windows target
//    - Enable DirectWrite for fonts
//    - Enable D3D11/D3D12 backend
//
// 2. Create windows-sdk/SVGPlayer/svg_player.cpp:
//    - Similar to linux-sdk structure
//    - Include shared/svg_player_api.cpp
//    - Compile with FBFSVG_PLAYER_BUILDING_DLL defined
//
// 3. Build system (CMakeLists.txt or vcxproj):
//    - Link against Skia Windows libraries
//    - Link against DirectWrite, D3D11/D3D12
//    - Export as DLL (svgplayer.dll)
//
// 4. Windows-specific considerations:
//    - Use DirectWrite for font management
//    - Handle HRESULT errors appropriately
//    - Support both x64 and ARM64 (Windows on ARM)
//    - Consider WinRT/UWP compatibility
//
// Example implementation skeleton:
//
//   #define FBFSVG_PLAYER_BUILDING_DLL
//   #include "../../shared/svg_player_api.cpp"
//   #include "../../shared/SVGAnimationController.cpp"
//   #include "../../shared/SVGGridCompositor.cpp"
//   #include "../../shared/svg_instrumentation.cpp"
//
//   // Windows-specific extensions here

