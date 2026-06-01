#include "timer.h"
#include <timeapi.h>

Timer::Timer() {
    QueryPerformanceFrequency(&m_frequency);
    // Request 1ms scheduler precision from OS
    timeBeginPeriod(1);
    Reset();
}

Timer::~Timer() {
    timeEndPeriod(1);
}

void Timer::Reset() {
    QueryPerformanceCounter(&m_startTime);
}

double Timer::GetElapsedMilliseconds() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - m_startTime.QuadPart) * 1000.0 / m_frequency.QuadPart;
}

void Timer::PreciseSleep(double milliseconds) {
    if (milliseconds <= 0.0) return;

    // Cache the high-resolution timer handle per-thread to avoid recreation overhead.
    // The struct destructor will automatically close the handle when the thread exits.
    struct ThreadTimer {
        HANDLE hTimer = nullptr;
        ThreadTimer() {
            hTimer = CreateWaitableTimerExW(
                NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS
            );
        }
        ~ThreadTimer() {
            if (hTimer) {
                CloseHandle(hTimer);
            }
        }
    };

    thread_local ThreadTimer threadTimer;

    if (threadTimer.hTimer) {
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = static_cast<LONGLONG>(-milliseconds * 10000.0); // 100ns intervals
        if (SetWaitableTimer(threadTimer.hTimer, &dueTime, 0, NULL, NULL, 0)) {
            WaitForSingleObject(threadTimer.hTimer, INFINITE);
            return;
        }
    }

    // Fallback if high-res timer is not available (e.g. older Windows versions)
    if (milliseconds >= 1.0) {
        Sleep(static_cast<DWORD>(milliseconds));
    } else {
        Sleep(0); // Yield thread
    }
}
