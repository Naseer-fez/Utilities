#pragma once

// ============================================================================
// power_monitor.h — System state detection for LiveWallpaper
//
// Provides static queries for power state, user activity, and per-monitor
// fullscreen app detection. All methods are pure queries with no side effects.
// ============================================================================

#include "utils.h"

namespace lw {

class PowerMonitor {
public:
    /// Check if the system is currently running on battery power.
    static bool IsOnBattery();

    /// Check if the user has been idle (no mouse/keyboard input) for longer
    /// than the specified timeout in milliseconds.
    static bool IsUserIdle(DWORD timeoutMs);

    /// Check if a fullscreen application is covering a specific physical monitor.
    /// Utilizes a high-precision bounds check with tolerance.
    static bool IsFullscreenAppOnMonitor(HMONITOR hMonitor);
};

} // namespace lw
