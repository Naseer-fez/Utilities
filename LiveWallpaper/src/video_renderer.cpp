#include "video_renderer.h"
#include "utils.h"
#include <d3dcompiler.h>

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
        r = g = b = l; 
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

VideoRenderer::VideoRenderer() {}

VideoRenderer::~VideoRenderer() {
    Shutdown();
}

bool VideoRenderer::Initialize(DeviceManager* pDeviceManager, SwapChainManager* pSwapChainManager) {
    if (!pDeviceManager || !pDeviceManager->GetDevice()) {
        LOG_ERROR("VideoRenderer requires a valid DeviceManager.");
        return false;
    }
    if (!pSwapChainManager) {
        LOG_ERROR("VideoRenderer requires a valid SwapChainManager.");
        return false;
    }
    m_pDeviceManager = pDeviceManager;
    m_pSwapChainManager = pSwapChainManager;

    if (!InitializeShaders()) {
        LOG_ERROR("Failed to initialize video quad shaders.");
        return false;
    }

    LOG_INFO("VideoRenderer successfully initialized.");
    return true;
}

void VideoRenderer::Shutdown() {
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_samplerState.Reset();
    m_constantBuffer.Reset();
    m_pDeviceManager = nullptr;
    m_pSwapChainManager = nullptr;
}

bool VideoRenderer::InitializeShaders() {
    auto d3dDevice = m_pDeviceManager->GetDevice();

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        g_vsShader, strlen(g_vsShader),
        nullptr, nullptr, nullptr,
        "main", "vs_4_0", 0, 0, &vsBlob, &errorBlob
    );
    if (FAILED(hr)) return false;

    hr = d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(
        g_psShader, strlen(g_psShader),
        nullptr, nullptr, nullptr,
        "main", "ps_4_0", 0, 0, &psBlob, &errorBlob
    );
    if (FAILED(hr)) return false;

    hr = d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = d3dDevice->CreateSamplerState(&sampDesc, &m_samplerState);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(AspectRatioCB);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = d3dDevice->CreateBuffer(&cbd, nullptr, &m_constantBuffer);
    if (FAILED(hr)) return false;

    return true;
}

void VideoRenderer::UpdateAspectRatioCB(int textureWidth, int textureHeight, int videoWidth, int videoHeight) {
    if (!m_pDeviceManager || !m_pSwapChainManager || !m_constantBuffer) return;
    auto d3dContext = m_pDeviceManager->GetContext();

    float videoAspect = (float)videoWidth / videoHeight;
    float windowAspect = (float)m_pSwapChainManager->GetWidth() / m_pSwapChainManager->GetHeight();

    AspectRatioCB cbData = {};
    cbData.uvScale[0] = 1.0f;
    cbData.uvScale[1] = 1.0f;
    cbData.uvOffset[0] = 0.0f;
    cbData.uvOffset[1] = 0.0f;

    if (videoAspect > windowAspect) {
        float scale = windowAspect / videoAspect;
        cbData.uvScale[0] = scale;
        cbData.uvOffset[0] = (1.0f - scale) / 2.0f;
    } else {
        float scale = videoAspect / windowAspect;
        cbData.uvScale[1] = scale;
        cbData.uvOffset[1] = (1.0f - scale) / 2.0f;
    }

    float textureRatioX = (float)videoWidth / textureWidth;
    float textureRatioY = (float)videoHeight / textureHeight;

    cbData.uvScale[0] *= textureRatioX;
    cbData.uvOffset[0] *= textureRatioX;
    cbData.uvScale[1] *= textureRatioY;
    cbData.uvOffset[1] *= textureRatioY;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = d3dContext->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        memcpy(mappedResource.pData, &cbData, sizeof(AspectRatioCB));
        d3dContext->Unmap(m_constantBuffer.Get(), 0);
    }
}

HRESULT VideoRenderer::RenderVideoFrame(
    ID3D11ShaderResourceView* pVideoSRV_Y, 
    ID3D11ShaderResourceView* pVideoSRV_UV, 
    int textureWidth, 
    int textureHeight, 
    int videoWidth, 
    int videoHeight
) {
    if (!m_pDeviceManager || !m_pSwapChainManager) return E_FAIL;
    auto d3dContext = m_pDeviceManager->GetContext();
    auto rtv = m_pSwapChainManager->GetRenderTargetView();

    if (!d3dContext || !rtv || !pVideoSRV_Y || !pVideoSRV_UV) return E_FAIL;

    D3D11_VIEWPORT vp = { 0 };
    vp.Width = (float)m_pSwapChainManager->GetWidth();
    vp.Height = (float)m_pSwapChainManager->GetHeight();
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    d3dContext->RSSetViewports(1, &vp);
    d3dContext->OMSetRenderTargets(1, &rtv, NULL);

    UpdateAspectRatioCB(textureWidth, textureHeight, videoWidth, videoHeight);

    d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    
    d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    ID3D11ShaderResourceView* srvs[2] = { pVideoSRV_Y, pVideoSRV_UV };
    d3dContext->PSSetShaderResources(0, 2, srvs);
    d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3dContext->IASetInputLayout(nullptr);

    d3dContext->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    d3dContext->PSSetShaderResources(0, 2, nullSRVs);

    return S_OK;
}

HRESULT VideoRenderer::RenderTestFrame() {
    if (!m_pDeviceManager || !m_pSwapChainManager) return E_FAIL;
    auto d3dContext = m_pDeviceManager->GetContext();
    auto rtv = m_pSwapChainManager->GetRenderTargetView();

    if (!d3dContext || !rtv) return E_FAIL;

    m_hue += 0.002f;
    if (m_hue > 1.0f) m_hue -= 1.0f;

    float clearColor[4];
    HSLtoRGB(m_hue, 0.7f, 0.2f, clearColor);

    D3D11_VIEWPORT vp = { 0 };
    vp.Width = (float)m_pSwapChainManager->GetWidth();
    vp.Height = (float)m_pSwapChainManager->GetHeight();
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    d3dContext->RSSetViewports(1, &vp);
    d3dContext->OMSetRenderTargets(1, &rtv, NULL);

    d3dContext->ClearRenderTargetView(rtv, clearColor);
    return S_OK;
}
