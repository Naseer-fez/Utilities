#include "utils.h"
#include <shlobj.h>
#include <knownfolders.h>
#include <cstdio>
#include <ctime>
#include <vector>
#include <filesystem>

namespace Utils {

static CRITICAL_SECTION g_LogCriticalSection;
static bool g_LogInitialized = false;

std::wstring GetAppDataPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        std::wstring appData(path);
        appData += L"\\LiveWallpaper";
        return appData;
    }
    return L"";
}

std::wstring GetLogFilePath() {
    std::wstring appData = GetAppDataPath();
    if (!appData.empty()) {
        return appData + L"\\log.txt";
    }
    return L"log.txt"; // Fallback to local
}

void InitializeLogging() {
    static bool initCS = []() {
        InitializeCriticalSection(&g_LogCriticalSection);
        g_LogInitialized = true;
        return true;
    }();

    std::wstring dir = GetAppDataPath();
    if (!dir.empty()) {
        CreateDirectoryW(dir.c_str(), NULL);
    }

    // Check log file size for rotation
    std::wstring logPath = GetLogFilePath();
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fileInfo.nFileSizeHigh;
        fileSize.LowPart = fileInfo.nFileSizeLow;
        
        // Rotate if size > 1MB (1,048,576 bytes)
        if (fileSize.QuadPart > 1024 * 1024) {
            std::wstring backupPath = dir + L"\\log.bak";
            DeleteFileW(backupPath.c_str());
            MoveFileW(logPath.c_str(), backupPath.c_str());
        }
    }
}

void ShutdownLogging() {
    if (g_LogInitialized) {
        DeleteCriticalSection(&g_LogCriticalSection);
        g_LogInitialized = false;
    }
}

static const char* GetLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

static const wchar_t* GetLevelStringW(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return L"DEBUG";
        case LogLevel::Info:  return L"INFO";
        case LogLevel::Warn:  return L"WARN";
        case LogLevel::Error: return L"ERROR";
    }
    return L"UNKNOWN";
}

void Log(LogLevel level, const char* format, ...) {
#ifndef _DEBUG
    if (level == LogLevel::Debug) return;
#endif

    if (!g_LogInitialized) {
        InitializeLogging();
    }

    EnterCriticalSection(&g_LogCriticalSection);

    // Get timestamp
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Format message
    va_list args;
    va_start(args, format);
    int len = _vscprintf(format, args) + 1;
    std::vector<char> buf(len);
    vsnprintf(buf.data(), len, format, args);
    va_end(args);

    // Output to debug console
    char debugMsg[1024];
    sprintf_s(debugMsg, "[%s] [%s] %s\n", timeStr, GetLevelString(level), buf.data());
    OutputDebugStringA(debugMsg);

    // Write to file
    std::wstring logPath = GetLogFilePath();
    FILE* file = nullptr;
    if (_wfopen_s(&file, logPath.c_str(), L"a") == 0 && file) {
        fprintf(file, "[%s] [%s] %s\n", timeStr, GetLevelString(level), buf.data());
        fclose(file);
    }

    LeaveCriticalSection(&g_LogCriticalSection);
}

void LogW(LogLevel level, const wchar_t* format, ...) {
#ifndef _DEBUG
    if (level == LogLevel::Debug) return;
#endif

    if (!g_LogInitialized) {
        InitializeLogging();
    }

    EnterCriticalSection(&g_LogCriticalSection);

    // Get timestamp
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    wchar_t timeStr[20];
    wcsftime(timeStr, sizeof(timeStr) / sizeof(wchar_t), L"%Y-%m-%d %H:%M:%S", &timeinfo);

    // Format message
    va_list args;
    va_start(args, format);
    int len = _vscwprintf(format, args) + 1;
    std::vector<wchar_t> buf(len);
    vswprintf(buf.data(), len, format, args);
    va_end(args);

    // Output to debug console
    wchar_t debugMsg[1024];
    swprintf_s(debugMsg, L"[%ls] [%ls] %ls\n", timeStr, GetLevelStringW(level), buf.data());
    OutputDebugStringW(debugMsg);

    // Write to file
    std::wstring logPath = GetLogFilePath();
    FILE* file = nullptr;
    if (_wfopen_s(&file, logPath.c_str(), L"a, ccs=UTF-8") == 0 && file) {
        fwprintf(file, L"[%ls] [%ls] %ls\n", timeStr, GetLevelStringW(level), buf.data());
        fclose(file);
    }

    LeaveCriticalSection(&g_LogCriticalSection);
}

std::wstring GetVideosFolderPath() {
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, NULL, &path))) {
        std::wstring videosPath(path);
        CoTaskMemFree(path);
        return videosPath;
    }
    // Fallback to environment variable %USERPROFILE%\Videos
    wchar_t userProfile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        return std::wstring(userProfile) + L"\\Videos";
    }
    return L"";
}

std::wstring FindFallbackVideo() {
    std::wstring videosFolder = GetVideosFolderPath();
    if (videosFolder.empty()) {
        return L"";
    }

    try {
        if (std::filesystem::exists(videosFolder)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(videosFolder)) {
                if (entry.is_regular_file()) {
                    std::wstring ext = entry.path().extension().wstring();
                    for (auto& c : ext) c = towlower(c);
                    if (ext == L".mp4") {
                        return entry.path().wstring();
                    }
                }
            }
        }
    } catch (...) {
        // Ignore access errors during traversal
    }

    return L"";
}

bool ValidateFilePath(const std::wstring& path, bool expectRelative) {
    if (path.empty() || path.length() >= MAX_PATH) return false;
    
    // Reject traversal sequences to prevent directory traversal
    if (path.find(L"..") != std::wstring::npos) {
        return false;
    }
    
    // Reject network / UNC paths to prevent credential leakage or remote execution
    if (path.compare(0, 2, L"\\\\") == 0 || path.compare(0, 2, L"//") == 0) {
        return false;
    }
    
    // If a relative path is expected, reject absolute paths
    if (expectRelative) {
        if (path.length() >= 2 && iswalpha(path[0]) && path[1] == L':') {
            return false;
        }
        if (path[0] == L'\\' || path[0] == L'/') {
            return false;
        }
    }
    
    // Resolve absolute path securely first
    wchar_t absPath[MAX_PATH];
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, absPath, nullptr);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }
    
    std::wstring absPathStr(absPath);
    
    // Check for Alternate Data Streams (ADS) to prevent spoofing and bypassing extension controls
    size_t firstColon = absPathStr.find(L':');
    if (firstColon != std::wstring::npos) {
        if (firstColon != 1) { // Drive letter colon is allowed at index 1 (e.g. C:\)
            return false;
        }
        size_t secondColon = absPathStr.find(L':', 2);
        if (secondColon != std::wstring::npos) {
            return false; // Alternate data stream detected
        }
    }
    
    // Check for null characters or control characters in path
    for (wchar_t c : absPathStr) {
        if (c < 32) {
            return false;
        }
    }
    
    // Check extension on the RESOLVED absolute path to prevent bypass tricks
    size_t extPos = absPathStr.find_last_of(L'.');
    if (extPos == std::wstring::npos) return false;
    
    std::wstring ext = absPathStr.substr(extPos);
    for (auto& c : ext) c = towlower(c);
    
    if (ext != L".mp4" && ext != L".mkv" && ext != L".avi" && ext != L".wmv" && ext != L".webm" && ext != L".hlsl") {
        return false;
    }
    
    DWORD attributes = GetFileAttributesW(absPath);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    
    return true;
}

} // namespace Utils
