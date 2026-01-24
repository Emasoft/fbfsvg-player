// ElementBoundsExtractor.cpp - Implementation of SVG element bounds extraction
// See ElementBoundsExtractor.h for documentation

#include "ElementBoundsExtractor.h"
#include "SVGAnimationController.h"  // For SMILAnimation definition
#include <regex>
#include <cstdio>
#include <algorithm>

namespace svgplayer {

std::map<std::string, DirtyRect> ElementBoundsExtractor::extractAnimationBounds(
    const std::string& svgContent,
    const std::vector<SMILAnimation>& animations)
{
    std::map<std::string, DirtyRect> result;

    for (const auto& anim : animations) {
        // Skip if we already have bounds for this target
        // (multiple animations may target the same element)
        if (result.find(anim.targetId) != result.end()) {
            continue;
        }

        DirtyRect bounds;
        if (extractBoundsForId(svgContent, anim.targetId, bounds)) {
            result[anim.targetId] = bounds;
        }
        // If bounds extraction fails, element won't be in result map
        // DirtyRegionTracker will use full render for that animation
    }

    return result;
}

bool ElementBoundsExtractor::extractBoundsForId(
    const std::string& svgContent,
    const std::string& elementId,
    DirtyRect& outBounds)
{
    size_t tagStart, tagEnd;
    if (!findElementById(svgContent, elementId, tagStart, tagEnd)) {
        return false;
    }

    std::string tag = svgContent.substr(tagStart, tagEnd - tagStart + 1);

    // Extract position attributes (x, y)
    std::string xVal, yVal, widthVal, heightVal;

    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

    // Try x, y attributes first (common for <use>, <rect>, <image>)
    if (extractAttribute(tag, "x", xVal)) {
        x = parseNumeric(xVal);
    }
    if (extractAttribute(tag, "y", yVal)) {
        y = parseNumeric(yVal);
    }

    // Try width, height attributes
    if (extractAttribute(tag, "width", widthVal)) {
        width = parseNumeric(widthVal);
    }
    if (extractAttribute(tag, "height", heightVal)) {
        height = parseNumeric(heightVal);
    }

    // Check for transform="translate(x,y)" which may offset the element
    std::string transformVal;
    if (extractAttribute(tag, "transform", transformVal)) {
        float tx = 0.0f, ty = 0.0f;
        if (parseTranslate(transformVal, tx, ty)) {
            x += tx;
            y += ty;
        }
    }

    // If no width/height, try to find referenced symbol's viewBox
    // For <use xlink:href="#symbolId"> elements
    if (width <= 0.0f || height <= 0.0f) {
        std::string href;
        // Try both xlink:href and href attributes
        if (extractAttribute(tag, "xlink:href", href) || extractAttribute(tag, "href", href)) {
            // href might be "#symbolId" or "url(#symbolId)"
            // Extract the ID part
            std::string refId;
            if (href.length() > 0 && href[0] == '#') {
                refId = href.substr(1);
            } else if (href.find("#") != std::string::npos) {
                size_t hashPos = href.find('#');
                size_t endPos = href.find(')', hashPos);
                if (endPos == std::string::npos) endPos = href.length();
                refId = href.substr(hashPos + 1, endPos - hashPos - 1);
            }

            // Find the referenced element (symbol, g, etc.) and get its viewBox
            if (!refId.empty()) {
                size_t refTagStart, refTagEnd;
                if (findElementById(svgContent, refId, refTagStart, refTagEnd)) {
                    std::string refTag = svgContent.substr(refTagStart, refTagEnd - refTagStart + 1);
                    std::string viewBoxVal;
                    if (extractAttribute(refTag, "viewBox", viewBoxVal)) {
                        float vbX, vbY, vbW, vbH;
                        if (parseViewBox(viewBoxVal, vbX, vbY, vbW, vbH)) {
                            // Use viewBox dimensions if we don't have width/height
                            if (width <= 0.0f) width = vbW;
                            if (height <= 0.0f) height = vbH;
                        }
                    }
                    // Also try width/height on the referenced element
                    if (width <= 0.0f) {
                        std::string refWidth;
                        if (extractAttribute(refTag, "width", refWidth)) {
                            width = parseNumeric(refWidth);
                        }
                    }
                    if (height <= 0.0f) {
                        std::string refHeight;
                        if (extractAttribute(refTag, "height", refHeight)) {
                            height = parseNumeric(refHeight);
                        }
                    }
                }
            }
        }
    }

    // Validate we got valid dimensions
    if (width <= 0.0f || height <= 0.0f) {
        return false;  // Cannot determine bounds
    }

    outBounds = DirtyRect(x, y, width, height);
    return true;
}

bool ElementBoundsExtractor::parseTranslate(
    const std::string& transformValue,
    float& outX,
    float& outY)
{
    // Match translate(x) or translate(x, y) or translate(x y)
    std::regex translatePattern(R"(translate\s*\(\s*([+-]?[\d.]+)\s*[,\s]*([+-]?[\d.]+)?\s*\))");
    std::smatch match;

    if (std::regex_search(transformValue, match, translatePattern)) {
        outX = std::stof(match[1].str());
        if (match[2].matched) {
            outY = std::stof(match[2].str());
        } else {
            outY = 0.0f;  // translate(x) means translate(x, 0)
        }
        return true;
    }

    return false;
}

bool ElementBoundsExtractor::parseViewBox(
    const std::string& viewBoxValue,
    float& outX,
    float& outY,
    float& outWidth,
    float& outHeight)
{
    // viewBox format: "minX minY width height"
    // Separator can be space or comma
    float values[4];
    int count = std::sscanf(viewBoxValue.c_str(), "%f %f %f %f",
                            &values[0], &values[1], &values[2], &values[3]);

    if (count != 4) {
        // Try comma-separated
        count = std::sscanf(viewBoxValue.c_str(), "%f,%f,%f,%f",
                            &values[0], &values[1], &values[2], &values[3]);
    }

    if (count == 4) {
        outX = values[0];
        outY = values[1];
        outWidth = values[2];
        outHeight = values[3];
        return true;
    }

    return false;
}

bool ElementBoundsExtractor::findElementById(
    const std::string& svgContent,
    const std::string& elementId,
    size_t& outTagStart,
    size_t& outTagEnd)
{
    // Search for id="elementId" or id='elementId'
    // Build pattern that matches the specific ID
    std::string patterns[] = {
        "id=\"" + elementId + "\"",
        "id='" + elementId + "'"
    };

    size_t foundPos = std::string::npos;
    for (const auto& pattern : patterns) {
        size_t pos = svgContent.find(pattern);
        if (pos != std::string::npos) {
            // Make sure this is a word boundary (not a substring of another ID)
            // The pattern already includes the closing quote, so this is safe
            foundPos = pos;
            break;
        }
    }

    if (foundPos == std::string::npos) {
        return false;
    }

    // Find the opening '<' of this element
    outTagStart = svgContent.rfind('<', foundPos);
    if (outTagStart == std::string::npos) {
        return false;
    }

    // Find the closing '>' of the opening tag
    // Handle self-closing tags like <use ... />
    outTagEnd = svgContent.find('>', foundPos);
    if (outTagEnd == std::string::npos) {
        return false;
    }

    return true;
}

bool ElementBoundsExtractor::extractAttribute(
    const std::string& tagContent,
    const std::string& attrName,
    std::string& outValue)
{
    // Match attrName="value" or attrName='value'
    // Also handle xlink:href properly
    std::string escapedName = attrName;
    // Escape special regex characters in attribute name
    for (size_t i = 0; i < escapedName.length(); ++i) {
        if (escapedName[i] == ':') {
            escapedName.insert(i, "\\");
            ++i;
        }
    }

    std::regex attrPattern(escapedName + R"(\s*=\s*["']([^"']*)["'])");
    std::smatch match;

    if (std::regex_search(tagContent, match, attrPattern)) {
        outValue = match[1].str();
        return true;
    }

    return false;
}

float ElementBoundsExtractor::parseNumeric(const std::string& value)
{
    if (value.empty()) return 0.0f;

    try {
        // Handle units like "100px", "50.5%", "10em"
        // For now, just parse the numeric part
        size_t idx = 0;
        float result = std::stof(value, &idx);

        // Check for percentage - would need canvas size context
        // For now, percentages are not supported (return raw value)

        return result;
    } catch (...) {
        return 0.0f;
    }
}

}  // namespace svgplayer
