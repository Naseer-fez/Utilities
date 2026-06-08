#pragma once
#include <windows.h>
#include <string>

struct SessionState {
    bool isActive;
    wchar_t activeProfile[64];
    ULONGLONG startTimeSeconds; // seconds since Unix epoch
    ULONGLONG durationSeconds;
    wchar_t originalWallpaper[MAX_PATH];
    int originalVolume;
    wchar_t unlockCode[16];
};

bool saveSessionState(const SessionState& state, const std::wstring& path);
bool loadSessionState(SessionState& state, const std::wstring& path);
void deleteSessionState(const std::wstring& path);
