// svg_render.cpp - Skia example that parses SVG and renders to PNG
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "modules/svg/include/SkSVGRenderContext.h"

#include <iostream>
#include <string>

// Sample SVG content - a colorful logo with shapes and gradients
const char* kSampleSVG = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 300" width="400" height="300">
  <defs>
    <!-- Linear gradient for background -->
    <linearGradient id="bgGradient" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#1a1a2e;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#16213e;stop-opacity:1" />
    </linearGradient>

    <!-- Radial gradient for glow effect -->
    <radialGradient id="glowGradient" cx="50%" cy="50%" r="50%">
      <stop offset="0%" style="stop-color:#e94560;stop-opacity:0.8" />
      <stop offset="100%" style="stop-color:#e94560;stop-opacity:0" />
    </radialGradient>

    <!-- Linear gradient for the main shape -->
    <linearGradient id="shapeGradient" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#0f3460" />
      <stop offset="50%" style="stop-color:#e94560" />
      <stop offset="100%" style="stop-color:#f39c12" />
    </linearGradient>
  </defs>

  <!-- Background -->
  <rect width="400" height="300" fill="url(#bgGradient)"/>

  <!-- Glow effect circle -->
  <circle cx="200" cy="150" r="120" fill="url(#glowGradient)"/>

  <!-- Decorative circles -->
  <circle cx="80" cy="60" r="30" fill="#e94560" opacity="0.6"/>
  <circle cx="320" cy="240" r="40" fill="#f39c12" opacity="0.5"/>
  <circle cx="350" cy="50" r="20" fill="#0f3460" opacity="0.7"/>

  <!-- Main hexagon shape -->
  <polygon points="200,50 280,100 280,200 200,250 120,200 120,100"
           fill="url(#shapeGradient)"
           stroke="#ffffff"
           stroke-width="3"
           opacity="0.9"/>

  <!-- Inner triangle -->
  <polygon points="200,80 250,180 150,180"
           fill="none"
           stroke="#ffffff"
           stroke-width="2"/>

  <!-- Center circle -->
  <circle cx="200" cy="150" r="25" fill="#e94560"/>
  <circle cx="200" cy="150" r="15" fill="#1a1a2e"/>
  <circle cx="200" cy="150" r="8" fill="#f39c12"/>

  <!-- Decorative lines -->
  <line x1="50" y1="280" x2="150" y2="280" stroke="#e94560" stroke-width="3" stroke-linecap="round"/>
  <line x1="250" y1="280" x2="350" y2="280" stroke="#f39c12" stroke-width="3" stroke-linecap="round"/>

  <!-- Small decorative dots -->
  <circle cx="60" cy="150" r="5" fill="#ffffff" opacity="0.5"/>
  <circle cx="340" cy="150" r="5" fill="#ffffff" opacity="0.5"/>
  <circle cx="200" cy="30" r="4" fill="#e94560"/>
  <circle cx="200" cy="270" r="4" fill="#f39c12"/>

  <!-- Corner accents -->
  <rect x="10" y="10" width="30" height="3" fill="#e94560"/>
  <rect x="10" y="10" width="3" height="30" fill="#e94560"/>
  <rect x="360" y="10" width="30" height="3" fill="#f39c12"/>
  <rect x="387" y="10" width="3" height="30" fill="#f39c12"/>
  <rect x="10" y="287" width="30" height="3" fill="#0f3460"/>
  <rect x="10" y="260" width="3" height="30" fill="#0f3460"/>
  <rect x="360" y="287" width="30" height="3" fill="#e94560"/>
  <rect x="387" y="260" width="3" height="30" fill="#e94560"/>
</svg>
)SVG";

int main(int argc, char* argv[]) {
    std::cout << "Skia SVG Renderer Example" << std::endl;
    std::cout << "=========================" << std::endl;

    // Create a memory stream from the SVG string
    sk_sp<SkData> svgData = SkData::MakeWithCopy(kSampleSVG, strlen(kSampleSVG));
    SkMemoryStream svgStream(svgData);

    // Parse the SVG
    sk_sp<SkSVGDOM> svgDom = SkSVGDOM::MakeFromStream(svgStream);
    if (!svgDom) {
        std::cerr << "Failed to parse SVG" << std::endl;
        return 1;
    }

    // Get the SVG dimensions
    SkSVGSVG* root = svgDom->getRoot();
    if (!root) {
        std::cerr << "SVG has no root element" << std::endl;
        return 1;
    }

    // Get intrinsic size or use default
    SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(SkSize::Make(400, 300)));
    int width = static_cast<int>(svgSize.width());
    int height = static_cast<int>(svgSize.height());

    if (width <= 0 || height <= 0) {
        width = 400;
        height = 300;
    }

    std::cout << "SVG dimensions: " << width << "x" << height << std::endl;

    // Set container size for the SVG DOM
    svgDom->setContainerSize(SkSize::Make(width, height));

    // Create a raster surface
    SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);

    if (!surface) {
        std::cerr << "Failed to create surface" << std::endl;
        return 1;
    }

    SkCanvas* canvas = surface->getCanvas();

    // Clear with transparent background (the SVG has its own background)
    canvas->clear(SK_ColorTRANSPARENT);

    // Render the SVG
    svgDom->render(canvas);

    std::cout << "SVG rendered successfully" << std::endl;

    // Get the image from the surface
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        std::cerr << "Failed to create image snapshot" << std::endl;
        return 1;
    }

    // Encode to PNG
    sk_sp<SkData> pngData = SkPngEncoder::Encode(nullptr, image.get(), {});
    if (!pngData) {
        std::cerr << "Failed to encode PNG" << std::endl;
        return 1;
    }

    // Write to file
    const char* outputFile = "svg_output.png";
    SkFILEWStream fileStream(outputFile);
    if (!fileStream.isValid()) {
        std::cerr << "Failed to open output file: " << outputFile << std::endl;
        return 1;
    }

    fileStream.write(pngData->data(), pngData->size());

    std::cout << "Output saved to: " << outputFile << std::endl;
    std::cout << "PNG size: " << pngData->size() << " bytes" << std::endl;

    // Also save the SVG source for reference
    const char* svgOutputFile = "sample.svg";
    SkFILEWStream svgFileStream(svgOutputFile);
    if (svgFileStream.isValid()) {
        svgFileStream.write(kSampleSVG, strlen(kSampleSVG));
        std::cout << "SVG source saved to: " << svgOutputFile << std::endl;
    }

    std::cout << "\nDone!" << std::endl;

    return 0;
}
