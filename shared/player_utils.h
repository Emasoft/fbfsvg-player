// player_utils.h - Shared utility functions for FBF.SVG Player
// Cross-platform file validation and SVG sequence handling
// Copyright (c) 2024 FBF.SVG Project

#ifndef SHARED_PLAYER_UTILS_H
#define SHARED_PLAYER_UTILS_H

#include <string>
#include <vector>
#include <regex>
#include <algorithm>

// Platform-specific includes for file operations
#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

namespace svgplayer {

// =============================================================================
// File validation helpers
// =============================================================================

// Check if file exists and is a regular file
inline bool fileExists(const char* path) {
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFREG);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

// Check if path is a directory
inline bool isDirectory(const char* path) {
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// Get file size in bytes (returns 0 on error)
inline size_t getFileSize(const char* path) {
#ifdef _WIN32
    struct _stat st;
    if (_stat(path, &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
#else
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
#endif
    return 0;
}

// Maximum SVG file size - effectively unlimited (8 GB practical limit)
static constexpr size_t MAX_SVG_FILE_SIZE = 8ULL * 1024 * 1024 * 1024;

// =============================================================================
// SVG content validation
// =============================================================================

// Validate SVG content (basic check for SVG structure)
inline bool validateSVGContent(const std::string& content) {
    // Check minimum length
    if (content.length() < 20) {
        return false;
    }
    // Check for SVG tag (case-insensitive search for <svg)
    size_t pos = content.find("<svg");
    if (pos == std::string::npos) {
        pos = content.find("<SVG");
    }
    return pos != std::string::npos;
}

// =============================================================================
// SVG Image Sequence (folder of individual SVG frames) support
// =============================================================================

// Extract frame number from filename (e.g., "frame_0001.svg" -> 1)
inline int extractFrameNumber(const std::string& filename) {
    // Try pattern: name_NNNN.svg (underscore before number)
    std::regex pattern("_(\\d+)\\.svg$", std::regex::icase);
    std::smatch match;
    if (std::regex_search(filename, match, pattern)) {
        return std::stoi(match[1].str());
    }
    // Try fallback: NNNN.svg (just number before extension)
    std::regex fallback("(\\d+)\\.svg$", std::regex::icase);
    if (std::regex_search(filename, match, fallback)) {
        return std::stoi(match[1].str());
    }
    return -1;  // No number found
}

#ifndef _WIN32
// Scan folder for SVG files and return sorted list of paths (POSIX only)
// Windows uses FindFirstFile/FindNextFile API instead
inline std::vector<std::string> scanFolderForSVGSequence(const std::string& folderPath) {
    std::vector<std::pair<int, std::string>> frameFiles;

    DIR* dir = opendir(folderPath.c_str());
    if (!dir) {
        return {};
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // Check for .svg extension (case-insensitive)
        if (name.size() > 4) {
            std::string ext = name.substr(name.size() - 4);
            if (ext == ".svg" || ext == ".SVG") {
                int frameNum = extractFrameNumber(name);
                std::string fullPath = folderPath + "/" + name;
                frameFiles.push_back({frameNum, fullPath});
            }
        }
    }
    closedir(dir);

    if (frameFiles.empty()) {
        return {};
    }

    // Sort by frame number (files without numbers sorted alphabetically at end)
    std::sort(frameFiles.begin(), frameFiles.end(), [](const auto& a, const auto& b) {
        if (a.first == -1 && b.first == -1) return a.second < b.second;  // Both no number: alphabetical
        if (a.first == -1) return false;  // No number goes after numbered
        if (b.first == -1) return true;   // Numbered goes before no number
        return a.first < b.first;         // Both numbered: sort by number
    });

    // Extract sorted paths
    std::vector<std::string> result;
    result.reserve(frameFiles.size());
    for (const auto& f : frameFiles) {
        result.push_back(f.second);
    }

    return result;
}
#endif  // !_WIN32

#ifdef _WIN32
// Windows version of scanFolderForSVGSequence using Windows API
inline std::vector<std::string> scanFolderForSVGSequence(const std::string& folderPath) {
    std::vector<std::pair<int, std::string>> frameFiles;

    std::string searchPath = folderPath + "\\*.svg";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return {};
    }

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string name = findData.cFileName;
            int frameNum = extractFrameNumber(name);
            std::string fullPath = folderPath + "\\" + name;
            frameFiles.push_back({frameNum, fullPath});
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    if (frameFiles.empty()) {
        return {};
    }

    // Sort by frame number
    std::sort(frameFiles.begin(), frameFiles.end(), [](const auto& a, const auto& b) {
        if (a.first == -1 && b.first == -1) return a.second < b.second;
        if (a.first == -1) return false;
        if (b.first == -1) return true;
        return a.first < b.first;
    });

    // Extract sorted paths
    std::vector<std::string> result;
    result.reserve(frameFiles.size());
    for (const auto& f : frameFiles) {
        result.push_back(f.second);
    }

    return result;
}
#endif  // _WIN32

}  // namespace svgplayer

#endif  // SHARED_PLAYER_UTILS_H
