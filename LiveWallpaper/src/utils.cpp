#include "utils.h"
#include <shlobj.h>
#include <cstdio>
#include <ctime>
#include <vector>

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
    if (!g_LogInitialized) {
        InitializeCriticalSection(&g_LogCriticalSection);
        g_LogInitialized = true;
    }

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

} // namespace Utils
