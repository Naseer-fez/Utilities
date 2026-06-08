#pragma once
// =============================================================================
// timer.h — High-resolution waitable timer for frame pacing
//
// Wraps a Windows waitable timer that fires periodically at the configured FPS.
// The timer handle can be used with WaitForMultipleObjects in the render thread,
// allowing the thread to sleep with zero CPU usage between frames.
//
// Prefers CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Win10 1803+) for sub-ms
// precision, falling back to the standard timer on older builds.
// =============================================================================

#include "utils.h"

namespace lw {

class FrameTimer {
public:
    FrameTimer() = default;
    ~FrameTimer();

    // Initialize with target FPS. Creates the waitable timer handle.
    // fps must be > 0. Typical values: 24, 30, 60.
    HRESULT Init(int fps);

    // Start the periodic timer. The timer will fire every (1000/fps) ms.
    HRESULT Start();

    // Stop the timer (cancels the periodic signal without closing the handle).
    void Stop();

    // Change FPS without recreating the timer. Stops and restarts with new period.
    HRESULT ChangeFPS(int newFps);

    // Get the timer handle for use in WaitForMultipleObjects.
    HANDLE GetHandle() const { return m_timer; }

    // Close the timer handle and release all resources.
    void Shutdown();

private:
    HANDLE m_timer = NULL;       // Waitable timer handle
    int    m_fps   = 24;         // Target frames per second
    LARGE_INTEGER m_dueTime = {};  // Relative due time for SetWaitableTimer
};

} // namespace lw
