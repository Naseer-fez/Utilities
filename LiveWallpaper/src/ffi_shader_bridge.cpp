#include "ffi_shader_bridge.h"
#include "utils.h"

FFIShaderBridge::FFIShaderBridge() {}

FFIShaderBridge::~FFIShaderBridge() {
    Unload();
}

bool FFIShaderBridge::Load() {
    if (m_rustDll) return true;

    // Secure DLL loading (PHASE 2 - SECURITY HARDENING)
    // Construct the absolute path strictly within the application directory
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        std::wstring path = exePath;
        size_t pos = path.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            std::wstring dllPath = path.substr(0, pos) + L"\\live_wallpaper_rust.dll";
            // Exclusively load from the application directory, restricting dependency searches there too
            m_rustDll = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
        }
    }

    if (!m_rustDll) {
        LOG_ERROR("Failed to load live_wallpaper_rust.dll securely. Error: %d", GetLastError());
        return false;
    }

    m_initShaderHost = (InitShaderHostFn)GetProcAddress(m_rustDll, "init_shader_host");
    m_renderShaderFrame = (RenderShaderFrameFn)GetProcAddress(m_rustDll, "render_shader_frame");
    m_shutdownShaderHost = (ShutdownShaderHostFn)GetProcAddress(m_rustDll, "shutdown_shader_host");

    if (!m_initShaderHost || !m_renderShaderFrame || !m_shutdownShaderHost) {
        LOG_ERROR("Failed to locate one or more FFI exports in live_wallpaper_rust.dll");
        Unload();
        return false;
    }

    LOG_INFO("Successfully loaded live_wallpaper_rust.dll securely and resolved all FFI shader symbols!");
    return true;
}

void FFIShaderBridge::Unload() {
    if (m_rustDll) {
        FreeLibrary(m_rustDll);
        m_rustDll = nullptr;
    }
    m_initShaderHost = nullptr;
    m_renderShaderFrame = nullptr;
    m_shutdownShaderHost = nullptr;
}

HRESULT FFIShaderBridge::InitShaderHost(ID3D11Device* device, ID3D11DeviceContext* context, const std::wstring& path, void** host_out) {
    if (!m_initShaderHost) return E_POINTER;
    return m_initShaderHost((void*)device, (void*)context, path.c_str(), host_out);
}

HRESULT FFIShaderBridge::RenderShaderFrame(void* host, ID3D11RenderTargetView* rtv, int width, int height, float mouseX, float mouseY, bool isMouseDown, const float* audioData, int audioLen) {
    if (!m_renderShaderFrame) return E_POINTER;
    return m_renderShaderFrame(host, (void*)rtv, width, height, mouseX, mouseY, isMouseDown, audioData, audioLen);
}

HRESULT FFIShaderBridge::ShutdownShaderHost(void* host) {
    if (!m_shutdownShaderHost) return E_POINTER;
    return m_shutdownShaderHost(host);
}
