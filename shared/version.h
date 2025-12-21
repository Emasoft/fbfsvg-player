/**
 * SVG Player - Unified Version Management
 *
 * This header provides centralized version information for all platforms.
 * Update version numbers here - they propagate to macOS, iOS, Linux builds.
 *
 * Version Format: MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
 * - MAJOR: Breaking API changes
 * - MINOR: New features, backward compatible
 * - PATCH: Bug fixes, backward compatible
 */

#ifndef SVG_PLAYER_VERSION_H
#define SVG_PLAYER_VERSION_H

// =============================================================================
// VERSION NUMBERS - Update these for releases
// =============================================================================

#define SVG_PLAYER_VERSION_MAJOR 0
#define SVG_PLAYER_VERSION_MINOR 9
#define SVG_PLAYER_VERSION_PATCH 0

// Pre-release identifier (comment out or set to 0 for stable releases)
// Set to 1 to enable prerelease, 0 for stable release
#define SVG_PLAYER_HAS_PRERELEASE 1
#define SVG_PLAYER_VERSION_PRERELEASE "alpha"

// Build metadata (optional, e.g., git commit hash - set by build system)
#ifndef SVG_PLAYER_BUILD_ID
#define SVG_PLAYER_BUILD_ID ""
#endif

// =============================================================================
// DERIVED VERSION STRINGS - Do not edit manually
// =============================================================================

// Stringify helpers
#define SVG_PLAYER_STRINGIFY_(x) #x
#define SVG_PLAYER_STRINGIFY(x) SVG_PLAYER_STRINGIFY_(x)

// Core version string: "0.9.0"
#define SVG_PLAYER_VERSION_CORE \
    SVG_PLAYER_STRINGIFY(SVG_PLAYER_VERSION_MAJOR) "." \
    SVG_PLAYER_STRINGIFY(SVG_PLAYER_VERSION_MINOR) "." \
    SVG_PLAYER_STRINGIFY(SVG_PLAYER_VERSION_PATCH)

// Full version with pre-release if present
#if SVG_PLAYER_HAS_PRERELEASE
    #define SVG_PLAYER_VERSION_STRING SVG_PLAYER_VERSION_CORE "-" SVG_PLAYER_VERSION_PRERELEASE
#else
    #define SVG_PLAYER_VERSION_STRING SVG_PLAYER_VERSION_CORE
#endif

// Alias for compatibility
#define SVG_PLAYER_VERSION SVG_PLAYER_VERSION_STRING

// =============================================================================
// BUILD INFORMATION
// =============================================================================

// Build date/time (set at compile time)
#define SVG_PLAYER_BUILD_DATE __DATE__
#define SVG_PLAYER_BUILD_TIME __TIME__

// Platform detection
#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
        #define SVG_PLAYER_PLATFORM "iOS"
        #define SVG_PLAYER_PLATFORM_IOS 1
    #else
        #define SVG_PLAYER_PLATFORM "macOS"
        #define SVG_PLAYER_PLATFORM_MACOS 1
    #endif
#elif defined(__linux__)
    #define SVG_PLAYER_PLATFORM "Linux"
    #define SVG_PLAYER_PLATFORM_LINUX 1
#elif defined(_WIN32)
    #define SVG_PLAYER_PLATFORM "Windows"
    #define SVG_PLAYER_PLATFORM_WINDOWS 1
#else
    #define SVG_PLAYER_PLATFORM "Unknown"
#endif

// Architecture detection
#if defined(__aarch64__) || defined(_M_ARM64)
    #define SVG_PLAYER_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
    #define SVG_PLAYER_ARCH "x64"
#elif defined(__i386__) || defined(_M_IX86)
    #define SVG_PLAYER_ARCH "x86"
#elif defined(__arm__) || defined(_M_ARM)
    #define SVG_PLAYER_ARCH "arm"
#else
    #define SVG_PLAYER_ARCH "unknown"
#endif

// Compiler detection
#if defined(__clang__)
    #define SVG_PLAYER_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
    #define SVG_PLAYER_COMPILER "GCC " SVG_PLAYER_STRINGIFY(__GNUC__) "." SVG_PLAYER_STRINGIFY(__GNUC_MINOR__)
#elif defined(_MSC_VER)
    #define SVG_PLAYER_COMPILER "MSVC " SVG_PLAYER_STRINGIFY(_MSC_VER)
#else
    #define SVG_PLAYER_COMPILER "Unknown"
#endif

// Build type
#ifdef NDEBUG
    #define SVG_PLAYER_BUILD_TYPE "Release"
#else
    #define SVG_PLAYER_BUILD_TYPE "Debug"
#endif

// Combined build info string for display
#define SVG_PLAYER_BUILD_INFO \
    SVG_PLAYER_PLATFORM "/" SVG_PLAYER_ARCH " " SVG_PLAYER_BUILD_TYPE \
    " (" SVG_PLAYER_BUILD_DATE " " SVG_PLAYER_BUILD_TIME ")"

// =============================================================================
// PROJECT INFORMATION
// =============================================================================

#define SVG_PLAYER_NAME "SVG Player"
#define SVG_PLAYER_DESCRIPTION "Multi-platform animated SVG player with SMIL animation support"
#define SVG_PLAYER_COPYRIGHT "Copyright (c) 2024-2025"
#define SVG_PLAYER_LICENSE "MIT License"
#define SVG_PLAYER_URL "https://github.com/Emasoft/svg-player"

// =============================================================================
// C++ INTERFACE (when included from C++)
// =============================================================================

#ifdef __cplusplus
#include <string>
#include <sstream>

namespace SVGPlayerVersion {

// Get version components
inline int getMajor() { return SVG_PLAYER_VERSION_MAJOR; }
inline int getMinor() { return SVG_PLAYER_VERSION_MINOR; }
inline int getPatch() { return SVG_PLAYER_VERSION_PATCH; }

// Get version string
inline const char* getVersion() { return SVG_PLAYER_VERSION; }
inline const char* getVersionCore() { return SVG_PLAYER_VERSION_CORE; }

// Get build info
inline const char* getBuildDate() { return SVG_PLAYER_BUILD_DATE; }
inline const char* getBuildTime() { return SVG_PLAYER_BUILD_TIME; }
inline const char* getBuildType() { return SVG_PLAYER_BUILD_TYPE; }
inline const char* getPlatform() { return SVG_PLAYER_PLATFORM; }
inline const char* getArch() { return SVG_PLAYER_ARCH; }
inline const char* getCompiler() { return SVG_PLAYER_COMPILER; }

// Get project info
inline const char* getName() { return SVG_PLAYER_NAME; }
inline const char* getDescription() { return SVG_PLAYER_DESCRIPTION; }
inline const char* getCopyright() { return SVG_PLAYER_COPYRIGHT; }
inline const char* getLicense() { return SVG_PLAYER_LICENSE; }
inline const char* getUrl() { return SVG_PLAYER_URL; }

// Get full version banner (for --version output)
inline std::string getVersionBanner() {
    std::ostringstream oss;
    oss << SVG_PLAYER_NAME << " v" << SVG_PLAYER_VERSION << "\n"
        << SVG_PLAYER_DESCRIPTION << "\n"
        << "\n"
        << "Build:    " << SVG_PLAYER_BUILD_TYPE << " (" << SVG_PLAYER_BUILD_DATE << " " << SVG_PLAYER_BUILD_TIME << ")\n"
        << "Platform: " << SVG_PLAYER_PLATFORM << " " << SVG_PLAYER_ARCH << "\n"
        << "Compiler: " << SVG_PLAYER_COMPILER << "\n"
        << "\n"
        << SVG_PLAYER_COPYRIGHT << "\n"
        << SVG_PLAYER_LICENSE << "\n"
        << SVG_PLAYER_URL;
    return oss.str();
}

// Get short version line (for startup banner)
inline std::string getStartupBanner() {
    std::ostringstream oss;
    oss << SVG_PLAYER_NAME << " v" << SVG_PLAYER_VERSION
        << " [" << SVG_PLAYER_PLATFORM << "/" << SVG_PLAYER_ARCH << "]";
    return oss.str();
}

// Compare version (returns -1, 0, or 1)
inline int compareVersion(int major, int minor, int patch) {
    if (SVG_PLAYER_VERSION_MAJOR != major)
        return SVG_PLAYER_VERSION_MAJOR < major ? -1 : 1;
    if (SVG_PLAYER_VERSION_MINOR != minor)
        return SVG_PLAYER_VERSION_MINOR < minor ? -1 : 1;
    if (SVG_PLAYER_VERSION_PATCH != patch)
        return SVG_PLAYER_VERSION_PATCH < patch ? -1 : 1;
    return 0;
}

// Check if version is at least the specified version
inline bool isAtLeast(int major, int minor = 0, int patch = 0) {
    return compareVersion(major, minor, patch) >= 0;
}

} // namespace SVGPlayerVersion
#endif // __cplusplus

#endif // SVG_PLAYER_VERSION_H
