#include "renderer.h"
#include "utils.h"
#include <cmath>
#include <d3dcompiler.h>
#include <dxgi1_2.h>


const char* g_vsShader = R"(
cbuffer AspectRatioBuffer : register(b0) {
    float2 uvScale;
    float2 uvOffset;
};

struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT main(uint VertexID : SV_VertexID) {
    VS_OUTPUT Output;
    float2 baseUV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.TexCoord = baseUV * uvScale + uvOffset;
    Output.Position = float4(baseUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}
)";

const char* g_psShader = R"(
Texture2D<float> txY : register(t0);
Texture2D<float2> txUV : register(t1);
SamplerState samLinear : register(s0);

struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target {
    float y = txY.Sample(samLinear, input.TexCoord).r;
    float2 uv = txUV.Sample(samLinear, input.TexCoord).rg;

    // Convert from limited range YUV to RGB (BT.709)
    y = (y - 16.0f / 255.0f) * (255.0f / 219.0f);
    float u = (uv.x - 128.0f / 255.0f) * (255.0f / 224.0f);
    float v = (uv.y - 128.0f / 255.0f) * (255.0f / 224.0f);

    float r = y + 1.5748f * v;
    float g = y - 0.1873f * u - 0.4681f * v;
    float b = y + 1.8556f * u;

    return saturate(float4(r, g, b, 1.0f));
}
)";

struct AspectRatioCB {
    float uvScale[2];
    float uvOffset[2];
};


static void HSLtoRGB(float h, float s, float l, float rgb[4]) {
    float r, g, b;
    if (s == 0.0f) {
        r = g = b = l; // achromatic
    } else {
        auto hue2rgb = [](float p, float q, float t) {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
            if (t < 1.0f / 2.0f) return q;
            if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
            return p;
        };

        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        r = hue2rgb(p, q, h + 1.0f / 3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f / 3.0f);
    }
    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
    rgb[3] = 1.0f;
}

Renderer::Renderer() {
}

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(HWND hWnd) {
    m_hWnd = hWnd;
    LOG_INFO("Initializing Direct3D 11 Renderer...");

    RECT rect;
    GetClientRect(hWnd, &rect);
    m_width = rect.right - rect.left;
    m_height = rect.bottom - rect.top;

    if (!CreateDeviceAndSwapChain()) {
        LOG_ERROR("Failed to create D3D11 Device and Swap Chain.");
        return false;
    }

    if (!CreateRenderTargetView()) {
        LOG_ERROR("Failed to create Render Target View.");
        return false;
    }

    if (!InitializeShaders()) {
        LOG_ERROR("Failed to initialize quad shaders.");
        return false;
    }

    LOG_INFO("D3D11 Renderer successfully initialized (%d x %d).", m_width, m_height);
    return true;
}

void Renderer::Shutdown() {
    ReleaseResources();
    LOG_INFO("D3D11 Renderer shut down.");
}

bool Renderer::CreateDeviceAndSwapChain() {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL supportedLevel;
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT; // Necessary for DirectWrite/2D and Media Foundation video decoding
#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // 1. Create Device and Context (without Swap Chain)
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

    LOG_INFO("Successfully created D3D11 Device (Feature Level = 0x%X). Querying DXGI structures...", supportedLevel);

    // 2. Query DXGI Factory to create the Swap Chain
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to query IDXGIDevice. HRESULT = 0x%08X", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get IDXGIAdapter. HRESULT = 0x%08X", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get IDXGIFactory2. HRESULT = 0x%08X", hr);
        return false;
    }

    // 3. Setup Swap Chain Description for CreateSwapChainForHwnd
    DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
    scd.Width = m_width;
    scd.Height = m_height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Fast zero-copy on Windows 10/11
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsd = { 0 };
    fsd.RefreshRate.Numerator = 60;
    fsd.RefreshRate.Denominator = 1;
    fsd.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    fsd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    fsd.Windowed = TRUE;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    hr = dxgiFactory->CreateSwapChainForHwnd(
        m_d3dDevice.Get(),
        m_hWnd,
        &scd,
        &fsd,
        NULL,
        &swapChain1
    );

    if (FAILED(hr)) {
        LOG_ERROR("CreateSwapChainForHwnd failed. HRESULT = 0x%08X", hr);
        return false;
    }

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to query IDXGISwapChain from IDXGISwapChain1. HRESULT = 0x%08X", hr);
        return false;
    }

    LOG_INFO("D3D11 Renderer Swap Chain successfully created for HWND.");
    return true;
}

bool Renderer::CreateRenderTargetView() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get swap chain back buffer. HRESULT = 0x%08X", hr);
        return false;
    }

    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Render Target View. HRESULT = 0x%08X", hr);
        return false;
    }

    return true;
}

void Renderer::ReleaseResources() {
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_samplerState.Reset();
    m_constantBuffer.Reset();

    m_renderTargetView.Reset();
    m_swapChain.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}

void Renderer::Clear(const float color[4]) {
    if (m_d3dContext && m_renderTargetView) {
        m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), color);
    }
}

HRESULT Renderer::Present() {
    if (m_swapChain) {
        return m_swapChain->Present(1, 0); // VSync enabled
    }
    return S_FALSE;
}

HRESULT Renderer::RenderTestFrame() {
    // Generate HSL color based on cycled hue
    m_hue += 0.002f;
    if (m_hue > 1.0f) m_hue -= 1.0f;

    float clearColor[4];
    HSLtoRGB(m_hue, 0.7f, 0.2f, clearColor); // Vibrant dark background to look professional

    // Bind render targets and set viewport
    D3D11_VIEWPORT vp = { 0 };
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports(1, &vp);
    m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), NULL);

    Clear(clearColor);
    return Present();
}

bool Renderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width == m_width && height == m_height) return true;

    LOG_INFO("Resizing D3D11 Renderer from %dx%d to %dx%d...", m_width, m_height, width, height);
    m_width = width;
    m_height = height;

    if (!m_swapChain) return false;

    // Must release all references to backbuffer before resizing
    if (m_d3dContext) {
        m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_d3dContext->ClearState();
        m_d3dContext->Flush();
    }
    m_renderTargetView.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        2,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0
    );

    if (FAILED(hr)) {
        LOG_ERROR("IDXGISwapChain::ResizeBuffers failed. HRESULT = 0x%08X", hr);
        return false;
    }

    if (!CreateRenderTargetView()) {
        LOG_ERROR("Failed to recreate Render Target View after resize.");
        return false;
    }

    LOG_INFO("Successfully resized renderer swap chain buffers.");
    return true;
}



bool Renderer::InitializeShaders() {
    // Compile and create Vertex Shader
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        g_vsShader,
        strlen(g_vsShader),
        nullptr, nullptr, nullptr,
        "main", "vs_4_0",
        0, 0, &vsBlob, &errorBlob
    );
    if (FAILED(hr)) {
        if (errorBlob) {
            LOG_ERROR("VS compilation failed: %s", (char*)errorBlob->GetBufferPointer());
        } else {
            LOG_ERROR("VS compilation failed with HRESULT 0x%08X", hr);
        }
        return false;
    }

    hr = m_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Vertex Shader. HRESULT: 0x%08X", hr);
        return false;
    }

    // Compile and create Pixel Shader
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(
        g_psShader,
        strlen(g_psShader),
        nullptr, nullptr, nullptr,
        "main", "ps_4_0",
        0, 0, &psBlob, &errorBlob
    );
    if (FAILED(hr)) {
        if (errorBlob) {
            LOG_ERROR("PS compilation failed: %s", (char*)errorBlob->GetBufferPointer());
        } else {
            LOG_ERROR("PS compilation failed with HRESULT 0x%08X", hr);
        }
        return false;
    }

    hr = m_d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Pixel Shader. HRESULT: 0x%08X", hr);
        return false;
    }

    // Create Sampler State (Linear filtering, clamp address modes)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_d3dDevice->CreateSamplerState(&sampDesc, &m_samplerState);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Sampler State. HRESULT: 0x%08X", hr);
        return false;
    }

    // Create Constant Buffer for aspect ratio scale/offset
    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(AspectRatioCB);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_d3dDevice->CreateBuffer(&cbd, nullptr, &m_constantBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create aspect ratio Constant Buffer. HRESULT: 0x%08X", hr);
        return false;
    }

    return true;
}

void Renderer::UpdateAspectRatioCB(int videoWidth, int videoHeight) {
    if (!m_d3dContext || !m_constantBuffer || videoWidth <= 0 || videoHeight <= 0) return;

    float videoAspect = (float)videoWidth / videoHeight;
    float windowAspect = (float)m_width / m_height;

    AspectRatioCB cbData = {};
    cbData.uvScale[0] = 1.0f;
    cbData.uvScale[1] = 1.0f;
    cbData.uvOffset[0] = 0.0f;
    cbData.uvOffset[1] = 0.0f;

    if (videoAspect > windowAspect) {
        // Video is wider, crop left/right to aspect fill
        float scale = windowAspect / videoAspect;
        cbData.uvScale[0] = scale;
        cbData.uvOffset[0] = (1.0f - scale) / 2.0f;
    } else {
        // Video is taller, crop top/bottom to aspect fill
        float scale = videoAspect / windowAspect;
        cbData.uvScale[1] = scale;
        cbData.uvOffset[1] = (1.0f - scale) / 2.0f;
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = m_d3dContext->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        memcpy(mappedResource.pData, &cbData, sizeof(AspectRatioCB));
        m_d3dContext->Unmap(m_constantBuffer.Get(), 0);
    }
}

HRESULT Renderer::RenderVideoFrame(ID3D11ShaderResourceView* pVideoSRV_Y, ID3D11ShaderResourceView* pVideoSRV_UV, int videoWidth, int videoHeight) {
    if (!m_d3dContext || !m_renderTargetView) return E_FAIL;

    // Bind render targets and set viewport
    D3D11_VIEWPORT vp = { 0 };
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports(1, &vp);
    m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), NULL);

    // Clear backbuffer to solid black
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    Clear(clearColor);

    if (pVideoSRV_Y && pVideoSRV_UV && videoWidth > 0 && videoHeight > 0) {
        // Update aspect ratio scale and offset constants
        UpdateAspectRatioCB(videoWidth, videoHeight);

        // Bind Shaders and States
        m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        
        m_d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        
        // Bind both Y and UV Shader Resource Views to t0 and t1
        ID3D11ShaderResourceView* srvs[2] = { pVideoSRV_Y, pVideoSRV_UV };
        m_d3dContext->PSSetShaderResources(0, 2, srvs);
        m_d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

        m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_d3dContext->IASetInputLayout(nullptr); // No input layout needed since SV_VertexID is used

        // Draw 3 vertices for our full-screen triangle
        m_d3dContext->Draw(3, 0);

        // Unbind shader resources to prevent pipeline warnings when the decoder updates them next frame
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        m_d3dContext->PSSetShaderResources(0, 2, nullSRVs);
    }

    return Present();
}
