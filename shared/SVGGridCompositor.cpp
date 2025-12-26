// SVGGridCompositor.cpp - Fast C++ grid compositor for SVG animations
// Implementation of real-time SVG grid composition with ID prefixing

#include "SVGGridCompositor.h"
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>

namespace svgplayer {

SVGGridCompositor::SVGGridCompositor() {}
SVGGridCompositor::~SVGGridCompositor() {}

GridResult SVGGridCompositor::compose(const std::vector<CompositorCell>& cells, const GridConfig& config) {
    GridResult result;
    result.cellCount = static_cast<int>(cells.size());

    if (cells.empty()) {
        // Empty grid - just background
        std::ostringstream ss;
        ss << R"(<svg xmlns="http://www.w3.org/2000/svg" )"
           << R"(width=")" << config.containerWidth << R"(" height=")" << config.containerHeight << R"(" )"
           << R"(viewBox="0 0 )" << config.containerWidth << " " << config.containerHeight << R"(">)"
           << R"(<rect width="100%" height="100%" fill=")" << config.bgColor << R"("/>)"
           << R"(</svg>)";
        result.svgContent = ss.str();
        result.totalWidth = config.containerWidth;
        result.totalHeight = config.containerHeight;
        return result;
    }

    // Calculate cell layout
    float cellWidth, cellHeight;
    int actualRows;
    calculateCellLayout(config, result.cellCount, cellWidth, cellHeight, actualRows);

    std::ostringstream ss;

    // SVG header
    ss << R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" )"
       << R"(width=")" << config.containerWidth << R"(" height=")" << config.containerHeight << R"(" )"
       << R"(viewBox="0 0 )" << config.containerWidth << " " << config.containerHeight << R"(">)";

    // Background
    ss << R"(<rect width="100%" height="100%" fill=")" << config.bgColor << R"("/>)";

    // Render each cell
    for (size_t i = 0; i < cells.size(); i++) {
        const CompositorCell& cell = cells[i];

        // Calculate cell position
        int col = static_cast<int>(i) % config.columns;
        int row = static_cast<int>(i) / config.columns;

        float cellX = config.cellMargin + col * (cellWidth + config.cellMargin);
        float cellY = config.cellMargin + row * (cellHeight + config.cellMargin + config.labelHeight);

        // Skip if cell has no content
        if (cell.svgContent.empty()) continue;

        // Prefix IDs to avoid collisions between cells
        std::string prefix = "c" + std::to_string(i) + "_";
        std::string prefixedSvg = prefixSVGIds(cell.svgContent, prefix);

        // Extract inner content from SVG
        std::string innerContent = extractSVGContent(prefixedSvg);

        // Get SVG dimensions for scaling
        float svgWidth = cell.originalWidth > 0 ? cell.originalWidth : 100.0f;
        float svgHeight = cell.originalHeight > 0 ? cell.originalHeight : 100.0f;

        // Generate transform for this cell
        std::string transform = generateCellTransform(
            cellX, cellY, cellWidth, cellHeight,
            svgWidth, svgHeight, config.preserveAspectRatio
        );

        // Wrap cell content in a group with transform
        ss << R"(<g transform=")" << transform << R"(">)";

        // Create a nested SVG to contain the cell content with its original viewBox
        ss << R"(<svg width=")" << svgWidth << R"(" height=")" << svgHeight
           << R"(" viewBox="0 0 )" << svgWidth << " " << svgHeight << R"(">)";
        ss << innerContent;
        ss << R"(</svg>)";

        ss << R"(</g>)";

        // Add label if configured
        if (config.labelHeight > 0 && !cell.label.empty()) {
            float labelY = cellY + cellHeight + config.labelHeight * 0.7f;
            ss << generateLabel(cell.label, cellX, cellWidth, labelY, config.labelFontSize);
        }
    }

    ss << R"(</svg>)";

    result.svgContent = ss.str();
    result.totalWidth = config.containerWidth;
    result.totalHeight = config.containerHeight;

    return result;
}

GridResult SVGGridCompositor::composeWithBackground(
    const std::vector<CompositorCell>& cells,
    const GridConfig& config,
    const std::string& backgroundSvg
) {
    GridResult result;
    result.cellCount = static_cast<int>(cells.size());

    // Extract background dimensions
    float bgWidth = config.containerWidth;
    float bgHeight = config.containerHeight;
    extractViewBox(backgroundSvg, bgWidth, bgHeight);

    // Prefix background IDs
    std::string prefixedBg = prefixSVGIds(backgroundSvg, "bg_");
    std::string bgContent = extractSVGContent(prefixedBg);

    // Calculate cell layout
    float cellWidth, cellHeight;
    int actualRows;
    calculateCellLayout(config, result.cellCount, cellWidth, cellHeight, actualRows);

    std::ostringstream ss;

    // SVG header with background dimensions
    ss << R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" )"
       << R"(width=")" << bgWidth << R"(" height=")" << bgHeight << R"(" )"
       << R"(viewBox="0 0 )" << bgWidth << " " << bgHeight << R"(">)";

    // Background content first (behind grid)
    ss << bgContent;

    // Render cells on top of background
    for (size_t i = 0; i < cells.size(); i++) {
        const CompositorCell& cell = cells[i];

        int col = static_cast<int>(i) % config.columns;
        int row = static_cast<int>(i) / config.columns;

        float cellX = config.cellMargin + col * (cellWidth + config.cellMargin);
        float cellY = config.cellMargin + row * (cellHeight + config.cellMargin + config.labelHeight);

        if (cell.svgContent.empty()) continue;

        std::string prefix = "c" + std::to_string(i) + "_";
        std::string prefixedSvg = prefixSVGIds(cell.svgContent, prefix);
        std::string innerContent = extractSVGContent(prefixedSvg);

        float svgWidth = cell.originalWidth > 0 ? cell.originalWidth : 100.0f;
        float svgHeight = cell.originalHeight > 0 ? cell.originalHeight : 100.0f;

        std::string transform = generateCellTransform(
            cellX, cellY, cellWidth, cellHeight,
            svgWidth, svgHeight, config.preserveAspectRatio
        );

        ss << R"(<g transform=")" << transform << R"(">)";
        ss << R"(<svg width=")" << svgWidth << R"(" height=")" << svgHeight
           << R"(" viewBox="0 0 )" << svgWidth << " " << svgHeight << R"(">)";
        ss << innerContent;
        ss << R"(</svg>)";
        ss << R"(</g>)";

        if (config.labelHeight > 0 && !cell.label.empty()) {
            float labelY = cellY + cellHeight + config.labelHeight * 0.7f;
            ss << generateLabel(cell.label, cellX, cellWidth, labelY, config.labelFontSize);
        }
    }

    ss << R"(</svg>)";

    result.svgContent = ss.str();
    result.totalWidth = bgWidth;
    result.totalHeight = bgHeight;

    return result;
}

bool SVGGridCompositor::extractViewBox(const std::string& svg, float& width, float& height) {
    // Pattern: viewBox="x y width height"
    std::regex viewBoxRegex(R"(viewBox\s*=\s*["']([^"']+)["'])");
    std::smatch match;

    if (std::regex_search(svg, match, viewBoxRegex) && match.size() > 1) {
        std::istringstream iss(match[1].str());
        float x, y;
        if (iss >> x >> y >> width >> height) {
            return true;
        }
    }

    // Fallback: try width/height attributes
    std::regex widthRegex(R"(width\s*=\s*["'](\d+(?:\.\d+)?)(?:px)?["'])");
    std::regex heightRegex(R"(height\s*=\s*["'](\d+(?:\.\d+)?)(?:px)?["'])");

    bool foundWidth = false, foundHeight = false;

    // Use try-catch because std::stof can throw on invalid/extreme values
    if (std::regex_search(svg, match, widthRegex) && match.size() > 1) {
        try {
            width = std::stof(match[1].str());
            foundWidth = true;
        } catch (const std::exception&) {
            // Ignore invalid width value
        }
    }
    if (std::regex_search(svg, match, heightRegex) && match.size() > 1) {
        try {
            height = std::stof(match[1].str());
            foundHeight = true;
        } catch (const std::exception&) {
            // Ignore invalid height value
        }
    }

    return foundWidth && foundHeight;
}

bool SVGGridCompositor::extractFullViewBox(const std::string& svg, float& minX, float& minY, float& width, float& height) {
    // Pattern: viewBox="minX minY width height"
    std::regex viewBoxRegex(R"(viewBox\s*=\s*["']([^"']+)["'])");
    std::smatch match;

    if (std::regex_search(svg, match, viewBoxRegex) && match.size() > 1) {
        std::istringstream iss(match[1].str());
        if (iss >> minX >> minY >> width >> height) {
            return true;
        }
    }

    // Fallback: try width/height attributes (assume minX=0, minY=0)
    std::regex widthRegex(R"(width\s*=\s*["'](\d+(?:\.\d+)?)(?:px)?["'])");
    std::regex heightRegex(R"(height\s*=\s*["'](\d+(?:\.\d+)?)(?:px)?["'])");

    bool foundWidth = false, foundHeight = false;
    minX = 0.0f;
    minY = 0.0f;

    // Use try-catch because std::stof can throw on invalid/extreme values
    if (std::regex_search(svg, match, widthRegex) && match.size() > 1) {
        try {
            width = std::stof(match[1].str());
            foundWidth = true;
        } catch (const std::exception&) {
            // Ignore invalid width value
        }
    }
    if (std::regex_search(svg, match, heightRegex) && match.size() > 1) {
        try {
            height = std::stof(match[1].str());
            foundHeight = true;
        } catch (const std::exception&) {
            // Ignore invalid height value
        }
    }

    return foundWidth && foundHeight;
}

std::string SVGGridCompositor::prefixSVGIds(const std::string& svg, const std::string& prefix) {
    // Static regex objects - compiled once, reused for all calls
    // This provides massive performance improvement for large files
    static const std::regex idAttrRegex(R"(id\s*=\s*["']([^"']+)["'])");
    static const std::regex hrefRegex(R"((xlink:)?href\s*=\s*["']#([^"']+)["'])");
    static const std::regex urlRegex(R"(url\s*\(\s*#([^)]+)\s*\))");
    static const std::regex beginRegex(R"(begin\s*=\s*["']([^"'.]+)\.([^"']+)["'])");
    static const std::regex valuesRegex(R"(values\s*=\s*["']([^"']+)["'])");
    static const std::regex idRefRegex(R"(#([^;#]+))");

    std::string result = svg;

    // Pattern 1: id="value" -> id="prefix_value"
    result = std::regex_replace(result, idAttrRegex, "id=\"" + prefix + "$1\"");

    // Pattern 2: href="#value" -> href="#prefix_value" (includes xlink:href)
    result = std::regex_replace(result, hrefRegex, "$1href=\"#" + prefix + "$2\"");

    // Pattern 3: url(#value) -> url(#prefix_value)
    result = std::regex_replace(result, urlRegex, "url(#" + prefix + "$1)");

    // Pattern 4: begin="id.event" -> begin="prefix_id.event" (for SMIL animations)
    result = std::regex_replace(result, beginRegex, "begin=\"" + prefix + "$1.$2\"");

    // Pattern 5: values="#frame1;#frame2" -> values="#prefix_frame1;#prefix_frame2"
    // Handle semicolon-separated ID references in animate values
    std::smatch match;
    std::string::const_iterator searchStart = result.cbegin();
    std::string processedResult;
    size_t lastEnd = 0;

    while (std::regex_search(searchStart, result.cend(), match, valuesRegex)) {
        size_t matchStart = match.position(0) + (searchStart - result.cbegin());
        processedResult += result.substr(lastEnd, matchStart - lastEnd);

        std::string valuesContent = match[1].str();
        // Check if values contain ID references (start with #)
        if (valuesContent.find('#') != std::string::npos) {
            valuesContent = std::regex_replace(valuesContent, idRefRegex, "#" + prefix + "$1");
        }
        processedResult += "values=\"" + valuesContent + "\"";

        lastEnd = matchStart + match[0].length();
        searchStart = result.cbegin() + lastEnd;
    }
    processedResult += result.substr(lastEnd);
    result = processedResult;

    return result;
}

std::string SVGGridCompositor::extractSVGContent(const std::string& svg) {
    // Find opening <svg ...> tag end
    size_t svgStart = svg.find("<svg");
    if (svgStart == std::string::npos) return svg;

    size_t tagEnd = svg.find('>', svgStart);
    if (tagEnd == std::string::npos) return svg;

    // Find closing </svg> tag
    size_t svgEnd = svg.rfind("</svg>");
    if (svgEnd == std::string::npos || svgEnd <= tagEnd) return svg;

    // Extract content between tags
    return svg.substr(tagEnd + 1, svgEnd - tagEnd - 1);
}

std::string SVGGridCompositor::escapeXml(const std::string& text) {
    std::string result;
    result.reserve(text.size() * 1.1);  // Pre-allocate slightly larger

    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default:   result += c; break;
        }
    }
    return result;
}

void SVGGridCompositor::calculateCellLayout(
    const GridConfig& config,
    int cellCount,
    float& cellWidth,
    float& cellHeight,
    int& actualRows
) {
    // Calculate actual rows needed
    actualRows = config.rows > 0 ? config.rows :
                 static_cast<int>(std::ceil(static_cast<float>(cellCount) / config.columns));

    // Available space after margins
    float availableWidth = config.containerWidth - config.cellMargin * (config.columns + 1);
    float availableHeight = config.containerHeight - config.cellMargin * (actualRows + 1)
                          - config.labelHeight * actualRows;

    // Cell dimensions
    cellWidth = availableWidth / config.columns;
    cellHeight = availableHeight / actualRows;
}

std::string SVGGridCompositor::generateCellTransform(
    float cellX, float cellY,
    float cellWidth, float cellHeight,
    float svgWidth, float svgHeight,
    bool preserveAspectRatio
) {
    std::ostringstream ss;

    if (preserveAspectRatio) {
        // Calculate scale to fit while preserving aspect ratio
        float scaleX = cellWidth / svgWidth;
        float scaleY = cellHeight / svgHeight;
        float scale = std::min(scaleX, scaleY);

        // Center in cell
        float scaledWidth = svgWidth * scale;
        float scaledHeight = svgHeight * scale;
        float offsetX = cellX + (cellWidth - scaledWidth) / 2;
        float offsetY = cellY + (cellHeight - scaledHeight) / 2;

        ss << "translate(" << offsetX << "," << offsetY << ") scale(" << scale << ")";
    } else {
        // Stretch to fill cell
        float scaleX = cellWidth / svgWidth;
        float scaleY = cellHeight / svgHeight;
        ss << "translate(" << cellX << "," << cellY << ") scale(" << scaleX << "," << scaleY << ")";
    }

    return ss.str();
}

std::string SVGGridCompositor::generateLabel(
    const std::string& text,
    float cellX, float cellWidth,
    float labelY, float fontSize
) {
    std::ostringstream ss;
    float textX = cellX + cellWidth / 2;

    ss << R"(<text x=")" << textX << R"(" y=")" << labelY
       << R"(" text-anchor="middle" fill="#cccccc" font-family="sans-serif" font-size=")" << fontSize << R"(">)"
       << escapeXml(text) << R"(</text>)";

    return ss.str();
}

} // namespace svgplayer
