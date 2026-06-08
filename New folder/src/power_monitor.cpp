// ============================================================================
// power_monitor.cpp — System state detection implementation
// ============================================================================

#include "power_monitor.h"
#include <ShellAPI.h>
#include <dwmapi.h>

namespace lw {

bool PowerMonitor::IsOnBattery()
{
    SYSTEM_POWER_STATUS sps = {};
    if (!GetSystemPowerStatus(&sps)) {
        LOG_WARN("GetSystemPowerStatus failed, assuming AC power");
        return false;
    }
    return sps.ACLineStatus == 0;
}

bool PowerMonitor::IsUserIdle(DWORD timeoutMs)
{
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(LASTINPUTINFO);

    if (!GetLastInputInfo(&lii)) {
        LOG_WARN("GetLastInputInfo failed, assuming user is active");
        return false;
    }

    DWORD elapsedMs = GetTickCount() - lii.dwTime;
    return elapsedMs > timeoutMs;
}

bool PowerMonitor::IsFullscreenAppOnMonitor(HMONITOR hMonitor)
{
    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd || fgWnd == GetDesktopWindow() || fgWnd == GetShellWindow()) {
        return false;
    }

    // Exclude Windows shell components
    wchar_t className[256];
    if (GetClassNameW(fgWnd, className, 256)) {
        if (wcscmp(className, L"Progman") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
            return false;
        }
    }

    // Exclude standard maximized windows (which are not true fullscreen games/apps)
    LONG style = GetWindowLongW(fgWnd, GWL_STYLE);
    if (style & WS_MAXIMIZE) {
        return false;
    }

    // Verify the foreground window is located on the queried monitor
    HMONITOR wndMonitor = MonitorFromWindow(fgWnd, MONITOR_DEFAULTTONULL);
    if (!wndMonitor || wndMonitor != hMonitor) {
        return false;
    }

    MONITORINFO mi = {};
    mi.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfoW(hMonitor, &mi)) {
        return false;
    }

    RECT rect = {};
    // Use DwmGetWindowAttribute to get the actual bounds without drop shadows/invisible borders
    HRESULT hr = DwmGetWindowAttribute(fgWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        if (!GetWindowRect(fgWnd, &rect)) {
            return false;
        }
    }

    // High tolerance check (±2px) to handle borderless Snap, DirectFlip,Snapped layouts, and snaps
    bool isFullscreen = (rect.left <= mi.rcMonitor.left + 2 &&
                         rect.top <= mi.rcMonitor.top + 2 &&
                         rect.right >= mi.rcMonitor.right - 2 &&
                         rect.bottom >= mi.rcMonitor.bottom - 2);

    return isFullscreen;
}

} // namespace lw
