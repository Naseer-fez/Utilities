#pragma once
#include <windows.h>

class Timer {
public:
    Timer();
    ~Timer();

    // Resets the timer
    void Reset();

    // Returns elapsed time in milliseconds
    double GetElapsedMilliseconds() const;

    // High precision sleep using a combination of Sleep(0/1) and spin-locking
    static void PreciseSleep(double milliseconds);

private:
    LARGE_INTEGER m_frequency;
    LARGE_INTEGER m_startTime;
};
