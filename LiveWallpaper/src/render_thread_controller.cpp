#include "render_thread_controller.h"
#include "utils.h"
#include "timer.h"
#include <algorithm>

RenderThreadController::RenderThreadController() {
    m_playlistManager = std::make_unique<PlaylistManager>();
    m_syncManager = std::make_unique<SynchronizationManager>();
    m_stateMachine = std::make_unique<RenderStateMachine>();
}

RenderThreadController::~RenderThreadController() {
    Stop();
}

bool RenderThreadController::Start(HWND hWnd, const std::wstring& videoPath) {
    if (m_syncManager->IsRunning()) {
        LOG_WARN("RenderThreadController is already running.");
        return false;
    }

    m_hWnd = hWnd;
    m_videoPath = videoPath;
    m_playlistManager->SetPlaylist({ videoPath });
    m_syncManager->SetRunning(true);

    m_renderThread = std::thread(&RenderThreadController::ThreadProc, this);
    LOG_INFO("RenderThreadController thread successfully started.");
    return true;
}

void RenderThreadController::Stop() {
    if (!m_syncManager->IsRunning()) return;

    LOG_INFO("Stopping RenderThreadController thread...");
    m_syncManager->SetRunning(false);

    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }
    LOG_INFO("RenderThreadController thread stopped.");
}

void RenderThreadController::RequestResize(int width, int height) {
    m_syncManager->RequestResize(width, height);
}

void RenderThreadController::RequestRecreate(HWND newHWnd) {
    m_syncManager->RequestRecreate(newHWnd);
}

void RenderThreadController::RequestChangeVideo(const std::wstring& path) {
    m_syncManager->RequestChangeVideo(path);
}

void RenderThreadController::SetPaused(bool paused) {
    m_syncManager->SetPaused(paused);
}

void RenderThreadController::SetFPSLimit(int fpsLimit) {
    m_syncManager->SetFPSLimit(fpsLimit);
}

void RenderThreadController::SetPlaylist(const std::vector<std::wstring>& playlist, size_t startIndex) {
    m_playlistManager->SetPlaylist(playlist, startIndex);
    if (!playlist.empty()) {
        m_syncManager->RequestChangeVideo(playlist[startIndex]);
    }
}

void RenderThreadController::SetRotationInterval(int minutes) {
    m_playlistManager->SetRotationInterval(minutes);
}

bool RenderThreadController::IsShaderFile(const std::wstring& path) {
    if (path.length() < 5) return false;
    std::wstring ext = path.substr(path.length() - 5);
    for (auto& c : ext) c = std::tolower(c);
    return ext == L".hlsl";
}

void RenderThreadController::ThreadProc() {
    LOG_INFO("RenderThreadController thread procedure started.");
    m_screenCleared = false;

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in RenderThreadController thread. HRESULT: 0x%08X", hrCOM);
    }

    m_deviceManager = std::make_unique<DeviceManager>();
    m_swapChainManager = std::make_unique<SwapChainManager>();
    m_videoRenderer = std::make_unique<VideoRenderer>();
    m_decoder = std::make_unique<VideoDecoder>();
    m_shaderBridge = std::make_unique<FFIShaderBridge>();

    // Initial load
    if (m_hWnd && !m_videoPath.empty()) {
        RECT rect;
        GetClientRect(m_hWnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        if (w == 0) w = 800;
        if (h == 0) h = 600;

        if (m_deviceManager->Initialize()) {
            if (m_swapChainManager->Initialize(m_deviceManager->GetDevice(), m_hWnd, w, h)) {
                m_videoRenderer->Initialize(m_deviceManager.get(), m_swapChainManager.get());
                
                if (IsShaderFile(m_videoPath)) {
                    if (m_shaderBridge->Load()) {
                        HRESULT hr = m_shaderBridge->InitShaderHost(
                            m_deviceManager->GetDevice(),
                            m_deviceManager->GetContext(),
                            m_videoPath,
                            &m_shaderHost
                        );
                        if (FAILED(hr)) LOG_ERROR("Failed to initialize Rust Shader Host. HR: 0x%08X", hr);
                    }
                } else {
                    if (m_decoder->Initialize(m_deviceManager->GetDevice())) {
                        m_decoder->LoadVideo(m_videoPath);
                    }
                }
            }
        }
    }

    Timer frameRateTimer;
    Timer deltaTimer; // Used for measuring delta time for playlist updates
    deltaTimer.Reset();

    while (m_syncManager->IsRunning()) {
        frameRateTimer.Reset();
        double deltaMs = deltaTimer.GetElapsedMilliseconds();
        deltaTimer.Reset();

        // 1. Handle HWND Recreation (Phase 7 - Explorer Recovery)
        HWND targetHWnd = nullptr;
        bool recreateNeeded = m_syncManager->CheckRecreate(targetHWnd);
        if (recreateNeeded) {
            m_hWnd = targetHWnd;
            LOG_INFO("RenderThreadController: Re-initializing device manager on new handle.");
            m_screenCleared = false;
            
            // Teardown in reverse order of creation
            if (m_shaderHost) {
                m_shaderBridge->ShutdownShaderHost(m_shaderHost);
                m_shaderHost = nullptr;
            }
            m_decoder->Shutdown();
            m_videoRenderer->Shutdown();
            m_swapChainManager->Shutdown();
            m_deviceManager->Shutdown();

            if (targetHWnd && !m_videoPath.empty()) {
                RECT rect;
                GetClientRect(m_hWnd, &rect);
                int w = rect.right - rect.left;
                int h = rect.bottom - rect.top;
                if (w == 0) w = 800;
                if (h == 0) h = 600;

                if (m_deviceManager->Initialize()) {
                    if (m_swapChainManager->Initialize(m_deviceManager->GetDevice(), targetHWnd, w, h)) {
                        m_videoRenderer->Initialize(m_deviceManager.get(), m_swapChainManager.get());
                        if (IsShaderFile(m_videoPath)) {
                            if (m_shaderBridge->Load()) {
                                m_shaderBridge->InitShaderHost(m_deviceManager->GetDevice(), m_deviceManager->GetContext(), m_videoPath, &m_shaderHost);
                            }
                        } else {
                            if (m_decoder->Initialize(m_deviceManager->GetDevice())) {
                                m_decoder->LoadVideo(m_videoPath);
                            }
                        }
                    }
                }
            }
        }

        // 2. Handle Video/Shader Change
        std::wstring poppedPath;
        bool changeVideoNeeded = false;
        while (m_syncManager->PopVideoChange(poppedPath)) {
            changeVideoNeeded = true;
        }
        if (changeVideoNeeded) {
            m_videoPath = poppedPath;
            m_playlistManager->SetPlaylist({ m_videoPath });
            LOG_INFO_W(L"RenderThreadController: Changing wallpaper to: %ls", m_videoPath.c_str());
            m_screenCleared = false;

            if (m_shaderHost) {
                m_shaderBridge->ShutdownShaderHost(m_shaderHost);
                m_shaderHost = nullptr;
            }
            m_decoder->Shutdown();
            
            if (m_deviceManager->GetDevice()) {
                if (!m_videoPath.empty()) {
                    if (IsShaderFile(m_videoPath)) {
                        if (m_shaderBridge->Load()) {
                            m_shaderBridge->InitShaderHost(m_deviceManager->GetDevice(), m_deviceManager->GetContext(), m_videoPath, &m_shaderHost);
                        }
                    } else {
                        if (m_decoder->Initialize(m_deviceManager->GetDevice())) {
                            m_decoder->LoadVideo(m_videoPath);
                        }
                    }
                }
            }
        }

        // 3. Handle Playlist Rotation
        if (!m_syncManager->IsPaused() && m_playlistManager->Update(deltaMs)) {
            std::wstring nextPath = m_playlistManager->GetCurrentTrack();
            if (nextPath != m_videoPath) {
                m_videoPath = nextPath;
                LOG_INFO_W(L"RenderThreadController: Rotating playlist to: %ls", m_videoPath.c_str());
                m_screenCleared = false;

                if (m_shaderHost) {
                    m_shaderBridge->ShutdownShaderHost(m_shaderHost);
                    m_shaderHost = nullptr;
                }
                m_decoder->Shutdown();

                if (m_deviceManager->GetDevice() && !m_videoPath.empty()) {
                    if (IsShaderFile(m_videoPath)) {
                        if (m_shaderBridge->Load()) {
                            m_shaderBridge->InitShaderHost(m_deviceManager->GetDevice(), m_deviceManager->GetContext(), m_videoPath, &m_shaderHost);
                        }
                    } else {
                        if (m_decoder->Initialize(m_deviceManager->GetDevice())) {
                            m_decoder->LoadVideo(m_videoPath);
                        }
                    }
                }
            }
        }

        // 4. Handle Resizing
        int newWidth = 0, newHeight = 0;
        bool resizeNeeded = m_syncManager->CheckResize(newWidth, newHeight);
        if (resizeNeeded && m_deviceManager->GetDevice()) {
            m_swapChainManager->Resize(m_deviceManager->GetDevice(), m_deviceManager->GetContext(), newWidth, newHeight);
        }

        // 5. Pause Checks
        bool isPaused = m_syncManager->IsPaused();
        if (m_decoder && !m_shaderHost) {
            m_decoder->SetPaused(isPaused || m_videoPath.empty());
        }

        if (m_videoPath.empty() || isPaused) {
            if (m_videoPath.empty() && m_deviceManager->GetContext() && m_swapChainManager->GetSwapChain() && !m_screenCleared) {
                float black[4] = {0,0,0,1};
                m_deviceManager->GetContext()->ClearRenderTargetView(m_swapChainManager->GetRenderTargetView(), black);
                m_swapChainManager->Present(m_syncManager->GetFPSLimit());
                m_screenCleared = true;
            }
            Timer::PreciseSleep(100.0);
            continue;
        }

        // 6. Update and Render Frame
        bool deviceValid = m_deviceManager->GetDevice() != nullptr;
        HRESULT hrPresent = S_OK;
        bool frameUpdated = false;
        bool forceRedraw = recreateNeeded || changeVideoNeeded || resizeNeeded;

        // Update state machine state
        RenderState nextState = m_stateMachine->DetermineNextState(
            m_syncManager->IsRunning(),
            isPaused,
            m_hWnd != nullptr,
            IsShaderFile(m_videoPath),
            m_decoder->IsVideoLoaded(),
            !deviceValid
        );
        m_stateMachine->TransitionTo(nextState);

        if (deviceValid) {
            if (m_shaderHost) {
                POINT ptCursor = { 0 };
                GetCursorPos(&ptCursor);
                ScreenToClient(m_hWnd, &ptCursor);
                bool isMouseDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
                float audioData[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

                m_shaderBridge->RenderShaderFrame(
                    m_shaderHost, 
                    m_swapChainManager->GetRenderTargetView(),
                    m_swapChainManager->GetWidth(),
                    m_swapChainManager->GetHeight(),
                    (float)ptCursor.x, (float)ptCursor.y, isMouseDown, audioData, 4
                );
                
                hrPresent = m_swapChainManager->Present(m_syncManager->GetFPSLimit());
                frameUpdated = true;
            } else if (m_decoder->IsVideoLoaded()) {
                double waitTimeMs = 0.0;
                frameUpdated = m_decoder->UpdateFrame(m_deviceManager->GetContext(), waitTimeMs);

                if (frameUpdated || forceRedraw) {
                    m_videoRenderer->RenderVideoFrame(
                        m_decoder->GetSRV_Y(),
                        m_decoder->GetSRV_UV(),
                        m_decoder->GetTextureWidth(),
                        m_decoder->GetTextureHeight(),
                        m_decoder->GetVideoWidth(),
                        m_decoder->GetVideoHeight()
                    );
                    hrPresent = m_swapChainManager->Present(m_syncManager->GetFPSLimit());
                } else {
                    if (waitTimeMs > 0.0) {
                        Timer::PreciseSleep(waitTimeMs < 1.0 ? 1.0 : waitTimeMs);
                    }
                }
            }
        }

        // 7. Device Loss Recovery
        if (!deviceValid || FAILED(hrPresent)) {
            if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET) {
                LOG_WARN("RenderThreadController: Device loss detected. Triggering recovery...");
                m_syncManager->RequestRecreate(m_hWnd);
                Timer::PreciseSleep(500.0);
            }
        }

        // 8. Frame rate limiting
        int fps = m_syncManager->GetFPSLimit();
        if (fps > 0 && frameUpdated) {
            double targetFrameTimeMs = 1000.0 / fps;
            double elapsedMs = frameRateTimer.GetElapsedMilliseconds();
            if (elapsedMs < targetFrameTimeMs) {
                Timer::PreciseSleep(targetFrameTimeMs - elapsedMs);
            }
        }
    }

    LOG_INFO("RenderThreadController: Releasing resources on thread exit.");
    if (m_shaderHost) {
        m_shaderBridge->ShutdownShaderHost(m_shaderHost);
        m_shaderHost = nullptr;
    }
    m_decoder->Shutdown();
    m_videoRenderer->Shutdown();
    m_swapChainManager->Shutdown();
    m_deviceManager->Shutdown();

    m_shaderBridge->Unload();

    m_decoder.reset();
    m_videoRenderer.reset();
    m_swapChainManager.reset();
    m_deviceManager.reset();
    m_shaderBridge.reset();

    if (SUCCEEDED(hrCOM)) CoUninitialize();
    LOG_INFO("RenderThreadController thread procedure exiting.");
}
