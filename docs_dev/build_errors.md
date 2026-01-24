# macOS Build Errors - 2026-01-01

## Compilation Error

**File:** `src/svg_player_animated.cpp:1474:32`

**Error:**
```
error: no member named 'MakeDefault' in 'SkTypeface'
1474 |         typeface = SkTypeface::MakeDefault();
```

**Fix needed:**
Replace `SkTypeface::MakeDefault()` with the correct Skia API for creating a default typeface. The current Skia version does not support this method.

---

## Warnings (Non-Critical)

**Files:**
- `shared/SVGAnimationController.h:95`
- `shared/SVGAnimationController.h:96`

**Warning:**
```
warning: explicitly defaulted move constructor is implicitly deleted [-Wdefaulted-function-deleted]
warning: explicitly defaulted move assignment operator is implicitly deleted [-Wdefaulted-function-deleted]
```

**Cause:**
The class has a `mutable std::mutex mutex_` member, which cannot be moved. Declaring `= default` move operations creates implicitly deleted functions.

**Fix needed:**
Either:
1. Change `= default` to `= delete` on lines 95-96
2. Or implement custom move operations that handle the mutex appropriately
3. Or remove the defaulted move operations entirely (rely on implicit behavior)
