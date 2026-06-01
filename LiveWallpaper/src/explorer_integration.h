#pragma once
#include <windows.h>
#include <atomic>

class ExplorerIntegration {
public:
    ExplorerIntegration();
    ~ExplorerIntegration();

    bool Initialize(HINSTANCE hInstance);
    void Shutdown();
    void Update();

    HWND GetHWND() const { return m_hWnd; }

private:
    bool FindWorkerW();
    bool CreateHostWindow(HINSTANCE hInstance);
    bool InjectIntoDesktop();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    HWND m_hWorkerW = nullptr;
    HWND m_hShellDefView = nullptr;
    bool m_useLegacyWorkerW = true;

    DWORD m_lastUpdateTick = 0;
    DWORD m_lastRecoveryAttemptTick = 0;
    DWORD m_retryIntervalMs = 1000;
    std::atomic<bool> m_isRecovering{ false };
    std::atomic<bool> m_isShuttingDown{ false };
};
