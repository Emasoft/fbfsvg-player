// svg_player.cpp - Real-time SVG renderer with performance monitoring
// Usage: svg_player <input.svg>
// Renders SVG continuously in a resizable window (aspect ratio preserved)
// Displays real-time debug info overlay

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_mac_ct.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "modules/svg/include/SkSVGRenderContext.h"

#include <SDL.h>
#include <chrono>
#include <deque>
#include <iostream>
#include <iomanip>
#include <sstream>

using Clock = std::chrono::high_resolution_clock;
using DurationMs = std::chrono::duration<double, std::milli>;
using DurationMicro = std::chrono::duration<double, std::micro>;

// Rolling average calculator
class RollingAverage {
public:
    RollingAverage(size_t windowSize = 120) : maxSize_(windowSize) {}

    void add(double value) {
        values_.push_back(value);
        if (values_.size() > maxSize_) {
            values_.pop_front();
        }
    }

    double average() const {
        if (values_.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values_) sum += v;
        return sum / values_.size();
    }

    double min() const {
        if (values_.empty()) return 0.0;
        double m = values_[0];
        for (double v : values_) if (v < m) m = v;
        return m;
    }

    double max() const {
        if (values_.empty()) return 0.0;
        double m = values_[0];
        for (double v : values_) if (v > m) m = v;
        return m;
    }

    double last() const {
        if (values_.empty()) return 0.0;
        return values_.back();
    }

    size_t count() const { return values_.size(); }

    void reset() { values_.clear(); }

private:
    std::deque<double> values_;
    size_t maxSize_;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.svg>" << std::endl;
        return 1;
    }

    const char* inputPath = argv[1];

    // Load SVG
    SkFILEStream svgStream(inputPath);
    if (!svgStream.isValid()) {
        std::cerr << "Failed to open: " << inputPath << std::endl;
        return 1;
    }

    sk_sp<SkSVGDOM> svgDom = SkSVGDOM::MakeFromStream(svgStream);
    if (!svgDom) {
        std::cerr << "Failed to parse SVG: " << inputPath << std::endl;
        return 1;
    }

    SkSVGSVG* root = svgDom->getRoot();
    if (!root) {
        std::cerr << "SVG has no root element" << std::endl;
        return 1;
    }

    // Get SVG intrinsic dimensions
    SkSize defaultSize = SkSize::Make(800, 600);
    SkSize svgSize = root->intrinsicSize(SkSVGLengthContext(defaultSize));

    int svgWidth = (svgSize.width() > 0) ? static_cast<int>(svgSize.width()) : 800;
    int svgHeight = (svgSize.height() > 0) ? static_cast<int>(svgSize.height()) : 600;
    float aspectRatio = static_cast<float>(svgWidth) / svgHeight;

    std::cout << "SVG dimensions: " << svgWidth << "x" << svgHeight << std::endl;
    std::cout << "Aspect ratio: " << aspectRatio << std::endl;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create window at SVG native resolution (or scaled if too large)
    int windowWidth = svgWidth;
    int windowHeight = svgHeight;

    // Limit initial window size to 1200px max dimension
    if (windowWidth > 1200 || windowHeight > 1200) {
        if (windowWidth > windowHeight) {
            windowWidth = 1200;
            windowHeight = static_cast<int>(1200 / aspectRatio);
        } else {
            windowHeight = 1200;
            windowWidth = static_cast<int>(1200 * aspectRatio);
        }
    }

    SDL_Window* window = SDL_CreateWindow(
        "SVG Player - Skia",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // VSync state
    bool vsyncEnabled = false;

    // Create renderer (initially without VSync)
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Get actual renderer output size (accounts for HiDPI/Retina)
    int rendererW, rendererH;
    SDL_GetRendererOutputSize(renderer, &rendererW, &rendererH);
    float hiDpiScale = static_cast<float>(rendererW) / windowWidth;
    std::cout << "HiDPI scale factor: " << hiDpiScale << std::endl;

    // Setup font for debug overlay
    sk_sp<SkFontMgr> fontMgr = SkFontMgr_New_CoreText(nullptr);
    sk_sp<SkTypeface> typeface = fontMgr->matchFamilyStyle("Menlo", SkFontStyle::Normal());
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle("Courier", SkFontStyle::Normal());
    }
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    }

    SkFont debugFont(typeface, 14 * hiDpiScale);
    debugFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);

    // Paint for debug text background
    SkPaint bgPaint;
    bgPaint.setColor(SkColorSetARGB(200, 0, 0, 0));
    bgPaint.setStyle(SkPaint::kFill_Style);

    // Paint for debug text
    SkPaint textPaint;
    textPaint.setColor(SK_ColorWHITE);
    textPaint.setAntiAlias(true);

    // Paint for highlight values
    SkPaint highlightPaint;
    highlightPaint.setColor(SkColorSetRGB(0, 255, 128));
    highlightPaint.setAntiAlias(true);

    // Paint for key hints
    SkPaint keyPaint;
    keyPaint.setColor(SkColorSetRGB(255, 200, 100));
    keyPaint.setAntiAlias(true);

    // Performance tracking
    RollingAverage renderTimes(120);
    RollingAverage frameTimes(120);
    RollingAverage copyTimes(120);
    uint64_t frameCount = 0;
    auto startTime = Clock::now();
    auto lastFrameTime = Clock::now();

    // Current render dimensions (in actual pixels, not logical points)
    int renderWidth = static_cast<int>(windowWidth * hiDpiScale);
    int renderHeight = static_cast<int>(windowHeight * hiDpiScale);

    // Create initial texture
    SDL_Texture* texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        renderWidth, renderHeight);

    // Skia surface
    sk_sp<SkSurface> surface;

    auto createSurface = [&](int w, int h) {
        SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(w, h);
        surface = SkSurfaces::Raster(imageInfo);
        return surface != nullptr;
    };

    if (!createSurface(renderWidth, renderHeight)) {
        std::cerr << "Failed to create Skia surface" << std::endl;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    SDL_Event event;

    std::cout << "\nControls:" << std::endl;
    std::cout << "  ESC/Q - Quit" << std::endl;
    std::cout << "  V - Toggle VSync" << std::endl;
    std::cout << "  R - Reset statistics" << std::endl;
    std::cout << "  Resize window to change render resolution" << std::endl;
    std::cout << "\nRendering..." << std::endl;

    while (running) {
        auto frameStart = Clock::now();

        // Calculate frame time from last frame
        DurationMs frameTime = frameStart - lastFrameTime;
        lastFrameTime = frameStart;
        if (frameCount > 0) {
            frameTimes.add(frameTime.count());
        }

        // Handle events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE ||
                        event.key.keysym.sym == SDLK_q) {
                        running = false;
                    } else if (event.key.keysym.sym == SDLK_r) {
                        renderTimes.reset();
                        frameTimes.reset();
                        copyTimes.reset();
                        frameCount = 0;
                        startTime = Clock::now();
                        std::cout << "Statistics reset" << std::endl;
                    } else if (event.key.keysym.sym == SDLK_v) {
                        // Toggle VSync by recreating renderer
                        vsyncEnabled = !vsyncEnabled;

                        SDL_DestroyTexture(texture);
                        SDL_DestroyRenderer(renderer);

                        Uint32 flags = SDL_RENDERER_ACCELERATED;
                        if (vsyncEnabled) {
                            flags |= SDL_RENDERER_PRESENTVSYNC;
                        }

                        renderer = SDL_CreateRenderer(window, -1, flags);
                        if (!renderer) {
                            std::cerr << "Failed to recreate renderer!" << std::endl;
                            running = false;
                            break;
                        }

                        // Recreate texture
                        texture = SDL_CreateTexture(renderer,
                            SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            renderWidth, renderHeight);

                        // Reset stats after VSync change
                        renderTimes.reset();
                        frameTimes.reset();
                        copyTimes.reset();
                        frameCount = 0;
                        startTime = Clock::now();

                        std::cout << "VSync: " << (vsyncEnabled ? "ON" : "OFF") << std::endl;
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {

                        // Get actual renderer output size (HiDPI aware)
                        int actualW, actualH;
                        SDL_GetRendererOutputSize(renderer, &actualW, &actualH);

                        float windowAspect = static_cast<float>(actualW) / actualH;

                        if (windowAspect > aspectRatio) {
                            renderHeight = actualH;
                            renderWidth = static_cast<int>(actualH * aspectRatio);
                        } else {
                            renderWidth = actualW;
                            renderHeight = static_cast<int>(actualW / aspectRatio);
                        }

                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(renderer,
                            SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            renderWidth, renderHeight);

                        createSurface(renderWidth, renderHeight);
                    }
                    break;
            }
        }

        if (!running) break;

        // === RENDER SVG ===
        auto renderStart = Clock::now();

        SkCanvas* canvas = surface->getCanvas();
        canvas->clear(SK_ColorWHITE);

        float scaleX = static_cast<float>(renderWidth) / svgWidth;
        float scaleY = static_cast<float>(renderHeight) / svgHeight;
        float scale = std::min(scaleX, scaleY);

        float offsetX = (renderWidth - svgWidth * scale) / 2.0f;
        float offsetY = (renderHeight - svgHeight * scale) / 2.0f;

        canvas->save();
        canvas->translate(offsetX, offsetY);
        canvas->scale(scale, scale);

        svgDom->setContainerSize(SkSize::Make(svgWidth, svgHeight));
        svgDom->render(canvas);

        canvas->restore();

        auto renderEnd = Clock::now();
        DurationMs renderTime = renderEnd - renderStart;
        renderTimes.add(renderTime.count());

        // === DRAW DEBUG OVERLAY ===
        auto totalElapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
        double fps = (frameCount > 0) ? frameCount / totalElapsed : 0;
        double instantFps = (frameTimes.last() > 0) ? 1000.0 / frameTimes.last() : 0;

        // Draw semi-transparent background for debug info (scaled for HiDPI)
        float lineHeight = 18 * hiDpiScale;
        float padding = 8 * hiDpiScale;
        float boxWidth = 300 * hiDpiScale;
        float boxHeight = lineHeight * 14 + padding * 2;

        canvas->drawRect(SkRect::MakeXYWH(0, 0, boxWidth, boxHeight), bgPaint);

        // Draw debug text lines
        float y = padding + lineHeight;
        float x = padding;
        float valueX = 150 * hiDpiScale;

        auto drawLine = [&](const char* label, const std::string& value, bool highlight = false) {
            canvas->drawString(label, x, y, debugFont, textPaint);
            canvas->drawString(value.c_str(), valueX, y, debugFont, highlight ? highlightPaint : textPaint);
            y += lineHeight;
        };

        auto drawKeyLine = [&](const char* key, const char* label, const std::string& value) {
            canvas->drawString(key, x, y, debugFont, keyPaint);
            canvas->drawString(label, x + 30 * hiDpiScale, y, debugFont, textPaint);
            canvas->drawString(value.c_str(), valueX, y, debugFont, highlightPaint);
            y += lineHeight;
        };

        std::ostringstream oss;

        oss.str(""); oss << std::fixed << std::setprecision(2) << renderTimes.average() << " ms";
        drawLine("Render (avg):", oss.str(), true);

        oss.str(""); oss << std::fixed << std::setprecision(2) << renderTimes.last() << " ms";
        drawLine("Render (last):", oss.str());

        oss.str(""); oss << std::fixed << std::setprecision(2) << renderTimes.min() << " / " << renderTimes.max() << " ms";
        drawLine("Render (min/max):", oss.str());

        y += 4 * hiDpiScale; // Small gap

        oss.str(""); oss << std::fixed << std::setprecision(1) << fps;
        drawLine("FPS (avg):", oss.str(), true);

        oss.str(""); oss << std::fixed << std::setprecision(1) << instantFps;
        drawLine("FPS (instant):", oss.str());

        oss.str(""); oss << std::fixed << std::setprecision(2) << frameTimes.average() << " ms";
        drawLine("Frame time:", oss.str());

        y += 4 * hiDpiScale; // Small gap

        oss.str(""); oss << renderWidth << " x " << renderHeight;
        drawLine("Resolution:", oss.str());

        oss.str(""); oss << svgWidth << " x " << svgHeight;
        drawLine("SVG size:", oss.str());

        oss.str(""); oss << std::fixed << std::setprecision(2) << scale << "x";
        drawLine("Scale:", oss.str());

        oss.str(""); oss << frameCount;
        drawLine("Frames:", oss.str());

        y += 8 * hiDpiScale; // Larger gap before controls

        // VSync toggle line
        drawKeyLine("[V]", "VSync:", vsyncEnabled ? "ON" : "OFF");

        // Reset hint
        canvas->drawString("[R] Reset stats", x, y, debugFont, keyPaint);

        frameCount++;

        // === COPY TO SDL TEXTURE ===
        auto copyStart = Clock::now();

        SkPixmap pixmap;
        if (surface->peekPixels(&pixmap)) {
            void* pixels;
            int pitch;
            SDL_LockTexture(texture, nullptr, &pixels, &pitch);

            const uint8_t* src = static_cast<const uint8_t*>(pixmap.addr());
            uint8_t* dst = static_cast<uint8_t*>(pixels);
            size_t rowBytes = renderWidth * 4;

            for (int row = 0; row < renderHeight; row++) {
                memcpy(dst + row * pitch, src + row * pixmap.rowBytes(), rowBytes);
            }

            SDL_UnlockTexture(texture);
        }

        auto copyEnd = Clock::now();
        DurationMs copyTime = copyEnd - copyStart;
        copyTimes.add(copyTime.count());

        // Clear and render to screen
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderClear(renderer);

        // Get actual renderer output size for proper centering
        int outW, outH;
        SDL_GetRendererOutputSize(renderer, &outW, &outH);

        SDL_Rect destRect;
        destRect.w = renderWidth;
        destRect.h = renderHeight;
        destRect.x = (outW - renderWidth) / 2;
        destRect.y = (outH - renderHeight) / 2;

        SDL_RenderCopy(renderer, texture, nullptr, &destRect);
        SDL_RenderPresent(renderer);
    }

    // Final statistics
    auto totalElapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Total frames: " << frameCount << std::endl;
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << totalElapsed << "s" << std::endl;
    std::cout << "Average FPS: " << (frameCount / totalElapsed) << std::endl;
    std::cout << "Average render time: " << renderTimes.average() << "ms" << std::endl;
    std::cout << "Min render time: " << renderTimes.min() << "ms" << std::endl;
    std::cout << "Max render time: " << renderTimes.max() << "ms" << std::endl;
    std::cout << "Average copy time: " << copyTimes.average() << "ms" << std::endl;

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
