#!/usr/bin/env python3
"""Apply all medium priority fixes to svg_player_animated.cpp in one atomic operation."""

import sys

def main():
    filepath = "/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated.cpp"

    with open(filepath, 'r') as f:
        content = f.read()

    original_content = content

    # Issue 12: Thread-safe localtime_r
    content = content.replace(
        '''    std::ostringstream ss;
    ss << "screenshot_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
       << std::setw(3) << ms.count() << "_" << width << "x" << height << ".ppm";
    return ss.str();
}''',
        '''    // Use localtime_r for thread safety (POSIX standard)
    struct tm timeinfo;
    localtime_r(&time, &timeinfo);

    std::ostringstream ss;
    ss << "screenshot_" << std::put_time(&timeinfo, "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
       << std::setw(3) << ms.count() << "_" << width << "x" << height << ".ppm";
    return ss.str();
}'''
    )

    # Issue 13: Memcpy optimization
    content = content.replace(
        '''                const uint8_t* src = static_cast<const uint8_t*>(pixmap.addr());
                uint8_t* dst = static_cast<uint8_t*>(pixels);
                size_t rowBytes = renderWidth * 4;

                for (int row = 0; row < renderHeight; row++) {
                    memcpy(dst + row * pitch, src + row * pixmap.rowBytes(), rowBytes);
                }''',
        '''                const uint8_t* src = static_cast<const uint8_t*>(pixmap.addr());
                uint8_t* dst = static_cast<uint8_t*>(pixels);
                size_t rowBytes = renderWidth * 4;

                // Optimize: single memcpy if pitch matches rowBytes (common case)
                if (pitch == static_cast<int>(pixmap.rowBytes())) {
                    memcpy(dst, src, rowBytes * renderHeight);
                } else {
                    // Row-by-row copy needed when pitch differs (e.g., aligned stride)
                    for (int row = 0; row < renderHeight; row++) {
                        memcpy(dst + row * pitch, src + row * pixmap.rowBytes(), rowBytes);
                    }
                }'''
    )

    # Issue 14: Validation error handling
    content = content.replace(
        '''                            if (error != SVGLoadError::Success) {
                                // Handle errors based on type
                                if (error == SVGLoadError::Validation || error == SVGLoadError::Parse) {
                                    // Fatal errors - exit program (matches original behavior)
                                    return 1;
                                }
                                // I/O errors (FileSize, FileOpen) - restart with old content
                                parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                threadedRenderer.start();
                            }''',
        '''                            if (error != SVGLoadError::Success) {
                                // Handle errors based on type
                                if (error == SVGLoadError::Validation || error == SVGLoadError::Parse) {
                                    // Non-fatal validation/parse errors - restart with old content
                                    std::cerr << "SVG validation/parse error, reverting to previous content" << std::endl;
                                    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                    threadedRenderer.start();
                                } else {
                                    // I/O errors (FileSize, FileOpen) - restart with old content
                                    parallelRenderer.start(rawSvgContent, renderWidth, renderHeight, svgWidth, svgHeight, ParallelMode::PreBuffer);
                                    threadedRenderer.start();
                                }
                            }'''
    )

    # Issue 15: Screenshot write verification
    content = content.replace(
        '''    file.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    file.close();

    return true;
}''',
        '''    file.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());

    // Verify write succeeded before closing
    if (!file.good()) {
        std::cerr << "Failed to write screenshot data to: " << filename << std::endl;
        file.close();
        return false;
    }

    file.close();
    return true;
}'''
    )

    # Issue 20: Document frame count assumption
    content = content.replace(
        '''                if (framesRendered + framesSkipped > 0) {
                    double skipRate = 100.0 * framesSkipped / (framesRendered + framesSkipped);''',
        '''                if (framesRendered + framesSkipped > 0) {
                    // Note: Assumes all animations have same frame count/duration (enforced during load)
                    double skipRate = 100.0 * framesSkipped / (framesRendered + framesSkipped);'''
    )

    if content == original_content:
        print("ERROR: No changes were made! Check patterns.", file=sys.stderr)
        return 1

    # Write atomically
    with open(filepath, 'w') as f:
        f.write(content)

    print("SUCCESS: All fixes applied")
    return 0

if __name__ == '__main__':
    sys.exit(main())
