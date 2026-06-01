#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <memory>
#include <vector>
#include "device_manager.h"
#include "swap_chain_manager.h"
#include "playlist_manager.h"
#include "synchronization_manager.h"
#include "render_state_machine.h"
#include "video_renderer.h"
#include "video_decoder.h"
#include "ffi_shader_bridge.h"

class RenderThreadController {
public:
    RenderThreadController();
    ~RenderThreadController();

    bool Start(HWND hWnd, const std::wstring& videoPath);
    void Stop();

    void RequestResize(int width, int height);
    void RequestRecreate(HWND newHWnd);
    void RequestChangeVideo(const std::wstring& path);

    void SetPaused(bool paused);
    void SetFPSLimit(int fpsLimit);

    // Playlist support
    void SetPlaylist(const std::vector<std::wstring>& playlist, size_t startIndex = 0);
    void SetRotationInterval(int minutes);

private:
    void ThreadProc();
    bool IsShaderFile(const std::wstring& path);

    HWND m_hWnd = nullptr;
    std::wstring m_videoPath;

    std::thread m_renderThread;

    // Component dependencies (split per architecture design)
    std::unique_ptr<DeviceManager> m_deviceManager;
    std::unique_ptr<SwapChainManager> m_swapChainManager;
    std::unique_ptr<PlaylistManager> m_playlistManager;
    std::unique_ptr<SynchronizationManager> m_syncManager;
    std::unique_ptr<RenderStateMachine> m_stateMachine;
    
    std::unique_ptr<VideoRenderer> m_videoRenderer;
    std::unique_ptr<VideoDecoder> m_decoder;
    std::unique_ptr<FFIShaderBridge> m_shaderBridge;
    
    void* m_shaderHost = nullptr; // Opaque Rust pointer
    bool m_screenCleared = false;
};
