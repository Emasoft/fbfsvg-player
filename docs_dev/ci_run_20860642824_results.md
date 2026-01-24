Windows Build	Build Windows Player	﻿2026-01-09T18:08:48.6645038Z ##[group]Run call scripts\build-windows.bat /y
Windows Build	Build Windows Player	2026-01-09T18:08:48.6645499Z [36;1mcall scripts\build-windows.bat /y[0m
Windows Build	Build Windows Player	2026-01-09T18:08:48.6665474Z shell: C:\Windows\system32\cmd.EXE /D /E:ON /V:OFF /S /C "CALL "{0}""
Windows Build	Build Windows Player	2026-01-09T18:08:48.6665824Z env:
Windows Build	Build Windows Player	2026-01-09T18:08:48.6666067Z   pythonLocation: C:\hostedtoolcache\windows\Python\3.11.9\x64
Windows Build	Build Windows Player	2026-01-09T18:08:48.6666503Z   PKG_CONFIG_PATH: C:\hostedtoolcache\windows\Python\3.11.9\x64/lib/pkgconfig
Windows Build	Build Windows Player	2026-01-09T18:08:48.6666930Z   Python_ROOT_DIR: C:\hostedtoolcache\windows\Python\3.11.9\x64
Windows Build	Build Windows Player	2026-01-09T18:08:48.6667302Z   Python2_ROOT_DIR: C:\hostedtoolcache\windows\Python\3.11.9\x64
Windows Build	Build Windows Player	2026-01-09T18:08:48.6667663Z   Python3_ROOT_DIR: C:\hostedtoolcache\windows\Python\3.11.9\x64
Windows Build	Build Windows Player	2026-01-09T18:08:48.6667954Z ##[endgroup]
Windows Build	Build Windows Player	2026-01-09T18:08:48.6880328Z 
Windows Build	Build Windows Player	2026-01-09T18:08:48.6883765Z ===================================================
Windows Build	Build Windows Player	2026-01-09T18:08:48.6886799Z  SVG Player - Windows Build Script
Windows Build	Build Windows Player	2026-01-09T18:08:48.6889921Z ===================================================
Windows Build	Build Windows Player	2026-01-09T18:08:48.6913915Z 
Windows Build	Build Windows Player	2026-01-09T18:08:48.6919302Z [INFO] Checking for Visual Studio...
Windows Build	Build Windows Player	2026-01-09T18:08:48.7367670Z [INFO] Found Visual Studio: C:\Program Files\Microsoft Visual Studio\2022\Enterprise
Windows Build	Build Windows Player	2026-01-09T18:08:49.9252223Z [INFO] Visual Studio environment initialized
Windows Build	Build Windows Player	2026-01-09T18:08:49.9272310Z [INFO] Found Skia at D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\out\release-windows
Windows Build	Build Windows Player	2026-01-09T18:08:49.9276010Z [INFO] Checking for SDL2...
Windows Build	Build Windows Player	2026-01-09T18:08:49.9298696Z [INFO] Found SDL2 at D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\external\SDL2
Windows Build	Build Windows Player	2026-01-09T18:08:49.9418676Z 
Windows Build	Build Windows Player	2026-01-09T18:08:49.9444016Z [STEP] Compiling SVG Player for Windows (release)...
Windows Build	Build Windows Player	2026-01-09T18:08:49.9469000Z 
Windows Build	Build Windows Player	2026-01-09T18:08:49.9586235Z Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35222 for x64
Windows Build	Build Windows Player	2026-01-09T18:08:49.9588026Z Copyright (C) Microsoft Corporation.  All rights reserved.
Windows Build	Build Windows Player	2026-01-09T18:08:49.9589019Z 
Windows Build	Build Windows Player	2026-01-09T18:08:49.9639685Z svg_player_animated_windows.cpp
Windows Build	Build Windows Player	2026-01-09T18:08:50.3200652Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(885): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3202381Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(886): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3204011Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(887): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3205608Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(888): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3237241Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(1007): warning C4244: 'argument': conversion from 'int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3239348Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(1007): warning C4244: 'argument': conversion from 'int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3571865Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkMatrix.h(88): warning C4244: 'argument': conversion from 'int32_t' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.3573772Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkMatrix.h(88): warning C4244: 'argument': conversion from 'int32_t' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.4695647Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTArray.h(761): warning C4267: 'initializing': conversion from 'size_t' to 'const int', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:50.4698703Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTArray.h(761): note: the template instantiation context (the oldest one first) is
Windows Build	Build Windows Player	2026-01-09T18:08:50.4704913Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkCanvas.h(2494): note: see reference to class template instantiation 'skia_private::STArray<1,sk_sp<SkImageFilter>,true>' being compiled
Windows Build	Build Windows Player	2026-01-09T18:08:51.3094786Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTemplates.h(125): warning C5030: attribute [[clang::reinitializes]] is not recognized
Windows Build	Build Windows Player	2026-01-09T18:08:51.3097020Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTemplates.h(125): note: the template instantiation context (the oldest one first) is
Windows Build	Build Windows Player	2026-01-09T18:08:51.3099140Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTemplates.h(101): note: while compiling class template 'skia_private::AutoTArray'
Windows Build	Build Windows Player	2026-01-09T18:08:51.5840160Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\src\platform.h(262): warning C4244: 'initializing': conversion from 'ULONGLONG' to 'double', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.6904424Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(730): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.6906617Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(730): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.7024992Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1074): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.7026918Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1074): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.7228328Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1306): warning C4996: 'localtime': This function or variable may be unsafe. Consider using localtime_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
Windows Build	Build Windows Player	2026-01-09T18:08:51.7302019Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1394): error C2039: 'isValid': is not a member of 'std::optional<SkRect>'
Windows Build	Build Windows Player	2026-01-09T18:08:51.7303148Z C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\include\optional(227): note: see declaration of 'std::optional<SkRect>'
Windows Build	Build Windows Player	2026-01-09T18:08:51.7872766Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(2723): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.7874506Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(2723): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:51.8468515Z file_dialog_windows.cpp
Windows Build	Build Windows Player	2026-01-09T18:08:52.7417658Z folder_browser.cpp
Windows Build	Build Windows Player	2026-01-09T18:08:53.3370981Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\folder_browser.cpp(52): warning C4244: 'argument': conversion from 'double' to 'const unsigned __int64', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:53.7376599Z thumbnail_cache.cpp
Windows Build	Build Windows Player	2026-01-09T18:08:54.7193758Z SVGAnimationController.cpp
Windows Build	Build Windows Player	2026-01-09T18:08:55.2077530Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\shared\SVGAnimationController.cpp(400): warning C4267: '=': conversion from 'size_t' to 'int', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:55.4212949Z SVGGridCompositor.cpp
Windows Build	Build Windows Player	2026-01-09T18:08:55.9198727Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\shared\SVGGridCompositor.cpp(429): warning C4244: 'argument': conversion from 'double' to 'const unsigned __int64', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:08:56.2251990Z Generating Code...
Windows Build	Build Windows Player	2026-01-09T18:09:02.0301727Z 
Windows Build	Build Windows Player	2026-01-09T18:09:02.0303428Z [ERROR] Build failed
Windows Build	Build Windows Player	2026-01-09T18:09:02.0335058Z ##[error]Process completed with exit code 1.
Build Summary	Check Results	﻿2026-01-09T18:26:42.3674825Z ##[group]Run echo "Build Summary"
Build Summary	Check Results	2026-01-09T18:26:42.3675624Z [36;1mecho "Build Summary"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3676180Z [36;1mecho "============="[0m
Build Summary	Check Results	2026-01-09T18:26:42.3676730Z [36;1mecho ""[0m
Build Summary	Check Results	2026-01-09T18:26:42.3677279Z [36;1mecho "API Header Test: success"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3678063Z [36;1mecho "Linux Build: success"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3678751Z [36;1mecho "macOS Build: success"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3679378Z [36;1mecho "iOS Build: success"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3679942Z [36;1mecho "Windows Build: failure"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3680573Z [36;1mecho ""[0m
Build Summary	Check Results	2026-01-09T18:26:42.3681010Z [36;1m[0m
Build Summary	Check Results	2026-01-09T18:26:42.3681523Z [36;1mif [ "success" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:26:42.3682495Z [36;1m   [ "success" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:26:42.3683161Z [36;1m   [ "success" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:26:42.3683758Z [36;1m   [ "success" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:26:42.3684413Z [36;1m   [ "failure" != "success" ]; then[0m
Build Summary	Check Results	2026-01-09T18:26:42.3685104Z [36;1m  echo "One or more builds failed!"[0m
Build Summary	Check Results	2026-01-09T18:26:42.3685759Z [36;1m  exit 1[0m
Build Summary	Check Results	2026-01-09T18:26:42.3686231Z [36;1mfi[0m
Build Summary	Check Results	2026-01-09T18:26:42.3686631Z [36;1m[0m
Build Summary	Check Results	2026-01-09T18:26:42.3687200Z [36;1mecho "All builds successful!"[0m
Build Summary	Check Results	2026-01-09T18:26:42.4567906Z shell: /usr/bin/bash -e {0}
Build Summary	Check Results	2026-01-09T18:26:42.4569635Z ##[endgroup]
Build Summary	Check Results	2026-01-09T18:26:42.4765030Z Build Summary
Build Summary	Check Results	2026-01-09T18:26:42.4765824Z =============
Build Summary	Check Results	2026-01-09T18:26:42.4766099Z 
Build Summary	Check Results	2026-01-09T18:26:42.4766311Z API Header Test: success
Build Summary	Check Results	2026-01-09T18:26:42.4766839Z Linux Build: success
Build Summary	Check Results	2026-01-09T18:26:42.4767383Z macOS Build: success
Build Summary	Check Results	2026-01-09T18:26:42.4767876Z iOS Build: success
Build Summary	Check Results	2026-01-09T18:26:42.4768346Z Windows Build: failure
Build Summary	Check Results	2026-01-09T18:26:42.4768700Z 
Build Summary	Check Results	2026-01-09T18:26:42.4768915Z One or more builds failed!
Build Summary	Check Results	2026-01-09T18:26:42.4787736Z ##[error]Process completed with exit code 1.
