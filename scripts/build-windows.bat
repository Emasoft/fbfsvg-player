@echo off
setlocal enabledelayedexpansion

rem build-windows.bat - Build SVG player for Windows
rem Usage: build-windows.bat [options]
rem Options:
rem   /y               Skip confirmation prompts
rem   /debug           Build with debug symbols
rem   /help            Show this help message

rem Project paths
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
cd /d "%PROJECT_ROOT%"

rem Configuration
set "BUILD_TYPE=release"
set "NON_INTERACTIVE=false"

rem Parse arguments
:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="/y" (
    set "NON_INTERACTIVE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="/debug" (
    set "BUILD_TYPE=debug"
    shift
    goto :parse_args
)
if /i "%~1"=="/help" (
    echo Usage: %~nx0 [options]
    echo Options:
    echo   /y               Skip confirmation prompts
    echo   /debug           Build with debug symbols
    echo   /help            Show this help message
    exit /b 0
)
echo [ERROR] Unknown option: %~1
echo Use /help for usage information
exit /b 1

:done_args

echo.
echo ===================================================
echo  SVG Player - Windows Build Script
echo ===================================================
echo.

rem Check for Visual Studio
echo [INFO] Checking for Visual Studio...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio not found. Please install Visual Studio 2019 or later.
    echo         Download from: https://visualstudio.microsoft.com/
    exit /b 1
)

rem Find VS installation
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if "%VS_PATH%"=="" (
    echo [ERROR] Visual Studio C++ tools not found.
    echo         Please install "Desktop development with C++" workload.
    exit /b 1
)

echo [INFO] Found Visual Studio: %VS_PATH%

rem Initialize VS environment
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found at %VCVARS%
    exit /b 1
)

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio environment
    exit /b 1
)

echo [INFO] Visual Studio environment initialized

rem Check for Skia
set "SKIA_DIR=%PROJECT_ROOT%\skia-build\src\skia"
set "SKIA_OUT=%SKIA_DIR%\out\release-windows"

rem Windows Skia build produces skia.lib (not libskia.lib - that's Unix naming)
if not exist "%SKIA_OUT%\skia.lib" (
    echo [ERROR] Skia library not found at %SKIA_OUT%
    echo.
    echo Please build Skia for Windows first:
    echo   1. Install depot_tools: https://chromium.googlesource.com/chromium/tools/depot_tools.git
    echo   2. Add depot_tools to PATH
    echo   3. In skia-build\src\skia, run:
    echo      gn gen out/release-windows --args="is_debug=false is_official_build=true skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_zlib=false skia_use_system_expat=false skia_use_system_icu=false skia_use_system_harfbuzz=false skia_use_system_freetype2=false skia_enable_svg=true skia_enable_tools=false target_cpu=\"x64\""
    echo      ninja -C out/release-windows skia svg
    exit /b 1
)

echo [INFO] Found Skia at %SKIA_OUT%

rem Check for SDL2
echo [INFO] Checking for SDL2...
set "SDL2_DIR="
for %%D in ("C:\SDL2" "C:\Libraries\SDL2" "%USERPROFILE%\SDL2" "%PROJECT_ROOT%\external\SDL2") do (
    if exist "%%~D\include\SDL.h" (
        set "SDL2_DIR=%%~D"
        goto :found_sdl2
    )
)

rem Try pkg-config (if installed via vcpkg or similar)
where pkg-config >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%i in ('pkg-config --variable=includedir sdl2 2^>nul') do (
        if exist "%%i\SDL.h" (
            for /f "tokens=*" %%j in ('pkg-config --variable=prefix sdl2') do set "SDL2_DIR=%%j"
        )
    )
)

:found_sdl2
if "%SDL2_DIR%"=="" (
    echo [ERROR] SDL2 not found.
    echo.
    echo Please install SDL2:
    echo   1. Download SDL2 development libraries from: https://libsdl.org/download-2.0.php
    echo   2. Extract to C:\SDL2 or %PROJECT_ROOT%\external\SDL2
    echo   3. Ensure include\SDL.h and lib\x64\SDL2.lib exist
    echo.
    echo Or use vcpkg: vcpkg install sdl2:x64-windows
    exit /b 1
)

echo [INFO] Found SDL2 at %SDL2_DIR%

rem Check for Vulkan SDK (required for Graphite backend)
echo [INFO] Checking for Vulkan SDK...
set "VULKAN_SDK_DIR="
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
        set "VULKAN_SDK_DIR=%VULKAN_SDK%"
        goto :found_vulkan
    )
)
rem Try common Vulkan SDK locations
for %%D in ("C:\VulkanSDK\*" "%LOCALAPPDATA%\VulkanSDK\*") do (
    if exist "%%~D\Include\vulkan\vulkan.h" (
        set "VULKAN_SDK_DIR=%%~D"
        goto :found_vulkan
    )
)

:found_vulkan
if "%VULKAN_SDK_DIR%"=="" (
    echo [WARN] Vulkan SDK not found - Graphite GPU backend will not be available.
    echo        Download from: https://vulkan.lunarg.com/
    echo        Set VULKAN_SDK environment variable after installation.
    set "VULKAN_AVAILABLE=false"
) else (
    echo [INFO] Found Vulkan SDK at %VULKAN_SDK_DIR%
    set "VULKAN_AVAILABLE=true"
)

rem Create build directory
set "BUILD_DIR=%PROJECT_ROOT%\build\windows"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

rem Determine compiler flags (NOMINMAX prevents Windows min/max macros conflicting with Skia)
set "CXXFLAGS=/std:c++17 /EHsc /W3 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /DNOMINMAX"
set "INCLUDES=/I"%PROJECT_ROOT%" /I"%SKIA_DIR%" /I"%SKIA_DIR%\include" /I"%SKIA_DIR%\src" /I"%SDL2_DIR%\include" /I"%PROJECT_ROOT%\src" /I"%PROJECT_ROOT%\shared""
set "LIBPATHS=/LIBPATH:"%SKIA_OUT%" /LIBPATH:"%SDL2_DIR%\lib\x64""
set "LIBS=skia.lib svg.lib skshaper.lib skresources.lib skunicode_core.lib skunicode_icu.lib SDL2.lib SDL2main.lib opengl32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib ole32.lib shlwapi.lib advapi32.lib dwrite.lib"

rem Add Vulkan include/lib paths if Vulkan SDK is available (for Graphite backend)
if "%VULKAN_AVAILABLE%"=="true" (
    set "INCLUDES=%INCLUDES% /I"%VULKAN_SDK_DIR%\Include""
    set "LIBPATHS=%LIBPATHS% /LIBPATH:"%VULKAN_SDK_DIR%\Lib""
    set "LIBS=%LIBS% vulkan-1.lib"
    rem Define preprocessor macro to disable the stub implementation
    set "CXXFLAGS=%CXXFLAGS% /DGRAPHITE_VULKAN_AVAILABLE"
    echo [INFO] Vulkan Graphite backend will be available
)

if "%BUILD_TYPE%"=="debug" (
    set "CXXFLAGS=%CXXFLAGS% /Od /Zi /DEBUG"
    set "LDFLAGS=/DEBUG"
) else (
    set "CXXFLAGS=%CXXFLAGS% /O2 /DNDEBUG"
    set "LDFLAGS="
)

rem Source files
set "SOURCES="
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\svg_player_animated_windows.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\file_dialog_windows.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\folder_browser.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\thumbnail_cache.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\remote_control.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\shared\SVGAnimationController.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\shared\SVGGridCompositor.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\shared\svg_instrumentation.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\shared\DirtyRegionTracker.cpp""
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\shared\ElementBoundsExtractor.cpp""

rem Always include the Graphite stub (provides createGraphiteContext returning nullptr)
rem The stub is disabled via preprocessor when GRAPHITE_VULKAN_AVAILABLE is defined
set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\graphite_context_stub.cpp""

rem Add Graphite Vulkan context if Vulkan SDK is available (overrides stub)
if "%VULKAN_AVAILABLE%"=="true" (
    set "SOURCES=%SOURCES% "%PROJECT_ROOT%\src\graphite_context_vulkan.cpp""
)

echo.
echo [STEP] Compiling SVG Player for Windows (%BUILD_TYPE%)...
echo.

rem Compile
cl.exe %CXXFLAGS% %INCLUDES% %SOURCES% /Fe:"%BUILD_DIR%\fbfsvg-player.exe" /link %LIBPATHS% %LIBS% %LDFLAGS% /SUBSYSTEM:CONSOLE

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    exit /b 1
)

rem Copy SDL2.dll to build directory
if exist "%SDL2_DIR%\lib\x64\SDL2.dll" (
    copy /y "%SDL2_DIR%\lib\x64\SDL2.dll" "%BUILD_DIR%\" >nul
    echo [INFO] Copied SDL2.dll to build directory
)

echo.
echo ===================================================
echo  Build Successful!
echo ===================================================
echo.
echo Output: %BUILD_DIR%\fbfsvg-player.exe
echo.
echo Usage: fbfsvg-player.exe ^<input.svg^>
echo.

exit /b 0
