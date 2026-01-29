// screenshot_utils.h - Screenshot saving utilities for FBF.SVG Player
// Cross-platform PPM file generation with timestamped filenames
// Copyright (c) 2024 FBF.SVG Project

#ifndef SHARED_SCREENSHOT_UTILS_H
#define SHARED_SCREENSHOT_UTILS_H

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdint>

namespace svgplayer {

// Maximum reasonable screenshot size: 32768x32768 (1 gigapixel)
static constexpr int MAX_SCREENSHOT_DIM = 32768;

// Save screenshot as PPM (Portable Pixmap) - uncompressed format
// PPM P6 format: binary RGB data, no compression, maximum compatibility
// Input: ARGB8888 pixel buffer (32-bit per pixel in BGRA order)
// Output: PPM file with 24-bit RGB (8 bits per channel)
inline bool saveScreenshotPPM(const std::vector<uint32_t>& pixels, int width, int height, const std::string& filename) {
    // Integer overflow protection: validate dimensions before calculating buffer size
    if (width <= 0 || height <= 0 || width > MAX_SCREENSHOT_DIM || height > MAX_SCREENSHOT_DIM) {
        std::cerr << "Invalid screenshot dimensions: " << width << "x" << height << std::endl;
        return false;
    }

    // Use size_t to avoid integer overflow in buffer size calculation
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t rgbBufferSize = pixelCount * 3;

    // Sanity check: ensure input buffer has expected size
    if (pixels.size() < pixelCount) {
        std::cerr << "Pixel buffer too small: " << pixels.size() << " < " << pixelCount << std::endl;
        return false;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for screenshot: " << filename << std::endl;
        return false;
    }

    // PPM P6 header: magic number, width, height, max color value
    file << "P6\n" << width << " " << height << "\n255\n";

    // Convert BGRA to RGB24 and write raw bytes
    // ThreadedRenderer uses kBGRA_8888_SkColorType for consistent cross-platform behavior
    // BGRA in memory: [B, G, R, A] → uint32_t on little-endian: 0xAARRGGBB
    std::vector<uint8_t> rgb(rgbBufferSize);
    for (size_t i = 0; i < pixelCount; ++i) {
        uint32_t pixel = pixels[i];
        rgb[i * 3 + 0] = (pixel >> 16) & 0xFF;  // R (byte 2 in BGRA)
        rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;   // G (byte 1 in BGRA)
        rgb[i * 3 + 2] = pixel & 0xFF;          // B (byte 0 in BGRA)
    }

    file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));

    // Verify write succeeded before closing
    if (!file.good()) {
        std::cerr << "Failed to write screenshot data to: " << filename << std::endl;
        file.close();
        return false;
    }

    file.close();
    return true;
}

// Generate timestamped screenshot filename with resolution
inline std::string generateScreenshotFilename(int width, int height) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm timeinfo;
#ifdef _WIN32
    // Windows uses localtime_s with reversed parameter order
    localtime_s(&timeinfo, &time);
#else
    // POSIX uses localtime_r for thread safety
    localtime_r(&time, &timeinfo);
#endif

    std::ostringstream ss;
    ss << "screenshot_" << std::put_time(&timeinfo, "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
       << std::setw(3) << ms.count() << "_" << width << "x" << height << ".ppm";
    return ss.str();
}

}  // namespace svgplayer

#endif  // SHARED_SCREENSHOT_UTILS_H
