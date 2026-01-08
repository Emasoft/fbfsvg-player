// file_dialog_windows.cpp - Windows file dialog implementation
// Uses IFileDialog (Vista+) for modern file picker experience
// Falls back to GetOpenFileName for older Windows versions

#if defined(_WIN32) || defined(_WIN64)

#include "file_dialog.h"

// Windows headers (order matters for COM)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>  // IFileDialog interfaces
#include <commdlg.h>   // GetOpenFileName fallback
#include <shlwapi.h>   // Path utilities

#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

// Convert wide string (UTF-16) to narrow string (UTF-8)
std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                          static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return "";

    std::string utf8(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()),
                        &utf8[0], sizeNeeded, nullptr, nullptr);
    return utf8;
}

// Convert narrow string (UTF-8) to wide string (UTF-16)
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";

    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                          static_cast<int>(utf8.size()),
                                          nullptr, 0);
    if (sizeNeeded <= 0) return L"";

    std::wstring wide(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()),
                        &wide[0], sizeNeeded);
    return wide;
}

// Get current working directory
std::string getCurrentDirectory() {
    DWORD size = GetCurrentDirectoryW(0, nullptr);
    if (size == 0) return "";

    std::vector<wchar_t> buffer(size);
    GetCurrentDirectoryW(size, buffer.data());
    return wideToUtf8(buffer.data());
}

// Check if path is a directory
bool isDirectory(const std::string& path) {
    DWORD attribs = GetFileAttributesW(utf8ToWide(path).c_str());
    return (attribs != INVALID_FILE_ATTRIBUTES) &&
           (attribs & FILE_ATTRIBUTE_DIRECTORY);
}

// Modern IFileDialog implementation (Vista+)
// Returns empty string on cancel or error
std::string openFileDialogModern(const wchar_t* title,
                                  const std::string& initialPath,
                                  bool folderMode,
                                  bool svgFilter) {
    std::string result;

    // Initialize COM (required for IFileDialog)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return "";  // COM init failed
    }

    // Create file dialog instance
    IFileDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));

    if (SUCCEEDED(hr) && pDialog) {
        // Set dialog options
        DWORD options = 0;
        pDialog->GetOptions(&options);
        if (folderMode) {
            options |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
        } else {
            options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
        }
        pDialog->SetOptions(options);

        // Set title
        pDialog->SetTitle(title);

        // Set file filter for SVG files (only in file mode)
        if (!folderMode && svgFilter) {
            COMDLG_FILTERSPEC filters[] = {
                { L"SVG Files (*.svg)", L"*.svg" },
                { L"All Files (*.*)", L"*.*" }
            };
            pDialog->SetFileTypes(2, filters);
            pDialog->SetFileTypeIndex(1);  // Default to SVG filter
            pDialog->SetDefaultExtension(L"svg");
        }

        // Set initial directory
        std::string startPath = initialPath.empty() ? getCurrentDirectory() : initialPath;
        if (!startPath.empty()) {
            std::wstring widePath = utf8ToWide(startPath);
            IShellItem* pStartFolder = nullptr;
            hr = SHCreateItemFromParsingName(widePath.c_str(), nullptr,
                                              IID_IShellItem,
                                              reinterpret_cast<void**>(&pStartFolder));
            if (SUCCEEDED(hr) && pStartFolder) {
                pDialog->SetFolder(pStartFolder);
                pStartFolder->Release();
            }
        }

        // Show dialog
        hr = pDialog->Show(nullptr);

        if (SUCCEEDED(hr)) {
            // Get result
            IShellItem* pItem = nullptr;
            hr = pDialog->GetResult(&pItem);
            if (SUCCEEDED(hr) && pItem) {
                PWSTR pszPath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                if (SUCCEEDED(hr) && pszPath) {
                    result = wideToUtf8(pszPath);
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }

        pDialog->Release();
    }

    CoUninitialize();
    return result;
}

// Legacy GetOpenFileName fallback (pre-Vista)
std::string openFileDialogLegacy(const char* title,
                                  const std::string& initialPath,
                                  bool folderMode,
                                  bool svgFilter) {
    if (folderMode) {
        // GetOpenFileName doesn't support folder picking
        // Would need SHBrowseForFolder here, but modern API is better
        fprintf(stderr, "Note: Folder dialog requires Windows Vista or later.\n");
        return "";
    }

    wchar_t filename[MAX_PATH] = { 0 };

    // Build filter string (double-null terminated)
    // Note: Using L"" concatenation for proper null termination
    const wchar_t filter[] = L"SVG Files (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";

    std::wstring wideTitle = utf8ToWide(title);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = svgFilter ? 1 : 2;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wideTitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    // Set initial directory
    std::wstring wideInitPath;
    if (!initialPath.empty() && isDirectory(initialPath)) {
        wideInitPath = utf8ToWide(initialPath);
        ofn.lpstrInitialDir = wideInitPath.c_str();
    }

    if (GetOpenFileNameW(&ofn)) {
        return wideToUtf8(filename);
    }

    return "";
}

} // anonymous namespace

std::string openSVGFileDialog(const char* title, const char* initialPath) {
    std::string initPath = (initialPath && initialPath[0] != '\0')
                            ? std::string(initialPath)
                            : "";

    std::wstring wideTitle = utf8ToWide(title);

    // Try modern IFileDialog first (Vista+)
    std::string result = openFileDialogModern(wideTitle.c_str(), initPath, false, true);

    if (result.empty()) {
        // Fall back to legacy API if modern failed
        result = openFileDialogLegacy(title, initPath, false, true);
    }

    // Validate SVG extension
    if (!result.empty()) {
        size_t len = result.length();
        if (len >= 4) {
            std::string ext = result.substr(len - 4);
            // Convert to lowercase for comparison
            for (char& c : ext) {
                if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
            }
            if (ext == ".svg") {
                return result;
            }
        }
        // Not .svg but user selected it - return anyway (permissive)
        return result;
    }

    return "";  // Cancelled or error
}

std::string openFolderDialog(const char* title, const char* initialPath) {
    std::string initPath = (initialPath && initialPath[0] != '\0')
                            ? std::string(initialPath)
                            : "";

    std::wstring wideTitle = utf8ToWide(title);

    // Modern IFileDialog with FOS_PICKFOLDERS
    std::string result = openFileDialogModern(wideTitle.c_str(), initPath, true, false);

    // Validate it's a directory
    if (!result.empty() && isDirectory(result)) {
        return result;
    }

    return "";  // Cancelled or invalid
}

#endif // _WIN32 || _WIN64
