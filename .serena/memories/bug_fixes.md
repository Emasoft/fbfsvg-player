# Bug Fix Log

## Composite SVG Rendering Issue (2025-12-24)

### Issue
The composite SVG file `svg_input_samples/girl_hair_with_composite.fbf.svg` had two rendering issues:
1. Main animation's lower-right quadrant appeared white/empty
2. Embedded seagull animation was not visible

### Root Cause
**File:** `svg_input_samples/girl_hair_with_composite.fbf.svg`
**Line:** 49816

The `emb_PROSKENION` animate element incorrectly referenced main FRAME definitions instead of embedded frame definitions.

```xml
<!-- BEFORE (broken) -->
<animate ... values="#FRAME00001;#FRAME00002;...#FRAME00010" />

<!-- AFTER (fixed) -->
<animate ... values="#emb_FRAME00001;#emb_FRAME00002;...#emb_FRAME00010" />
```

### Why This Caused Rendering Issues
When the embedded animation referenced main FRAME definitions (1200x674 sized frames) instead of its own `emb_FRAME*` definitions, Skia's SVG renderer experienced conflicts that affected the parent SVG context's rendering, causing the main animation's lower-right quadrant to be clipped.

### Verification Results
- Before fix: Lower-right quadrant = 8.5KB (mostly background color)
- After fix: Lower-right quadrant = 325KB (full animation content)
- Both main animation and embedded seagull animation now render correctly

### Investigation Steps Taken
1. Captured screenshots comparing original vs composite file
2. Extracted and compared lower-right quadrants (size difference confirmed issue)
3. Created test version without embedded_composite (verified it fixed the issue)
4. Created test version with fixed frame references (verified it fixed the issue)
5. Applied fix to actual file and confirmed both animations render correctly
