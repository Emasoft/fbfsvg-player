// DirtyRegionTracker.h - Dirty region tracking for partial rendering optimization
// Platform-independent C++17 implementation
// Used by: macOS player, Linux player, iOS SDK, Linux SDK
//
// This class tracks which animated elements changed between frames and calculates
// dirty rectangles for partial canvas rendering. For animations where only a small
// portion of the canvas changes, partial rendering can provide 3-10x performance gains.
//
// Memory-efficient design: Only stores per-animation state (~80 bytes each),
// NOT per-frame data. Safe for 1-2 hour animations at 60fps (432,000 frames).

#ifndef DIRTY_REGION_TRACKER_H
#define DIRTY_REGION_TRACKER_H

#include <vector>
#include <string>
#include <unordered_map>
#include <cstddef>

namespace svgplayer {

/**
 * @brief Rectangle structure for dirty region tracking
 *
 * Platform-independent rectangle that can be converted to Skia SkRect.
 * Uses float coordinates to match SVG coordinate space.
 */
struct DirtyRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    // Default constructor - empty rect
    DirtyRect() = default;

    // Parameterized constructor
    DirtyRect(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_) {}

    // Check if rectangle has zero or negative area
    bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }

    // Calculate area (for coverage ratio calculations)
    float area() const { return width * height; }

    // Get right edge (x + width)
    float right() const { return x + width; }

    // Get bottom edge (y + height)
    float bottom() const { return y + height; }

    // Check if this rectangle intersects with another
    bool intersects(const DirtyRect& other) const {
        if (isEmpty() || other.isEmpty()) return false;
        return !(other.x >= right() || other.right() <= x || other.y >= bottom() || other.bottom() <= y);
    }

    // Check if this rectangle fully contains another
    bool contains(const DirtyRect& other) const {
        if (isEmpty() || other.isEmpty()) return false;
        return other.x >= x && other.right() <= right() && other.y >= y && other.bottom() <= bottom();
    }

    // Merge two rectangles into their union (bounding box)
    DirtyRect merge(const DirtyRect& other) const {
        if (isEmpty()) return other;
        if (other.isEmpty()) return *this;

        float newX = (x < other.x) ? x : other.x;
        float newY = (y < other.y) ? y : other.y;
        float newRight = (right() > other.right()) ? right() : other.right();
        float newBottom = (bottom() > other.bottom()) ? bottom() : other.bottom();

        return DirtyRect(newX, newY, newRight - newX, newBottom - newY);
    }

    // Expand rectangle by margin on all sides (for anti-aliasing artifacts)
    DirtyRect expand(float margin) const {
        if (isEmpty()) return *this;
        return DirtyRect(x - margin, y - margin, width + 2.0f * margin, height + 2.0f * margin);
    }

    // Clamp rectangle to canvas bounds
    DirtyRect clamp(float canvasWidth, float canvasHeight) const {
        if (isEmpty()) return *this;

        float newX = (x < 0.0f) ? 0.0f : x;
        float newY = (y < 0.0f) ? 0.0f : y;
        float newRight = (right() > canvasWidth) ? canvasWidth : right();
        float newBottom = (bottom() > canvasHeight) ? canvasHeight : bottom();

        if (newRight <= newX || newBottom <= newY) {
            return DirtyRect();  // Empty after clamping
        }

        return DirtyRect(newX, newY, newRight - newX, newBottom - newY);
    }
};

/**
 * @brief Per-animation dirty tracking state
 *
 * Stores the minimal state needed to track frame changes for one animation.
 * Memory footprint: ~80 bytes per animation.
 */
struct AnimationDirtyState {
    std::string targetId;              // Element ID being animated (e.g., "PROSKENION")
    size_t previousFrameIndex = 0;     // Frame index from previous update()
    size_t currentFrameIndex = 0;      // Frame index from current update()
    DirtyRect cachedBounds;            // Bounds of the animated element (set once on load)
    bool boundsValid = false;          // Whether cachedBounds has been set
    bool isDirty = false;              // Whether this animation changed this frame
};

/**
 * @brief Main dirty region tracking class
 *
 * Tracks which animations changed between frames and calculates dirty rectangles
 * for efficient partial canvas rendering.
 *
 * @section usage Usage Pattern
 * 1. Call initialize() after loading SVG animations
 * 2. Call setAnimationBounds() for each animation target (bounds extracted from SVG)
 * 3. Each frame:
 *    a. Call markDirty() for animations that changed frame
 *    b. Call shouldUseFullRender() to decide render path
 *    c. If partial: use getUnionDirtyRect() to clip canvas
 *    d. Call clearDirtyFlags() after rendering
 *
 * @section memory Memory Efficiency
 * - Only stores per-animation state, NOT per-frame data
 * - Safe for arbitrarily long animations (no memory growth over time)
 * - For 10 animations: ~1KB total memory
 */
class DirtyRegionTracker {
public:
    // Threshold for switching to full render (50% of canvas dirty = full render faster)
    static constexpr float FULL_RENDER_THRESHOLD = 0.5f;

    // Maximum dirty rectangles before merging overhead exceeds benefit
    static constexpr int MAX_DIRTY_RECTS = 8;

    // Margin to expand dirty rects for anti-aliasing artifacts (1 pixel)
    static constexpr float DIRTY_RECT_MARGIN = 1.0f;

    DirtyRegionTracker() = default;

    /**
     * @brief Initialize tracker for a set of animations
     * @param animationCount Number of animations to track
     * @note Call this after loading SVG, before first render
     */
    void initialize(size_t animationCount);

    /**
     * @brief Reset all tracking state
     * @note Clears bounds cache and dirty flags
     */
    void reset();

    /**
     * @brief Set cached bounds for an animation target element
     * @param targetId The element ID (from SMILAnimation::targetId)
     * @param bounds The bounding rectangle of the element
     * @note Call once per animation after extracting bounds from SVG
     */
    void setAnimationBounds(const std::string& targetId, const DirtyRect& bounds);

    /**
     * @brief Check if bounds are cached for a target
     * @param targetId The element ID to check
     * @return true if bounds have been set
     */
    bool hasCachedBounds(const std::string& targetId) const;

    /**
     * @brief Mark an animation as dirty (frame changed)
     * @param targetId The element ID that changed
     * @param newFrameIndex The new frame index
     * @note Call for each animation that changed in update()
     */
    void markDirty(const std::string& targetId, size_t newFrameIndex);

    /**
     * @brief Get list of dirty rectangles for partial rendering
     * @return Vector of dirty rectangles (may be merged)
     * @note Rectangles are expanded by DIRTY_RECT_MARGIN for AA
     */
    std::vector<DirtyRect> getDirtyRects() const;

    /**
     * @brief Get union of all dirty rectangles (single clip rect)
     * @return Bounding box of all dirty regions
     * @note Use this for simple canvas.clipRect() approach
     */
    DirtyRect getUnionDirtyRect() const;

    /**
     * @brief Calculate ratio of dirty area to canvas area
     * @param canvasWidth Canvas width in pixels
     * @param canvasHeight Canvas height in pixels
     * @return Ratio from 0.0 (nothing dirty) to 1.0+ (all or more dirty)
     */
    float getDirtyAreaRatio(float canvasWidth, float canvasHeight) const;

    /**
     * @brief Decide whether to use full render instead of partial
     * @param canvasWidth Canvas width in pixels
     * @param canvasHeight Canvas height in pixels
     * @return true if full render is recommended
     *
     * Returns true (use full render) when:
     * - No dirty regions (nothing to render)
     * - Too many dirty rectangles (merge overhead)
     * - Dirty area exceeds FULL_RENDER_THRESHOLD
     * - Any dirty animation has invalid bounds
     * - Single animation covers >90% of canvas
     */
    bool shouldUseFullRender(float canvasWidth, float canvasHeight) const;

    /**
     * @brief Clear dirty flags for next frame
     * @note Call after rendering, before next update cycle
     */
    void clearDirtyFlags();

    /**
     * @brief Get number of currently dirty animations
     * @return Count of animations with isDirty=true
     */
    size_t getDirtyCount() const;

    /**
     * @brief Get total number of tracked animations
     * @return Count of animations in tracker
     */
    size_t getAnimationCount() const;

    /**
     * @brief Check if tracking is enabled (has any animations)
     * @return true if at least one animation is tracked
     */
    bool isEnabled() const { return !states_.empty(); }

private:
    // Per-animation tracking state (keyed by targetId)
    std::unordered_map<std::string, AnimationDirtyState> states_;

    // Cached list of dirty rectangles (rebuilt on getDirtyRects())
    mutable std::vector<DirtyRect> cachedDirtyRects_;
    mutable bool dirtyRectsCacheValid_ = false;

    // Rebuild cachedDirtyRects_ from current dirty states
    void rebuildDirtyRects() const;
};

}  // namespace svgplayer

#endif  // DIRTY_REGION_TRACKER_H
