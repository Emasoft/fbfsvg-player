// simple_draw.cpp - Simple Skia example that draws shapes to a PNG file
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkStream.h"
#include "include/core/SkData.h"
#include "include/encode/SkPngEncoder.h"

#include <iostream>

int main() {
    // Create a raster surface (CPU-based) with dimensions 800x600
    const int width = 800;
    const int height = 600;

    SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);

    if (!surface) {
        std::cerr << "Failed to create surface" << std::endl;
        return 1;
    }

    SkCanvas* canvas = surface->getCanvas();

    // Clear the canvas with a light blue background
    canvas->clear(SkColorSetRGB(135, 206, 235));  // Sky blue

    // Draw a red rectangle
    SkPaint rectPaint;
    rectPaint.setColor(SkColorSetRGB(220, 20, 60));  // Crimson
    rectPaint.setAntiAlias(true);
    canvas->drawRect(SkRect::MakeXYWH(50, 50, 200, 150), rectPaint);

    // Draw a green filled circle
    SkPaint circlePaint;
    circlePaint.setColor(SkColorSetRGB(34, 139, 34));  // Forest green
    circlePaint.setAntiAlias(true);
    canvas->drawCircle(450, 150, 100, circlePaint);

    // Draw a blue outlined circle
    SkPaint outlinePaint;
    outlinePaint.setColor(SkColorSetRGB(0, 0, 139));  // Dark blue
    outlinePaint.setAntiAlias(true);
    outlinePaint.setStyle(SkPaint::kStroke_Style);
    outlinePaint.setStrokeWidth(5);
    canvas->drawCircle(650, 350, 80, outlinePaint);

    // Draw a yellow rounded rectangle
    SkPaint roundRectPaint;
    roundRectPaint.setColor(SkColorSetRGB(255, 215, 0));  // Gold
    roundRectPaint.setAntiAlias(true);
    canvas->drawRoundRect(SkRect::MakeXYWH(100, 300, 250, 180), 20, 20, roundRectPaint);

    // Draw an orange triangle using path
    SkPaint trianglePaint;
    trianglePaint.setColor(SkColorSetRGB(255, 140, 0));  // Dark orange
    trianglePaint.setAntiAlias(true);

    SkPath triangle;
    triangle.moveTo(550, 450);
    triangle.lineTo(650, 550);
    triangle.lineTo(450, 550);
    triangle.close();
    canvas->drawPath(triangle, trianglePaint);

    // Draw text
    SkPaint textPaint;
    textPaint.setColor(SkColorSetRGB(25, 25, 112));  // Midnight blue
    textPaint.setAntiAlias(true);

    SkFont font;
    font.setSize(36);

    canvas->drawString("Skia Universal Binary Demo", 180, 550, font, textPaint);

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
    SkFILEWStream fileStream("skia_output.png");
    if (!fileStream.isValid()) {
        std::cerr << "Failed to open output file" << std::endl;
        return 1;
    }

    fileStream.write(pngData->data(), pngData->size());

    std::cout << "Successfully created skia_output.png (" << width << "x" << height << ")" << std::endl;
    std::cout << "PNG size: " << pngData->size() << " bytes" << std::endl;

    return 0;
}
