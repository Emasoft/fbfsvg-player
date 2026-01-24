// Skia folder sequence player for benchmarking - plays numbered SVG frames from a folder
// This is a minimal player specifically for fair ThorVG comparison
// Usage: skia_folder_player <folder_or_file> [duration_seconds] [options]

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/core/SkStream.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/ports/SkFontMgr_mac_ct.h"  // macOS Core Text font manager
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/skresources/include/SkResources.h"
#include <SDL.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <regex>
#include <sys/stat.h>
#include <cstring>

// Save screenshot as PPM
bool saveScreenshotPPM(const uint32_t* pixels, int width, int height, const char* filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;

    file << "P6\n" << width << " " << height << "\n255\n";

    // Skia MakeN32Premul uses RGBA on macOS ARM64 (kRGBA_8888_SkColorType)
    // Little-endian uint32_t layout: 0xAABBGGRR
    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; i++) {
        uint32_t pixel = pixels[i];
        rgb[i * 3 + 0] = pixel & 0xFF;          // R (byte 0)
        rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;   // G (byte 1)
        rgb[i * 3 + 2] = (pixel >> 16) & 0xFF;  // B (byte 2)
    }

    file.write(reinterpret_cast<char*>(rgb.data()), rgb.size());
    return file.good();
}

// Load SVG file content
std::string loadSvgFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Extract frame number from filename
int extractFrameNumber(const std::string& filename) {
    std::regex pattern("_(\\d+)\\.svg$", std::regex::icase);
    std::smatch match;
    if (std::regex_search(filename, match, pattern)) {
        return std::stoi(match[1].str());
    }
    std::regex fallback("(\\d+)\\.svg$", std::regex::icase);
    if (std::regex_search(filename, match, fallback)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

// Scan folder for SVG files
std::vector<std::string> scanFolderForFrames(const std::string& folderPath) {
    std::vector<std::pair<int, std::string>> frameFiles;

    DIR* dir = opendir(folderPath.c_str());
    if (!dir) {
        std::cerr << "Cannot open folder: " << folderPath << std::endl;
        return {};
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".svg") {
            int frameNum = extractFrameNumber(name);
            std::string fullPath = folderPath + "/" + name;
            frameFiles.push_back({frameNum, fullPath});
        }
    }
    closedir(dir);

    std::sort(frameFiles.begin(), frameFiles.end(), [](const auto& a, const auto& b) {
        if (a.first == -1 && b.first == -1) return a.second < b.second;
        if (a.first == -1) return false;
        if (b.first == -1) return true;
        return a.first < b.first;
    });

    std::vector<std::string> result;
    for (const auto& f : frameFiles) {
        result.push_back(f.second);
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <folder_or_file> [duration_seconds] [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  --loop              Run indefinitely until Escape pressed (ignores duration)" << std::endl;
        std::cerr << "  --json              Output benchmark stats as JSON" << std::endl;
        std::cerr << "  --screenshot=FILE   Save first frame as PPM screenshot" << std::endl;
        std::cerr << "  --folder            Treat input as folder of numbered SVG frames" << std::endl;
        std::cerr << "  --size=WxH          Set window size (e.g. --size=1920x1080)" << std::endl;
        return 1;
    }

    const char* inputPath = argv[1];
    int duration = (argc > 2 && argv[2][0] != '-') ? std::atoi(argv[2]) : 10;
    bool jsonOutput = false;
    bool folderMode = false;
    bool loopMode = false;  // Run indefinitely if true
    const char* screenshotPath = nullptr;
    int forceWidth = 0, forceHeight = 0;  // 0 means use default

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
        if (strcmp(argv[i], "--folder") == 0) folderMode = true;
        if (strcmp(argv[i], "--loop") == 0) loopMode = true;
        if (strncmp(argv[i], "--screenshot=", 13) == 0) screenshotPath = argv[i] + 13;
        if (strncmp(argv[i], "--size=", 7) == 0) {
            if (sscanf(argv[i] + 7, "%dx%d", &forceWidth, &forceHeight) != 2) {
                std::cerr << "Invalid --size format. Use --size=WIDTHxHEIGHT (e.g. --size=1920x1080)" << std::endl;
                return 1;
            }
        }
    }

    // Auto-detect folder mode
    struct stat pathStat;
    if (stat(inputPath, &pathStat) == 0 && S_ISDIR(pathStat.st_mode)) {
        folderMode = true;
    }

    // Load SVG frames
    std::vector<std::string> svgPaths;
    std::vector<std::string> svgContents;

    if (folderMode) {
        svgPaths = scanFolderForFrames(inputPath);
        if (svgPaths.empty()) {
            std::cerr << "No SVG files found in: " << inputPath << std::endl;
            return 1;
        }
        if (!jsonOutput) {
            std::cerr << "Folder mode: Found " << svgPaths.size() << " SVG frames" << std::endl;
        }
        for (const auto& path : svgPaths) {
            std::string content = loadSvgFile(path);
            if (content.empty()) {
                std::cerr << "Failed to load: " << path << std::endl;
                return 1;
            }
            svgContents.push_back(std::move(content));
        }
    } else {
        svgPaths.push_back(inputPath);
        std::string content = loadSvgFile(inputPath);
        if (content.empty()) {
            std::cerr << "Cannot open: " << inputPath << std::endl;
            return 1;
        }
        svgContents.push_back(std::move(content));
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create window - use forced size if specified, otherwise maximized
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);

    int windowWidth = (forceWidth > 0) ? forceWidth : (dm.w - 100);
    int windowHeight = (forceHeight > 0) ? forceHeight : (dm.h - 100);
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (forceWidth == 0 && forceHeight == 0) {
        windowFlags |= SDL_WINDOW_MAXIMIZED;  // Only maximize if no forced size
    }

    SDL_Window* window = SDL_CreateWindow("Skia Folder Player",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight, windowFlags);

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create SDL renderer without vsync for raw throughput
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    // Use forced size if specified, otherwise get window size
    int drawWidth, drawHeight;
    if (forceWidth > 0 && forceHeight > 0) {
        drawWidth = forceWidth;
        drawHeight = forceHeight;
    } else {
        SDL_GetWindowSize(window, &drawWidth, &drawHeight);
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, drawWidth, drawHeight);

    // Create Skia surface
    SkImageInfo info = SkImageInfo::MakeN32Premul(drawWidth, drawHeight);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) {
        std::cerr << "Failed to create Skia surface" << std::endl;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Resource provider for SVG (fonts, images)
    auto resourceProvider = skresources::DataURIResourceProviderProxy::Make(
        skresources::FileResourceProvider::Make(SkString(".")),
        skresources::ImageDecodeStrategy::kLazyDecode);

    // CRITICAL: Create FontMgr ONCE outside the loop!
    // This was the major bottleneck - creating CoreText FontMgr took ~1234ms per frame
    auto fontMgr = SkFontMgr_New_CoreText(nullptr);
    if (!fontMgr) {
        std::cerr << "Failed to create CoreText FontMgr" << std::endl;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Benchmark loop with detailed phase tracing
    std::vector<double> frameTimes;
    std::vector<double> parseTimes;

    // Phase timing accumulators for detailed tracing
    std::vector<double> phase_dataCopy;
    std::vector<double> phase_streamCreate;
    std::vector<double> phase_fontMgr;
    std::vector<double> phase_domParse;
    std::vector<double> phase_containerSize;
    std::vector<double> phase_canvasClear;
    std::vector<double> phase_domRender;
    std::vector<double> phase_pixelExtract;
    std::vector<double> phase_textureUpdate;
    std::vector<double> phase_sdlPresent;

    auto startTime = std::chrono::high_resolution_clock::now();
    auto endTime = startTime + std::chrono::seconds(duration);

    SDL_Event event;
    bool running = true;
    bool screenshotSaved = false;
    size_t currentFrame = 0;
    size_t totalFramesRendered = 0;

    // Main loop: in loop mode, ignore time limit; otherwise stop after duration
    while (running && (loopMode || std::chrono::high_resolution_clock::now() < endTime)) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        auto frameStart = std::chrono::high_resolution_clock::now();
        auto phaseStart = frameStart;

        // Phase 1: Data copy
        const std::string& svgData = svgContents[currentFrame];
        sk_sp<SkData> data = SkData::MakeWithCopy(svgData.data(), svgData.size());
        auto phaseEnd = std::chrono::high_resolution_clock::now();
        phase_dataCopy.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 2: Stream creation
        phaseStart = phaseEnd;
        auto stream = SkMemoryStream::Make(data);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_streamCreate.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 3: Font manager (reuse cached instance - creation moved outside loop)
        // This records 0ms because fontMgr was pre-created (see line 221)
        phase_fontMgr.push_back(0.0);

        // Phase 4: DOM parsing (this is expected to be the main bottleneck)
        phaseStart = phaseEnd;
        auto dom = SkSVGDOM::Builder()
            .setFontManager(fontMgr)
            .setResourceProvider(resourceProvider)
            .make(*stream);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_domParse.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Calculate total parse time (phases 1-4)
        auto parseEnd = phaseEnd;
        double parseMs = std::chrono::duration<double, std::milli>(parseEnd - frameStart).count();
        parseTimes.push_back(parseMs);

        if (!dom) {
            std::cerr << "Failed to parse SVG frame " << currentFrame << std::endl;
            // Remove partial phase data to keep vectors synchronized
            phase_dataCopy.pop_back();
            phase_streamCreate.pop_back();
            phase_fontMgr.pop_back();
            phase_domParse.pop_back();
            parseTimes.pop_back();
            running = false;
            continue;
        }

        // Phase 5: Set container size
        phaseStart = phaseEnd;
        dom->setContainerSize(SkSize::Make(drawWidth, drawHeight));
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_containerSize.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 6: Canvas clear
        phaseStart = phaseEnd;
        SkCanvas* canvas = surface->getCanvas();
        canvas->clear(SK_ColorBLACK);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasClear.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 7: DOM render (SVG rasterization)
        phaseStart = phaseEnd;
        dom->render(canvas);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_domRender.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // FPS overlay DISABLED - using window title for FPS display to avoid benchmark interference
        // FPS is shown in SDL window title instead (updated below)

        // Phase 8: Pixel extraction
        phaseStart = phaseEnd;
        SkPixmap pixmap;
        surface->peekPixels(&pixmap);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_pixelExtract.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Save screenshot after first frame
        if (screenshotPath && !screenshotSaved) {
            if (saveScreenshotPPM((const uint32_t*)pixmap.addr(), drawWidth, drawHeight, screenshotPath)) {
                std::cerr << "Screenshot saved: " << screenshotPath << " (" << drawWidth << "x" << drawHeight << ")" << std::endl;
            }
            screenshotSaved = true;
        }

        // Phase 9: SDL texture update
        phaseStart = phaseEnd;
        SDL_UpdateTexture(texture, nullptr, pixmap.addr(), pixmap.rowBytes());
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_textureUpdate.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 10: SDL render and present
        phaseStart = phaseEnd;
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_sdlPresent.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        auto frameEnd = phaseEnd;
        double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        frameTimes.push_back(frameMs);

        totalFramesRendered++;

        // Update window title with FPS every 10 frames (for efficiency)
        if (totalFramesRendered % 10 == 0 && !frameTimes.empty()) {
            double currentFps = 1000.0 / frameMs;
            // Rolling average of last 30 frames
            size_t sampleCount = std::min(frameTimes.size(), (size_t)30);
            double sumMs = 0;
            for (size_t i = frameTimes.size() - sampleCount; i < frameTimes.size(); i++) {
                sumMs += frameTimes[i];
            }
            double avgFps = 1000.0 * sampleCount / sumMs;

            char title[128];
            snprintf(title, sizeof(title), "Skia Folder Player - FPS: %.1f (avg: %.1f) | Frame: %zu/%zu",
                     currentFps, avgFps, currentFrame + 1, svgContents.size());
            SDL_SetWindowTitle(window, title);
        }

        // Advance frame in folder mode
        if (folderMode) {
            currentFrame = (currentFrame + 1) % svgContents.size();
        }
    }

    // Calculate stats
    double totalTime = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - startTime).count();

    double avgFrameTime = 0, minFrameTime = 1e9, maxFrameTime = 0;
    for (double t : frameTimes) {
        avgFrameTime += t;
        minFrameTime = std::min(minFrameTime, t);
        maxFrameTime = std::max(maxFrameTime, t);
    }
    if (!frameTimes.empty()) avgFrameTime /= frameTimes.size();

    double avgParseTime = 0;
    for (double t : parseTimes) avgParseTime += t;
    if (!parseTimes.empty()) avgParseTime /= parseTimes.size();

    // Helper to compute average from vector
    auto avgOf = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0;
        double sum = 0;
        for (double t : v) sum += t;
        return sum / v.size();
    };

    // Calculate phase averages
    double avg_dataCopy = avgOf(phase_dataCopy);
    double avg_streamCreate = avgOf(phase_streamCreate);
    double avg_fontMgr = avgOf(phase_fontMgr);
    double avg_domParse = avgOf(phase_domParse);
    double avg_containerSize = avgOf(phase_containerSize);
    double avg_canvasClear = avgOf(phase_canvasClear);
    double avg_domRender = avgOf(phase_domRender);
    double avg_pixelExtract = avgOf(phase_pixelExtract);
    double avg_textureUpdate = avgOf(phase_textureUpdate);
    double avg_sdlPresent = avgOf(phase_sdlPresent);

    double avgFps = frameTimes.empty() ? 0 : frameTimes.size() / totalTime;
    // Guard against edge case where no frames were rendered (minFrameTime stays at 1e9)
    double minFps = (maxFrameTime > 0 && !frameTimes.empty()) ? 1000.0 / maxFrameTime : 0;
    double maxFps = (minFrameTime < 1e8 && !frameTimes.empty()) ? 1000.0 / minFrameTime : 0;

    // Cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    // Output
    if (jsonOutput) {
        std::cout << "{";
        std::cout << "\"player\":\"skia\",";
        std::cout << "\"mode\":\"" << (folderMode ? "folder" : "single") << "\",";
        std::cout << "\"file\":\"" << inputPath << "\",";
        if (folderMode) {
            std::cout << "\"frame_count\":" << svgContents.size() << ",";
        }
        std::cout << "\"duration_seconds\":" << totalTime << ",";
        std::cout << "\"total_frames\":" << totalFramesRendered << ",";
        std::cout << "\"avg_fps\":" << avgFps << ",";
        std::cout << "\"avg_frame_time_ms\":" << avgFrameTime << ",";
        std::cout << "\"avg_parse_time_ms\":" << avgParseTime << ",";
        std::cout << "\"min_fps\":" << minFps << ",";
        std::cout << "\"max_fps\":" << maxFps << ",";
        std::cout << "\"resolution\":\"" << drawWidth << "x" << drawHeight << "\",";
        // Detailed phase timing
        std::cout << "\"phases\":{";
        std::cout << "\"data_copy_ms\":" << avg_dataCopy << ",";
        std::cout << "\"stream_create_ms\":" << avg_streamCreate << ",";
        std::cout << "\"font_mgr_ms\":" << avg_fontMgr << ",";
        std::cout << "\"dom_parse_ms\":" << avg_domParse << ",";
        std::cout << "\"container_size_ms\":" << avg_containerSize << ",";
        std::cout << "\"canvas_clear_ms\":" << avg_canvasClear << ",";
        std::cout << "\"dom_render_ms\":" << avg_domRender << ",";
        std::cout << "\"pixel_extract_ms\":" << avg_pixelExtract << ",";
        std::cout << "\"texture_update_ms\":" << avg_textureUpdate << ",";
        std::cout << "\"sdl_present_ms\":" << avg_sdlPresent;
        std::cout << "}";
        std::cout << "}" << std::endl;
    } else {
        std::cout << "\n=== Skia Folder Player Benchmark Results ===" << std::endl;
        std::cout << "Mode: " << (folderMode ? "Folder sequence" : "Single file") << std::endl;
        std::cout << "Input: " << inputPath << std::endl;
        if (folderMode) {
            std::cout << "Frame count: " << svgContents.size() << std::endl;
        }
        std::cout << "Resolution: " << drawWidth << "x" << drawHeight << std::endl;
        std::cout << "Duration: " << totalTime << "s" << std::endl;
        std::cout << "Frames rendered: " << totalFramesRendered << std::endl;
        std::cout << "Average FPS: " << avgFps << std::endl;
        std::cout << "Average frame time: " << avgFrameTime << " ms" << std::endl;
        std::cout << "Average parse time: " << avgParseTime << " ms" << std::endl;
        std::cout << "FPS range: " << minFps << " - " << maxFps << std::endl;
        std::cout << "\n--- Phase Timing Breakdown ---" << std::endl;
        std::cout << "  Data copy:       " << avg_dataCopy << " ms" << std::endl;
        std::cout << "  Stream create:   " << avg_streamCreate << " ms" << std::endl;
        std::cout << "  Font manager:    " << avg_fontMgr << " ms" << std::endl;
        std::cout << "  DOM parse:       " << avg_domParse << " ms" << std::endl;
        std::cout << "  Container size:  " << avg_containerSize << " ms" << std::endl;
        std::cout << "  Canvas clear:    " << avg_canvasClear << " ms" << std::endl;
        std::cout << "  DOM render:      " << avg_domRender << " ms" << std::endl;
        std::cout << "  Pixel extract:   " << avg_pixelExtract << " ms" << std::endl;
        std::cout << "  Texture update:  " << avg_textureUpdate << " ms" << std::endl;
        std::cout << "  SDL present:     " << avg_sdlPresent << " ms" << std::endl;
    }

    return 0;
}
