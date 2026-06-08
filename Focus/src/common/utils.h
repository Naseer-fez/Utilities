#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::wstring getAppDirectory();
ULONGLONG getCurrentTimeSeconds();
void logMessage(const std::wstring& msg);
bool isProcessRunning(const std::wstring& exeName, DWORD& pid);
bool launchProcess(const std::wstring& exePath, const std::wstring& args = L"");
bool spawnSelf(const std::wstring& args);
std::wstring stringToWstring(const std::string& str);
std::string wstringToString(const std::wstring& wstr);
std::wstring escapeJsonString(const std::wstring& str);
std::wstring base64Encode(const void* data, size_t size);


