#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class SwapChainManager {
public:
    SwapChainManager();
    ~SwapChainManager();

    bool Initialize(ID3D11Device* device, HWND hWnd, int width, int height);
    void Shutdown();

    bool Resize(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height);
    HRESULT Present(int fpsLimit);

    IDXGISwapChain1* GetSwapChain() const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* GetRenderTargetView() const { return m_renderTargetView.Get(); }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    HWND GetHWND() const { return m_hWnd; }

private:
    bool CreateSwapChain(ID3D11Device* device);
    bool CreateRenderTargetView(ID3D11Device* device);

    HWND m_hWnd = nullptr;
    int m_width = 0;
    int m_height = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
};
