#include "video_decoder.h"
#include "utils.h"
#include "timer.h"
#include <chrono>

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    Shutdown();
}

bool VideoDecoder::Initialize(ID3D11Device* pDevice) {
    if (!pDevice) {
        LOG_ERROR("VideoDecoder::Initialize received null D3D11 device.");
        return false;
    }
    m_pDevice = pDevice;

    // Enable multithread protection on the D3D11 device for safe concurrent access by MF hardware decoder
    Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pMultithread));
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
    } else {
        LOG_WARN("Failed to enable D3D11 multithread protection. DXVA2 hardware decoding may be unstable.");
    }

    // Startup Media Foundation
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        LOG_ERROR("MFStartup failed. HRESULT: 0x%08X", hr);
        return false;
    }

    // Create the DXGI Device Manager
    hr = MFCreateDXGIDeviceManager(&m_deviceResetToken, &m_pDeviceManager);
    if (FAILED(hr)) {
        LOG_ERROR("MFCreateDXGIDeviceManager failed. HRESULT: 0x%08X", hr);
        MFShutdown();
        return false;
    }

    // Reset device manager with our D3D11 device to enable DXVA2/D3D11 hardware acceleration
    hr = m_pDeviceManager->ResetDevice(m_pDevice, m_deviceResetToken);
    if (FAILED(hr)) {
        LOG_ERROR("IMFDXGIDeviceManager::ResetDevice failed. HRESULT: 0x%08X", hr);
        m_pDeviceManager.Reset();
        MFShutdown();
        return false;
    }

    LOG_INFO("VideoDecoder initialized successfully with DXVA2/D3D11 hardware acceleration.");
    return true;
}

void VideoDecoder::Shutdown() {
    CloseVideo();
    m_pDeviceManager.Reset();
    m_pDevice = nullptr;
    MFShutdown();
    LOG_INFO("VideoDecoder shut down successfully.");
}

bool VideoDecoder::LoadVideo(const std::wstring& filePath) {
    CloseVideo();

    m_filePath = filePath;

    // Robust diagnostic probing of Source Reader attributes combinations
    struct AttrCombo {
        const wchar_t* name;
        bool useD3DManager;
        bool useHardwareTransforms;
        bool useVideoProcessing;
    };

    AttrCombo combos[] = {
        { L"D3D Manager + HW Transforms + Video Processing", true, true, true },
        { L"D3D Manager + Video Processing", true, false, true },
        { L"D3D Manager Only", true, false, false },
        { L"Software (No Attributes)", false, false, false }
    };

    HRESULT hr = E_FAIL;
    int successfulCombo = -1;

    for (int i = 0; i < 4; ++i) {
        Microsoft::WRL::ComPtr<IMFAttributes> pAltAttributes;
        int attrCount = 0;
        if (combos[i].useD3DManager) attrCount++;
        if (combos[i].useHardwareTransforms) attrCount++;
        if (combos[i].useVideoProcessing) attrCount++;

        if (attrCount > 0) {
            hr = MFCreateAttributes(&pAltAttributes, attrCount);
            if (SUCCEEDED(hr)) {
                if (combos[i].useD3DManager) {
                    pAltAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager.Get());
                }
                if (combos[i].useHardwareTransforms) {
                    pAltAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
                }
                if (combos[i].useVideoProcessing) {
                    pAltAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
                }
            }
        }

        hr = MFCreateSourceReaderFromURL(m_filePath.c_str(), pAltAttributes.Get(), &m_pSourceReader);
        if (SUCCEEDED(hr)) {
            successfulCombo = i;
            LOG_INFO_W(L"Successfully created Source Reader with combination: %ls", combos[i].name);
            break;
        } else {
            LOG_WARN_W(L"Source Reader creation failed for combination %ls. HRESULT: 0x%08X", combos[i].name, hr);
        }
    }

    if (FAILED(hr) || !m_pSourceReader) {
        LOG_ERROR_W(L"All Source Reader creation attempts failed for path: %ls.", m_filePath.c_str());
        return false;
    }

    // Disable all streams except first video stream (audio stream is disabled to save resources)
    hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to disable all streams. HRESULT: 0x%08X", hr);
        return false;
    }

    hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to enable video stream. HRESULT: 0x%08X", hr);
        return false;
    }

    // Set output type to RGB32 (so it's converted to standard DXGI_FORMAT_B8G8R8A8_UNORM internally)
    Microsoft::WRL::ComPtr<IMFMediaType> pType;
    hr = MFCreateMediaType(&pType);
    if (FAILED(hr)) {
        LOG_ERROR("MFCreateMediaType failed. HRESULT: 0x%08X", hr);
        return false;
    }

    hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) return false;

    hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr)) return false;

    hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get());
    if (FAILED(hr)) {
        LOG_WARN("SetCurrentMediaType to RGB32 failed on hardware reader. HRESULT: 0x%08X. Falling back to software decoding...", hr);
        
        // Recreate Source Reader in Software Mode (no attributes except Video Processing)
        m_pSourceReader.Reset();
        
        Microsoft::WRL::ComPtr<IMFAttributes> pSoftwareAttributes;
        HRESULT hrSoft = MFCreateAttributes(&pSoftwareAttributes, 1);
        if (SUCCEEDED(hrSoft)) {
            pSoftwareAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }
        
        hr = MFCreateSourceReaderFromURL(m_filePath.c_str(), pSoftwareAttributes.Get(), &m_pSourceReader);
        if (FAILED(hr)) {
            LOG_ERROR_W(L"Software fallback MFCreateSourceReaderFromURL failed. HRESULT: 0x%08X", hr);
            return false;
        }

        // Re-enable only video stream
        m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

        // Re-set media type to RGB32
        hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get());
        if (FAILED(hr)) {
            LOG_ERROR("Software fallback SetCurrentMediaType to RGB32 failed. HRESULT: 0x%08X", hr);
            return false;
        }
        
        LOG_INFO("Successfully fell back to high-performance software decoding with automatic RGB32 conversion.");
    }

    // Retrieve video width and height
    Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
    hr = m_pSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get current media type. HRESULT: 0x%08X", hr);
        return false;
    }

    UINT32 width = 0, height = 0;
    hr = MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get video frame size. HRESULT: 0x%08X", hr);
        return false;
    }

    m_videoWidth = width;
    m_videoHeight = height;

    LOG_INFO_W(L"Loaded video: %ls (%dx%d)", m_filePath.c_str(), m_videoWidth, m_videoHeight);

    // Create local texture and SRV for rendering
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_videoWidth;
    desc.Height = m_videoHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pVideoTexture);
    if (FAILED(hr)) {
        LOG_ERROR("CreateTexture2D for video frame failed. HRESULT: 0x%08X", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_pDevice->CreateShaderResourceView(m_pVideoTexture.Get(), &srvDesc, &m_pVideoSRV);
    if (FAILED(hr)) {
        LOG_ERROR("CreateShaderResourceView for video frame failed. HRESULT: 0x%08X", hr);
        return false;
    }

    // Start decoding thread
    m_runThread = true;
    m_videoLoaded = true;
    m_decodeThread = std::thread(&VideoDecoder::DecodingThreadProc, this);

    return true;
}

void VideoDecoder::CloseVideo() {
    m_videoLoaded = false;
    m_runThread = false;

    // Flush any pending synchronous ReadSample calls to prevent joining threads from hanging
    if (m_pSourceReader) {
        m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    }

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_sampleMutex);
        m_pLatestSample.Reset();
        m_bNewSampleAvailable = false;
    }

    m_pSourceReader.Reset();
    m_pVideoSRV.Reset();
    m_pVideoTexture.Reset();
    m_videoWidth = 0;
    m_videoHeight = 0;
}

void VideoDecoder::DecodingThreadProc() {
    LOG_INFO("VideoDecoder background thread started.");

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in background decoding thread. HRESULT: 0x%08X", hrCOM);
    }

    Timer timer;
    double timerOffset = 0.0;
    bool wasPaused = false;

    while (m_runThread) {
        if (m_isPaused.load()) {
            wasPaused = true;
            Timer::PreciseSleep(100.0);
            continue;
        }
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> pSample;

        // Read next sample from Media Foundation Source Reader
        HRESULT hr = m_pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );

        if (FAILED(hr)) {
            LOG_ERROR("ReadSample failed. HRESULT: 0x%08X", hr);
            Timer::PreciseSleep(10.0);
            continue;
        }

        // Loop playbacks automatically when we reach the end of stream
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            LOG_INFO("Reached end of video stream. Looping...");
            
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = 0;
            
            hr = m_pSourceReader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);

            if (FAILED(hr)) {
                LOG_ERROR("SetCurrentPosition(0) failed. HRESULT: 0x%08X", hr);
            }

            timer.Reset();
            timerOffset = 0.0;
            continue;
        }

        if (pSample) {
            // Safe pacing logic
            double sampleTimeMs = static_cast<double>(timestamp) / 10000.0;

            if (wasPaused) {
                timerOffset = timer.GetElapsedMilliseconds() - sampleTimeMs;
                wasPaused = false;
            }

            double elapsedMs = timer.GetElapsedMilliseconds() - timerOffset;

            if (sampleTimeMs > elapsedMs) {
                double sleepMs = sampleTimeMs - elapsedMs;
                // Safe guard limit for unexpected timestamp leaps
                if (sleepMs > 2000.0) {
                    sleepMs = 33.3;
                    timerOffset = timer.GetElapsedMilliseconds() - sampleTimeMs;
                }
                Timer::PreciseSleep(sleepMs);
            }

            // Expose the latest decoded sample to the render thread safely
            {
                std::lock_guard<std::mutex> lock(m_sampleMutex);
                m_pLatestSample = pSample;
                m_bNewSampleAvailable = true;
            }
        } else {
            // Sleep briefly when no sample is fetched but no end-of-stream reached yet
            Timer::PreciseSleep(2.0);
        }
    }

    LOG_INFO("VideoDecoder background thread stopped.");
    if (SUCCEEDED(hrCOM)) {
        CoUninitialize();
    }
}

bool VideoDecoder::UpdateFrame(ID3D11DeviceContext* pContext) {
    if (!m_videoLoaded || !m_pVideoTexture) return false;

    Microsoft::WRL::ComPtr<IMFSample> pSample;
    bool hasNewSample = false;

    {
        std::lock_guard<std::mutex> lock(m_sampleMutex);
        if (m_bNewSampleAvailable) {
            pSample = m_pLatestSample;
            m_bNewSampleAvailable = false;
            hasNewSample = true;
        }
    }

    if (!hasNewSample || !pSample) {
        return false;
    }

    // Extract the buffer out of the MF sample
    Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
    HRESULT hr = pSample->GetBufferByIndex(0, &pBuffer);
    if (FAILED(hr)) return false;

    // Get the D3D11 resource / texture from the Media Foundation DXGI buffer (Hardware Path)
    Microsoft::WRL::ComPtr<IMFDXGIBuffer> pDXGIBuffer;
    hr = pBuffer.As(&pDXGIBuffer);
    if (SUCCEEDED(hr)) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pMFTexture;
        hr = pDXGIBuffer->GetResource(IID_PPV_ARGS(&pMFTexture));
        if (SUCCEEDED(hr)) {
            UINT subresourceIndex = 0;
            pDXGIBuffer->GetSubresourceIndex(&subresourceIndex);

            // Fast hardware GPU-to-GPU copy from MF texture to local texture
            pContext->CopySubresourceRegion(
                m_pVideoTexture.Get(),
                0, // Destination subresource
                0, 0, 0, // Destination coordinates
                pMFTexture.Get(),
                subresourceIndex, // Source subresource
                nullptr // Copy entire resource
            );
            return true;
        }
    }

    // Software Fallback: Copy CPU memory buffer to GPU texture
    BYTE* pData = nullptr;
    DWORD cbCurrentLength = 0;
    hr = pBuffer->Lock(&pData, nullptr, &cbCurrentLength);
    if (SUCCEEDED(hr)) {
        UINT32 rowPitch = m_videoWidth * 4; // B8G8R8A8 format = 4 bytes per pixel
        pContext->UpdateSubresource(
            m_pVideoTexture.Get(),
            0,
            nullptr,
            pData,
            rowPitch,
            0
        );
        pBuffer->Unlock();
        return true;
    }

    return false;
}
