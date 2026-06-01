#pragma once
#include <windows.h>
#include <functional>

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();

    bool Initialize(HINSTANCE hInstance);
    void Shutdown();

    void SetPauseCallback(std::function<void(bool)> callback);
    void CheckForegroundAndIdleStates(int idleTimeoutMinutes);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void EvaluatePowerState();

    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HPOWERNOTIFY m_hPowerNotifyAcDc = nullptr;
    HPOWERNOTIFY m_hPowerNotifyDisplay = nullptr;
    std::function<void(bool)> m_pauseCallback;

    bool m_isOnBattery = false;
    bool m_isDisplayOff = false;
    bool m_isFullscreenAppRunning = false;
    bool m_isUserIdle = false;
    bool m_isObscured = false;
};
