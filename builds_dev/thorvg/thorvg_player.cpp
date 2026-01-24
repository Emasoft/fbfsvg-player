// Minimal ThorVG player for benchmarking - outputs JSON stats
// Updated for ThorVG 1.0 API - measures raw throughput (no vsync)
// Supports: single file mode OR folder sequence mode (files named *_00001.svg, etc.)
#include <thorvg.h>
#include <SDL.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <regex>
#include <sys/stat.h>

// Save screenshot as PPM (Portable Pixmap) - simple format, no dependencies
bool saveScreenshotPPM(const std::vector<uint32_t>& pixels, int width, int height, const char* filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return false;
    }

    // PPM header
    file << "P6\n" << width << " " << height << "\n255\n";

    // Convert ARGB8888 to RGB and write
    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; i++) {
        uint32_t pixel = pixels[i];
        rgb[i * 3 + 0] = (pixel >> 16) & 0xFF;  // R
        rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;   // G
        rgb[i * 3 + 2] = pixel & 0xFF;          // B
    }

    file.write(reinterpret_cast<char*>(rgb.data()), rgb.size());
    return file.good();
}

// Load SVG data from file
std::string loadSvgFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Extract frame number from filename (e.g., "frame_00001.svg" -> 1)
int extractFrameNumber(const std::string& filename) {
    // Match pattern: _NNNNN.svg or _NNN.svg at end of filename
    std::regex pattern("_(\\d+)\\.svg$", std::regex::icase);
    std::smatch match;
    if (std::regex_search(filename, match, pattern)) {
        return std::stoi(match[1].str());
    }
    // Fallback: extract any trailing number before .svg
    std::regex fallback("(\\d+)\\.svg$", std::regex::icase);
    if (std::regex_search(filename, match, fallback)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

// Scan folder for SVG files and sort by frame number
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

    // Sort by frame number (files without number go last)
    std::sort(frameFiles.begin(), frameFiles.end(), [](const auto& a, const auto& b) {
        if (a.first == -1 && b.first == -1) return a.second < b.second;
        if (a.first == -1) return false;
        if (b.first == -1) return true;
        return a.first < b.first;
    });

    // Extract just the paths
    std::vector<std::string> result;
    for (const auto& f : frameFiles) {
        result.push_back(f.second);
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <svg_file_or_folder> [duration_seconds] [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  --loop              Run indefinitely until Escape pressed (ignores duration)" << std::endl;
        std::cerr << "  --json              Output benchmark stats as JSON" << std::endl;
        std::cerr << "  --hidpi             Render at 2x resolution (4K on Retina)" << std::endl;
        std::cerr << "  --screenshot=FILE   Save first frame as PPM screenshot" << std::endl;
        std::cerr << "  --folder            Treat input as folder of numbered SVG frames" << std::endl;
        std::cerr << "                      (Files should be named like frame_00001.svg)" << std::endl;
        std::cerr << "  --size=WxH          Set window size (e.g. --size=1920x1080)" << std::endl;
        return 1;
    }

    const char* inputPath = argv[1];
    int duration = (argc > 2 && argv[2][0] != '-') ? std::atoi(argv[2]) : 10;
    bool jsonOutput = false;
    bool folderMode = false;
    bool loopMode = false;  // Run indefinitely if true
    bool useHiDPI = false;
    const char* screenshotPath = nullptr;
    int forceWidth = 0, forceHeight = 0;  // 0 means use default

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
        if (strcmp(argv[i], "--folder") == 0) folderMode = true;
        if (strcmp(argv[i], "--loop") == 0) loopMode = true;
        if (strcmp(argv[i], "--hidpi") == 0) useHiDPI = true;
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

    // Load SVG frames (single file or folder)
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
        // Pre-load all frames for fair timing (exclude I/O from render benchmark)
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

    // Create maximized window at HiDPI resolution for fair comparison
    // Create window - use forced size if specified, otherwise maximized
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    int width = (forceWidth > 0) ? forceWidth : (dm.w - 100);
    int height = (forceHeight > 0) ? forceHeight : (dm.h - 100);
    if (useHiDPI && forceWidth == 0) {
        width *= 2;
        height *= 2;
    }
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (forceWidth == 0 && forceHeight == 0) {
        windowFlags |= SDL_WINDOW_MAXIMIZED;  // Only maximize if no forced size
    }

    SDL_Window* window = SDL_CreateWindow("ThorVG Player",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, windowFlags);

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create renderer WITHOUT vsync for raw throughput measurement
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width, height);

    // Initialize ThorVG
    tvg::Initializer::init(0);

    // Load system fonts for text rendering
    // ThorVG requires explicit font loading - it doesn't auto-load system fonts
    const char* fontPaths[] = {
        // macOS system fonts
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Geneva.ttf",
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/NewYork.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        // Linux common fonts
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        nullptr
    };

    int fontsLoaded = 0;
    for (int i = 0; fontPaths[i] != nullptr; i++) {
        if (tvg::Text::load(fontPaths[i]) == tvg::Result::Success) {
            fontsLoaded++;
            if (!jsonOutput) {
                std::cerr << "Loaded font: " << fontPaths[i] << std::endl;
            }
        }
    }
    if (!jsonOutput && fontsLoaded > 0) {
        std::cerr << "Total fonts loaded: " << fontsLoaded << std::endl;
    } else if (!jsonOutput && fontsLoaded == 0) {
        std::cerr << "Warning: No fonts loaded - text may not render!" << std::endl;
    }

    std::vector<uint32_t> pixels(width * height);
    std::vector<double> frameTimes;
    std::vector<double> parseTimes;

    // Phase timing accumulators for detailed tracing
    std::vector<double> phase_canvasCreate;
    std::vector<double> phase_canvasTarget;
    std::vector<double> phase_pictureLoad;
    std::vector<double> phase_transform;
    std::vector<double> phase_canvasAdd;
    std::vector<double> phase_bufferClear;
    std::vector<double> phase_canvasUpdate;
    std::vector<double> phase_canvasDraw;
    std::vector<double> phase_canvasSync;
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

        // Phase 1: Canvas creation
        tvg::SwCanvas* canvas = tvg::SwCanvas::gen();
        auto phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasCreate.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 2: Canvas target setup
        phaseStart = phaseEnd;
        canvas->target(pixels.data(), width, width, height, tvg::ColorSpace::ARGB8888);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasTarget.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 3: Picture load (SVG parsing)
        phaseStart = phaseEnd;
        tvg::Picture* picture = tvg::Picture::gen();
        const std::string& svgData = svgContents[currentFrame];

        if (picture->load(svgData.data(), svgData.size(), "svg", nullptr, false) != tvg::Result::Success) {
            std::cerr << "Failed to load SVG frame " << currentFrame << std::endl;
            // Remove partial phase data to keep vectors synchronized
            phase_canvasCreate.pop_back();
            phase_canvasTarget.pop_back();
            tvg::Paint::rel(picture);  // Use ThorVG's release API - picture was created but not added to canvas
            delete canvas;
            running = false;
            continue;
        }
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_pictureLoad.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 4: Transform setup (scale/translate)
        phaseStart = phaseEnd;
        float pw, ph;
        picture->size(&pw, &ph);
        float scale = std::min((float)width / pw, (float)height / ph);
        picture->scale(scale);
        picture->translate((width - pw * scale) / 2, (height - ph * scale) / 2);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_transform.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 5: Canvas add
        phaseStart = phaseEnd;
        canvas->add(picture);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasAdd.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Calculate total parse time (phases 1-5)
        auto parseEnd = phaseEnd;
        double parseMs = std::chrono::duration<double, std::milli>(parseEnd - frameStart).count();
        parseTimes.push_back(parseMs);

        // Phase 6: Buffer clear
        phaseStart = phaseEnd;
        std::fill(pixels.begin(), pixels.end(), 0);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_bufferClear.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 7: Canvas update (scene graph update)
        phaseStart = phaseEnd;
        canvas->update();
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasUpdate.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 8: Canvas draw (rasterization)
        phaseStart = phaseEnd;
        canvas->draw();
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasDraw.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 9: Canvas sync (wait for completion)
        phaseStart = phaseEnd;
        canvas->sync();
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_canvasSync.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Save screenshot after first frame
        if (screenshotPath && !screenshotSaved) {
            if (saveScreenshotPPM(pixels, width, height, screenshotPath)) {
                std::cerr << "Screenshot saved: " << screenshotPath << " (" << width << "x" << height << ")" << std::endl;
            }
            screenshotSaved = true;
        }

        // Phase 10: Texture update
        phaseStart = phaseEnd;
        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_textureUpdate.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        // Phase 11: SDL render and present
        phaseStart = phaseEnd;
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        phaseEnd = std::chrono::high_resolution_clock::now();
        phase_sdlPresent.push_back(std::chrono::duration<double, std::milli>(phaseEnd - phaseStart).count());

        delete canvas;  // This also deletes picture (canvas owns it after add())

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
            snprintf(title, sizeof(title), "ThorVG Player - FPS: %.1f (avg: %.1f) | Frame: %zu/%zu",
                     currentFps, avgFps, currentFrame + 1, svgContents.size());
            SDL_SetWindowTitle(window, title);
        }

        // Advance to next frame (loop in folder mode, stay at 0 in single file mode)
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

    double avgFps = frameTimes.empty() ? 0 : frameTimes.size() / totalTime;
    // Guard against edge case where no frames were rendered (minFrameTime stays at 1e9)
    double minFps = (maxFrameTime > 0 && !frameTimes.empty()) ? 1000.0 / maxFrameTime : 0;
    double maxFps = (minFrameTime < 1e8 && !frameTimes.empty()) ? 1000.0 / minFrameTime : 0;

    // Calculate phase averages
    auto calcAvg = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0;
        double sum = 0;
        for (double t : v) sum += t;
        return sum / v.size();
    };

    double avg_canvasCreate = calcAvg(phase_canvasCreate);
    double avg_canvasTarget = calcAvg(phase_canvasTarget);
    double avg_pictureLoad = calcAvg(phase_pictureLoad);
    double avg_transform = calcAvg(phase_transform);
    double avg_canvasAdd = calcAvg(phase_canvasAdd);
    double avg_bufferClear = calcAvg(phase_bufferClear);
    double avg_canvasUpdate = calcAvg(phase_canvasUpdate);
    double avg_canvasDraw = calcAvg(phase_canvasDraw);
    double avg_canvasSync = calcAvg(phase_canvasSync);
    double avg_textureUpdate = calcAvg(phase_textureUpdate);
    double avg_sdlPresent = calcAvg(phase_sdlPresent);

    // Cleanup
    tvg::Initializer::term();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    // Output
    if (jsonOutput) {
        std::cout << "{";
        std::cout << "\"player\":\"thorvg\",";
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
        std::cout << "\"resolution\":\"" << width << "x" << height << "\",";
        // Detailed phase timing
        std::cout << "\"phases\":{";
        std::cout << "\"canvas_create_ms\":" << avg_canvasCreate << ",";
        std::cout << "\"canvas_target_ms\":" << avg_canvasTarget << ",";
        std::cout << "\"picture_load_ms\":" << avg_pictureLoad << ",";
        std::cout << "\"transform_ms\":" << avg_transform << ",";
        std::cout << "\"canvas_add_ms\":" << avg_canvasAdd << ",";
        std::cout << "\"buffer_clear_ms\":" << avg_bufferClear << ",";
        std::cout << "\"canvas_update_ms\":" << avg_canvasUpdate << ",";
        std::cout << "\"canvas_draw_ms\":" << avg_canvasDraw << ",";
        std::cout << "\"canvas_sync_ms\":" << avg_canvasSync << ",";
        std::cout << "\"texture_update_ms\":" << avg_textureUpdate << ",";
        std::cout << "\"sdl_present_ms\":" << avg_sdlPresent;
        std::cout << "}";
        std::cout << "}" << std::endl;
    } else {
        std::cout << "\n=== ThorVG Benchmark Results ===" << std::endl;
        std::cout << "Mode: " << (folderMode ? "Folder sequence" : "Single file") << std::endl;
        std::cout << "Input: " << inputPath << std::endl;
        if (folderMode) {
            std::cout << "Frame count: " << svgContents.size() << std::endl;
        }
        std::cout << "Resolution: " << width << "x" << height << std::endl;
        std::cout << "Duration: " << totalTime << "s" << std::endl;
        std::cout << "Frames rendered: " << totalFramesRendered << std::endl;
        std::cout << "Average FPS: " << avgFps << std::endl;
        std::cout << "Average frame time: " << avgFrameTime << " ms" << std::endl;
        std::cout << "Average parse time: " << avgParseTime << " ms" << std::endl;
        std::cout << "FPS range: " << minFps << " - " << maxFps << std::endl;
        std::cout << "\n--- Phase Timing Breakdown ---" << std::endl;
        std::cout << "Canvas create:   " << avg_canvasCreate << " ms" << std::endl;
        std::cout << "Canvas target:   " << avg_canvasTarget << " ms" << std::endl;
        std::cout << "Picture load:    " << avg_pictureLoad << " ms (SVG parsing)" << std::endl;
        std::cout << "Transform:       " << avg_transform << " ms" << std::endl;
        std::cout << "Canvas add:      " << avg_canvasAdd << " ms" << std::endl;
        std::cout << "Buffer clear:    " << avg_bufferClear << " ms" << std::endl;
        std::cout << "Canvas update:   " << avg_canvasUpdate << " ms" << std::endl;
        std::cout << "Canvas draw:     " << avg_canvasDraw << " ms (rasterization)" << std::endl;
        std::cout << "Canvas sync:     " << avg_canvasSync << " ms" << std::endl;
        std::cout << "Texture update:  " << avg_textureUpdate << " ms" << std::endl;
        std::cout << "SDL present:     " << avg_sdlPresent << " ms" << std::endl;
    }

    return 0;
}
