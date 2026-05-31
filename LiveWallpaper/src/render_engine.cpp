#include "render_engine.h"
#include "utils.h"
#include "timer.h"
#include <shellapi.h>

// Define missing Windows 8+ shell notification state enums for older MinGW toolchains
#ifndef QUNS_RUNNING_D3D_FULL_SCREEN
#define QUNS_RUNNING_D3D_FULL_SCREEN static_cast<QUERY_USER_NOTIFICATION_STATE>(3)
#endif
#ifndef QUNS_APP
#define QUNS_APP static_cast<QUERY_USER_NOTIFICATION_STATE>(7)
#endif
#ifndef QUNS_RUNNING_PLAY_TO
#define QUNS_RUNNING_PLAY_TO static_cast<QUERY_USER_NOTIFICATION_STATE>(8)
#endif

RenderEngine::RenderEngine() {
}

RenderEngine::~RenderEngine() {
    Stop();
}

bool RenderEngine::Start(HWND hWnd, const std::wstring& videoPath) {
    if (m_runThread.load()) {
        LOG_WARN("RenderEngine is already running.");
        return false;
    }

    m_hWnd = hWnd;
    m_videoPath = videoPath;
    m_runThread.store(true);

    m_renderThread = std::thread(&RenderEngine::ThreadProc, this);
    LOG_INFO("RenderEngine thread successfully started.");
    return true;
}

void RenderEngine::Stop() {
    if (!m_runThread.load()) {
        return;
    }

    LOG_INFO("Stopping RenderEngine thread...");
    m_runThread.store(false);

    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }
    LOG_INFO("RenderEngine thread stopped.");
}

void RenderEngine::RequestResize(int width, int height) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_newWidth = width;
    m_newHeight = height;
    m_resizeRequested = true;
}

void RenderEngine::RequestRecreate(HWND newHWnd) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_newHWnd = newHWnd;
    m_recreateRequested = true;
    m_screenCleared = false;
}

void RenderEngine::RequestChangeVideo(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_newVideoPath = path;
    m_changeVideoRequested = true;
    m_screenCleared = false;
}

void RenderEngine::SetPaused(bool paused) {
    m_isPaused.store(paused);
}

void RenderEngine::ThreadProc() {
    LOG_INFO("RenderEngine thread procedure started.");
    m_screenCleared = false;

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in RenderEngine thread. HRESULT: 0x%08X", hrCOM);
    }

    m_renderer = std::make_unique<Renderer>();
    m_decoder = std::make_unique<VideoDecoder>();

    if (m_hWnd) {
        if (m_renderer->Initialize(m_hWnd)) {
            if (m_decoder->Initialize(m_renderer->GetDevice().Get())) {
                m_decoder->LoadVideo(m_videoPath);
            }
        }
    }

    while (m_runThread.load()) {
        // 1. Handle HWND Recreation (e.g. Explorer recovery)
        bool recreateNeeded = false;
        HWND targetHWnd = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_recreateRequested) {
                targetHWnd = m_newHWnd;
                m_hWnd = targetHWnd;
                m_recreateRequested = false;
                recreateNeeded = true;
            }
        }

        if (recreateNeeded) {
            LOG_INFO("RenderEngine: Re-initializing renderer and decoder on new handle.");
            m_screenCleared = false;
            m_decoder->Shutdown();
            m_renderer->Shutdown();
            if (targetHWnd) {
                if (m_renderer->Initialize(targetHWnd)) {
                    if (m_decoder->Initialize(m_renderer->GetDevice().Get())) {
                        m_decoder->LoadVideo(m_videoPath);
                    }
                }
            }
        }

        // 1.5 Handle Video Change
        bool changeVideoNeeded = false;
        std::wstring newPath;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_changeVideoRequested) {
                newPath = m_newVideoPath;
                m_videoPath = newPath;
                m_changeVideoRequested = false;
                changeVideoNeeded = true;
            }
        }

        if (changeVideoNeeded) {
            LOG_INFO_W(L"RenderEngine: Changing video to: %ls", newPath.c_str());
            m_screenCleared = false;
            m_decoder->LoadVideo(newPath);
        }

        // 2. Handle Resizing
        bool resizeNeeded = false;
        int w = 0, h = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_resizeRequested) {
                w = m_newWidth;
                h = m_newHeight;
                m_resizeRequested = false;
                resizeNeeded = true;
            }
        }

        if (resizeNeeded && w > 0 && h > 0) {
            m_renderer->Resize(w, h);
        }

        // 3. Pause & Obscuration Checks for Power & GPU Saving
        bool isPaused = m_isPaused.load() || IsObscured();
        if (m_decoder) {
            m_decoder->SetPaused(isPaused || m_videoPath.empty());
        }

        if (m_videoPath.empty()) {
            if (m_renderer->GetDevice() && m_renderer->GetContext()) {
                if (!m_screenCleared) {
                    LOG_INFO("RenderEngine: Video path is empty. Clearing screen to solid black.");
                    float blackColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    for (int i = 0; i < 2; ++i) {
                        m_renderer->Clear(blackColor);
                        m_renderer->Present();
                    }
                    m_screenCleared = true;
                }
            }
            Timer::PreciseSleep(100.0);
            continue;
        }

        if (isPaused) {
            // When obscured/paused, sleep longer (100ms) to reduce CPU/GPU usage to zero
            Timer::PreciseSleep(100.0);
            continue;
        }

        // 4. Update and Render Frame
        if (m_renderer->GetDevice() && m_renderer->GetContext()) {
            m_decoder->UpdateFrame(m_renderer->GetContext().Get());

            HRESULT hrPresent = S_OK;
            if (m_decoder->IsVideoLoaded()) {
                hrPresent = m_renderer->RenderVideoFrame(
                    m_decoder->GetShaderResourceView(),
                    m_decoder->GetVideoWidth(),
                    m_decoder->GetVideoHeight()
                );
            } else {
                hrPresent = m_renderer->RenderTestFrame();
            }

            // Coordinated Device Loss Recovery
            if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET) {
                LOG_WARN("RenderEngine: D3D11 device loss detected (0x%08X). Triggering coordinated recovery...", hrPresent);
                m_decoder->Shutdown();
                m_renderer->Shutdown();
                
                // Sleep briefly to prevent tight looping in case of persistent device loss
                Timer::PreciseSleep(100.0);

                if (m_hWnd) {
                    if (m_renderer->Initialize(m_hWnd)) {
                        if (m_decoder->Initialize(m_renderer->GetDevice().Get())) {
                            m_decoder->LoadVideo(m_videoPath);
                        }
                    }
                }
            }
        } else {
            // Fallback sleep if renderer is not initialized
            Timer::PreciseSleep(16.6);
        }
    }

    LOG_INFO("RenderEngine: Releasing resources on thread exit.");
    m_decoder->Shutdown();
    m_renderer->Shutdown();

    m_decoder.reset();
    m_renderer.reset();

    if (SUCCEEDED(hrCOM)) {
        CoUninitialize();
    }
    LOG_INFO("RenderEngine thread procedure exiting.");
}

bool RenderEngine::IsObscured() {
    DWORD now = GetTickCount();
    if (now - m_lastObscurationCheck < 1000) {
        return m_isObscured; // Cache the value for 1 second to avoid heavy polling
    }
    m_lastObscurationCheck = now;

    // 1. Check user notification state (e.g. fullscreen exclusive apps like games, screen saver)
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        if (state == QUNS_NOT_PRESENT ||
            state == QUNS_BUSY ||
            state == QUNS_RUNNING_D3D_FULL_SCREEN ||
            state == QUNS_PRESENTATION_MODE ||
            state == QUNS_APP ||
            state == QUNS_RUNNING_PLAY_TO) {
            if (!m_isObscured) {
                LOG_INFO("System is obscured/busy (SHQueryUserNotificationState = %d). Pausing rendering.", state);
            }
            m_isObscured = true;
            return true;
        }
    }

    // 2. Fallback check for foreground window taking up the entire screen (fullscreen borderless apps)
    HWND hwndForeground = GetForegroundWindow();
    if (hwndForeground && hwndForeground != m_hWnd && hwndForeground != GetShellWindow() && hwndForeground != GetDesktopWindow()) {
        char className[256];
        GetClassNameA(hwndForeground, className, sizeof(className));
        
        // Ignore desktop sibling windows
        if (strcmp(className, "WorkerW") != 0 && strcmp(className, "Progman") != 0) {
            RECT rcForeground;
            if (GetWindowRect(hwndForeground, &rcForeground)) {
                HMONITOR hMonitor = MonitorFromWindow(hwndForeground, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(MONITORINFO) };
                if (GetMonitorInfoW(hMonitor, &mi)) {
                    RECT rcMonitor = mi.rcMonitor;
                    if (rcForeground.left <= rcMonitor.left && rcForeground.top <= rcMonitor.top &&
                        rcForeground.right >= rcMonitor.right && rcForeground.bottom >= rcMonitor.bottom) {
                        if (!m_isObscured) {
                            LOG_INFO("Fullscreen window detected (%s). Pausing rendering.", className);
                        }
                        m_isObscured = true;
                        return true;
                    }
                }
            }
        }
    }

    if (m_isObscured) {
        LOG_INFO("System is no longer obscured. Resuming rendering.");
    }
    m_isObscured = false;
    return false;
}
