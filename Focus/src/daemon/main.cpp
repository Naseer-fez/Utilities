#include <windows.h>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include "engine.h"
#include "../common/ipc.h"
#include "../common/utils.h"
#include "../common/config.h"

static std::atomic<bool> s_running{true};
static FocusEngine s_engine;
static HANDLE s_daemonMutex = NULL;
static HANDLE s_shutdownEvent = NULL;
static HANDLE s_watchdogHandle = NULL;
static DWORD s_watchdogPid = 0;
static HWND s_hwndShutdown = NULL;

// Registry auto-start registration
void registerStartup() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring exePath = L"\"" + std::wstring(buffer) + L"\" --daemon";
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"FocusModeEngine", 0, REG_SZ, reinterpret_cast<const BYTE*>(exePath.c_str()), static_cast<DWORD>((exePath.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

// Check if tray is running by window handle
bool isTrayRunning(DWORD& pid) {
    HWND hwnd = FindWindowExW(HWND_MESSAGE, NULL, L"FocusTrayUIWindow", NULL);
    if (hwnd) {
        GetWindowThreadProcessId(hwnd, &pid);
        return true;
    }
    return false;
}

// Check if watchdog is running by named mutex
bool isWatchdogRunning() {
    HANDLE hMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, L"Global\\FocusModeWatchdogMutex");
    if (hMutex) {
        CloseHandle(hMutex);
        return true;
    }
    return false;
}

// Launches Tray UI and Watchdog processes
void launchCompanionProcesses() {
    DWORD trayPid = 0;
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring exePath(buffer);

    if (!isTrayRunning(trayPid)) {
        logMessage(L"Daemon: Launching Tray UI...");
        launchProcess(exePath, L"--tray");
        int retries = 10;
        while (retries-- > 0 && !isTrayRunning(trayPid)) {
            Sleep(200);
        }
    }

    if (!isWatchdogRunning()) {
        DWORD myPid = GetCurrentProcessId();
        std::wstring args = L"--watchdog " + std::to_wstring(myPid) + L" " + std::to_wstring(trayPid);
        logMessage(L"Daemon: Launching Watchdog with args: " + args);
        
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        std::wstring cmdLine = L"\"" + exePath + L"\" " + args;
        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(L'\0');
        
        if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            s_watchdogHandle = pi.hProcess;
            s_watchdogPid = pi.dwProcessId;
            CloseHandle(pi.hThread);
        }
    }
}

// Named IPC Server thread using Overlapped I/O
void ipcServerThread() {
    logMessage(L"Daemon: IPC named pipe server thread started.");
    
    HANDLE hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!hEvent) {
        logMessage(L"Daemon: Failed to create IPC helper event.");
        return;
    }

    while (s_running) {
        HANDLE hPipe = CreateNamedPipeW(
            FOCUS_PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(IpcMessage),
            sizeof(IpcMessage),
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            logMessage(L"Daemon: Failed to create named pipe. Error: " + std::to_wstring(GetLastError()));
            Sleep(1000);
            continue;
        }

        OVERLAPPED ov = { 0 };
        ov.hEvent = hEvent;
        ResetEvent(hEvent);

        BOOL connected = ConnectNamedPipe(hPipe, &ov);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { hEvent, s_shutdownEvent };
                DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0 + 1) {
                    CancelIo(hPipe);
                    DisconnectNamedPipe(hPipe);
                    CloseHandle(hPipe);
                    break;
                }
                connected = TRUE;
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }

        if (connected) {
            IpcMessage msg;
            DWORD bytesRead = 0;
            OVERLAPPED ovRead = { 0 };
            ovRead.hEvent = hEvent;
            ResetEvent(hEvent);

            BOOL readResult = ReadFile(hPipe, &msg, sizeof(IpcMessage), &bytesRead, &ovRead);
            if (!readResult) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[2] = { hEvent, s_shutdownEvent };
                    DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 5000); // 5s timeout
                    if (waitResult == WAIT_OBJECT_0) {
                        readResult = GetOverlappedResult(hPipe, &ovRead, &bytesRead, TRUE);
                    } else {
                        CancelIo(hPipe);
                        readResult = FALSE;
                    }
                }
            }
            
            if (readResult && bytesRead == sizeof(IpcMessage)) {
                IpcMessage reply;
                ZeroMemory(&reply, sizeof(IpcMessage));
                reply.type = msg.type;

                switch (msg.type) {
                    case IpcCommandType::GetStatus: {
                        reply.isStrictActive = s_engine.isSessionActive();
                        reply.timeRemainingSeconds = s_engine.getTimeRemainingSeconds();
                        wcscpy_s(reply.profileName, s_engine.getState().activeProfile);
                        reply.success = true;
                        break;
                    }
                    case IpcCommandType::StartSession: {
                        if (s_engine.isSessionActive()) {
                            reply.success = false;
                            wcscpy_s(reply.statusText, L"Session already active.");
                        } else {
                            std::wstring configPath = getAppDirectory() + L"\\config.json";
                            std::vector<Profile> profiles = loadProfiles(configPath);
                            auto it = std::find_if(profiles.begin(), profiles.end(), 
                                [&msg](const Profile& p) {
                                    return _wcsicmp(p.name.c_str(), msg.profileName) == 0;
                                });
                            
                            if (it != profiles.end()) {
                                int dur = (msg.durationMinutes > 0) ? msg.durationMinutes : it->durationMinutes;
                                reply.success = s_engine.startSession(*it, dur);
                                if (reply.success) {
                                    wcscpy_s(reply.statusText, L"Session started successfully.");
                                } else {
                                    wcscpy_s(reply.statusText, L"Failed to start session.");
                                }
                            } else {
                                reply.success = false;
                                wcscpy_s(reply.statusText, L"Profile not found in config.");
                            }
                        }
                        break;
                    }
                    case IpcCommandType::RequestUnlock: {
                        if (!s_engine.isSessionActive()) {
                            reply.success = false;
                            wcscpy_s(reply.statusText, L"No active session.");
                        } else {
                            std::wstring code = s_engine.requestUnlockCode();
                            wcscpy_s(reply.unlockCode, code.c_str());
                            reply.success = true;
                        }
                        break;
                    }
                    case IpcCommandType::SubmitUnlockCode: {
                        reply.success = s_engine.verifyUnlockCode(msg.unlockCode);
                        if (reply.success) {
                            wcscpy_s(reply.statusText, L"Unlock verified. Session stopped.");
                        } else {
                            wcscpy_s(reply.statusText, L"Invalid unlock code.");
                        }
                        break;
                    }
                    case IpcCommandType::ForceQuit: {
                        if (s_engine.isSessionActive()) {
                            if (s_engine.verifyUnlockCode(msg.unlockCode)) {
                                s_running = false;
                                reply.success = true;
                                wcscpy_s(reply.statusText, L"Daemon stopping via authorized bypass.");
                                if (s_hwndShutdown) {
                                    PostMessageW(s_hwndShutdown, WM_CLOSE, 0, 0);
                                }
                            } else {
                                reply.success = false;
                                wcscpy_s(reply.statusText, L"Cannot force quit: strict focus active.");
                            }
                        } else {
                            s_running = false;
                            reply.success = true;
                            wcscpy_s(reply.statusText, L"Daemon stopping cleanly.");
                            if (s_hwndShutdown) {
                                PostMessageW(s_hwndShutdown, WM_CLOSE, 0, 0);
                            }
                        }
                        break;
                    }
                    case IpcCommandType::ReloadConfig: {
                        s_engine.reloadConfig();
                        reply.success = true;
                        wcscpy_s(reply.statusText, L"Configuration reloaded successfully.");
                        break;
                    }
                }

                DWORD bytesWritten = 0;
                OVERLAPPED ovWrite = { 0 };
                ovWrite.hEvent = hEvent;
                ResetEvent(hEvent);
                BOOL writeResult = WriteFile(hPipe, &reply, sizeof(IpcMessage), &bytesWritten, &ovWrite);
                if (!writeResult && GetLastError() == ERROR_IO_PENDING) {
                    WaitForSingleObject(hEvent, INFINITE);
                    GetOverlappedResult(hPipe, &ovWrite, &bytesWritten, TRUE);
                }
            }
        }
        
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    
    CloseHandle(hEvent);
    logMessage(L"Daemon: IPC named pipe server thread stopped.");
}

// Daemon tick and evaluation thread (updates every 1s)
void tickThread() {
    while (s_running) {
        if (s_engine.isSessionActive()) {
            s_engine.tick();
        }
        Sleep(1000);
    }
}

// Window procedure for hidden message window to handle shutdown
LRESULT CALLBACK ShutdownWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_QUERYENDSESSION:
            logMessage(L"Daemon: WM_QUERYENDSESSION received (system shutdown initiated).");
            return TRUE;
        case WM_ENDSESSION:
            if (wParam == TRUE) {
                logMessage(L"Daemon: WM_ENDSESSION true. Serializing focus state for reboot recovery...");
                if (s_engine.isSessionActive()) {
                    s_engine.resumeSession(s_engine.getState());
                }
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int runDaemon(HINSTANCE hInstance, int argc, wchar_t* argv[]) {
    s_daemonMutex = CreateMutexW(NULL, TRUE, L"Global\\FocusModeDaemonMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        logMessage(L"Daemon: Another instance of Daemon is already running. Exiting.");
        if (s_daemonMutex) CloseHandle(s_daemonMutex);
        return 0;
    }

    logMessage(L"Daemon: Starting up Focus Mode Engine Daemon V2 (Unified focus.exe)...");

    s_shutdownEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s_shutdownEvent) {
        logMessage(L"Daemon: Failed to create shutdown event.");
        if (s_daemonMutex) CloseHandle(s_daemonMutex);
        return 1;
    }

    registerStartup();
    s_engine.initialize();

    std::thread ticker(tickThread);
    std::thread ipcServer(ipcServerThread);

    launchCompanionProcesses();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = ShutdownWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FocusDaemonShutdownListener";
    RegisterClassW(&wc);
    
    s_hwndShutdown = CreateWindowExW(
        0, L"FocusDaemonShutdownListener", L"FocusDaemonShutdownListener",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL
    );

    if (!s_hwndShutdown) {
        logMessage(L"Daemon: Failed to create hidden shutdown listener window.");
    }

    // Standard non-polling GetMessage message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    logMessage(L"Daemon: Shutting down daemon processes.");
    s_running = false;
    
    if (s_shutdownEvent) {
        SetEvent(s_shutdownEvent);
    }

    if (s_watchdogHandle != NULL) {
        TerminateProcess(s_watchdogHandle, 0);
        CloseHandle(s_watchdogHandle);
        s_watchdogHandle = NULL;
        logMessage(L"Daemon: Watchdog process terminated for clean exit.");
    }

    s_engine.shutdown();

    if (ticker.joinable()) ticker.join();
    if (ipcServer.joinable()) ipcServer.join();

    if (s_hwndShutdown) DestroyWindow(s_hwndShutdown);
    UnregisterClassW(L"FocusDaemonShutdownListener", hInstance);

    if (s_shutdownEvent) CloseHandle(s_shutdownEvent);
    if (s_daemonMutex) {
        ReleaseMutex(s_daemonMutex);
        CloseHandle(s_daemonMutex);
    }

    logMessage(L"Daemon: Clean exit completed.");
    return 0;
}
