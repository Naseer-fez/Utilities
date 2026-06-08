#include <windows.h>
#include <string>
#include <vector>
#include "../common/state.h"
#include "../common/utils.h"

// Launch a process and retrieve its process handle
HANDLE launchProcessAndGetHandle(const std::wstring& exePath, const std::wstring& args, DWORD& outPid) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmdLine = L"\"" + exePath + L"\"";
    if (!args.empty()) {
        cmdLine += L" " + args;
    }
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
        outPid = pi.dwProcessId;
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }
    return NULL;
}

int runWatchdog(HINSTANCE hInstance, int argc, wchar_t* argv[]) {
    // 0. Enforce single instance / register watchdog running status via named Mutex
    HANDLE hWatchdogMutex = CreateMutexW(NULL, TRUE, L"Global\\FocusModeWatchdogMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        logMessage(L"Watchdog: Another instance of Watchdog is already running. Exiting.");
        if (hWatchdogMutex) CloseHandle(hWatchdogMutex);
        return 0;
    }

    // Check command line arguments: --watchdog <daemon_pid> <tray_pid>
    if (argc < 4) {
        logMessage(L"Watchdog: Insufficient arguments. Exiting watchdog.");
        if (hWatchdogMutex) {
            ReleaseMutex(hWatchdogMutex);
            CloseHandle(hWatchdogMutex);
        }
        return 0;
    }

    DWORD daemonPid = static_cast<DWORD>(_wtoi(argv[2]));
    DWORD trayPid = static_cast<DWORD>(_wtoi(argv[3]));

    logMessage(L"Watchdog: Starting process monitor for Daemon (PID: " + std::to_wstring(daemonPid) + L") and Tray UI (PID: " + std::to_wstring(trayPid) + L")...");

    HANDLE processes[2] = { NULL, NULL };
    processes[0] = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, daemonPid);
    if (!processes[0]) {
        logMessage(L"Watchdog: Warning - Failed to open Daemon process.");
    }
    processes[1] = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, trayPid);
    if (!processes[1]) {
        logMessage(L"Watchdog: Warning - Failed to open Tray UI process.");
    }

    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring exePath(buffer);
    std::wstring stateFilePath = getAppDirectory() + L"\\.focus_session";

    bool running = true;
    while (running) {
        HANDLE waitHandles[2];
        DWORD waitCount = 0;
        int handleMap[2]; // maps waitHandles index back to processes index (0 = daemon, 1 = tray)
        
        if (processes[0] != NULL) {
            waitHandles[waitCount] = processes[0];
            handleMap[waitCount] = 0;
            waitCount++;
        }
        if (processes[1] != NULL) {
            waitHandles[waitCount] = processes[1];
            handleMap[waitCount] = 1;
            waitCount++;
        }

        if (waitCount == 0) {
            // Both are dead.
            // If active session, we should restart both. Otherwise exit.
            SessionState state;
            ZeroMemory(&state, sizeof(SessionState));
            bool hasActiveSession = loadSessionState(state, stateFilePath) && state.isActive;
            if (hasActiveSession) {
                logMessage(L"Watchdog: Both processes are dead during active session. Restarting both.");
                DWORD newDaemonPid = 0;
                processes[0] = launchProcessAndGetHandle(exePath, L"--daemon", newDaemonPid);
                DWORD newTrayPid = 0;
                processes[1] = launchProcessAndGetHandle(exePath, L"--tray", newTrayPid);
                if (!processes[0] && !processes[1]) {
                    logMessage(L"Watchdog: Critical - Failed to restart both processes. Exiting.");
                    running = false;
                }
            } else {
                running = false;
            }
            Sleep(1000);
            continue;
        }

        // Wait up to 1 second for either process to signal termination
        DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles, FALSE, 1000);

        // Check if there is an active session
        SessionState state;
        ZeroMemory(&state, sizeof(SessionState));
        bool hasActiveSession = loadSessionState(state, stateFilePath) && state.isActive;

        if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + waitCount) {
            DWORD idx = waitResult - WAIT_OBJECT_0;
            int processIdx = handleMap[idx];
            CloseHandle(processes[processIdx]);
            processes[processIdx] = NULL;
            
            if (processIdx == 0) {
                logMessage(L"Watchdog: Daemon process terminated.");
                if (hasActiveSession) {
                    logMessage(L"Watchdog: Active focus session detected. Restarting Daemon...");
                    DWORD newDaemonPid = 0;
                    processes[0] = launchProcessAndGetHandle(exePath, L"--daemon", newDaemonPid);
                    if (processes[0]) {
                        logMessage(L"Watchdog: Daemon restarted with new PID: " + std::to_wstring(newDaemonPid));
                    } else {
                        logMessage(L"Watchdog: Failed to restart Daemon.");
                        running = false;
                    }
                } else {
                    logMessage(L"Watchdog: Clean exit. No active session. Exiting watchdog.");
                    running = false;
                }
            } 
            else if (processIdx == 1) {
                logMessage(L"Watchdog: Tray UI process terminated.");
                if (hasActiveSession) {
                    logMessage(L"Watchdog: Active focus session detected. Restarting Tray UI...");
                    DWORD newTrayPid = 0;
                    processes[1] = launchProcessAndGetHandle(exePath, L"--tray", newTrayPid);
                    if (processes[1]) {
                        logMessage(L"Watchdog: Tray UI restarted with new PID: " + std::to_wstring(newTrayPid));
                    } else {
                        logMessage(L"Watchdog: Failed to restart Tray UI.");
                        running = false;
                    }
                } else {
                    logMessage(L"Watchdog: Clean exit. No active session. Exiting watchdog.");
                    running = false;
                }
            }
        }
        else if (waitResult == WAIT_FAILED) {
            logMessage(L"Watchdog: Wait failed. Error: " + std::to_wstring(GetLastError()));
            running = false;
        }

        // If both handles are dead and not restarted, exit watchdog
        if (processes[0] == NULL && processes[1] == NULL) {
            running = false;
        }
    }

    if (processes[0]) CloseHandle(processes[0]);
    if (processes[1]) CloseHandle(processes[1]);

    if (hWatchdogMutex) {
        ReleaseMutex(hWatchdogMutex);
        CloseHandle(hWatchdogMutex);
    }

    logMessage(L"Watchdog: Watchdog exited.");
    return 0;
}
