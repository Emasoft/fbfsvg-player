# Graphite Build Errors - 2026-01-19

## Build Command
```bash
./scripts/build-macos-arch.sh arm64
```

## Errors in `src/graphite_context_metal.mm`

### Error 1: Missing MtlHandle type
```
/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/graphite_context_metal.mm:241:40: error: no member named 'MtlHandle' in namespace 'skgpu::graphite'
  241 |             (__bridge skgpu::graphite::MtlHandle)texture
      |                       ~~~~~~~~~~~~~~~~~^
```

### Error 2: Undeclared identifier
```
/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/graphite_context_metal.mm:255:13: error: use of undeclared identifier 'kTopLeft_GrSurfaceOrigin'
  255 |             kTopLeft_GrSurfaceOrigin,
      |             ^
```

### Error 3: Syntax error (consequence of Error 1)
```
/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/graphite_context_metal.mm:241:50: error: expected ')'
  241 |             (__bridge skgpu::graphite::MtlHandle)texture
```

## Analysis

The Graphite API has changed. The code uses:
- `skgpu::graphite::MtlHandle` - This type doesn't exist in current Skia
- `kTopLeft_GrSurfaceOrigin` - This is a Ganesh constant, not Graphite

### Likely fixes needed:
1. For Metal handle: Use `CFTypeRef` or check current Skia Graphite Metal headers
2. For surface origin: Graphite may use different origin constants or none at all

## Files to investigate
- Skia headers: `include/gpu/graphite/mtl/MtlGraphiteTypes.h`
- Current BackendTexture constructors in Skia Graphite
