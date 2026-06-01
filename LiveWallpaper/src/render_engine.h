#pragma once
#include <windows.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <memory>
#include "renderer.h"
#include "video_decoder.h"

class RenderEngine {
public:
    RenderEngine();
    ~RenderEngine();

    bool Start(HWND hWnd, const std::wstring& videoPath);
    void Stop();

    void RequestResize(int width, int height);
    void RequestRecreate(HWND newHWnd);
    
    void RequestChangeVideo(const std::wstring& path);
    void SetPaused(bool paused);
    void SetFPSLimit(int fpsLimit) { m_fpsLimit.store(fpsLimit); }

    bool IsRunning() const { return m_runThread.load(); }

private:
    void ThreadProc();
    
    // Checks if the window is obscured by a full-screen app or not visible
    bool IsObscured();

    HWND m_hWnd = nullptr;
    std::wstring m_videoPath;
    
    std::thread m_renderThread;
    std::atomic<bool> m_runThread{ false };

    // Thread synchronization for runtime commands
    std::mutex m_stateMutex;
    
    // Resize requests
    bool m_resizeRequested = false;
    int m_newWidth = 0;
    int m_newHeight = 0;

    // Window recreation requests (e.g. Explorer recovery)
    bool m_recreateRequested = false;
    HWND m_newHWnd = nullptr;

    // Video change requests
    bool m_changeVideoRequested = false;
    std::wstring m_newVideoPath;

    // Pause state
    std::atomic<bool> m_isPaused{ false };
    std::atomic<int> m_fpsLimit{ 0 }; // 0 = Unlimited / VSync

    // Instances managed entirely on the render thread
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<VideoDecoder> m_decoder;

    // Track obscuration state to avoid polling API too frequently
    DWORD m_lastObscurationCheck = 0;
    bool m_isObscured = false;
    bool m_screenCleared = false;
};
