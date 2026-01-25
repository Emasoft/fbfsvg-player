# Quick Fix: Rename SVGPlayer to FBFSVGPlayer in Test Files
Generated: 2026-01-25

## Change Made
- Files: `tests/test_svg_player_api.cpp`, `tests/test_api_compile.cpp`
- Change: Updated all SVGPlayer API references to FBFSVGPlayer naming convention

## Verification
- Syntax check: Not required (header-only changes)
- Pattern followed: Global find-replace with `replace_all` flag

## Files Modified
1. `tests/test_svg_player_api.cpp`
   - `SVGPlayerRef` → `FBFSVGPlayerRef` (all occurrences)
   - `SVGPlayer_*` → `FBFSVGPlayer_*` (all function calls)
   - `SVG_PLAYER_API_VERSION_*` → `FBFSVG_PLAYER_API_VERSION_*` (version macros)

2. `tests/test_api_compile.cpp`
   - `SVGPlayerRef` → `FBFSVGPlayerRef` (all occurrences)
   - `SVGPlayer_*` → `FBFSVGPlayer_*` (all function calls)
   - `SVG_PLAYER_API_VERSION_*` → `FBFSVG_PLAYER_API_VERSION_*` (version macros)

## Notes
- Preserved enum types: `SVGPlaybackState`, `SVGRepeatMode`, `SVGRenderStats`
- Preserved callback types: `SVGStateChangeCallback`, etc.
- Only renamed player handle type and API functions as requested
