#ifndef SVG_PLAYER_TEST_ENVIRONMENT_H
#define SVG_PLAYER_TEST_ENVIRONMENT_H

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace svgplayer {
namespace testing {

/**
 * RAII-based test fixture providing controlled SVG test files.
 * Creates a temporary directory on construction and cleans up on destruction.
 */
class ControlledTestEnvironment {
public:
    /**
     * Creates a temporary test directory with unique name.
     * Pattern: /tmp/svgplayer_test_XXXXXX
     */
    ControlledTestEnvironment() {
        // Create unique temporary directory using mkdtemp
        char temp_pattern[] = "/tmp/svgplayer_test_XXXXXX";
        char* temp_dir = mkdtemp(temp_pattern);
        if (!temp_dir) {
            throw std::runtime_error("Failed to create temporary test directory");
        }
        test_directory_ = temp_dir;
    }

    /**
     * RAII cleanup - removes all test files and directory.
     */
    ~ControlledTestEnvironment() {
        cleanup();
    }

    // Disable copy/move to prevent double-cleanup
    ControlledTestEnvironment(const ControlledTestEnvironment&) = delete;
    ControlledTestEnvironment& operator=(const ControlledTestEnvironment&) = delete;
    ControlledTestEnvironment(ControlledTestEnvironment&&) = delete;
    ControlledTestEnvironment& operator=(ControlledTestEnvironment&&) = delete;

    /**
     * Create a minimal static SVG file.
     * @param name Filename (without .svg extension)
     * @param width SVG width in pixels
     * @param height SVG height in pixels
     * @return Full path to created file
     */
    std::string addStaticSVG(const std::string& name, int width = 100, int height = 100) {
        std::ostringstream svg;
        svg << R"(<?xml version="1.0" encoding="UTF-8"?>)"
            << "\n<svg viewBox=\"0 0 " << width << " " << height << "\" "
            << "xmlns=\"http://www.w3.org/2000/svg\">\n"
            << "  <rect width=\"100%\" height=\"100%\" fill=\"#333\"/>\n"
            << "  <text x=\"50%\" y=\"50%\" text-anchor=\"middle\" fill=\"white\">Test</text>\n"
            << "</svg>\n";

        return writeFile(name + ".svg", svg.str());
    }

    /**
     * Create an animated SVG with SMIL discrete animation.
     * @param name Filename (without .svg extension)
     * @param frames Number of discrete color frames
     * @param duration Total animation duration in seconds
     * @return Full path to created file
     */
    std::string addAnimatedSVG(const std::string& name, int frames = 4, double duration = 2.0) {
        // Generate color values for discrete animation
        std::ostringstream colors;
        std::ostringstream keyTimes;

        for (int i = 0; i < frames; ++i) {
            // Generate distinct colors using hue rotation
            int hue = (i * 360) / frames;
            colors << "hsl(" << hue << ", 70%, 50%)";
            keyTimes << (static_cast<double>(i) / frames);

            if (i < frames - 1) {
                colors << ";";
                keyTimes << ";";
            }
        }

        std::ostringstream svg;
        svg << R"(<?xml version="1.0" encoding="UTF-8"?>)"
            << "\n<svg viewBox=\"0 0 100 100\" xmlns=\"http://www.w3.org/2000/svg\">\n"
            << "  <rect id=\"frame\" width=\"100\" height=\"100\">\n"
            << "    <animate attributeName=\"fill\" dur=\"" << duration << "s\" "
            << "repeatCount=\"indefinite\" calcMode=\"discrete\"\n"
            << "             values=\"" << colors.str() << "\"\n"
            << "             keyTimes=\"" << keyTimes.str() << "\"/>\n"
            << "  </rect>\n"
            << "</svg>\n";

        return writeFile(name + ".svg", svg.str());
    }

    /**
     * Create an intentionally malformed SVG for error testing.
     * @param name Filename (without .svg extension)
     * @return Full path to created file
     */
    std::string addMalformedSVG(const std::string& name) {
        // Malformed: Unclosed tags, missing namespace, invalid attributes
        std::string svg = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg viewBox="0 0 100 100">
  <rect width="100" height="100" fill="red"
  <circle cx="50" cy="50" r="invalid"/>
  <text>Unclosed text
  <g>
    <path d="M10,10 L90,90
</svg>
)";
        return writeFile(name + ".svg", svg);
    }

    /**
     * Create an SVG padded to a specific file size (for testing large files).
     * @param name Filename (without .svg extension)
     * @param sizeBytes Target file size in bytes
     * @return Full path to created file
     */
    std::string addLargeSVG(const std::string& name, size_t sizeBytes) {
        std::ostringstream svg;
        svg << R"(<?xml version="1.0" encoding="UTF-8"?>)"
            << "\n<svg viewBox=\"0 0 100 100\" xmlns=\"http://www.w3.org/2000/svg\">\n"
            << "  <rect width=\"100\" height=\"100\" fill=\"#333\"/>\n"
            << "  <!-- Padding to reach target size: ";

        // Calculate how much padding is needed
        std::string base_content = svg.str();
        std::string footer = " -->\n</svg>\n";
        size_t current_size = base_content.size() + footer.size();

        if (sizeBytes > current_size) {
            size_t padding_size = sizeBytes - current_size;
            std::string padding(padding_size, 'X');
            svg << padding;
        }

        svg << footer;
        return writeFile(name + ".svg", svg.str());
    }

    /**
     * Get the full path to a test file.
     * @param name Filename (including extension)
     * @return Full filesystem path
     */
    std::string getPath(const std::string& name) const {
        std::filesystem::path path = test_directory_;
        path /= name;
        return path.string();
    }

    /**
     * Get the test directory path.
     * @return Full path to temporary test directory
     */
    std::string getTestDirectory() const {
        return test_directory_;
    }

    /**
     * Get list of all test files created.
     * @return Vector of filenames (with extensions)
     */
    std::vector<std::string> getTestFiles() const {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(test_directory_)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().filename().string());
            }
        }
        return files;
    }

    /**
     * Check if a test file exists.
     * @param name Filename (including extension)
     * @return true if file exists
     */
    bool fileExists(const std::string& name) const {
        return std::filesystem::exists(getPath(name));
    }

    /**
     * Manually cleanup all test files and directory.
     * Safe to call multiple times (idempotent).
     */
    void cleanup() {
        if (!test_directory_.empty() && std::filesystem::exists(test_directory_)) {
            std::filesystem::remove_all(test_directory_);
            test_directory_.clear();
        }
    }

private:
    /**
     * Write content to a file in the test directory.
     * @param filename Name of file to create
     * @param content File content
     * @return Full path to created file
     */
    std::string writeFile(const std::string& filename, const std::string& content) {
        std::filesystem::path path = test_directory_;
        path /= filename;

        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("Failed to create test file: " + path.string());
        }

        file << content;
        file.close();

        return path.string();
    }

    std::string test_directory_;  // Path to temporary test directory
};

} // namespace testing
} // namespace svgplayer

#endif // SVG_PLAYER_TEST_ENVIRONMENT_H
