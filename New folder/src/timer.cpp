// =============================================================================
// timer.cpp — Implementation of the high-resolution waitable timer
//
// The timer fires periodically at 1000/fps ms intervals. It uses the
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION flag available on Win10 1803+ for
// sub-millisecond precision. On older systems, falls back to a standard
// waitable timer (~15ms granularity, acceptable for 24-30 FPS targets).
//
// The due time is set as a negative LARGE_INTEGER (relative interval).
// SetWaitableTimer's lPeriod parameter auto-rearms the timer each period.
// =============================================================================

#include "timer.h"

namespace lw {

// ---------------------------------------------------------------------------
// Destructor — ensure the handle is closed
// ---------------------------------------------------------------------------
FrameTimer::~FrameTimer() {
    Shutdown();
}

// ---------------------------------------------------------------------------
// Init — create the waitable timer handle and store the target FPS.
// Does NOT start the timer; call Start() separately.
// ---------------------------------------------------------------------------
HRESULT FrameTimer::Init(int fps) {
    if (fps <= 0) {
        LOG_ERROR("FrameTimer::Init — invalid FPS %d, must be > 0", fps);
        return E_INVALIDARG;
    }

    m_fps = fps;

    // Try high-resolution timer first (Windows 10 1803+).
    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002
    m_timer = CreateWaitableTimerExW(
        NULL,   // no security attributes
        NULL,   // unnamed
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );

    if (m_timer) {
        LOG_INFO("FrameTimer::Init — created high-resolution timer at %d FPS", fps);
    } else {
        // Fallback to standard waitable timer
        m_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        if (!m_timer) {
            DWORD err = GetLastError();
            LOG_ERROR("FrameTimer::Init — CreateWaitableTimerW failed, error=%u", err);
            return HRESULT_FROM_WIN32(err);
        }
        LOG_WARN("FrameTimer::Init — high-res timer unavailable, using standard timer at %d FPS", fps);
    }

    return S_OK;
}

// ---------------------------------------------------------------------------
// Start — arm the timer with the configured FPS period.
//
// Due time is set to one period in the future (negative = relative).
// The lPeriod parameter causes the timer to re-arm automatically.
// ---------------------------------------------------------------------------
HRESULT FrameTimer::Start() {
    if (!m_timer) {
        LOG_ERROR("FrameTimer::Start — timer not initialized");
        return E_UNEXPECTED;
    }

    // Calculate the period in milliseconds.
    // Integer division is fine — at 24 FPS we get 41ms (actual: 41.67ms).
    // The slight drift is negligible for wallpaper rendering.
    LONG periodMs = 1000 / m_fps;
    if (periodMs <= 0) periodMs = 1;

    // Due time: negative value = relative time in 100-nanosecond intervals.
    // We set the first tick to fire one period from now.
    m_dueTime.QuadPart = -static_cast<LONGLONG>(periodMs) * 10000LL;

    BOOL ok = SetWaitableTimer(
        m_timer,
        &m_dueTime,
        periodMs,    // auto-rearm period in ms
        NULL,        // no completion routine
        NULL,        // no completion routine arg
        FALSE        // don't wake from sleep
    );

    if (!ok) {
        DWORD err = GetLastError();
        LOG_ERROR("FrameTimer::Start — SetWaitableTimer failed, error=%u", err);
        return HRESULT_FROM_WIN32(err);
    }

    LOG_INFO("FrameTimer::Start — timer armed, period=%ld ms (%d FPS)", periodMs, m_fps);
    return S_OK;
}

// ---------------------------------------------------------------------------
// Stop — cancel the periodic timer without closing the handle.
// The timer can be restarted with Start().
// ---------------------------------------------------------------------------
void FrameTimer::Stop() {
    if (m_timer) {
        CancelWaitableTimer(m_timer);
        LOG_INFO("FrameTimer::Stop — timer cancelled");
    }
}

// ---------------------------------------------------------------------------
// ChangeFPS — update the target FPS and restart the timer.
// ---------------------------------------------------------------------------
HRESULT FrameTimer::ChangeFPS(int newFps) {
    if (newFps <= 0) {
        LOG_ERROR("FrameTimer::ChangeFPS — invalid FPS %d", newFps);
        return E_INVALIDARG;
    }

    LOG_INFO("FrameTimer::ChangeFPS — changing from %d to %d FPS", m_fps, newFps);
    Stop();
    m_fps = newFps;
    return Start();
}

// ---------------------------------------------------------------------------
// Shutdown — close the timer handle and reset state.
// ---------------------------------------------------------------------------
void FrameTimer::Shutdown() {
    if (m_timer) {
        CancelWaitableTimer(m_timer);
        CloseHandle(m_timer);
        m_timer = NULL;
        LOG_INFO("FrameTimer::Shutdown — timer destroyed");
    }
}

} // namespace lw
