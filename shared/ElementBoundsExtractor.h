// ElementBoundsExtractor.h - Extract element bounds from SVG for dirty region tracking
// Platform-independent C++17 implementation
// Used by: macOS player, Linux player, iOS SDK, Linux SDK
//
// This utility parses SVG content to extract bounding rectangles for animated elements.
// For FBF.SVG files, the <use> elements have STATIC positions (only xlink:href changes),
// so bounds can be cached once on load and reused for all frames.
//
// Memory-efficient: Returns a map of target ID to bounds, no per-frame storage.

#ifndef ELEMENT_BOUNDS_EXTRACTOR_H
#define ELEMENT_BOUNDS_EXTRACTOR_H

#include "DirtyRegionTracker.h"
#include <string>
#include <map>
#include <vector>

// Forward declaration to avoid circular dependency
namespace svgplayer {
struct SMILAnimation;
}

namespace svgplayer {

/**
 * @brief Utility class for extracting element bounds from SVG content
 *
 * Parses SVG to find the bounding rectangles of animated elements.
 * Works with FBF.SVG <use> elements that reference <symbol> definitions.
 *
 * @section usage Usage Pattern
 * 1. After loading SVG content, call extractAnimationBounds()
 * 2. Pass the result to DirtyRegionTracker::setAnimationBounds()
 * 3. Bounds are cached for the lifetime of the animation
 *
 * @section supported Supported Element Types
 * - <use> elements with x, y, width, height attributes
 * - <symbol> elements with viewBox for fallback dimensions
 * - Elements with transform="translate(x,y)" attributes
 */
class ElementBoundsExtractor {
public:
    /**
     * @brief Extract bounds for all animated elements
     * @param svgContent The raw SVG file content
     * @param animations Vector of animations to extract bounds for
     * @return Map of targetId to DirtyRect bounds
     *
     * For each animation, finds the target element and extracts its position and size.
     * Returns empty bounds for elements that cannot be located.
     */
    static std::map<std::string, DirtyRect> extractAnimationBounds(
        const std::string& svgContent,
        const std::vector<SMILAnimation>& animations);

    /**
     * @brief Extract bounds for a single element by ID
     * @param svgContent The raw SVG file content
     * @param elementId The element ID to find
     * @param outBounds Output bounds rectangle
     * @return true if bounds were successfully extracted
     *
     * Searches for the element with the given ID and extracts its bounds.
     * Handles <use>, <g>, <rect>, <symbol> and other common SVG elements.
     */
    static bool extractBoundsForId(
        const std::string& svgContent,
        const std::string& elementId,
        DirtyRect& outBounds);

    /**
     * @brief Parse transform="translate(x,y)" and return offset
     * @param transformValue The transform attribute value
     * @param outX Output X offset
     * @param outY Output Y offset
     * @return true if a translate transform was found
     */
    static bool parseTranslate(
        const std::string& transformValue,
        float& outX,
        float& outY);

    /**
     * @brief Parse viewBox attribute for dimensions
     * @param viewBoxValue The viewBox attribute value (e.g., "0 0 100 100")
     * @param outX Output viewBox X origin
     * @param outY Output viewBox Y origin
     * @param outWidth Output viewBox width
     * @param outHeight Output viewBox height
     * @return true if viewBox was successfully parsed
     */
    static bool parseViewBox(
        const std::string& viewBoxValue,
        float& outX,
        float& outY,
        float& outWidth,
        float& outHeight);

private:
    /**
     * @brief Find an element by ID in SVG content
     * @param svgContent The SVG content to search
     * @param elementId The ID to find
     * @param outTagStart Output position of opening < character
     * @param outTagEnd Output position of closing > character
     * @return true if element was found
     */
    static bool findElementById(
        const std::string& svgContent,
        const std::string& elementId,
        size_t& outTagStart,
        size_t& outTagEnd);

    /**
     * @brief Extract a named attribute value from an element tag
     * @param tagContent The element tag content (including < and >)
     * @param attrName The attribute name to find
     * @param outValue Output attribute value (without quotes)
     * @return true if attribute was found
     */
    static bool extractAttribute(
        const std::string& tagContent,
        const std::string& attrName,
        std::string& outValue);

    /**
     * @brief Parse a numeric attribute value
     * @param value The attribute value string
     * @return Parsed float value, or 0.0f if parsing fails
     */
    static float parseNumeric(const std::string& value);
};

}  // namespace svgplayer

#endif  // ELEMENT_BOUNDS_EXTRACTOR_H
