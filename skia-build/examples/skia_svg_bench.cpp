// skia_svg_bench.cpp - Optimized SVG to PNG converter with zoom support and timing
// Usage: skia_svg_bench <input.svg> <output.png> [zoom_factor] [--perf]
// Designed for fair benchmarking against resvg

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

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.svg> <output.png> [zoom_factor] [--perf]" << std::endl;
        std::cerr << "  zoom_factor: scaling multiplier (default: 1.0)" << std::endl;
        std::cerr << "  --perf: print performance timing breakdown" << std::endl;
        return 1;
    }

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];
    float zoom = 1.0f;
    bool showPerf = false;

    // Parse arguments
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--perf") == 0) {
            showPerf = true;
        } else {
            zoom = std::stof(argv[i]);
        }
    }

    auto totalStart = Clock::now();

    // === READING ===
    auto readStart = Clock::now();
    SkFILEStream svgStream(inputPath);
    if (!svgStream.isValid()) {
        std::cerr << "Failed to open: " << inputPath << std::endl;
        return 1;
    }
    auto readEnd = Clock::now();

    // === SVG PARSING ===
    auto parseStart = Clock::now();
    sk_sp<SkSVGDOM> svgDom = SkSVGDOM::MakeFromStream(svgStream);
    if (!svgDom) {
        std::cerr << "Failed to parse SVG: " << inputPath << std::endl;
        return 1;
    }

    // Get SVG root and dimensions
    SkSVGSVG* root = svgDom->getRoot();
    if (!root) {
        std::cerr << "SVG has no root element" << std::endl;
        return 1;
    }

    // Get intrinsic size with default fallback
    SkSize defaultSize = SkSize::Make(800, 600);
    SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));

    int baseWidth = (svgSize.width() > 0) ? static_cast<int>(svgSize.width()) : 800;
    int baseHeight = (svgSize.height() > 0) ? static_cast<int>(svgSize.height()) : 600;

    // Apply zoom factor
    int width = static_cast<int>(baseWidth * zoom);
    int height = static_cast<int>(baseHeight * zoom);

    // Set container size to zoomed dimensions
    svgDom->setContainerSize(SkSize::Make(width, height));
    auto parseEnd = Clock::now();

    // === SURFACE CREATION ===
    auto surfaceStart = Clock::now();
    SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);
    if (!surface) {
        std::cerr << "Failed to create surface (" << width << "x" << height << ")" << std::endl;
        return 1;
    }
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);
    auto surfaceEnd = Clock::now();

    // === RENDERING ===
    auto renderStart = Clock::now();
    if (zoom != 1.0f) {
        canvas->scale(zoom, zoom);
    }
    svgDom->render(canvas);
    auto renderEnd = Clock::now();

    // === IMAGE SNAPSHOT ===
    auto snapshotStart = Clock::now();
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        std::cerr << "Failed to create image snapshot" << std::endl;
        return 1;
    }
    auto snapshotEnd = Clock::now();

    // === PNG ENCODING ===
    auto encodeStart = Clock::now();
    SkPngEncoder::Options pngOptions;
    pngOptions.fZLibLevel = 1;  // Fastest compression

    sk_sp<SkData> pngData = SkPngEncoder::Encode(nullptr, image.get(), pngOptions);
    if (!pngData) {
        std::cerr << "Failed to encode PNG" << std::endl;
        return 1;
    }
    auto encodeEnd = Clock::now();

    // === FILE WRITING ===
    auto writeStart = Clock::now();
    SkFILEWStream fileStream(outputPath);
    if (!fileStream.isValid()) {
        std::cerr << "Failed to open output: " << outputPath << std::endl;
        return 1;
    }
    fileStream.write(pngData->data(), pngData->size());
    auto writeEnd = Clock::now();

    auto totalEnd = Clock::now();

    // Print timing breakdown if requested
    if (showPerf) {
        Duration readTime = readEnd - readStart;
        Duration parseTime = parseEnd - parseStart;
        Duration surfaceTime = surfaceEnd - surfaceStart;
        Duration renderTime = renderEnd - renderStart;
        Duration snapshotTime = snapshotEnd - snapshotStart;
        Duration encodeTime = encodeEnd - encodeStart;
        Duration writeTime = writeEnd - writeStart;
        Duration totalTime = totalEnd - totalStart;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Reading: " << readTime.count() << "ms" << std::endl;
        std::cout << "SVG Parsing: " << parseTime.count() << "ms" << std::endl;
        std::cout << "Surface Creation: " << surfaceTime.count() << "ms" << std::endl;
        std::cout << "Rendering: " << renderTime.count() << "ms" << std::endl;
        std::cout << "Snapshot: " << snapshotTime.count() << "ms" << std::endl;
        std::cout << "PNG Encoding: " << encodeTime.count() << "ms" << std::endl;
        std::cout << "File Writing: " << writeTime.count() << "ms" << std::endl;
        std::cout << "---" << std::endl;
        std::cout << "Total: " << totalTime.count() << "ms" << std::endl;
    }

    return 0;
}
