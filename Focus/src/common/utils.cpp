#include "utils.h"
#include <tlhelp32.h>
#include <time.h>
#include <fstream>
#include <sstream>
#include <iomanip>

std::wstring getAppDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return path.substr(0, pos);
    }
    return L"";
}

ULONGLONG getCurrentTimeSeconds() {
    return static_cast<ULONGLONG>(time(NULL));
}

void logMessage(const std::wstring& msg) {
    std::wstring logPath = getAppDirectory() + L"\\focus.log";
    std::wofstream logFile(logPath.c_str(), std::ios::out | std::ios::app);
    if (!logFile.is_open()) return;

    time_t now = time(NULL);
    struct tm tstruct;
    localtime_s(&tstruct, &now);
    
    logFile << L"[" << (tstruct.tm_year + 1900) << L"-"
            << std::setw(2) << std::setfill(L'0') << (tstruct.tm_mon + 1) << L"-"
            << std::setw(2) << std::setfill(L'0') << tstruct.tm_mday << L" "
            << std::setw(2) << std::setfill(L'0') << tstruct.tm_hour << L":"
            << std::setw(2) << std::setfill(L'0') << tstruct.tm_min << L":"
            << std::setw(2) << std::setfill(L'0') << tstruct.tm_sec << L"] "
            << msg << std::endl;
    logFile.close();
}

bool isProcessRunning(const std::wstring& exeName, DWORD& pid) {
    bool running = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
                    running = true;
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return running;
}

bool launchProcess(const std::wstring& exePath, const std::wstring& args) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmdLine = L"\"" + exePath + L"\"";
    if (!args.empty()) {
        cmdLine += L" " + args;
    }

    // Copy command line to mutable buffer since CreateProcessW can modify it
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    bool success = CreateProcessW(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return success;
}

bool spawnSelf(const std::wstring& args) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    
    BOOL success = CreateProcessW(
        NULL,
        cmdBuf.data(),
        NULL, NULL, FALSE,
        CREATE_NO_WINDOW,
        NULL, NULL, &si, &pi
    );
    
    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}


std::wstring stringToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (wlen <= 0) return L"";
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wlen);
    if (!wstr.empty() && wstr.back() == L'\0') {
        wstr.pop_back();
    }
    return wstr;
}

std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
    if (!str.empty() && str.back() == L'\0') {
        str.pop_back();
    }
    return str;
}

std::wstring escapeJsonString(const std::wstring& str) {
    std::wstring result;
    for (wchar_t c : str) {
        if (c == L'\\') {
            result += L"\\\\";
        } else if (c == L'"') {
            result += L"\\\"";
        } else {
            result += c;
        }
    }
    return result;
}

std::wstring base64Encode(const void* data, size_t size) {
    const BYTE* bytes = reinterpret_cast<const BYTE*>(data);
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    r.reserve((size + 2) / 3 * 4);
    int val = 0, valb = -6;
    for (size_t i = 0; i < size; ++i) {
        val = (val << 8) + bytes[i];
        valb += 8;
        while (valb >= 0) {
            r.push_back(tbl[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) r.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
    while (r.size() % 4) r.push_back('=');
    return std::wstring(r.begin(), r.end());
}

