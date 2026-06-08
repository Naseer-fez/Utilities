#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "common/utils.h"

// Forward declarations of entry points
int runDaemon(HINSTANCE hInstance, int argc, wchar_t* argv[]);
int runTray(HINSTANCE hInstance, int argc, wchar_t* argv[], bool spawnDaemonIfMissing = false);
int runWatchdog(HINSTANCE hInstance, int argc, wchar_t* argv[]);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    std::wstring mode = L"";
    if (argc > 1) {
        mode = argv[1];
    }

    int result = 0;
    if (mode == L"--daemon") {
        result = runDaemon(hInstance, argc, argv);
    } else if (mode == L"--tray") {
        result = runTray(hInstance, argc, argv, false);
    } else if (mode == L"--watchdog") {
        result = runWatchdog(hInstance, argc, argv);
    } else {
        // Default mode: User launched focus.exe manually.
        // Run tray UI in this process and spawn daemon if missing
        result = runTray(hInstance, argc, argv, true);
    }


    LocalFree(argv);
    return result;
}
