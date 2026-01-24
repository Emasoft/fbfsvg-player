Windows Build	Build Windows Player	2026-01-09T18:52:47.1834116Z   Python2_ROOT_DIR: C:\hostedtoolcache\windows\Python\3.11.9\x64
Windows Build	Build Windows Player	2026-01-09T18:52:47.1834485Z   Python3_ROOT_DIR: C:\hostedtoolcache\windows\Python\3.11.9\x64
Windows Build	Build Windows Player	2026-01-09T18:52:47.1834767Z ##[endgroup]
Windows Build	Build Windows Player	2026-01-09T18:52:47.2037236Z 
Windows Build	Build Windows Player	2026-01-09T18:52:47.2040577Z ===================================================
Windows Build	Build Windows Player	2026-01-09T18:52:47.2043774Z  SVG Player - Windows Build Script
Windows Build	Build Windows Player	2026-01-09T18:52:47.2047370Z ===================================================
Windows Build	Build Windows Player	2026-01-09T18:52:47.2070073Z 
Windows Build	Build Windows Player	2026-01-09T18:52:47.2075430Z [INFO] Checking for Visual Studio...
Windows Build	Build Windows Player	2026-01-09T18:52:47.2494983Z [INFO] Found Visual Studio: C:\Program Files\Microsoft Visual Studio\2022\Enterprise
Windows Build	Build Windows Player	2026-01-09T18:52:48.3944086Z [INFO] Visual Studio environment initialized
Windows Build	Build Windows Player	2026-01-09T18:52:48.3967321Z [INFO] Found Skia at D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\out\release-windows
Windows Build	Build Windows Player	2026-01-09T18:52:48.3971248Z [INFO] Checking for SDL2...
Windows Build	Build Windows Player	2026-01-09T18:52:48.3996142Z [INFO] Found SDL2 at D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\external\SDL2
Windows Build	Build Windows Player	2026-01-09T18:52:48.4131863Z 
Windows Build	Build Windows Player	2026-01-09T18:52:48.4132713Z [STEP] Compiling SVG Player for Windows (release)...
Windows Build	Build Windows Player	2026-01-09T18:52:48.4169915Z 
Windows Build	Build Windows Player	2026-01-09T18:52:48.4289755Z Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35222 for x64
Windows Build	Build Windows Player	2026-01-09T18:52:48.4290802Z Copyright (C) Microsoft Corporation.  All rights reserved.
Windows Build	Build Windows Player	2026-01-09T18:52:48.4291429Z 
Windows Build	Build Windows Player	2026-01-09T18:52:48.4349869Z svg_player_animated_windows.cpp
Windows Build	Build Windows Player	2026-01-09T18:52:48.7519897Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(885): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7521592Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(886): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7523197Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(887): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7524714Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(888): warning C4244: '=': conversion from 'const int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7556675Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(1007): warning C4244: 'argument': conversion from 'int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7558228Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkRect.h(1007): warning C4244: 'argument': conversion from 'int32_t' to 'float', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7864691Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkMatrix.h(88): warning C4244: 'argument': conversion from 'int32_t' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.7866661Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkMatrix.h(88): warning C4244: 'argument': conversion from 'int32_t' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.8928218Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTArray.h(761): warning C4267: 'initializing': conversion from 'size_t' to 'const int', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:48.8929913Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTArray.h(761): note: the template instantiation context (the oldest one first) is
Windows Build	Build Windows Player	2026-01-09T18:52:48.8934477Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/core/SkCanvas.h(2494): note: see reference to class template instantiation 'skia_private::STArray<1,sk_sp<SkImageFilter>,true>' being compiled
Windows Build	Build Windows Player	2026-01-09T18:52:49.6839649Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTemplates.h(125): warning C5030: attribute [[clang::reinitializes]] is not recognized
Windows Build	Build Windows Player	2026-01-09T18:52:49.6841016Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTemplates.h(125): note: the template instantiation context (the oldest one first) is
Windows Build	Build Windows Player	2026-01-09T18:52:49.6842259Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\include/private/base/SkTemplates.h(101): note: while compiling class template 'skia_private::AutoTArray'
Windows Build	Build Windows Player	2026-01-09T18:52:49.9377684Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\src\platform.h(262): warning C4244: 'initializing': conversion from 'ULONGLONG' to 'double', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.0383628Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(730): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.0384898Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(730): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.0501788Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1074): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.0503257Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1074): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.0701571Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(1306): warning C4996: 'localtime': This function or variable may be unsafe. Consider using localtime_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
Windows Build	Build Windows Player	2026-01-09T18:52:50.1333831Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(2724): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.1335382Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\svg_player_animated_windows.cpp(2724): warning C4244: 'argument': conversion from 'int' to 'SkScalar', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:50.6426970Z file_dialog_windows.cpp
Windows Build	Build Windows Player	2026-01-09T18:52:51.4603880Z folder_browser.cpp
Windows Build	Build Windows Player	2026-01-09T18:52:52.0642323Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\src\folder_browser.cpp(52): warning C4244: 'argument': conversion from 'double' to 'const unsigned __int64', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:52.4551098Z thumbnail_cache.cpp
Windows Build	Build Windows Player	2026-01-09T18:52:53.4020467Z SVGAnimationController.cpp
Windows Build	Build Windows Player	2026-01-09T18:52:53.8777668Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\shared\SVGAnimationController.cpp(400): warning C4267: '=': conversion from 'size_t' to 'int', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:54.0855045Z SVGGridCompositor.cpp
Windows Build	Build Windows Player	2026-01-09T18:52:54.5608469Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\shared\SVGGridCompositor.cpp(429): warning C4244: 'argument': conversion from 'double' to 'const unsigned __int64', possible loss of data
Windows Build	Build Windows Player	2026-01-09T18:52:54.8637857Z Generating Code...
Windows Build	Build Windows Player	2026-01-09T18:53:03.7905182Z Microsoft (R) Incremental Linker Version 14.44.35222.0
Windows Build	Build Windows Player	2026-01-09T18:53:03.7905658Z Copyright (C) Microsoft Corporation.  All rights reserved.
Windows Build	Build Windows Player	2026-01-09T18:53:03.7905907Z 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7906162Z /out:D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\build\windows\svg_player_animated.exe 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7906800Z /LIBPATH:D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\skia-build\src\skia\out\release-windows 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7908439Z /LIBPATH:D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\external\SDL2\lib\x64 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7908845Z skia.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7908985Z svg.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7909111Z SDL2.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7909245Z SDL2main.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7909387Z opengl32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7909536Z user32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7909666Z gdi32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7909824Z shell32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7910148Z comdlg32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7910397Z ole32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7910585Z shlwapi.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7910730Z advapi32.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7910870Z dwrite.lib 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7911025Z /SUBSYSTEM:CONSOLE 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7911215Z svg_player_animated_windows.obj 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7911440Z file_dialog_windows.obj 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7911624Z folder_browser.obj 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7911782Z thumbnail_cache.obj 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7911964Z SVGAnimationController.obj 
Windows Build	Build Windows Player	2026-01-09T18:53:03.7912175Z SVGGridCompositor.obj 
Windows Build	Build Windows Player	2026-01-09T18:53:06.1934712Z svg_player_animated_windows.obj : error LNK2019: unresolved external symbol "class sk_sp<class SkShapers::Factory> __cdecl SkShapers::Primitive::Factory(void)" (?Factory@Primitive@SkShapers@@YA?AV?$sk_sp@VFactory@SkShapers@@@@XZ) referenced in function "class sk_sp<class SkShapers::Factory> __cdecl SkShapers::BestAvailable(void)" (?BestAvailable@SkShapers@@YA?AV?$sk_sp@VFactory@SkShapers@@@@XZ)
Windows Build	Build Windows Player	2026-01-09T18:53:06.1937451Z svg.lib(svg.SkSVGDOM.obj) : error LNK2001: unresolved external symbol "class sk_sp<class SkShapers::Factory> __cdecl SkShapers::Primitive::Factory(void)" (?Factory@Primitive@SkShapers@@YA?AV?$sk_sp@VFactory@SkShapers@@@@XZ)
Windows Build	Build Windows Player	2026-01-09T18:53:06.2143410Z svg.lib(svg.SkSVGText.obj) : error LNK2019: unresolved external symbol "public: static class std::unique_ptr<class SkShaper::FontRunIterator,struct std::default_delete<class SkShaper::FontRunIterator> > __cdecl SkShaper::MakeFontMgrRunIterator(char const *,unsigned __int64,class SkFont const &,class sk_sp<class SkFontMgr>)" (?MakeFontMgrRunIterator@SkShaper@@SA?AV?$unique_ptr@VFontRunIterator@SkShaper@@U?$default_delete@VFontRunIterator@SkShaper@@@std@@@std@@PEBD_KAEBVSkFont@@V?$sk_sp@VSkFontMgr@@@@@Z) referenced in function "private: void __cdecl SkSVGTextContext::shapePendingBuffer(class SkSVGRenderContext const &,class SkFont const &)" (?shapePendingBuffer@SkSVGTextContext@@AEAAXAEBVSkSVGRenderContext@@AEBVSkFont@@@Z)
Windows Build	Build Windows Player	2026-01-09T18:53:06.2149556Z svg.lib(svg.SkSVGText.obj) : error LNK2019: unresolved external symbol "public: static class std::unique_ptr<class SkShaper::LanguageRunIterator,struct std::default_delete<class SkShaper::LanguageRunIterator> > __cdecl SkShaper::MakeStdLanguageRunIterator(char const *,unsigned __int64)" (?MakeStdLanguageRunIterator@SkShaper@@SA?AV?$unique_ptr@VLanguageRunIterator@SkShaper@@U?$default_delete@VLanguageRunIterator@SkShaper@@@std@@@std@@PEBD_K@Z) referenced in function "private: void __cdecl SkSVGTextContext::shapePendingBuffer(class SkSVGRenderContext const &,class SkFont const &)" (?shapePendingBuffer@SkSVGTextContext@@AEAAXAEBVSkSVGRenderContext@@AEBVSkFont@@@Z)
Windows Build	Build Windows Player	2026-01-09T18:53:06.2155100Z svg.lib(svg.SkSVGText.obj) : error LNK2019: unresolved external symbol "class std::unique_ptr<class SkShaper,struct std::default_delete<class SkShaper> > __cdecl SkShapers::Primitive::PrimitiveText(void)" (?PrimitiveText@Primitive@SkShapers@@YA?AV?$unique_ptr@VSkShaper@@U?$default_delete@VSkShaper@@@std@@@std@@XZ) referenced in function "public: __cdecl SkSVGTextContext::SkSVGTextContext(class SkSVGRenderContext const &,class std::function<void __cdecl(class SkSVGRenderContext const &,class sk_sp<class SkTextBlob> const &,class SkPaint const *,class SkPaint const *)> const &,class SkSVGTextPath const *)" (??0SkSVGTextContext@@QEAA@AEBVSkSVGRenderContext@@AEBV?$function@$$A6AXAEBVSkSVGRenderContext@@AEBV?$sk_sp@VSkTextBlob@@@@PEBVSkPaint@@2@Z@std@@PEBVSkSVGTextPath@@@Z)
Windows Build	Build Windows Player	2026-01-09T18:53:06.2174499Z D:\a\SKIA-BUILD-ARM64\SKIA-BUILD-ARM64\scripts\..\build\windows\svg_player_animated.exe : fatal error LNK1120: 4 unresolved externals
Windows Build	Build Windows Player	2026-01-09T18:53:06.2474226Z 
Windows Build	Build Windows Player	2026-01-09T18:53:06.2475274Z [ERROR] Build failed
Windows Build	Build Windows Player	2026-01-09T18:53:06.2506865Z ##[error]Process completed with exit code 1.
macOS Build	Build macOS Desktop Player	﻿2026-01-09T18:36:06.4729490Z ##[group]Run ./scripts/build-macos.sh -y
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.4729850Z [36;1m./scripts/build-macos.sh -y[0m
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.4784680Z shell: /bin/bash -e {0}
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.4784960Z ##[endgroup]
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.5394720Z [0;36m[STEP][0m Building for current architecture (arm64)...
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.5496540Z [0;32m[INFO][0m Building SVG Player for macOS (arm64)...
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.5599250Z [0;32m[INFO][0m Checking for SDL2...
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.5700530Z [0;32m[INFO][0m Found SDL2 version: 2.32.10
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:06.5801800Z [0;32m[INFO][0m Detecting ICU installation...
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.4373890Z [0;32m[INFO][0m Found ICU via Homebrew at: /opt/homebrew/opt/icu4c@78
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.4475130Z [0;32m[INFO][0m ICU version: 78.1
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.4576250Z [0;32m[INFO][0m Checking for Skia libraries...
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9306970Z [0;32m[INFO][0m Skia library architectures: Non-fat file: /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/skia-build/src/skia/out/release-macos/libskia.a is architecture: arm64
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9408600Z [0;32m[INFO][0m Build type: RELEASE
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9509750Z [0;32m[INFO][0m Compiling SVG player with shared animation controller...
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9611130Z [0;32m[INFO][0m Sources: /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/src/svg_player_animated.cpp
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9712650Z [0;32m[INFO][0m          /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared/SVGAnimationController.cpp
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9814590Z [0;32m[INFO][0m          /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared/SVGGridCompositor.cpp
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:07.9916520Z [0;32m[INFO][0m          /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared/svg_instrumentation.cpp
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:08.0017980Z [0;32m[INFO][0m          /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/src/file_dialog_macos.mm
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:08.0119580Z [0;32m[INFO][0m          /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/src/folder_browser.cpp
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:08.0220960Z [0;32m[INFO][0m          /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/src/thumbnail_cache.cpp
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:08.0323120Z [0;32m[INFO][0m Target: /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/build/svg_player_animated-macos-arm64
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.4287000Z /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/src/svg_player_animated.cpp:1367:17: error: no member named 'has_value' in 'SkTLazy<SkRect>'
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.4287670Z     if (viewBox.has_value()) {
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.4287920Z         ~~~~~~~ ^
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.4322500Z /Users/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/src/svg_player_animated.cpp:1539:17: error: no member named 'has_value' in 'SkTLazy<SkRect>'
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.4323100Z     if (viewBox.has_value()) {
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.4323290Z         ~~~~~~~ ^
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:10.6778710Z 2 errors generated.
macOS Build	Build macOS Desktop Player	2026-01-09T18:36:15.9925390Z ##[error]Process completed with exit code 1.
Linux Build	Build Linux SDK	﻿2026-01-09T18:36:32.5571846Z ##[group]Run ./scripts/build-linux-sdk.sh -y
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5572235Z [36;1m./scripts/build-linux-sdk.sh -y[0m
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5605740Z shell: /usr/bin/bash -e {0}
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5605995Z ##[endgroup]
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5674193Z ==============================================
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5674752Z SVGPlayer SDK for Linux - Build Script
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5675214Z ==============================================
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5675510Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5688250Z Detected architecture: x86_64 (x64)
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5711240Z Project root: /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5712245Z SDK source:   /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/linux-sdk/SVGPlayer
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5713217Z Shared src:   /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5714019Z Build output: /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/build/linux
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5715130Z Skia path:    /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/skia-build/src/skia/out/release-linux-x64
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5716034Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5716220Z Shared animation controller found
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5716645Z Skia static library found
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5716923Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5717115Z Checking for system library dependencies...
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5717617Z Clang found (recommended)
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5838273Z FreeType found
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5855911Z FontConfig found
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5867355Z libpng found
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5867683Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5868259Z Checking for OpenGL/EGL dependencies...
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5891669Z OpenGL/EGL dependencies found
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5891983Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5892249Z All required dependencies found. Proceeding with build...
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5892675Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5893352Z Using Clang compiler
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5917578Z Building RELEASE version
Linux Build	Build Linux SDK	2026-01-09T18:36:32.5918135Z Using symbol version script
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6143921Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6144569Z Compiling SVGPlayer with shared animation controller...
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6145200Z Compiler: clang++
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6146525Z Flags:    -std=c++17 -fPIC -fvisibility=hidden -fexceptions -frtti -O2 -DNDEBUG -I/usr/include/freetype2 -I/usr/include/libpng16  -I/usr/include/freetype2 -I/usr/include/libpng16 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6147719Z Sources:  /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/linux-sdk/SVGPlayer/svg_player.cpp
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6148775Z           /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared/SVGAnimationController.cpp
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6149453Z           /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared/SVGGridCompositor.cpp
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6150097Z           /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/shared/svg_instrumentation.cpp
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6150747Z 
Linux Build	Build Linux SDK	2026-01-09T18:36:32.6150855Z Compiling svg_player.cpp...
Linux Build	Build Linux SDK	2026-01-09T18:36:38.4356236Z In file included from /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/linux-sdk/SVGPlayer/svg_player.cpp:14:
Linux Build	Build Linux SDK	2026-01-09T18:36:38.4357995Z /home/runner/work/SKIA-BUILD-ARM64/SKIA-BUILD-ARM64/linux-sdk/SVGPlayer/../../shared/svg_player_api.cpp:288:28: error: no member named 'has_value' in 'SkTLazy<SkRect>'
Linux Build	Build Linux SDK	2026-01-09T18:36:38.4359234Z   288 |     if (root->getViewBox().has_value()) {
Linux Build	Build Linux SDK	2026-01-09T18:36:38.4359696Z       |         ~~~~~~~~~~~~~~~~~~ ^
Linux Build	Build Linux SDK	2026-01-09T18:36:38.7274043Z 1 error generated.
Linux Build	Build Linux SDK	2026-01-09T18:36:38.8366086Z ##[error]Process completed with exit code 1.
Build Summary	Check Results	﻿2026-01-09T18:53:12.6655178Z ##[group]Run echo "Build Summary"
Build Summary	Check Results	2026-01-09T18:53:12.6656007Z [36;1mecho "Build Summary"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6656826Z [36;1mecho "============="[0m
Build Summary	Check Results	2026-01-09T18:53:12.6657473Z [36;1mecho ""[0m
Build Summary	Check Results	2026-01-09T18:53:12.6658086Z [36;1mecho "API Header Test: success"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6658924Z [36;1mecho "Linux Build: failure"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6659597Z [36;1mecho "macOS Build: failure"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6660245Z [36;1mecho "iOS Build: success"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6660840Z [36;1mecho "Windows Build: failure"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6661535Z [36;1mecho ""[0m
Build Summary	Check Results	2026-01-09T18:53:12.6662034Z [36;1m[0m
Build Summary	Check Results	2026-01-09T18:53:12.6662616Z [36;1mif [ "success" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:53:12.6663370Z [36;1m   [ "failure" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:53:12.6664036Z [36;1m   [ "failure" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:53:12.6664709Z [36;1m   [ "success" != "success" ] || \[0m
Build Summary	Check Results	2026-01-09T18:53:12.6665363Z [36;1m   [ "failure" != "success" ]; then[0m
Build Summary	Check Results	2026-01-09T18:53:12.6666137Z [36;1m  echo "One or more builds failed!"[0m
Build Summary	Check Results	2026-01-09T18:53:12.6667141Z [36;1m  exit 1[0m
Build Summary	Check Results	2026-01-09T18:53:12.6667718Z [36;1mfi[0m
Build Summary	Check Results	2026-01-09T18:53:12.6668192Z [36;1m[0m
Build Summary	Check Results	2026-01-09T18:53:12.6668750Z [36;1mecho "All builds successful!"[0m
Build Summary	Check Results	2026-01-09T18:53:12.7250254Z shell: /usr/bin/bash -e {0}
Build Summary	Check Results	2026-01-09T18:53:12.7251345Z ##[endgroup]
Build Summary	Check Results	2026-01-09T18:53:12.7450280Z Build Summary
Build Summary	Check Results	2026-01-09T18:53:12.7450889Z =============
Build Summary	Check Results	2026-01-09T18:53:12.7451171Z 
Build Summary	Check Results	2026-01-09T18:53:12.7451518Z API Header Test: success
Build Summary	Check Results	2026-01-09T18:53:12.7452084Z Linux Build: failure
Build Summary	Check Results	2026-01-09T18:53:12.7452616Z macOS Build: failure
Build Summary	Check Results	2026-01-09T18:53:12.7453089Z iOS Build: success
Build Summary	Check Results	2026-01-09T18:53:12.7453728Z Windows Build: failure
Build Summary	Check Results	2026-01-09T18:53:12.7454032Z 
Build Summary	Check Results	2026-01-09T18:53:12.7454286Z One or more builds failed!
Build Summary	Check Results	2026-01-09T18:53:12.7472913Z ##[error]Process completed with exit code 1.
