#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hWnd);
    void Shutdown();

    // Renders a test frame (color cycling or gradient) and presents it
    HRESULT RenderTestFrame();
    
    // Renders a video frame texture using a full-screen quad (NV12)
    HRESULT RenderVideoFrame(ID3D11ShaderResourceView* pVideoSRV_Y, ID3D11ShaderResourceView* pVideoSRV_UV, int videoWidth, int videoHeight);

    // Resizes the swap chain buffers
    bool Resize(int width, int height);

    // Clears the backbuffer with a specific color
    void Clear(const float color[4]);

    // Swaps buffers
    HRESULT Present();

    // Accessors
    Microsoft::WRL::ComPtr<ID3D11Device> GetDevice() const { return m_d3dDevice; }
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> GetContext() const { return m_d3dContext; }

private:
    bool CreateDeviceAndSwapChain();
    bool CreateRenderTargetView();
    bool InitializeShaders();
    void UpdateAspectRatioCB(int videoWidth, int videoHeight);
    void ReleaseResources();

    HWND m_hWnd = nullptr;
    int m_width = 0;
    int m_height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    // Shader-based quad rendering resources
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;

    float m_hue = 0.0f; // For test pattern color cycling
};
