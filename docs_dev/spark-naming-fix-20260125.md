# Spark: SVGPlayer→FBFSVGPlayer Naming Fix
Generated: 2026-01-25

## Change Made
Replaced all `SVGPlayer_` symbol references with `FBFSVGPlayer_` in CI/CD and build scripts.

## Files Modified
1. `.github/workflows/ci.yml` - 6 replacements
   - Lines 116, 119-121, 133, 135
2. `scripts/test-all.sh` - 3 replacements
   - Lines 262, 263, 310

## Total Replacements
**9 occurrences** updated across both files

## Verification
- Symbol checks: `FBFSVGPlayer_Create`, `FBFSVGPlayer_Destroy`, `FBFSVGPlayer_LoadSVG`
- dlsym lookups: `FBFSVGPlayer_Create`
- grep patterns: `FBFSVGPlayer_`

## Pattern Followed
Used `replace_all=true` for consistent global replacement across both files.
