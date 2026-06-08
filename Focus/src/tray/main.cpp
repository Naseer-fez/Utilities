#include <windows.h>
#include "gui.h"
#include "../common/utils.h"

int runTray(HINSTANCE hInstance, int argc, wchar_t* argv[], bool spawnDaemonIfMissing) {
    // Initialize COM for Shell Dialogs (GetOpenFileNameW)
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // 0. Check if another instance is already running and wake it up
    HWND hExisting = FindWindowExW(HWND_MESSAGE, NULL, L"FocusTrayUIWindow", NULL);
    if (hExisting) {
        PostMessageW(hExisting, WM_COMMAND, ID_MENU_CONTROL_PANEL, 0);
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 0;
    }

    // 1. Single instance enforcement via Mutex
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\FocusModeTrayMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        logMessage(L"TrayUI: Another instance of tray UI is already running. Exiting.");
        if (hMutex) CloseHandle(hMutex);
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 0;
    }

    logMessage(L"TrayUI: Starting Focus Mode System Tray UI (Unified focus.exe)...");

    // 2. Initialize and run UI
    TrayUI trayUI(hInstance);
    if (trayUI.initialize()) {
        // 2.1 Spawn daemon if requested and not running
        if (spawnDaemonIfMissing) {
            HANDLE hDaemonMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, L"Global\\FocusModeDaemonMutex");
            if (!hDaemonMutex) {
                logMessage(L"TrayUI: Daemon not running. Spawning daemon process...");
                spawnSelf(L"--daemon");
                // Wait a bit for the daemon to start and register its mutex
                Sleep(500);
            } else {
                CloseHandle(hDaemonMutex);
            }
            
            // Auto open control panel on manual run
            PostMessageW(trayUI.getWindowHandle(), WM_COMMAND, ID_MENU_CONTROL_PANEL, 0);
        }
        trayUI.run();
    } else {
        logMessage(L"TrayUI: Failed to initialize Tray UI application.");
    }


    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    logMessage(L"TrayUI: Exiting.");
    if (SUCCEEDED(hrCom)) CoUninitialize();
    return 0;
}
