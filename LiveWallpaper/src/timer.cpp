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

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER start, current;
    QueryPerformanceCounter(&start);

    double targetTicks = milliseconds * freq.QuadPart / 1000.0;

    // Yield CPU for the bulk of the wait if it's long enough
    if (milliseconds > 2.0) {
        Sleep(static_cast<DWORD>(milliseconds - 1.0));
    }

    // Spin-lock the rest of the way for microsecond accuracy
    do {
        QueryPerformanceCounter(&current);
    } while (static_cast<double>(current.QuadPart - start.QuadPart) < targetTicks);
}
