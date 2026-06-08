#pragma once
#include <windows.h>
#include "utils.h"

namespace lw {

class Telemetry {
public:
    static Telemetry& Instance() {
        static Telemetry s_instance;
        return s_instance;
    }

    void StartFrame() {
        QueryPerformanceCounter(&m_frameStart);
    }

    void StartPresent() {
        QueryPerformanceCounter(&m_presentStart);
    }

    void EndPresent() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        
        double presentMs = CalcDeltaMs(m_presentStart, now);
        double totalMs = CalcDeltaMs(m_frameStart, now);
        
        m_totalPresentTimeMs += presentMs;
        m_totalFrameTimeMs += totalMs;
        m_frameCount++;
        
        if (m_frameCount >= 300) { // Log stats every 300 frames (~10 seconds at 30 FPS)
            LogStats();
        }
    }

    void RecordDroppedFrame() {
        m_droppedFrames++;
    }

    void Reset() {
        m_totalFrameTimeMs = 0.0;
        m_totalPresentTimeMs = 0.0;
        m_frameCount = 0;
        m_droppedFrames = 0;
    }

private:
    Telemetry() {
        QueryPerformanceFrequency(&m_frequency);
    }

    double CalcDeltaMs(LARGE_INTEGER start, LARGE_INTEGER end) const {
        return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / m_frequency.QuadPart;
    }

    void LogStats() {
        double avgFrameTime = m_frameCount > 0 ? (m_totalFrameTimeMs / m_frameCount) : 0.0;
        double avgPresent = m_frameCount > 0 ? (m_totalPresentTimeMs / m_frameCount) : 0.0;
        LOG_INFO("[Telemetry] Frames: %llu | Avg Frame Time: %.2f ms | Avg Present Latency: %.2f ms | Dropped Frames: %llu",
                 m_frameCount, avgFrameTime, avgPresent, m_droppedFrames);
        Reset();
    }

    LARGE_INTEGER m_frequency = {};
    LARGE_INTEGER m_frameStart = {};
    LARGE_INTEGER m_presentStart = {};
    
    double m_totalFrameTimeMs = 0.0;
    double m_totalPresentTimeMs = 0.0;
    UINT64 m_frameCount = 0;
    UINT64 m_droppedFrames = 0;
};

} // namespace lw
