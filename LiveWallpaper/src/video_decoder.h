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
#include <queue>
#include "timer.h"
#include "spsc_ring_buffer.h"

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool Initialize(ID3D11Device* pDevice);
    void Shutdown();

    bool LoadVideo(const std::wstring& filePath);
    void CloseVideo();

    // Called on the render thread to upload/copy the latest decoded frame to the local texture
    bool UpdateFrame(ID3D11DeviceContext* pContext, double& outWaitTimeMs);

    // Dynamic Pausing for power conservation
    void SetPaused(bool paused);

    // Accessors
    ID3D11ShaderResourceView* GetSRV_Y() const { return m_pActiveSRV_Y.Get(); }
    ID3D11ShaderResourceView* GetSRV_UV() const { return m_pActiveSRV_UV.Get(); }
    bool IsVideoLoaded() const { return m_videoLoaded; }
    int GetVideoWidth() const { return m_videoWidth; }
    int GetVideoHeight() const { return m_videoHeight; }
    int GetTextureWidth() const { return m_videoTextureWidth; }
    int GetTextureHeight() const { return m_videoTextureHeight; }

private:
    void DecodingThreadProc();
    bool ReallocateVideoTexture(int width, int height);

    ID3D11Device* m_pDevice = nullptr; // Weak ref to Renderer's device
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_pDeviceManager;
    UINT m_deviceResetToken = 0;

    Microsoft::WRL::ComPtr<IMFSourceReader> m_pSourceReader;
    
    std::wstring m_filePath;
    std::atomic<bool> m_videoLoaded{ false };
    
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    int m_videoTextureWidth = 0;
    int m_videoTextureHeight = 0;

    // Local GPU texture for rendering (software fallback)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pVideoTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pVideoSRV_Y;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pVideoSRV_UV;

    // Active SRVs (can point to either MF hardware texture or local fallback texture)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pActiveSRV_Y;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pActiveSRV_UV;

    // Threading & Synchronization
    std::thread m_decodeThread;
    std::atomic<bool> m_runThread{ false };
    std::atomic<bool> m_isPaused{ false };
    
    // Shared SPSC lock-free atomic sample queue (max capacity 16 is a power of 2)
    SPSCRingBuffer<IMFSample*, 16> m_sampleQueue;
    double m_playbackTimeMs = 0.0;
    double m_currentFrameTimestamp = -1.0;
    Timer m_playbackTimer;
};
