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

    struct FallbackOption {
        const char* name;
        bool useD3DManager;
        bool useVideoProcessing;
    };

    FallbackOption options[] = {
        { "D3D Manager + Video Processing", true, true },
        { "D3D Manager Only", true, false },
        { "Software + Video Processing", false, true },
        { "Software (No Attributes)", false, false }
    };

    bool initialized = false;
    HRESULT hr = E_FAIL;

    for (const auto& opt : options) {
        if (opt.useD3DManager && !m_pDeviceManager) {
            continue; // Skip hardware options if device manager is not initialized
        }

        Microsoft::WRL::ComPtr<IMFAttributes> pAttributes;
        UINT32 attrCount = 0;
        if (opt.useD3DManager) attrCount++;
        if (opt.useVideoProcessing) attrCount++;

        if (attrCount > 0) {
            hr = MFCreateAttributes(&pAttributes, attrCount);
            if (FAILED(hr)) {
                LOG_WARN("MFCreateAttributes failed for combination: %s. HRESULT: 0x%08X", opt.name, hr);
                continue;
            }
            if (opt.useD3DManager) {
                pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager.Get());
            }
            if (opt.useVideoProcessing) {
                pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
            }
        }

        hr = MFCreateSourceReaderFromURL(m_filePath.c_str(), pAttributes.Get(), &m_pSourceReader);
        if (FAILED(hr)) {
            LOG_WARN("Source Reader creation failed for combination: %s. HRESULT: 0x%08X", opt.name, hr);
            m_pSourceReader.Reset();
            continue;
        }

        // Disable all streams except first video stream
        hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED(hr)) {
            LOG_WARN("Failed to disable all streams for combination: %s. HRESULT: 0x%08X", opt.name, hr);
            m_pSourceReader.Reset();
            continue;
        }

        hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) {
            LOG_WARN("Failed to enable video stream for combination: %s. HRESULT: 0x%08X", opt.name, hr);
            m_pSourceReader.Reset();
            continue;
        }

        // Set output type to NV12
        Microsoft::WRL::ComPtr<IMFMediaType> pType;
        hr = MFCreateMediaType(&pType);
        if (FAILED(hr)) {
            LOG_WARN("MFCreateMediaType failed for combination: %s. HRESULT: 0x%08X", opt.name, hr);
            m_pSourceReader.Reset();
            continue;
        }

        hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) {
            m_pSourceReader.Reset();
            continue;
        }

        hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (FAILED(hr)) {
            m_pSourceReader.Reset();
            continue;
        }

        hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get());
        if (FAILED(hr)) {
            LOG_WARN("SetCurrentMediaType to NV12 failed for combination: %s. HRESULT: 0x%08X", opt.name, hr);
            m_pSourceReader.Reset();
            continue;
        }

        LOG_INFO("Successfully created Source Reader with combination: %s", opt.name);
        initialized = true;
        break;
    }

    if (!initialized || !m_pSourceReader) {
        LOG_ERROR_W(L"All Source Reader creation attempts failed for path: %ls.", m_filePath.c_str());
        return false;
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

    // Create local texture and SRVs for rendering
    if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
        return false;
    }

    // Reset playback timeline
    m_playbackTimeMs = 0.0;
    m_currentFrameTimestamp = -1.0;
    m_playbackTimer.Reset();

    m_pActiveSRV_Y = m_pVideoSRV_Y;
    m_pActiveSRV_UV = m_pVideoSRV_UV;

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

    m_sampleQueue.Clear();

    m_pSourceReader.Reset();
    m_pVideoSRV_Y.Reset();
    m_pVideoSRV_UV.Reset();
    m_pVideoTexture.Reset();
    m_pActiveSRV_Y.Reset();
    m_pActiveSRV_UV.Reset();
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_videoTextureWidth = 0;
    m_videoTextureHeight = 0;
}

void VideoDecoder::SetPaused(bool paused) {
    if (m_isPaused.load() && !paused) {
        // Transition from paused to resumed: reset the playback timer
        // to prevent large elapsed time jumps.
        m_playbackTimer.Reset();
    }
    m_isPaused.store(paused);
}

bool VideoDecoder::ReallocateVideoTexture(int width, int height) {
    m_pVideoSRV_Y.Reset();
    m_pVideoSRV_UV.Reset();
    m_pVideoTexture.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    HRESULT hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pVideoTexture);
    if (FAILED(hr)) {
        LOG_ERROR("ReallocateVideoTexture CreateTexture2D failed. HRESULT: 0x%08X", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDescY = {};
    srvDescY.Format = DXGI_FORMAT_R8_UNORM;
    srvDescY.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDescY.Texture2D.MipLevels = 1;
    srvDescY.Texture2D.MostDetailedMip = 0;

    hr = m_pDevice->CreateShaderResourceView(m_pVideoTexture.Get(), &srvDescY, &m_pVideoSRV_Y);
    if (FAILED(hr)) {
        LOG_ERROR("ReallocateVideoTexture CreateShaderResourceView Y failed. HRESULT: 0x%08X", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDescUV = {};
    srvDescUV.Format = DXGI_FORMAT_R8G8_UNORM;
    srvDescUV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDescUV.Texture2D.MipLevels = 1;
    srvDescUV.Texture2D.MostDetailedMip = 0;

    hr = m_pDevice->CreateShaderResourceView(m_pVideoTexture.Get(), &srvDescUV, &m_pVideoSRV_UV);
    if (FAILED(hr)) {
        LOG_ERROR("ReallocateVideoTexture CreateShaderResourceView UV failed. HRESULT: 0x%08X", hr);
        return false;
    }

    m_videoTextureWidth = width;
    m_videoTextureHeight = height;

    LOG_INFO("Reallocated local video texture to match hardware/software frame size: %dx%d", width, height);
    return true;
}

void VideoDecoder::DecodingThreadProc() {
    LOG_INFO("VideoDecoder background thread started.");

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in background decoding thread. HRESULT: 0x%08X", hrCOM);
    }

    while (m_runThread) {
        if (m_isPaused.load()) {
            Timer::PreciseSleep(100.0);
            continue;
        }

        // Wait if the queue is full to avoid decoding too far ahead
        while (m_runThread && !m_isPaused.load()) {
            if (m_sampleQueue.Size() < 5) { // Maximum of 5 frames buffered
                break;
            }
            Timer::PreciseSleep(5.0);
        }

        if (!m_runThread) {
            break;
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
            
            // Flush decoder pipeline to release DXVA2 buffers and prevent VRAM accumulation
            m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = 0;
            
            hr = m_pSourceReader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);

            if (FAILED(hr)) {
                LOG_ERROR("SetCurrentPosition(0) failed. HRESULT: 0x%08X", hr);
            }
            continue;
        }

        if (pSample) {
            IMFSample* pRawSample = pSample.Detach();
            if (!m_sampleQueue.Push(pRawSample)) {
                // If queue push fails, release the sample to prevent leak
                pRawSample->Release();
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

bool VideoDecoder::UpdateFrame(ID3D11DeviceContext* pContext, double& outWaitTimeMs) {
    outWaitTimeMs = 0.0;
    if (!m_videoLoaded || !m_pVideoTexture) return false;



    // Advance playback time based on elapsed clock time since the last rendered frame
    double elapsed = m_playbackTimer.GetElapsedMilliseconds();

    if (m_isPaused.load()) {
        return false;
    }

    if (m_currentFrameTimestamp >= 0.0) {
        m_playbackTimeMs = m_currentFrameTimestamp + elapsed;
    } else {
        m_playbackTimeMs += elapsed;
        m_playbackTimer.Reset();
    }

    Microsoft::WRL::ComPtr<IMFSample> pSelectedSample;
    bool hasNewFrame = false;

    while (true) {
        IMFSample* frontSample = m_sampleQueue.Peek();
        if (!frontSample) {
            break;
        }

        LONGLONG hnsTimestamp = 0;
        if (FAILED(frontSample->GetSampleTime(&hnsTimestamp))) {
            m_sampleQueue.PopAndDiscard();
            continue;
        }

        double sampleTimeMs = static_cast<double>(hnsTimestamp) / 10000.0;

        // First frame: display immediately and align playback timer
        if (m_currentFrameTimestamp < 0.0) {
            m_playbackTimeMs = sampleTimeMs;
            m_currentFrameTimestamp = sampleTimeMs;
            IMFSample* poppedSample = nullptr;
            if (m_sampleQueue.Pop(poppedSample)) {
                pSelectedSample.Attach(poppedSample);
                hasNewFrame = true;
            }
            continue;
        }

        // Check for loop transition:
        // If the next frame's timestamp is less than the current frame's timestamp,
        // then the video must have looped.
        if (sampleTimeMs < m_currentFrameTimestamp) {
            m_playbackTimeMs = sampleTimeMs; // Reset playback time to match the new loop start
            m_currentFrameTimestamp = sampleTimeMs;
            IMFSample* poppedSample = nullptr;
            if (m_sampleQueue.Pop(poppedSample)) {
                pSelectedSample.Attach(poppedSample);
                hasNewFrame = true;
            }
            continue; // Continue checking if we need to catch up
        }

        if (m_playbackTimeMs >= sampleTimeMs) {
            m_currentFrameTimestamp = sampleTimeMs;
            IMFSample* poppedSample = nullptr;
            if (m_sampleQueue.Pop(poppedSample)) {
                pSelectedSample.Attach(poppedSample);
                hasNewFrame = true;
            }
        } else {
            // Next frame is in the future, stop popping and calculate wait time
            outWaitTimeMs = sampleTimeMs - m_playbackTimeMs;
            break;
        }
    }

    if (!hasNewFrame && m_sampleQueue.IsEmpty()) {
        // Queue is empty, wait a default short duration (e.g. 2.0ms) to let decoder thread decode.
        outWaitTimeMs = 2.0;
    }

    if (!hasNewFrame || !pSelectedSample) {
        return false;
    }

    // Reset the playback timer to anchor the elapsed time for the next frame
    m_playbackTimer.Reset();

    // Extract the buffer out of the MF sample
    Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
    HRESULT hr = pSelectedSample->GetBufferByIndex(0, &pBuffer);
    if (FAILED(hr)) return false;

    // Get the D3D11 resource / texture from the Media Foundation DXGI buffer (Hardware Path)
    Microsoft::WRL::ComPtr<IMFDXGIBuffer> pDXGIBuffer;
    hr = pBuffer.As(&pDXGIBuffer);
    if (SUCCEEDED(hr)) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pMFTexture;
        hr = pDXGIBuffer->GetResource(IID_PPV_ARGS(&pMFTexture));
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC mfDesc;
            pMFTexture->GetDesc(&mfDesc);

            if (mfDesc.Width != m_videoTextureWidth || mfDesc.Height != m_videoTextureHeight) {
                if (!ReallocateVideoTexture(mfDesc.Width, mfDesc.Height)) {
                    return false;
                }
            }

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
            m_pActiveSRV_Y = m_pVideoSRV_Y;
            m_pActiveSRV_UV = m_pVideoSRV_UV;
            return true;
        }
    }

    // Software Fallback: Copy CPU memory buffer to GPU texture
    Microsoft::WRL::ComPtr<IMF2DBuffer> p2DBuffer;
    hr = pBuffer.As(&p2DBuffer);
    if (SUCCEEDED(hr)) {
        if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
            if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                return false;
            }
        }
        BYTE* pScanline0 = nullptr;
        LONG pitch = 0;
        hr = p2DBuffer->Lock2D(&pScanline0, &pitch);
        if (SUCCEEDED(hr)) {
            pContext->UpdateSubresource(
                m_pVideoTexture.Get(),
                0,
                nullptr,
                pScanline0,
                pitch,
                0
            );
            p2DBuffer->Unlock2D();
            m_pActiveSRV_Y = m_pVideoSRV_Y;
            m_pActiveSRV_UV = m_pVideoSRV_UV;
            return true;
        }
    }

    // Secondary software fallback: lock standard contiguous buffer
    BYTE* pData = nullptr;
    DWORD cbCurrentLength = 0;
    hr = pBuffer->Lock(&pData, nullptr, &cbCurrentLength);
    if (SUCCEEDED(hr)) {
        if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
            if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                pBuffer->Unlock();
                return false;
            }
        }
        // For NV12, the row pitch of the Y plane is m_videoWidth (1 byte per pixel)
        UINT32 rowPitch = m_videoWidth;
        pContext->UpdateSubresource(
            m_pVideoTexture.Get(),
            0,
            nullptr,
            pData,
            rowPitch,
            0
        );
        pBuffer->Unlock();
        m_pActiveSRV_Y = m_pVideoSRV_Y;
        m_pActiveSRV_UV = m_pVideoSRV_UV;
        return true;
    }

    return false;
}
