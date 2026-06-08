#pragma once

// =============================================================================
// renderer.h — D3D11 renderer for frame presentation
//
// Manages the D3D11 device, device context, DXGI factory, and multi-swap chains.
// Copies a decoded video texture to the swap chain back buffer of each active,
// non-paused display and presents it to the desktop.
// =============================================================================

#include "utils.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <mutex>

namespace lw {

/// Structure representing a DXGI SwapChain bound to a physical monitor.
struct SwapChainOutput {
    HMONITOR hMonitor;
    HWND hWnd;
    ComPtr<IDXGISwapChain1> swapChain;
    UINT width;
    UINT height;
    bool isPaused;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    /// Create the D3D11 device with VIDEO_SUPPORT and BGRA_SUPPORT flags.
    HRESULT InitDevice(bool preferIntegrated = true);

    /// Create a DXGI swap chain specifically for a physical monitor's render HWND.
    HRESULT CreateSwapChainForMonitor(HMONITOR hMonitor, HWND hwnd, UINT width, UINT height);

    /// Copy the decoded frame texture to the back buffer of all active display swap chains.
    HRESULT PresentFrame(ID3D11Texture2D* srcTexture, UINT srcSubresource);

    /// Rebuild all device objects and SwapChains concurrently when device-lost occurs.
    HRESULT HandleDeviceLost(const std::vector<HWND>& hwnds, const std::vector<HMONITOR>& hMonitors, bool preferIntegrated);

    /// Accessors for other components that need the D3D11 device
    ID3D11Device*        GetDevice()        const { return m_device.Get(); }
    ID3D11DeviceContext* GetDeviceContext()  const { return m_context.Get(); }

    /// Release all allocated SwapChains safely
    void ReleaseSwapChains();

    /// Set individual monitor pause states thread-safely
    void SetMonitorPauseState(HMONITOR hMonitor, bool paused);

    /// Release all D3D11/DXGI resources in the correct order.
    void Shutdown();

private:
    HRESULT FindAdapter(bool preferIntegrated, IDXGIAdapter1** adapter);

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIFactory2>       m_factory;
    
    std::vector<SwapChainOutput> m_swapChains;
    std::mutex                   m_mutex; // Mutex to synchronize multi-monitor state swaps
};

} // namespace lw
