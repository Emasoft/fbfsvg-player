// benchmark_svg.cpp - Benchmark Skia SVG rendering performance
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
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// Simple SVG - basic shapes
const char* kSimpleSVG = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600">
  <rect x="50" y="50" width="200" height="150" fill="#DC143C"/>
  <circle cx="450" cy="150" r="100" fill="#228B22"/>
  <circle cx="650" cy="350" r="80" fill="none" stroke="#00008B" stroke-width="5"/>
  <rect x="100" y="300" width="250" height="180" rx="20" fill="#FFD700"/>
  <polygon points="550,450 650,550 450,550" fill="#FF8C00"/>
</svg>
)SVG";

// Medium SVG - gradients, paths, more shapes
const char* kMediumSVG = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 800 600" width="800" height="600">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#1a1a2e"/>
      <stop offset="100%" style="stop-color:#16213e"/>
    </linearGradient>
    <radialGradient id="glow" cx="50%" cy="50%" r="50%">
      <stop offset="0%" style="stop-color:#e94560;stop-opacity:0.8"/>
      <stop offset="100%" style="stop-color:#e94560;stop-opacity:0"/>
    </radialGradient>
    <linearGradient id="shape" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#0f3460"/>
      <stop offset="50%" style="stop-color:#e94560"/>
      <stop offset="100%" style="stop-color:#f39c12"/>
    </linearGradient>
  </defs>
  <rect width="800" height="600" fill="url(#bg)"/>
  <circle cx="400" cy="300" r="200" fill="url(#glow)"/>
  <circle cx="150" cy="100" r="50" fill="#e94560" opacity="0.6"/>
  <circle cx="650" cy="500" r="70" fill="#f39c12" opacity="0.5"/>
  <polygon points="400,100 550,200 550,400 400,500 250,400 250,200" fill="url(#shape)" stroke="#fff" stroke-width="3"/>
  <polygon points="400,150 500,350 300,350" fill="none" stroke="#fff" stroke-width="2"/>
  <circle cx="400" cy="300" r="40" fill="#e94560"/>
  <circle cx="400" cy="300" r="25" fill="#1a1a2e"/>
  <circle cx="400" cy="300" r="12" fill="#f39c12"/>
</svg>
)SVG";

// Complex SVG - many shapes, paths, gradients
const char* kComplexSVG = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 900" width="1200" height="900">
  <defs>
    <linearGradient id="sky" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#0c0c1e"/>
      <stop offset="50%" style="stop-color:#1a1a3e"/>
      <stop offset="100%" style="stop-color:#2d2d5a"/>
    </linearGradient>
    <radialGradient id="sun" cx="50%" cy="50%" r="50%">
      <stop offset="0%" style="stop-color:#ffeb3b"/>
      <stop offset="70%" style="stop-color:#ff9800"/>
      <stop offset="100%" style="stop-color:#ff5722;stop-opacity:0"/>
    </radialGradient>
    <linearGradient id="mountain1" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#4a4a6a"/>
      <stop offset="100%" style="stop-color:#2a2a4a"/>
    </linearGradient>
    <linearGradient id="mountain2" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#3a3a5a"/>
      <stop offset="100%" style="stop-color:#1a1a3a"/>
    </linearGradient>
    <linearGradient id="water" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#1e3a5f"/>
      <stop offset="100%" style="stop-color:#0d1b2a"/>
    </linearGradient>
  </defs>
  <rect width="1200" height="900" fill="url(#sky)"/>
  <circle cx="200" cy="150" r="80" fill="url(#sun)"/>
  <circle cx="200" cy="150" r="40" fill="#ffeb3b"/>
  <!-- Stars -->
  <circle cx="100" cy="50" r="2" fill="#fff"/>
  <circle cx="300" cy="80" r="1.5" fill="#fff"/>
  <circle cx="500" cy="40" r="2" fill="#fff"/>
  <circle cx="700" cy="70" r="1" fill="#fff"/>
  <circle cx="900" cy="30" r="2" fill="#fff"/>
  <circle cx="1100" cy="60" r="1.5" fill="#fff"/>
  <circle cx="150" cy="120" r="1" fill="#fff"/>
  <circle cx="450" cy="100" r="1.5" fill="#fff"/>
  <circle cx="650" cy="130" r="2" fill="#fff"/>
  <circle cx="850" cy="90" r="1" fill="#fff"/>
  <circle cx="1050" cy="110" r="1.5" fill="#fff"/>
  <circle cx="250" cy="180" r="1" fill="#fff"/>
  <circle cx="550" cy="160" r="2" fill="#fff"/>
  <circle cx="750" cy="200" r="1.5" fill="#fff"/>
  <circle cx="950" cy="170" r="1" fill="#fff"/>
  <!-- Mountains back -->
  <polygon points="0,600 200,300 400,500 600,250 800,450 1000,200 1200,400 1200,600" fill="url(#mountain2)"/>
  <!-- Mountains front -->
  <polygon points="0,650 150,400 350,550 500,350 700,500 900,300 1100,450 1200,550 1200,650" fill="url(#mountain1)"/>
  <!-- Water -->
  <rect x="0" y="650" width="1200" height="250" fill="url(#water)"/>
  <!-- Water reflections -->
  <line x1="50" y1="700" x2="150" y2="700" stroke="#fff" stroke-width="1" opacity="0.3"/>
  <line x1="300" y1="720" x2="450" y2="720" stroke="#fff" stroke-width="1" opacity="0.2"/>
  <line x1="600" y1="750" x2="800" y2="750" stroke="#fff" stroke-width="1" opacity="0.25"/>
  <line x1="900" y1="730" x2="1100" y2="730" stroke="#fff" stroke-width="1" opacity="0.3"/>
  <line x1="100" y1="780" x2="250" y2="780" stroke="#fff" stroke-width="1" opacity="0.15"/>
  <line x1="400" y1="800" x2="600" y2="800" stroke="#fff" stroke-width="1" opacity="0.2"/>
  <line x1="750" y1="820" x2="950" y2="820" stroke="#fff" stroke-width="1" opacity="0.25"/>
  <!-- Trees silhouette -->
  <polygon points="50,650 70,550 90,650" fill="#1a1a3a"/>
  <polygon points="80,650 110,520 140,650" fill="#1a1a3a"/>
  <polygon points="1050,650 1080,530 1110,650" fill="#1a1a3a"/>
  <polygon points="1100,650 1140,500 1180,650" fill="#1a1a3a"/>
  <!-- Birds -->
  <path d="M400,250 Q420,240 440,250 Q420,245 400,250" fill="none" stroke="#333" stroke-width="2"/>
  <path d="M450,220 Q470,210 490,220 Q470,215 450,220" fill="none" stroke="#333" stroke-width="2"/>
  <path d="M500,260 Q520,250 540,260 Q520,255 500,260" fill="none" stroke="#333" stroke-width="2"/>
</svg>
)SVG";

struct BenchmarkResult {
    std::string name;
    int width;
    int height;
    int iterations;
    double skia_avg_ms;
    double skia_min_ms;
    double skia_max_ms;
};

bool renderWithSkia(const char* svgContent, int width, int height, const std::string& outputPath) {
    sk_sp<SkData> svgData = SkData::MakeWithCopy(svgContent, strlen(svgContent));
    SkMemoryStream svgStream(svgData);

    sk_sp<SkSVGDOM> svgDom = SkSVGDOM::MakeFromStream(svgStream);
    if (!svgDom) return false;

    svgDom->setContainerSize(SkSize::Make(width, height));

    SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);
    if (!surface) return false;

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    svgDom->render(canvas);

    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) return false;

    sk_sp<SkData> pngData = SkPngEncoder::Encode(nullptr, image.get(), {});
    if (!pngData) return false;

    SkFILEWStream fileStream(outputPath.c_str());
    if (!fileStream.isValid()) return false;

    fileStream.write(pngData->data(), pngData->size());
    return true;
}

BenchmarkResult benchmarkSkia(const char* name, const char* svgContent, int width, int height, int iterations) {
    BenchmarkResult result;
    result.name = name;
    result.width = width;
    result.height = height;
    result.iterations = iterations;

    std::vector<double> times;
    times.reserve(iterations);

    // Warmup run
    std::string warmupPath = "/tmp/skia_warmup.png";
    renderWithSkia(svgContent, width, height, warmupPath);

    for (int i = 0; i < iterations; i++) {
        std::string outputPath = "/tmp/skia_bench_" + std::to_string(i) + ".png";

        auto start = std::chrono::high_resolution_clock::now();
        renderWithSkia(svgContent, width, height, outputPath);
        auto end = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        times.push_back(ms);
    }

    double sum = 0;
    result.skia_min_ms = times[0];
    result.skia_max_ms = times[0];

    for (double t : times) {
        sum += t;
        if (t < result.skia_min_ms) result.skia_min_ms = t;
        if (t > result.skia_max_ms) result.skia_max_ms = t;
    }

    result.skia_avg_ms = sum / iterations;
    return result;
}

void saveSVGFile(const char* content, const std::string& path) {
    std::ofstream file(path);
    file << content;
    file.close();
}

int main(int argc, char* argv[]) {
    const int iterations = 20;

    std::cout << "╔════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║           Skia SVG Rendering Benchmark                         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    std::cout << "Iterations per test: " << iterations << std::endl;
    std::cout << std::endl;

    // Save SVG files for resvg testing
    saveSVGFile(kSimpleSVG, "/tmp/bench_simple.svg");
    saveSVGFile(kMediumSVG, "/tmp/bench_medium.svg");
    saveSVGFile(kComplexSVG, "/tmp/bench_complex.svg");

    std::cout << "SVG test files saved to /tmp/bench_*.svg" << std::endl;
    std::cout << std::endl;

    // Run Skia benchmarks
    std::cout << "Running Skia benchmarks..." << std::endl;

    BenchmarkResult simple = benchmarkSkia("Simple", kSimpleSVG, 800, 600, iterations);
    std::cout << "  Simple SVG (800x600): " << std::fixed << std::setprecision(2) << simple.skia_avg_ms << " ms avg" << std::endl;

    BenchmarkResult medium = benchmarkSkia("Medium", kMediumSVG, 800, 600, iterations);
    std::cout << "  Medium SVG (800x600): " << std::fixed << std::setprecision(2) << medium.skia_avg_ms << " ms avg" << std::endl;

    BenchmarkResult complex = benchmarkSkia("Complex", kComplexSVG, 1200, 900, iterations);
    std::cout << "  Complex SVG (1200x900): " << std::fixed << std::setprecision(2) << complex.skia_avg_ms << " ms avg" << std::endl;

    std::cout << std::endl;
    std::cout << "╔════════════════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║                           Skia Benchmark Results                                   ║" << std::endl;
    std::cout << "╠═══════════════╦════════════════╦═══════════════╦═══════════════╦══════════════════╣" << std::endl;
    std::cout << "║ Test          ║ Resolution     ║ Avg (ms)      ║ Min (ms)      ║ Max (ms)         ║" << std::endl;
    std::cout << "╠═══════════════╬════════════════╬═══════════════╬═══════════════╬══════════════════╣" << std::endl;

    auto printRow = [](const BenchmarkResult& r) {
        std::cout << "║ " << std::left << std::setw(13) << r.name
                  << " ║ " << std::setw(14) << (std::to_string(r.width) + "x" + std::to_string(r.height))
                  << " ║ " << std::right << std::setw(13) << std::fixed << std::setprecision(2) << r.skia_avg_ms
                  << " ║ " << std::setw(13) << r.skia_min_ms
                  << " ║ " << std::setw(16) << r.skia_max_ms << " ║" << std::endl;
    };

    printRow(simple);
    printRow(medium);
    printRow(complex);

    std::cout << "╚═══════════════╩════════════════╩═══════════════╩═══════════════╩══════════════════╝" << std::endl;
    std::cout << std::endl;

    // Output commands for resvg testing
    std::cout << "To benchmark resvg, run these commands:" << std::endl;
    std::cout << "─────────────────────────────────────────" << std::endl;
    std::cout << "# Simple SVG:" << std::endl;
    std::cout << "hyperfine --warmup 3 --runs " << iterations << " 'resvg /tmp/bench_simple.svg /tmp/resvg_simple.png'" << std::endl;
    std::cout << std::endl;
    std::cout << "# Medium SVG:" << std::endl;
    std::cout << "hyperfine --warmup 3 --runs " << iterations << " 'resvg /tmp/bench_medium.svg /tmp/resvg_medium.png'" << std::endl;
    std::cout << std::endl;
    std::cout << "# Complex SVG:" << std::endl;
    std::cout << "hyperfine --warmup 3 --runs " << iterations << " 'resvg /tmp/bench_complex.svg /tmp/resvg_complex.png'" << std::endl;
    std::cout << std::endl;

    // Save one final output for verification
    renderWithSkia(kComplexSVG, 1200, 900, "skia_benchmark_output.png");
    std::cout << "Verification image saved to: skia_benchmark_output.png" << std::endl;

    return 0;
}
