#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    bool Initialize();
    void Shutdown();

    ID3D11Device* GetDevice() const { return m_d3dDevice.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_d3dContext.Get(); }

private:
    void ReleaseResources();

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
};
