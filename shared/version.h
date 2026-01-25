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

#ifndef FBFSVG_PLAYER_VERSION_H
#define FBFSVG_PLAYER_VERSION_H

// =============================================================================
// VERSION NUMBERS - Update these for releases
// =============================================================================

#define FBFSVG_PLAYER_VERSION_MAJOR 0
#define FBFSVG_PLAYER_VERSION_MINOR 10
#define FBFSVG_PLAYER_VERSION_PATCH 0

// Pre-release identifier (comment out or set to 0 for stable releases)
// Set to 1 to enable prerelease, 0 for stable release
#define FBFSVG_PLAYER_HAS_PRERELEASE 1
#define FBFSVG_PLAYER_VERSION_PRERELEASE "alpha"

// Build metadata (optional, e.g., git commit hash - set by build system)
#ifndef FBFSVG_PLAYER_BUILD_ID
#define FBFSVG_PLAYER_BUILD_ID ""
#endif

// =============================================================================
// DERIVED VERSION STRINGS - Do not edit manually
// =============================================================================

// Stringify helpers
#define FBFSVG_PLAYER_STRINGIFY_(x) #x
#define FBFSVG_PLAYER_STRINGIFY(x) FBFSVG_PLAYER_STRINGIFY_(x)

// Core version string: "0.9.0"
#define FBFSVG_PLAYER_VERSION_CORE                    \
    FBFSVG_PLAYER_STRINGIFY(FBFSVG_PLAYER_VERSION_MAJOR) \
    "." FBFSVG_PLAYER_STRINGIFY(FBFSVG_PLAYER_VERSION_MINOR) "." FBFSVG_PLAYER_STRINGIFY(FBFSVG_PLAYER_VERSION_PATCH)

// Full version with pre-release if present
#if FBFSVG_PLAYER_HAS_PRERELEASE
#define FBFSVG_PLAYER_VERSION_STRING FBFSVG_PLAYER_VERSION_CORE "-" FBFSVG_PLAYER_VERSION_PRERELEASE
#else
#define FBFSVG_PLAYER_VERSION_STRING FBFSVG_PLAYER_VERSION_CORE
#endif

// Alias for compatibility
#define FBFSVG_PLAYER_VERSION FBFSVG_PLAYER_VERSION_STRING

// =============================================================================
// BUILD INFORMATION
// =============================================================================

// Build date/time (set at compile time)
#define FBFSVG_PLAYER_BUILD_DATE __DATE__
#define FBFSVG_PLAYER_BUILD_TIME __TIME__

// Platform detection
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
#define FBFSVG_PLAYER_PLATFORM "iOS"
#define FBFSVG_PLAYER_PLATFORM_IOS 1
#else
#define FBFSVG_PLAYER_PLATFORM "macOS"
#define FBFSVG_PLAYER_PLATFORM_MACOS 1
#endif
#elif defined(__linux__)
#define FBFSVG_PLAYER_PLATFORM "Linux"
#define FBFSVG_PLAYER_PLATFORM_LINUX 1
#elif defined(_WIN32)
#define FBFSVG_PLAYER_PLATFORM "Windows"
#define FBFSVG_PLAYER_PLATFORM_WINDOWS 1
#else
#define FBFSVG_PLAYER_PLATFORM "Unknown"
#endif

// Architecture detection
#if defined(__aarch64__) || defined(_M_ARM64)
#define FBFSVG_PLAYER_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#define FBFSVG_PLAYER_ARCH "x64"
#elif defined(__i386__) || defined(_M_IX86)
#define FBFSVG_PLAYER_ARCH "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define FBFSVG_PLAYER_ARCH "arm"
#else
#define FBFSVG_PLAYER_ARCH "unknown"
#endif

// Compiler detection
#if defined(__clang__)
#define FBFSVG_PLAYER_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#define FBFSVG_PLAYER_COMPILER "GCC " FBFSVG_PLAYER_STRINGIFY(__GNUC__) "." FBFSVG_PLAYER_STRINGIFY(__GNUC_MINOR__)
#elif defined(_MSC_VER)
#define FBFSVG_PLAYER_COMPILER "MSVC " FBFSVG_PLAYER_STRINGIFY(_MSC_VER)
#else
#define FBFSVG_PLAYER_COMPILER "Unknown"
#endif

// Build type
#ifdef NDEBUG
#define FBFSVG_PLAYER_BUILD_TYPE "Release"
#else
#define FBFSVG_PLAYER_BUILD_TYPE "Debug"
#endif

// Combined build info string for display
#define FBFSVG_PLAYER_BUILD_INFO                                                                    \
    FBFSVG_PLAYER_PLATFORM "/" FBFSVG_PLAYER_ARCH " " FBFSVG_PLAYER_BUILD_TYPE " (" FBFSVG_PLAYER_BUILD_DATE \
                        " " FBFSVG_PLAYER_BUILD_TIME ")"

// =============================================================================
// PROJECT INFORMATION
// =============================================================================

#define FBFSVG_PLAYER_NAME "SVG Player"
#define FBFSVG_PLAYER_DESCRIPTION "Multi-platform animated SVG player with SMIL animation support"
#define FBFSVG_PLAYER_COPYRIGHT "Copyright (c) 2024-2025"
#define FBFSVG_PLAYER_LICENSE "MIT License"
#define FBFSVG_PLAYER_URL "https://github.com/Emasoft/svg-player"

// =============================================================================
// C++ INTERFACE (when included from C++)
// =============================================================================

#ifdef __cplusplus
#include <sstream>
#include <string>

namespace FBFSVGPlayerVersion {

// Get version components
inline int getMajor() { return FBFSVG_PLAYER_VERSION_MAJOR; }
inline int getMinor() { return FBFSVG_PLAYER_VERSION_MINOR; }
inline int getPatch() { return FBFSVG_PLAYER_VERSION_PATCH; }

// Get version string
inline const char* getVersion() { return FBFSVG_PLAYER_VERSION; }
inline const char* getVersionCore() { return FBFSVG_PLAYER_VERSION_CORE; }

// Get build info
inline const char* getBuildDate() { return FBFSVG_PLAYER_BUILD_DATE; }
inline const char* getBuildTime() { return FBFSVG_PLAYER_BUILD_TIME; }
inline const char* getBuildType() { return FBFSVG_PLAYER_BUILD_TYPE; }
inline const char* getPlatform() { return FBFSVG_PLAYER_PLATFORM; }
inline const char* getArch() { return FBFSVG_PLAYER_ARCH; }
inline const char* getCompiler() { return FBFSVG_PLAYER_COMPILER; }

// Get project info
inline const char* getName() { return FBFSVG_PLAYER_NAME; }
inline const char* getDescription() { return FBFSVG_PLAYER_DESCRIPTION; }
inline const char* getCopyright() { return FBFSVG_PLAYER_COPYRIGHT; }
inline const char* getLicense() { return FBFSVG_PLAYER_LICENSE; }
inline const char* getUrl() { return FBFSVG_PLAYER_URL; }

// Get full version banner (for --version output)
inline std::string getVersionBanner() {
    std::ostringstream oss;
    oss << FBFSVG_PLAYER_NAME << " v" << FBFSVG_PLAYER_VERSION << "\n"
        << FBFSVG_PLAYER_DESCRIPTION << "\n"
        << "\n"
        << "Build:    " << FBFSVG_PLAYER_BUILD_TYPE << " (" << FBFSVG_PLAYER_BUILD_DATE << " " << FBFSVG_PLAYER_BUILD_TIME
        << ")\n"
        << "Platform: " << FBFSVG_PLAYER_PLATFORM << " " << FBFSVG_PLAYER_ARCH << "\n"
        << "Compiler: " << FBFSVG_PLAYER_COMPILER << "\n"
        << "\n"
        << FBFSVG_PLAYER_COPYRIGHT << "\n"
        << FBFSVG_PLAYER_LICENSE << "\n"
        << FBFSVG_PLAYER_URL;
    return oss.str();
}

// Get short version line (for startup banner)
inline std::string getStartupBanner() {
    std::ostringstream oss;
    oss << FBFSVG_PLAYER_NAME << " v" << FBFSVG_PLAYER_VERSION << " [" << FBFSVG_PLAYER_PLATFORM << "/" << FBFSVG_PLAYER_ARCH
        << "]";
    return oss.str();
}

// Compare version (returns -1, 0, or 1)
inline int compareVersion(int major, int minor, int patch) {
    if (FBFSVG_PLAYER_VERSION_MAJOR != major) return FBFSVG_PLAYER_VERSION_MAJOR < major ? -1 : 1;
    if (FBFSVG_PLAYER_VERSION_MINOR != minor) return FBFSVG_PLAYER_VERSION_MINOR < minor ? -1 : 1;
    if (FBFSVG_PLAYER_VERSION_PATCH != patch) return FBFSVG_PLAYER_VERSION_PATCH < patch ? -1 : 1;
    return 0;
}

// Check if version is at least the specified version
inline bool isAtLeast(int major, int minor = 0, int patch = 0) { return compareVersion(major, minor, patch) >= 0; }

}  // namespace FBFSVGPlayerVersion
#endif  // __cplusplus

#endif  // FBFSVG_PLAYER_VERSION_H
