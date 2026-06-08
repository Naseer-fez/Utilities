#pragma once

// ============================================================================
// wallpaper_host.h — Desktop WorkerW window injection and management
//
// Handles the undocumented WorkerW hijack technique to embed a render
// surface behind desktop icons. Manages the child window lifecycle,
// explorer.exe crash recovery via RegisterWaitForSingleObject, and
// display change handling.
// ============================================================================

#include "utils.h"
#include <tlhelp32.h>
#include <vector>
#include <string>

namespace lw {

/// Structure representing a render window bound to a physical monitor.
struct MonitorOutput {
    HMONITOR hMonitor;
    HWND renderWnd;
    RECT rect;
    std::wstring deviceName;
    bool isPaused;
};

/// Callback type for explorer.exe crash notifications.
/// Called from a thread-pool thread when explorer.exe terminates.
using ExplorerCrashCallback = void(CALLBACK*)(void* context, BOOLEAN timerOrWaitFired);

class WallpaperHost {
public:
    WallpaperHost() = default;
    ~WallpaperHost();

    /// Find the Progman window, send 0x052C to spawn WorkerW,
    /// enumerate displays, and create child render windows in the WorkerW container.
    HRESULT Init();

    /// Re-run the WorkerW injection sequence after explorer.exe restarts.
    /// Safely clears stale handles and allocates new HWNDs at monitor coordinates.
    HRESULT Recover();

    /// Register a callback that fires when explorer.exe terminates.
    HRESULT StartExplorerWatch(ExplorerCrashCallback callback, void* context);

    /// Unregister the explorer watch and destroy all render windows.
    void Shutdown();

    // Multi-monitor state accessors
    size_t GetMonitorCount() const { return m_monitors.size(); }
    HWND GetMonitorHWND(size_t index) const { return m_monitors[index].renderWnd; }
    HMONITOR GetMonitorHandle(size_t index) const { return m_monitors[index].hMonitor; }
    const RECT& GetMonitorRect(size_t index) const { return m_monitors[index].rect; }
    bool IsMonitorPaused(size_t index) const { return m_monitors[index].isPaused; }
    void SetMonitorPauseState(size_t index, bool paused) { m_monitors[index].isPaused = paused; }

    HWND GetWorkerW() const { return m_workerW; }

private:
    /// Find the WorkerW window that sits behind desktop icons.
    HRESULT FindWorkerW();

    /// Get the PID of explorer.exe via CreateToolhelp32Snapshot.
    DWORD FindExplorerPID();

    /// Diagnostics trace helper for shell updates
    void LogShellHierarchy();

    HWND   m_progman   = nullptr;   // Progman window handle
    HWND   m_workerW   = nullptr;   // Target WorkerW (parent of our render windows)
    
    std::vector<MonitorOutput> m_monitors; // Active render windows per monitor
    
    HANDLE m_waitHandle = nullptr;  // RegisterWaitForSingleObject handle
    HANDLE m_explorerProcess = nullptr; // explorer.exe process handle
};

} // namespace lw
