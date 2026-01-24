// DirtyRegionTracker.cpp - Implementation of dirty region tracking
// See DirtyRegionTracker.h for documentation

#include "DirtyRegionTracker.h"
#include <algorithm>

namespace svgplayer {

void DirtyRegionTracker::initialize(size_t animationCount) {
    // Reserve space for expected number of animations
    // Actual states are created when setAnimationBounds() is called
    states_.reserve(animationCount);
    dirtyRectsCacheValid_ = false;
}

void DirtyRegionTracker::reset() {
    states_.clear();
    cachedDirtyRects_.clear();
    dirtyRectsCacheValid_ = false;
}

void DirtyRegionTracker::setAnimationBounds(const std::string& targetId, const DirtyRect& bounds) {
    // Create or update state for this target
    AnimationDirtyState& state = states_[targetId];
    state.targetId = targetId;
    state.cachedBounds = bounds;
    state.boundsValid = !bounds.isEmpty();
    // Don't reset frame indices or dirty flag - preserve tracking state
    dirtyRectsCacheValid_ = false;
}

bool DirtyRegionTracker::hasCachedBounds(const std::string& targetId) const {
    auto it = states_.find(targetId);
    if (it == states_.end()) return false;
    return it->second.boundsValid;
}

void DirtyRegionTracker::markDirty(const std::string& targetId, size_t newFrameIndex) {
    auto it = states_.find(targetId);
    if (it == states_.end()) {
        // Create state for unknown target (will have invalid bounds)
        AnimationDirtyState& state = states_[targetId];
        state.targetId = targetId;
        state.currentFrameIndex = newFrameIndex;
        state.isDirty = true;
        state.boundsValid = false;
    } else {
        AnimationDirtyState& state = it->second;
        // Only mark dirty if frame actually changed
        if (state.currentFrameIndex != newFrameIndex) {
            state.previousFrameIndex = state.currentFrameIndex;
            state.currentFrameIndex = newFrameIndex;
            state.isDirty = true;
        }
    }
    dirtyRectsCacheValid_ = false;
}

void DirtyRegionTracker::rebuildDirtyRects() const {
    cachedDirtyRects_.clear();

    for (const auto& [targetId, state] : states_) {
        if (state.isDirty && state.boundsValid) {
            // Expand bounds by margin for anti-aliasing artifacts
            DirtyRect rect = state.cachedBounds.expand(DIRTY_RECT_MARGIN);
            cachedDirtyRects_.push_back(rect);
        }
    }

    // Merge overlapping rectangles if we have too many
    // Simple approach: merge any that intersect until stable
    if (cachedDirtyRects_.size() > static_cast<size_t>(MAX_DIRTY_RECTS)) {
        bool merged = true;
        while (merged && cachedDirtyRects_.size() > 1) {
            merged = false;
            for (size_t i = 0; i < cachedDirtyRects_.size() && !merged; ++i) {
                for (size_t j = i + 1; j < cachedDirtyRects_.size() && !merged; ++j) {
                    if (cachedDirtyRects_[i].intersects(cachedDirtyRects_[j])) {
                        // Merge j into i
                        cachedDirtyRects_[i] = cachedDirtyRects_[i].merge(cachedDirtyRects_[j]);
                        // Remove j by swapping with last and popping
                        cachedDirtyRects_[j] = cachedDirtyRects_.back();
                        cachedDirtyRects_.pop_back();
                        merged = true;
                    }
                }
            }
        }
    }

    dirtyRectsCacheValid_ = true;
}

std::vector<DirtyRect> DirtyRegionTracker::getDirtyRects() const {
    if (!dirtyRectsCacheValid_) {
        rebuildDirtyRects();
    }
    return cachedDirtyRects_;
}

DirtyRect DirtyRegionTracker::getUnionDirtyRect() const {
    if (!dirtyRectsCacheValid_) {
        rebuildDirtyRects();
    }

    if (cachedDirtyRects_.empty()) {
        return DirtyRect();  // Empty rect
    }

    DirtyRect result = cachedDirtyRects_[0];
    for (size_t i = 1; i < cachedDirtyRects_.size(); ++i) {
        result = result.merge(cachedDirtyRects_[i]);
    }
    return result;
}

float DirtyRegionTracker::getDirtyAreaRatio(float canvasWidth, float canvasHeight) const {
    if (canvasWidth <= 0.0f || canvasHeight <= 0.0f) return 0.0f;

    float canvasArea = canvasWidth * canvasHeight;
    DirtyRect unionRect = getUnionDirtyRect();

    if (unionRect.isEmpty()) return 0.0f;

    // Clamp to canvas bounds before calculating ratio
    DirtyRect clampedRect = unionRect.clamp(canvasWidth, canvasHeight);
    return clampedRect.area() / canvasArea;
}

bool DirtyRegionTracker::shouldUseFullRender(float canvasWidth, float canvasHeight) const {
    // No animations tracked - use full render (shouldn't happen but be safe)
    if (states_.empty()) return true;

    // Count dirty animations and check for invalid bounds
    size_t dirtyCount = 0;
    bool hasInvalidBounds = false;

    for (const auto& [targetId, state] : states_) {
        if (state.isDirty) {
            dirtyCount++;
            if (!state.boundsValid) {
                hasInvalidBounds = true;
            }
        }
    }

    // No dirty regions - nothing to render (caller should skip render entirely)
    // But return true to use full render path which handles this case
    if (dirtyCount == 0) return true;

    // Any dirty animation with invalid bounds - can't do partial render
    if (hasInvalidBounds) return true;

    // Rebuild dirty rects if needed
    if (!dirtyRectsCacheValid_) {
        rebuildDirtyRects();
    }

    // Too many dirty rectangles - merge overhead exceeds benefit
    if (cachedDirtyRects_.size() > static_cast<size_t>(MAX_DIRTY_RECTS)) return true;

    // Check dirty area ratio
    float dirtyRatio = getDirtyAreaRatio(canvasWidth, canvasHeight);

    // Dirty area exceeds threshold - full render is faster
    if (dirtyRatio > FULL_RENDER_THRESHOLD) return true;

    // Special case: single animation covering most of canvas
    // For FBF.SVG with full-canvas PROSKENION, partial render has no benefit
    if (states_.size() == 1 && dirtyCount == 1) {
        // If the single dirty rect covers >90% of canvas, use full render
        if (dirtyRatio > 0.9f) return true;
    }

    // All checks passed - partial render should be beneficial
    return false;
}

void DirtyRegionTracker::clearDirtyFlags() {
    for (auto& [targetId, state] : states_) {
        state.isDirty = false;
    }
    dirtyRectsCacheValid_ = false;
}

size_t DirtyRegionTracker::getDirtyCount() const {
    size_t count = 0;
    for (const auto& [targetId, state] : states_) {
        if (state.isDirty) count++;
    }
    return count;
}

size_t DirtyRegionTracker::getAnimationCount() const {
    return states_.size();
}

}  // namespace svgplayer
