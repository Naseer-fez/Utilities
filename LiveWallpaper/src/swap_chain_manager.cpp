#include "swap_chain_manager.h"
#include "utils.h"

SwapChainManager::SwapChainManager() {}

SwapChainManager::~SwapChainManager() {
    Shutdown();
}

bool SwapChainManager::Initialize(ID3D11Device* device, HWND hWnd, int width, int height) {
    m_hWnd = hWnd;
    m_width = width;
    m_height = height;

    LOG_INFO("Initializing SwapChainManager...");

    if (!CreateSwapChain(device)) {
        LOG_ERROR("SwapChainManager: Failed to create Swap Chain.");
        Shutdown();
        return false;
    }

    if (!CreateRenderTargetView(device)) {
        LOG_ERROR("SwapChainManager: Failed to create Render Target View.");
        Shutdown();
        return false;
    }

    LOG_INFO("SwapChainManager successfully initialized (%d x %d).", m_width, m_height);
    return true;
}

void SwapChainManager::Shutdown() {
    m_renderTargetView.Reset();
    m_swapChain.Reset();
    m_hWnd = nullptr;
    m_width = 0;
    m_height = 0;
    LOG_INFO("SwapChainManager shut down.");
}

bool SwapChainManager::CreateSwapChain(ID3D11Device* device) {
    if (!device) return false;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
    scd.Width = m_width;
    scd.Height = m_height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsd = { 0 };
    fsd.RefreshRate.Numerator = 60;
    fsd.RefreshRate.Denominator = 1;
    fsd.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    fsd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    fsd.Windowed = TRUE;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        device,
        m_hWnd,
        &scd,
        &fsd,
        NULL,
        &m_swapChain
    );

    if (FAILED(hr)) {
        LOG_ERROR("SwapChainManager: CreateSwapChainForHwnd failed. HRESULT = 0x%08X", hr);
        return false;
    }

    return true;
}

bool SwapChainManager::CreateRenderTargetView(ID3D11Device* device) {
    if (!device || !m_swapChain) return false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = device->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);
    if (FAILED(hr)) return false;

    return true;
}

bool SwapChainManager::Resize(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width == m_width && height == m_height) return true;

    LOG_INFO("Resizing SwapChain from %dx%d to %dx%d...", m_width, m_height, width, height);
    m_width = width;
    m_height = height;

    if (!m_swapChain) return false;

    // Release RTV before resizing buffers
    if (context) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->ClearState();
        context->Flush();
    }
    m_renderTargetView.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0
    );

    if (FAILED(hr)) {
        LOG_ERROR("SwapChainManager: ResizeBuffers failed. HRESULT = 0x%08X", hr);
        return false;
    }

    return CreateRenderTargetView(device);
}

HRESULT SwapChainManager::Present(int fpsLimit) {
    if (!m_swapChain) return E_POINTER;
    UINT syncInterval = (fpsLimit == 0) ? 1 : 0;
    return m_swapChain->Present(syncInterval, 0);
}
