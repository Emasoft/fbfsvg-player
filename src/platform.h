// platform.h - Platform abstraction for SVG Video Player
// Provides cross-platform APIs for CPU monitoring and font management

#pragma once

#include <string>

//==============================================================================
// Platform Detection
//==============================================================================

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IOS
        #define PLATFORM_IOS 1
        #define PLATFORM_NAME "iOS"
    #elif TARGET_OS_MAC
        #define PLATFORM_MACOS 1
        #define PLATFORM_NAME "macOS"
    #endif
    #define PLATFORM_APPLE 1
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
    #define PLATFORM_NAME "Linux"
#elif defined(_WIN32)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_NAME "Windows"
#else
    #define PLATFORM_UNKNOWN 1
    #define PLATFORM_NAME "Unknown"
#endif

//==============================================================================
// CPU Statistics Structure
//==============================================================================

struct CPUStats {
    int totalThreads;       // Total threads in process
    int activeThreads;      // Threads currently running (not idle/waiting)
    double cpuUsagePercent; // Overall CPU usage percentage
};

//==============================================================================
// Platform-Specific Implementations
//==============================================================================

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

// macOS/iOS: Use Mach APIs for CPU monitoring
#include <mach/mach.h>
#include <mach/thread_info.h>
#include <mach/task.h>
#include <mach/mach_time.h>

inline CPUStats getProcessCPUStats() {
    CPUStats stats = {0, 0, 0.0};

    task_t task = mach_task_self();
    thread_array_t threadList;
    mach_msg_type_number_t threadCount;

    // Get all threads in the current process
    kern_return_t kr = task_threads(task, &threadList, &threadCount);
    if (kr != KERN_SUCCESS) {
        return stats;
    }

    stats.totalThreads = static_cast<int>(threadCount);
    double totalCPU = 0.0;

    // Check each thread's state and CPU usage
    for (mach_msg_type_number_t i = 0; i < threadCount; i++) {
        thread_basic_info_data_t info;
        mach_msg_type_number_t infoCount = THREAD_BASIC_INFO_COUNT;

        kr = thread_info(threadList[i], THREAD_BASIC_INFO,
                        (thread_info_t)&info, &infoCount);

        if (kr == KERN_SUCCESS) {
            // Thread is active if it's running or runnable (not sleeping/waiting)
            if (info.run_state == TH_STATE_RUNNING) {
                stats.activeThreads++;
            }

            // Calculate CPU usage for this thread
            // cpu_usage is in units of 1/TH_USAGE_SCALE (typically 1000)
            if (!(info.flags & TH_FLAGS_IDLE)) {
                totalCPU += static_cast<double>(info.cpu_usage) / TH_USAGE_SCALE * 100.0;
            }
        }

        // Deallocate thread port
        mach_port_deallocate(task, threadList[i]);
    }

    // Deallocate thread list
    vm_deallocate(task, (vm_address_t)threadList,
                  threadCount * sizeof(thread_t));

    stats.cpuUsagePercent = totalCPU;
    return stats;
}

#elif defined(PLATFORM_LINUX)

// Linux: Use /proc filesystem for CPU monitoring
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>

inline CPUStats getProcessCPUStats() {
    CPUStats stats = {0, 0, 0.0};

    // Count threads from /proc/self/task
    DIR* taskDir = opendir("/proc/self/task");
    if (taskDir) {
        struct dirent* entry;
        while ((entry = readdir(taskDir)) != nullptr) {
            if (entry->d_name[0] != '.') {
                stats.totalThreads++;
            }
        }
        closedir(taskDir);
    }

    // Read CPU stats from /proc/self/stat
    std::ifstream statFile("/proc/self/stat");
    if (statFile.is_open()) {
        std::string line;
        std::getline(statFile, line);

        // Parse the stat line - format: pid (comm) state ...
        // Fields 14 and 15 are utime and stime (user and system CPU time)
        std::istringstream iss(line);
        std::string token;

        // Skip to field 14 (utime)
        for (int i = 0; i < 13 && iss >> token; i++) {
            // Skip comm field which is in parentheses
            if (i == 1 && token[0] == '(') {
                // Read until closing parenthesis
                while (token.back() != ')' && iss >> token) {}
            }
        }

        long utime = 0, stime = 0;
        if (iss >> utime >> stime) {
            // Convert jiffies to percentage
            // This is a simplified calculation
            // Use static mutex to protect static state variables (thread-safe)
            static std::mutex cpuStatsMutex;
            static long lastUtime = 0, lastStime = 0;
            static auto lastTime = std::chrono::steady_clock::now();
            static double lastCpuPercent = 0.0;

            std::lock_guard<std::mutex> lock(cpuStatsMutex);
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - lastTime).count();

            if (elapsed > 0.1) {  // Update every 100ms
                long ticksPerSec = sysconf(_SC_CLK_TCK);
                double cpuTime = static_cast<double>((utime - lastUtime) + (stime - lastStime)) / ticksPerSec;
                lastCpuPercent = (cpuTime / elapsed) * 100.0;

                lastUtime = utime;
                lastStime = stime;
                lastTime = now;
            }
            stats.cpuUsagePercent = lastCpuPercent;
        }
        statFile.close();
    }

    // Estimate active threads (simplified - count threads in R state)
    DIR* taskDirAgain = opendir("/proc/self/task");
    if (taskDirAgain) {
        struct dirent* entry;
        while ((entry = readdir(taskDirAgain)) != nullptr) {
            if (entry->d_name[0] != '.') {
                std::string statPath = "/proc/self/task/" + std::string(entry->d_name) + "/stat";
                std::ifstream threadStat(statPath);
                if (threadStat.is_open()) {
                    std::string line;
                    std::getline(threadStat, line);
                    // Check if thread state is 'R' (running)
                    size_t pos = line.rfind(')');
                    if (pos != std::string::npos && pos + 2 < line.size()) {
                        char state = line[pos + 2];
                        if (state == 'R') {
                            stats.activeThreads++;
                        }
                    }
                    threadStat.close();
                }
            }
        }
        closedir(taskDirAgain);
    }

    return stats;
}

#elif defined(PLATFORM_WINDOWS)

// Windows: Use Windows API for CPU monitoring
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <mutex>

inline CPUStats getProcessCPUStats() {
    CPUStats stats = {0, 0, 0.0};

    // Count threads using toolhelp snapshot
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);
        DWORD currentPid = GetCurrentProcessId();

        if (Thread32First(snapshot, &te32)) {
            do {
                if (te32.th32OwnerProcessID == currentPid) {
                    stats.totalThreads++;
                }
            } while (Thread32Next(snapshot, &te32));
        }
        CloseHandle(snapshot);
    }

    // Get CPU usage
    // Use static mutex to protect static state variables (thread-safe)
    static std::mutex cpuStatsMutex;
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors = 0;
    static HANDLE self = GetCurrentProcess();

    std::lock_guard<std::mutex> lock(cpuStatsMutex);

    if (numProcessors == 0) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
    }

    FILETIME ftime, fsys, fuser;
    GetSystemTimeAsFileTime(&ftime);
    ULARGE_INTEGER now;
    now.LowPart = ftime.dwLowDateTime;
    now.HighPart = ftime.dwHighDateTime;

    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
    ULARGE_INTEGER sys, user;
    sys.LowPart = fsys.dwLowDateTime;
    sys.HighPart = fsys.dwHighDateTime;
    user.LowPart = fuser.dwLowDateTime;
    user.HighPart = fuser.dwHighDateTime;

    if (lastCPU.QuadPart != 0) {
        double percent = (sys.QuadPart - lastSysCPU.QuadPart) +
                        (user.QuadPart - lastUserCPU.QuadPart);
        percent /= (now.QuadPart - lastCPU.QuadPart);
        percent /= numProcessors;
        stats.cpuUsagePercent = percent * 100.0;
    }

    lastCPU = now;
    lastSysCPU = sys;
    lastUserCPU = user;

    return stats;
}

#else

// Fallback for unknown platforms
inline CPUStats getProcessCPUStats() {
    return {0, 0, 0.0};
}

#endif

//==============================================================================
// Font Manager Creation
//==============================================================================

#include "include/core/SkFontMgr.h"

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
    #include "include/ports/SkFontMgr_mac_ct.h"
    inline sk_sp<SkFontMgr> createPlatformFontMgr() {
        return SkFontMgr_New_CoreText(nullptr);
    }
#elif defined(PLATFORM_LINUX)
    #include "include/ports/SkFontMgr_fontconfig.h"
    #include "include/ports/SkFontScanner_FreeType.h"  // FreeType scanner for FontConfig
    inline sk_sp<SkFontMgr> createPlatformFontMgr() {
        // FontConfig requires a FreeType font scanner as second parameter
        return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    }
#elif defined(PLATFORM_WINDOWS)
    #include "include/ports/SkTypeface_win.h"
    inline sk_sp<SkFontMgr> createPlatformFontMgr() {
        return SkFontMgr_New_DirectWrite();
    }
#else
    inline sk_sp<SkFontMgr> createPlatformFontMgr() {
        return SkFontMgr::RefEmpty();
    }
#endif

//==============================================================================
// Platform Notes
//==============================================================================

inline const char* getPlatformNote() {
#if defined(PLATFORM_MACOS)
    return "Occasional stutters may be caused by macOS system tasks.";
#elif defined(PLATFORM_LINUX)
    return "For best performance, ensure Mesa/OpenGL drivers are up to date.";
#elif defined(PLATFORM_IOS)
    return "Touch the screen to toggle playback controls.";
#elif defined(PLATFORM_WINDOWS)
    return "Ensure graphics drivers are up to date for best performance.";
#else
    return "";
#endif
}

//==============================================================================
// GPU Backend Preference
//==============================================================================

inline const char* getPreferredGPUBackend() {
#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
    return "Metal";
#elif defined(PLATFORM_LINUX)
    return "OpenGL/EGL";
#elif defined(PLATFORM_WINDOWS)
    return "Direct3D/OpenGL";
#else
    return "OpenGL";
#endif
}
