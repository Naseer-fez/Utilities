#pragma once
#include <windows.h>
#include <string>
#include <wrl/client.h>

namespace Utils {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

void InitializeLogging();
void ShutdownLogging();
void Log(LogLevel level, const char* format, ...);
void LogW(LogLevel level, const wchar_t* format, ...);

bool ValidateFilePath(const std::wstring& path, bool expectRelative = false);

// COM Helper for checking HRESULT
inline void LogIfFailed(HRESULT hr, const char* context) {
    if (FAILED(hr)) {
        Log(LogLevel::Error, "%s failed: HRESULT = 0x%08X", context, hr);
    }
}

std::wstring GetAppDataPath();
std::wstring GetLogFilePath();
std::wstring GetVideosFolderPath();
std::wstring FindFallbackVideo();

} // namespace Utils

// Quick macros
#define LOG_DEBUG(fmt, ...) Utils::Log(Utils::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Utils::Log(Utils::LogLevel::Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Utils::Log(Utils::LogLevel::Warn, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Utils::Log(Utils::LogLevel::Error, fmt, ##__VA_ARGS__)

#define LOG_DEBUG_W(fmt, ...) Utils::LogW(Utils::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO_W(fmt, ...)  Utils::LogW(Utils::LogLevel::Info, fmt, ##__VA_ARGS__)
#define LOG_WARN_W(fmt, ...)  Utils::LogW(Utils::LogLevel::Warn, fmt, ##__VA_ARGS__)
#define LOG_ERROR_W(fmt, ...) Utils::LogW(Utils::LogLevel::Error, fmt, ##__VA_ARGS__)
