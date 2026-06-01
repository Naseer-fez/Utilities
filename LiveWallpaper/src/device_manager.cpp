#include "device_manager.h"
#include "utils.h"

DeviceManager::DeviceManager() {}

DeviceManager::~DeviceManager() {
    Shutdown();
}

bool DeviceManager::Initialize() {
    LOG_INFO("Initializing DeviceManager (D3D11 device and context)...");

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL supportedLevel;
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT; 
#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &supportedLevel,
        &m_d3dContext
    );

    if (FAILED(hr)) {
        LOG_WARN("Hardware D3D11 Device creation failed. Falling back to WARP software renderer. HRESULT: 0x%08X", hr);
        hr = D3D11CreateDevice(
            NULL,
            D3D_DRIVER_TYPE_WARP,
            NULL,
            creationFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &m_d3dDevice,
            &supportedLevel,
            &m_d3dContext
        );
    }

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D11 Device (HRESULT = 0x%08X)", hr);
        return false;
    }

    // Protect context for multithreading (needed by Media Foundation)
    Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
    if (SUCCEEDED(m_d3dDevice.As(&pMultithread))) {
        pMultithread->SetMultithreadProtected(TRUE);
    }

    LOG_INFO("DeviceManager successfully initialized.");
    return true;
}

void DeviceManager::Shutdown() {
    ReleaseResources();
    LOG_INFO("DeviceManager shut down.");
}

void DeviceManager::ReleaseResources() {
    if (m_d3dContext) {
        m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_d3dContext->ClearState();
        m_d3dContext->Flush();
    }
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}
