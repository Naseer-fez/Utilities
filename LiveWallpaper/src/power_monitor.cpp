#include "power_monitor.h"
#include "utils.h"
#include <winuser.h>
#include <initguid.h>

#ifndef GUID_ACDC_POWER_SOURCE
DEFINE_GUID(GUID_ACDC_POWER_SOURCE, 0x5D3E9A59, 0xE9D5, 0x4B00, 0xA6, 0xBD, 0xFF, 0x34, 0xEA, 0x32, 0xA2, 0xF1);
#endif

#ifndef GUID_CONSOLE_DISPLAY_STATE
DEFINE_GUID(GUID_CONSOLE_DISPLAY_STATE, 0x6FE69556, 0x704A, 0x47A0, 0x8F, 0x24, 0xC2, 0x8D, 0x93, 0x6F, 0xDA, 0x47);
#endif

PowerMonitor::PowerMonitor() {}

PowerMonitor::~PowerMonitor() {
    Shutdown();
}

bool PowerMonitor::Initialize(HINSTANCE hInstance) {
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof(wcx);
    wcx.lpfnWndProc = WndProc;
    wcx.hInstance = hInstance;
    wcx.lpszClassName = L"LiveWallpaperPowerMonitorClass";

    RegisterClassExW(&wcx);

    m_hWnd = CreateWindowExW(
        0,
        L"LiveWallpaperPowerMonitorClass",
        L"PowerMonitor",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        hInstance,
        this
    );

    if (!m_hWnd) {
        LOG_ERROR("Failed to create PowerMonitor window.");
        return false;
    }

    m_hPowerNotifyAcDc = RegisterPowerSettingNotification(m_hWnd, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
    m_hPowerNotifyDisplay = RegisterPowerSettingNotification(m_hWnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Initial state check
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        m_isOnBattery = (status.ACLineStatus == 0);
    }
    m_isDisplayOff = false; // Assume on at start

    EvaluatePowerState();

    LOG_INFO("PowerMonitor initialized. Initial battery state: %d", m_isOnBattery);
    return true;
}

void PowerMonitor::Shutdown() {
    if (m_hPowerNotifyAcDc) {
        UnregisterPowerSettingNotification(m_hPowerNotifyAcDc);
        m_hPowerNotifyAcDc = nullptr;
    }
    if (m_hPowerNotifyDisplay) {
        UnregisterPowerSettingNotification(m_hPowerNotifyDisplay);
        m_hPowerNotifyDisplay = nullptr;
    }
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

void PowerMonitor::SetPauseCallback(std::function<void(bool)> callback) {
    m_pauseCallback = callback;
    EvaluatePowerState(); // Notify initial state
}

void PowerMonitor::EvaluatePowerState() {
    bool shouldPause = m_isOnBattery || m_isDisplayOff || m_isFullscreenAppRunning || m_isUserIdle;
    if (m_pauseCallback) {
        m_pauseCallback(shouldPause);
    }
}

void PowerMonitor::CheckForegroundAndIdleStates(int idleTimeoutMinutes) {
    bool stateChanged = false;

    // 1. Idle Detection
    bool newIsUserIdle = false;
    if (idleTimeoutMinutes > 0) {
        LASTINPUTINFO lii;
        lii.cbSize = sizeof(LASTINPUTINFO);
        if (GetLastInputInfo(&lii)) {
            DWORD currentTick = GetTickCount();
            DWORD elapsedMs = currentTick - lii.dwTime;
            if (elapsedMs >= (DWORD)(idleTimeoutMinutes * 60 * 1000)) {
                newIsUserIdle = true;
            }
        }
    }

    if (newIsUserIdle != m_isUserIdle) {
        m_isUserIdle = newIsUserIdle;
        stateChanged = true;
        LOG_INFO("User idle state changed to: %d", m_isUserIdle);
    }

    // 2. Fullscreen Detection
    bool newIsFullscreenAppRunning = false;
    HWND hForeground = GetForegroundWindow();
    if (hForeground != nullptr) {
        wchar_t className[256];
        if (GetClassNameW(hForeground, className, 256) > 0) {
            if (wcscmp(className, L"WorkerW") != 0 && wcscmp(className, L"Progman") != 0 && wcscmp(className, L"Shell_TrayWnd") != 0) {
                RECT rcApp, rcMonitor;
                GetWindowRect(hForeground, &rcApp);
                HMONITOR hMonitor = MonitorFromWindow(hForeground, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(MONITORINFO) };
                if (GetMonitorInfoW(hMonitor, &mi)) {
                    rcMonitor = mi.rcMonitor;
                    if (rcApp.left <= rcMonitor.left && rcApp.top <= rcMonitor.top &&
                        rcApp.right >= rcMonitor.right && rcApp.bottom >= rcMonitor.bottom) {
                        newIsFullscreenAppRunning = true;
                    }
                }
            }
        }
    }

    if (newIsFullscreenAppRunning != m_isFullscreenAppRunning) {
        m_isFullscreenAppRunning = newIsFullscreenAppRunning;
        stateChanged = true;
        LOG_INFO("Fullscreen app state changed to: %d", m_isFullscreenAppRunning);
    }

    if (stateChanged) {
        EvaluatePowerState();
    }
}

LRESULT CALLBACK PowerMonitor::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PowerMonitor* pThis = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<PowerMonitor*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<PowerMonitor*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (message == WM_POWERBROADCAST && wParam == PBT_POWERSETTINGCHANGE && pThis) {
        POWERBROADCAST_SETTING* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
        
        if (setting->PowerSetting == GUID_ACDC_POWER_SOURCE && setting->DataLength == sizeof(int)) {
            int acStatus = *reinterpret_cast<int*>(setting->Data);
            pThis->m_isOnBattery = (acStatus == 0); // 0 = battery, 1 = AC, 2 = UPS
            LOG_INFO("Power state changed. Is on battery: %d", pThis->m_isOnBattery);
            pThis->EvaluatePowerState();
        } 
        else if (setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE && setting->DataLength == sizeof(int)) {
            int displayStatus = *reinterpret_cast<int*>(setting->Data);
            pThis->m_isDisplayOff = (displayStatus == 0); // 0 = off, 1 = on, 2 = dimmed
            LOG_INFO("Display state changed. Is display off: %d", pThis->m_isDisplayOff);
            pThis->EvaluatePowerState();
        }
        return TRUE;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}
