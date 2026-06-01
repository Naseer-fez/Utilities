#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include "spsc_ring_buffer.h"

struct PathMessage {
    std::wstring path;
    void Release() { delete this; }
};

class SynchronizationManager {
public:
    SynchronizationManager();
    ~SynchronizationManager();

    // Thread running state
    void SetRunning(bool running);
    bool IsRunning() const;

    // Paused state
    void SetPaused(bool paused);
    bool IsPaused() const;

    // FPS Limit
    void SetFPSLimit(int fps);
    int GetFPSLimit() const;

    // Resize requests
    void RequestResize(int width, int height);
    bool CheckResize(int& outWidth, int& outHeight);

    // Recreate requests
    void RequestRecreate(HWND hWnd);
    bool CheckRecreate(HWND& outHWnd);

    // Video/Shader path requests via SPSC ring buffer
    void RequestChangeVideo(const std::wstring& path);
    bool PopVideoChange(std::wstring& outPath);

    // Clear everything
    void Clear();

private:
    std::atomic<bool> m_runThread{ false };
    std::atomic<bool> m_isPaused{ false };
    std::atomic<int> m_fpsLimit{ 60 };

    std::atomic<bool> m_resizeRequested{ false };
    std::atomic<int> m_newWidth{ 0 };
    std::atomic<int> m_newHeight{ 0 };

    std::atomic<bool> m_recreateRequested{ false };
    std::atomic<HWND> m_newHWnd{ nullptr };

    // SPSC Lock-free Queue for path updates
    SPSCRingBuffer<PathMessage*, 16> m_pathQueue;
};
