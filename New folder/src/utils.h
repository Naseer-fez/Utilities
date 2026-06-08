#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>
#include <shlobj.h>
#include <string>
#include <cstdio>
#include <cstdarg>

using Microsoft::WRL::ComPtr;

namespace lw {

// Log levels
enum class LogLevel { Error, Warn, Info, Debug };

// Simple file logger - writes to %APPDATA%\LiveWallpaper\log.txt
// NOT for use in render hot path
class Logger {
public:
    static Logger& Instance() {
        static Logger s_instance;
        return s_instance;
    }

    void Log(LogLevel level, const char* file, int line, const char* fmt, ...) {
        if (!m_initialized) Initialize();
        if (!m_file) return;

#ifndef _DEBUG
        if (level == LogLevel::Debug) return;
#endif

        const char* levelStr = "";
        switch (level) {
            case LogLevel::Error: levelStr = "ERROR"; break;
            case LogLevel::Warn:  levelStr = "WARN "; break;
            case LogLevel::Info:  levelStr = "INFO "; break;
            case LogLevel::Debug: levelStr = "DEBUG"; break;
        }

        // Get timestamp
        SYSTEMTIME st;
        GetLocalTime(&st);

        fprintf(m_file, "[%02d:%02d:%02d.%03d] [%s] ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, levelStr);

        va_list args;
        va_start(args, fmt);
        vfprintf(m_file, fmt, args);
        va_end(args);

        fprintf(m_file, " (%s:%d)\n", file, line);
        fflush(m_file);

        // Simple rotation: if file > 1MB, rewind
        long pos = ftell(m_file);
        if (pos > 1024 * 1024) {
            fseek(m_file, 0, SEEK_SET);
        }
    }

    ~Logger() {
        if (m_file) fclose(m_file);
    }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Initialize() {
        m_initialized = true;
        wchar_t appData[MAX_PATH];
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) return;

        std::wstring logDir = std::wstring(appData) + L"\\LiveWallpaper";
        CreateDirectoryW(logDir.c_str(), NULL);

        std::wstring logPath = logDir + L"\\log.txt";
        m_file = _wfopen(logPath.c_str(), L"a");
    }

    FILE* m_file = nullptr;
    bool m_initialized = false;
};

// Logging macros
#define LOG_ERROR(fmt, ...) lw::Logger::Instance().Log(lw::LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  lw::Logger::Instance().Log(lw::LogLevel::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  lw::Logger::Instance().Log(lw::LogLevel::Info,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#ifdef _DEBUG
#define LOG_DEBUG(fmt, ...) lw::Logger::Instance().Log(lw::LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

// HRESULT checking macro - logs error and returns on failure
#define HR_CHECK(hr, msg) \
    do { \
        HRESULT _hr = (hr); \
        if (FAILED(_hr)) { \
            LOG_ERROR("%s failed with HRESULT 0x%08X", msg, (unsigned int)_hr); \
            return _hr; \
        } \
    } while(0)

// HRESULT check that returns a bool instead
#define HR_CHECK_BOOL(hr, msg) \
    do { \
        HRESULT _hr = (hr); \
        if (FAILED(_hr)) { \
            LOG_ERROR("%s failed with HRESULT 0x%08X", msg, (unsigned int)_hr); \
            return false; \
        } \
    } while(0)

// Safe release helper for raw COM pointers (when ComPtr isn't used)
template<typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

} // namespace lw
