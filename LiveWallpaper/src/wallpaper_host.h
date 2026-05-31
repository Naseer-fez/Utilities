#pragma once
#include <windows.h>

class WallpaperHost {
public:
    WallpaperHost();
    ~WallpaperHost();

    bool Initialize(HINSTANCE hInstance);
    void Shutdown();

    HWND GetHWND() const { return m_hWnd; }
    HWND GetWorkerW() const { return m_hWorkerW; }

    // Checks if WorkerW or the host window is still valid, attempts recovery if needed.
    void Update();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    
    bool FindWorkerW();
    bool CreateHostWindow(HINSTANCE hInstance);
    bool InjectIntoDesktop();

    HWND m_hWnd = nullptr;
    HWND m_hWorkerW = nullptr;
    HWND m_hShellDefView = nullptr;
    HINSTANCE m_hInstance = nullptr;
    DWORD m_lastUpdateTick = 0;
    bool m_useLegacyWorkerW = true;
    bool m_isRecovering = false;
    bool m_isShuttingDown = false;
};

