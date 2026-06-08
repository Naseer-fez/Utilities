// =============================================================================
// renderer.cpp — D3D11 device, swap chain, and frame presentation
// =============================================================================

#include "renderer.h"
#include <d3d10.h>  // For ID3D10Multithread interface

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace lw {

Renderer::~Renderer() {
    Shutdown();
}

HRESULT Renderer::FindAdapter(bool preferIntegrated, IDXGIAdapter1** adapter) {
    if (!adapter) return E_POINTER;
    *adapter = nullptr;

    if (!m_factory) {
        HR_CHECK(
            CreateDXGIFactory1(IID_PPV_ARGS(&m_factory)),
            "CreateDXGIFactory1 failed"
        );
    }

    ComPtr<IDXGIAdapter1> fallbackAdapter;
    ComPtr<IDXGIAdapter1> integratedAdapter;

    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> candidate;
        HRESULT hr = m_factory->EnumAdapters1(i, &candidate);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;

        DXGI_ADAPTER_DESC1 desc = {};
        candidate->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (!fallbackAdapter) {
            fallbackAdapter = candidate;
        }

        if (preferIntegrated && !integratedAdapter) {
            wchar_t descLower[128] = {};
            for (int j = 0; j < 127 && desc.Description[j]; ++j) {
                descLower[j] = towlower(desc.Description[j]);
            }
            if (wcsstr(descLower, L"intel") != nullptr) {
                integratedAdapter = candidate;
            }
        }
    }

    ComPtr<IDXGIAdapter1> selected = (preferIntegrated && integratedAdapter) ? integratedAdapter : fallbackAdapter;
    if (!selected) {
        LOG_ERROR("Renderer::FindAdapter — no suitable GPU adapter found");
        return E_FAIL;
    }

    *adapter = selected.Detach();
    return S_OK;
}

HRESULT Renderer::InitDevice(bool preferIntegrated) {
    ComPtr<IDXGIAdapter1> adapter;
    HR_CHECK(FindAdapter(preferIntegrated, &adapter), "FindAdapter failed");

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL actualLevel  = {};

    UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT
               | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HR_CHECK(
        D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            &featureLevel,
            1,
            D3D11_SDK_VERSION,
            &m_device,
            &actualLevel,
            &m_context
        ),
        "D3D11CreateDevice failed"
    );

    ComPtr<ID3D10Multithread> multithread;
    HRESULT hr = m_device.As(&multithread);
    if (SUCCEEDED(hr)) {
        multithread->SetMultithreadProtected(TRUE);
    }

    return S_OK;
}

HRESULT Renderer::CreateSwapChainForMonitor(HMONITOR hMonitor, HWND hwnd, UINT width, UINT height) {
    if (!m_device || !m_factory) {
        LOG_ERROR("Renderer::CreateSwapChainForMonitor — device or factory not initialized");
        return E_UNEXPECTED;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width       = width;
    desc.Height      = height;
    desc.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo      = FALSE;
    desc.SampleDesc  = { 1, 0 };
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling     = DXGI_SCALING_STRETCH;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags       = 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<IDXGISwapChain1> swapChain;
    HR_CHECK(
        m_factory->CreateSwapChainForHwnd(
            m_device.Get(),
            hwnd,
            &desc,
            nullptr,
            nullptr,
            &swapChain
        ),
        "CreateSwapChainForHwnd failed"
    );

    SwapChainOutput sco = { hMonitor, hwnd, swapChain, width, height, false };
    m_swapChains.push_back(sco);

    LOG_INFO("Renderer::CreateSwapChainForMonitor — SwapChain created for HWND 0x%p (%u x %u)", hwnd, width, height);
    return S_OK;
}

HRESULT Renderer::PresentFrame(ID3D11Texture2D* srcTexture, UINT srcSubresource) {
    if (!srcTexture || !m_context) return E_UNEXPECTED;

    std::lock_guard<std::mutex> lock(m_mutex);
    HRESULT lastHr = S_OK;

    for (auto& sc : m_swapChains) {
        if (sc.isPaused) {
            continue; // Skip paused display outputs completely
        }

        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = sc.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) continue;

        // Perform zero-copy GPU copy of decoded frame to screen backbuffer
        m_context->CopySubresourceRegion(
            backBuffer.Get(),
            0,
            0, 0, 0,
            srcTexture,
            srcSubresource,
            nullptr
        );

        hr = sc.swapChain->Present(0, 0);
        if (FAILED(hr)) {
            lastHr = hr;
        }
    }

    return lastHr;
}

void Renderer::ReleaseSwapChains() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_swapChains.clear();
    LOG_INFO("Renderer::ReleaseSwapChains — all monitor swapchains released cleanly");
}

void Renderer::SetMonitorPauseState(HMONITOR hMonitor, bool paused) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& sc : m_swapChains) {
        if (sc.hMonitor == hMonitor) {
            sc.isPaused = paused;
        }
    }
}

HRESULT Renderer::HandleDeviceLost(const std::vector<HWND>& hwnds, const std::vector<HMONITOR>& hMonitors, bool preferIntegrated) {
    LOG_WARN("Renderer::HandleDeviceLost — recreating D3D11 device and swapchains");
    Shutdown();

    HRESULT hr = InitDevice(preferIntegrated);
    if (FAILED(hr)) return hr;

    for (size_t i = 0; i < hwnds.size(); ++i) {
        RECT r = {};
        GetClientRect(hwnds[i], &r);
        hr = CreateSwapChainForMonitor(hMonitors[i], hwnds[i], r.right - r.left, r.bottom - r.top);
        if (FAILED(hr)) return hr;
    }

    LOG_INFO("Renderer::HandleDeviceLost — all swapchains restored");
    return S_OK;
}

void Renderer::Shutdown() {
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }
    ReleaseSwapChains();
    m_context.Reset();
    m_device.Reset();
    m_factory.Reset();
    LOG_INFO("Renderer::Shutdown — render device shutdown complete");
}

} // namespace lw
