#pragma once
#include <windows.h>
#include <string>
#include <d3d11.h>

class FFIShaderBridge {
public:
    FFIShaderBridge();
    ~FFIShaderBridge();

    bool Load();
    void Unload();

    // The host_out will receive the opaque Rust pointer
    HRESULT InitShaderHost(ID3D11Device* device, ID3D11DeviceContext* context, const std::wstring& path, void** host_out);
    HRESULT RenderShaderFrame(void* host, ID3D11RenderTargetView* rtv, int width, int height, float mouseX, float mouseY, bool isMouseDown, const float* audioData, int audioLen);
    HRESULT ShutdownShaderHost(void* host);

private:
    HMODULE m_rustDll = nullptr;
    
    typedef HRESULT(WINAPI *InitShaderHostFn)(void* device, void* context, const wchar_t* path, void** out_host);
    typedef HRESULT(WINAPI *RenderShaderFrameFn)(void* host, void* rtv, int width, int height, float mouseX, float mouseY, bool isMouseDown, const float* audioData, int audioLen);
    typedef HRESULT(WINAPI *ShutdownShaderHostFn)(void* host);

    InitShaderHostFn m_initShaderHost = nullptr;
    RenderShaderFrameFn m_renderShaderFrame = nullptr;
    ShutdownShaderHostFn m_shutdownShaderHost = nullptr;
};
