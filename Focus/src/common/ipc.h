#pragma once
#include <windows.h>
#include <string>

#define FOCUS_PIPE_NAME L"\\\\.\\pipe\\FocusModeEnginePipe"

enum class IpcCommandType : int {
    GetStatus = 0,
    StartSession = 1,
    RequestUnlock = 2,
    SubmitUnlockCode = 3,
    ForceQuit = 4,
    ReloadConfig = 5
};

struct IpcMessage {
    IpcCommandType type;
    wchar_t profileName[64];
    int durationMinutes;
    wchar_t unlockCode[16];
    bool success;
    wchar_t statusText[256];
    int timeRemainingSeconds;
    bool isStrictActive;
};
