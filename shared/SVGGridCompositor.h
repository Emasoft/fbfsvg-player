// SVGGridCompositor.h - Fast C++ grid compositor for SVG animations
// Composes multiple SVG files into a single grid layout with ID prefixing
// Used by folder browser for real-time thumbnail grid generation

#pragma once

#include <string>
#include <vector>
#include <functional>

namespace svgplayer {

// Compositor cell configuration (named distinctly to avoid collision with browser's GridCell)
struct CompositorCell {
    std::string svgContent;     // Raw SVG content (already loaded)
    std::string label;          // Optional label below cell
    float originalWidth;        // Original SVG width
    float originalHeight;       // Original SVG height
    float viewBoxMinX = 0.0f;   // ViewBox minX coordinate
    float viewBoxMinY = 0.0f;   // ViewBox minY coordinate
    float viewBoxWidth = 0.0f;  // ViewBox width
    float viewBoxHeight = 0.0f; // ViewBox height
};

// Grid composition configuration
struct GridConfig {
    int columns = 3;                    // Number of columns
    int rows = 3;                       // Number of rows (0 = auto from cell count)
    float containerWidth = 1920.0f;     // Container width
    float containerHeight = 1080.0f;    // Container height
    float cellMargin = 20.0f;           // Margin between cells
    float labelHeight = 0.0f;           // Height reserved for labels (0 = no labels)
    float labelFontSize = 14.0f;        // Label font size
    std::string bgColor = "#1a1a2e";    // Background color
    bool preserveAspectRatio = true;    // Keep SVG aspect ratios in cells
};

// Result of grid composition
struct GridResult {
    std::string svgContent;             // Composed SVG string
    float totalWidth;                   // Actual grid width
    float totalHeight;                  // Actual grid height
    int cellCount;                      // Number of cells rendered
};

class SVGGridCompositor {
public:
    SVGGridCompositor();
    ~SVGGridCompositor();

    // Main composition function - creates grid from cell contents
    // Returns composed SVG with all IDs prefixed to avoid collisions
    GridResult compose(const std::vector<CompositorCell>& cells, const GridConfig& config);

    // Compose with background SVG (cells overlaid on background)
    GridResult composeWithBackground(
        const std::vector<CompositorCell>& cells,
        const GridConfig& config,
        const std::string& backgroundSvg
    );

    // Utility: Extract viewBox dimensions from SVG string
    // Returns true if viewBox found, populates width/height
    static bool extractViewBox(const std::string& svg, float& width, float& height);

    // Utility: Extract full viewBox from SVG string (minX, minY, width, height)
    // Returns true if viewBox found, populates all 4 values
    // This is needed for proper thumbnail generation when viewBox doesn't start at (0,0)
    static bool extractFullViewBox(const std::string& svg, float& minX, float& minY, float& width, float& height);

    // Utility: Prefix all IDs in SVG content to avoid collisions
    // Prefixes: id="X" -> id="prefix_X", href="#X" -> href="#prefix_X", url(#X) -> url(#prefix_X)
    static std::string prefixSVGIds(const std::string& svg, const std::string& prefix);

    // Utility: Extract inner content from SVG (everything between <svg> and </svg>)
    static std::string extractSVGContent(const std::string& svg);

    // Utility: Escape XML special characters
    static std::string escapeXml(const std::string& text);

private:
    // Calculate cell dimensions based on config
    void calculateCellLayout(
        const GridConfig& config,
        int cellCount,
        float& cellWidth,
        float& cellHeight,
        int& actualRows
    );

    // Generate transform for positioning and scaling a cell
    std::string generateCellTransform(
        float cellX, float cellY,
        float cellWidth, float cellHeight,
        float svgWidth, float svgHeight,
        bool preserveAspectRatio
    );

    // Generate label SVG for a cell
    std::string generateLabel(
        const std::string& text,
        float cellX, float cellWidth,
        float labelY, float fontSize
    );
};

} // namespace svgplayer
