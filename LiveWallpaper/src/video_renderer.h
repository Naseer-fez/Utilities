#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "device_manager.h"
#include "swap_chain_manager.h"

class VideoRenderer {
public:
    VideoRenderer();
    ~VideoRenderer();

    bool Initialize(DeviceManager* pDeviceManager, SwapChainManager* pSwapChainManager);
    void Shutdown();

    HRESULT RenderVideoFrame(
        ID3D11ShaderResourceView* pVideoSRV_Y, 
        ID3D11ShaderResourceView* pVideoSRV_UV, 
        int textureWidth, 
        int textureHeight, 
        int videoWidth, 
        int videoHeight
    );

    HRESULT RenderTestFrame();

private:
    bool InitializeShaders();
    void UpdateAspectRatioCB(int textureWidth, int textureHeight, int videoWidth, int videoHeight);

    DeviceManager* m_pDeviceManager = nullptr;
    SwapChainManager* m_pSwapChainManager = nullptr;
    float m_hue = 0.0f;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
};
