// skia_svg_batch.cpp - Batch SVG to PNG converter using Skia
// Usage: skia_svg_batch <input.svg> <output.png>
//    or: skia_svg_batch --batch <input_dir> <output_dir>

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
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

bool renderSVGtoPNG(const std::string& inputPath, const std::string& outputPath, bool verbose = false) {
    // Read SVG file
    SkFILEStream svgStream(inputPath.c_str());
    if (!svgStream.isValid()) {
        if (verbose) std::cerr << "Failed to open: " << inputPath << std::endl;
        return false;
    }

    // Parse SVG
    sk_sp<SkSVGDOM> svgDom = SkSVGDOM::MakeFromStream(svgStream);
    if (!svgDom) {
        if (verbose) std::cerr << "Failed to parse SVG: " << inputPath << std::endl;
        return false;
    }

    // Get SVG dimensions
    SkSVGSVG* root = svgDom->getRoot();
    if (!root) {
        if (verbose) std::cerr << "SVG has no root: " << inputPath << std::endl;
        return false;
    }

    // Default size if not specified
    int width = 800;
    int height = 600;

    // Try to get intrinsic size
    SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(SkSize::Make(800, 600)));
    if (svgSize.width() > 0 && svgSize.height() > 0) {
        width = static_cast<int>(svgSize.width());
        height = static_cast<int>(svgSize.height());
    }

    svgDom->setContainerSize(SkSize::Make(width, height));

    // Create surface and render
    SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);
    if (!surface) {
        if (verbose) std::cerr << "Failed to create surface" << std::endl;
        return false;
    }

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);
    svgDom->render(canvas);

    // Encode to PNG
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        if (verbose) std::cerr << "Failed to create image snapshot" << std::endl;
        return false;
    }

    sk_sp<SkData> pngData = SkPngEncoder::Encode(nullptr, image.get(), {});
    if (!pngData) {
        if (verbose) std::cerr << "Failed to encode PNG" << std::endl;
        return false;
    }

    // Write output
    SkFILEWStream fileStream(outputPath.c_str());
    if (!fileStream.isValid()) {
        if (verbose) std::cerr << "Failed to open output: " << outputPath << std::endl;
        return false;
    }

    fileStream.write(pngData->data(), pngData->size());
    return true;
}

std::vector<std::string> getSVGFiles(const std::string& dirPath) {
    std::vector<std::string> files;
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".svg") {
            files.push_back(dirPath + "/" + name);
        }
    }
    closedir(dir);

    std::sort(files.begin(), files.end());
    return files;
}

std::string getBasename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    std::string filename = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    size_t dotPos = filename.find_last_of('.');
    return (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
}

void printUsage(const char* progName) {
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  " << progName << " <input.svg> <output.png>       Convert single SVG" << std::endl;
    std::cerr << "  " << progName << " --batch <input_dir> <output_dir>  Convert all SVGs in directory" << std::endl;
    std::cerr << "  " << progName << " --benchmark <input_dir>         Benchmark batch conversion" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "--batch" && argc >= 4) {
        // Batch mode
        std::string inputDir = argv[2];
        std::string outputDir = argv[3];

        // Create output directory if needed
        mkdir(outputDir.c_str(), 0755);

        auto files = getSVGFiles(inputDir);
        int success = 0, failed = 0;

        for (const auto& svgPath : files) {
            std::string outPath = outputDir + "/" + getBasename(svgPath) + ".png";
            if (renderSVGtoPNG(svgPath, outPath, true)) {
                success++;
            } else {
                failed++;
            }
        }

        std::cout << "Converted " << success << " files";
        if (failed > 0) std::cout << " (" << failed << " failed)";
        std::cout << std::endl;

    } else if (mode == "--benchmark" && argc >= 3) {
        // Benchmark mode - convert all and measure time
        std::string inputDir = argv[2];
        std::string outputDir = "/tmp/skia_bench_output";
        mkdir(outputDir.c_str(), 0755);

        auto files = getSVGFiles(inputDir);

        auto start = std::chrono::high_resolution_clock::now();

        int success = 0;
        for (const auto& svgPath : files) {
            std::string outPath = outputDir + "/" + getBasename(svgPath) + ".png";
            if (renderSVGtoPNG(svgPath, outPath, false)) {
                success++;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double avgMs = totalMs / files.size();

        std::cout << "Processed " << success << "/" << files.size() << " files" << std::endl;
        std::cout << "Total time: " << totalMs << " ms" << std::endl;
        std::cout << "Average per file: " << avgMs << " ms" << std::endl;

    } else if (argc >= 3 && mode[0] != '-') {
        // Single file mode
        std::string inputPath = argv[1];
        std::string outputPath = argv[2];

        if (renderSVGtoPNG(inputPath, outputPath, true)) {
            std::cout << "Converted: " << inputPath << " -> " << outputPath << std::endl;
            return 0;
        } else {
            return 1;
        }
    } else {
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
