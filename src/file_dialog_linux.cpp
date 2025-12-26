// file_dialog_linux.cpp - Linux file dialog implementation
// Currently a stub - returns empty string (no GUI file picker on Linux without dependencies)

#if defined(__linux__) || defined(__unix__)

#include "file_dialog.h"
#include <cstdio>

std::string openSVGFileDialog(const char* title, const char* initialPath) {
    fprintf(stderr, "Note: File dialog not implemented on Linux. Pass file path as command line argument.\n");
    return "";
}

std::string openFolderDialog(const char* title, const char* initialPath) {
    fprintf(stderr, "Note: Folder dialog not implemented on Linux. Pass folder path as command line argument.\n");
    return "";
}

#endif // __linux__ || __unix__
