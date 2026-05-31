#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool Initialize(ID3D11Device* pDevice);
    void Shutdown();

    bool LoadVideo(const std::wstring& filePath);
    void CloseVideo();

    // Called on the render thread to upload/copy the latest decoded frame to the local texture
    bool UpdateFrame(ID3D11DeviceContext* pContext);

    // Dynamic Pausing for power conservation
    void SetPaused(bool paused) { m_isPaused.store(paused); }

    // Accessors
    ID3D11ShaderResourceView* GetShaderResourceView() const { return m_pVideoSRV.Get(); }
    bool IsVideoLoaded() const { return m_videoLoaded; }
    int GetVideoWidth() const { return m_videoWidth; }
    int GetVideoHeight() const { return m_videoHeight; }

private:
    void DecodingThreadProc();

    ID3D11Device* m_pDevice = nullptr; // Weak ref to Renderer's device
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_pDeviceManager;
    UINT m_deviceResetToken = 0;

    Microsoft::WRL::ComPtr<IMFSourceReader> m_pSourceReader;
    
    std::wstring m_filePath;
    std::atomic<bool> m_videoLoaded{ false };
    
    int m_videoWidth = 0;
    int m_videoHeight = 0;

    // Local GPU texture for rendering
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pVideoTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pVideoSRV;

    // Threading & Synchronization
    std::thread m_decodeThread;
    std::atomic<bool> m_runThread{ false };
    std::atomic<bool> m_isPaused{ false };
    std::mutex m_sampleMutex;
    
    // Shared sample buffer
    Microsoft::WRL::ComPtr<IMFSample> m_pLatestSample;
    bool m_bNewSampleAvailable = false;
};
