// file_dialog_windows.cpp - Windows file dialog implementation
// Currently a stub - returns empty string

#if defined(_WIN32) || defined(_WIN64)

#include "file_dialog.h"
#include <cstdio>

std::string openSVGFileDialog(const char* title, const char* initialPath) {
    fprintf(stderr, "Note: File dialog not implemented on Windows. Pass file path as command line argument.\n");
    return "";
}

std::string openFolderDialog(const char* title, const char* initialPath) {
    fprintf(stderr, "Note: Folder dialog not implemented on Windows. Pass folder path as command line argument.\n");
    return "";
}

#endif // _WIN32 || _WIN64
