// =============================================================================
// video_decoder.cpp — Media Foundation DXVA2 hardware-accelerated video decoder
//
// Pipeline:
//   MP4 File → IMFSourceReader → DXVA2 Hardware Decoder → ID3D11Texture2D
//
// The source reader is configured to:
//   - Use our D3D11 device via MFDXGIDeviceManager for hardware decode
//   - Enable hardware transforms (DXVA2)
//   - Enable advanced video processing (color space conversion)
//   - Output ARGB32 format (matches B8G8R8A8 swap chain)
//   - Decode only the first video stream (audio is completely ignored)
//
// Each ReadSample() call returns an IMFSample containing an IMFDXGIBuffer
// that wraps an ID3D11Texture2D in GPU memory. The texture is part of the
// DXVA2 decode pool (a texture array) and is identified by a subresource
// index.
// =============================================================================

#include "video_decoder.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace lw {

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
VideoDecoder::~VideoDecoder() {
    Shutdown();
}

// ---------------------------------------------------------------------------
// Init — set up the full Media Foundation decode pipeline.
//
// This is a multi-step process:
//   1. Create the DXGI device manager and bind our D3D11 device to it.
//      This allows MF's internal decoder to allocate decode surfaces
//      on the same device as our swap chain — enabling zero-copy present.
//
//   2. Configure source reader attributes:
//      - MF_SOURCE_READER_D3D_MANAGER:  DXGI device manager for HW decode
//      - MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS: allow DXVA2 decoders
//      - MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING: enable the
//        video processor MFT for format conversion (NV12 → ARGB32)
//
//   3. Open the file with MFCreateSourceReaderFromURL.
//
//   4. Deselect all streams, then select only the first video stream.
//      This tells MF to completely ignore audio data.
//
//   5. Set the output media type to ARGB32. The source reader will
//      insert a video processor MFT if needed to convert from the
//      decoder's native NV12 output.
// ---------------------------------------------------------------------------
HRESULT VideoDecoder::Init(ID3D11Device* device, const wchar_t* videoPath, UINT targetWidth, UINT targetHeight) {
    if (!device || !videoPath) {
        LOG_ERROR("VideoDecoder::Init — null device or path");
        return E_INVALIDARG;
    }

    // Step 1: Create DXGI device manager
    HR_CHECK(
        MFCreateDXGIDeviceManager(&m_resetToken, &m_dxgiManager),
        "MFCreateDXGIDeviceManager failed"
    );

    HR_CHECK(
        m_dxgiManager->ResetDevice(device, m_resetToken),
        "MFDXGIDeviceManager::ResetDevice failed"
    );

    LOG_INFO("VideoDecoder::Init — DXGI device manager created and bound");

    // Step 2: Configure source reader attributes
    ComPtr<IMFAttributes> attributes;
    HR_CHECK(
        MFCreateAttributes(&attributes, 4),
        "MFCreateAttributes failed"
    );

    // Point the source reader at our DXGI device manager for HW decode
    HR_CHECK(
        attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_dxgiManager.Get()),
        "Failed to set MF_SOURCE_READER_D3D_MANAGER"
    );

    // Enable hardware transforms (DXVA2 H.264 decoder)
    HR_CHECK(
        attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
        "Failed to set MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS"
    );

    // Enable advanced video processing (format conversion via video processor MFT)
    HR_CHECK(
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE),
        "Failed to set MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING"
    );

    // Step 3: Open the video file
    HR_CHECK(
        MFCreateSourceReaderFromURL(videoPath, attributes.Get(), &m_reader),
        "MFCreateSourceReaderFromURL failed"
    );

    LOG_INFO("VideoDecoder::Init — source reader created for: %ls", videoPath);

    // Step 4: Deselect all streams, then select only the first video stream.
    // This ensures MF doesn't waste time demuxing or decoding audio.
    HR_CHECK(
        m_reader->SetStreamSelection(
            static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE),
        "Failed to deselect all streams"
    );

    HR_CHECK(
        m_reader->SetStreamSelection(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE),
        "Failed to select first video stream"
    );

    // Step 5: Set output media type to ARGB32.
    // The decoder outputs NV12 natively. By requesting ARGB32, the source
    // reader automatically inserts a video processor MFT that converts
    // NV12 → ARGB32 on the GPU. This matches our B8G8R8A8_UNORM swap chain.
    ComPtr<IMFMediaType> outputType;
    HR_CHECK(
        MFCreateMediaType(&outputType),
        "MFCreateMediaType failed"
    );

    HR_CHECK(
        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
        "Failed to set MF_MT_MAJOR_TYPE"
    );

    HR_CHECK(
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32),
        "Failed to set MF_MT_SUBTYPE to ARGB32"
    );

    // Request the video processor to scale the video output to our swap chain dimensions.
    if (targetWidth > 0 && targetHeight > 0) {
        HR_CHECK(
            MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, targetWidth, targetHeight),
            "Failed to set MF_MT_FRAME_SIZE"
        );
        LOG_INFO("VideoDecoder::Init — requesting video scale to target dimensions: %u x %u", targetWidth, targetHeight);
    }

    HR_CHECK(
        m_reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            NULL,
            outputType.Get()),
        "Failed to set output media type to ARGB32"
    );

    LOG_INFO("VideoDecoder::Init — pipeline configured (ARGB32 output, DXVA2 HW decode)");
    return S_OK;
}

// ---------------------------------------------------------------------------
// ReadFrame — decode the next video frame and return the GPU texture.
//
// This is called once per timer tick from the render thread.
// No dynamic allocation occurs here.
//
// The returned texture is part of the DXVA2 decode surface pool (a D3D11
// texture array). The subresource index identifies which slice of the array
// contains this frame. The caller MUST copy the texture before the next
// ReadFrame() call, because the decoder may reuse the surface.
// ---------------------------------------------------------------------------
FrameResult VideoDecoder::ReadFrame(ID3D11Texture2D** outTexture, UINT* outSubresource) {
    if (!m_reader || !outTexture || !outSubresource) return FrameResult::Error;

    *outTexture     = nullptr;
    *outSubresource = 0;

    DWORD  streamFlags = 0;
    ComPtr<IMFSample> sample;

    DWORD actualStreamIndex = 0;
    LONGLONG timestamp = 0;
    HRESULT hr = m_reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
        0,           // no flags
        &actualStreamIndex,
        &streamFlags,
        &timestamp,
        &sample
    );

    if (FAILED(hr)) {
        LOG_ERROR("VideoDecoder::ReadFrame — ReadSample failed (hr=0x%08X)",
                  static_cast<unsigned>(hr));
        return FrameResult::Error;
    }

    // Check for end of stream
    if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
        return FrameResult::EndOfStream;
    }

    // Transient condition: no sample available this call
    if (!sample) {
        return FrameResult::NoSample;
    }

    // Extract the D3D11 texture from the sample's media buffer.
    // Chain: IMFSample → IMFMediaBuffer → IMFDXGIBuffer → ID3D11Texture2D
    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) {
        LOG_ERROR("VideoDecoder::ReadFrame — GetBufferByIndex failed (hr=0x%08X)",
                  static_cast<unsigned>(hr));
        return FrameResult::Error;
    }

    ComPtr<IMFDXGIBuffer> dxgiBuffer;
    hr = buffer.As(&dxgiBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("VideoDecoder::ReadFrame — QueryInterface for IMFDXGIBuffer failed (hr=0x%08X)",
                  static_cast<unsigned>(hr));
        return FrameResult::Error;
    }

    // GetResource returns the ID3D11Texture2D from the DXVA2 decode pool.
    // This is a texture array; the subresource index tells us which slice.
    hr = dxgiBuffer->GetResource(IID_PPV_ARGS(outTexture));
    if (FAILED(hr)) {
        LOG_ERROR("VideoDecoder::ReadFrame — GetResource failed (hr=0x%08X)",
                  static_cast<unsigned>(hr));
        return FrameResult::Error;
    }

    hr = dxgiBuffer->GetSubresourceIndex(outSubresource);
    if (FAILED(hr)) {
        LOG_ERROR("VideoDecoder::ReadFrame — GetSubresourceIndex failed (hr=0x%08X)",
                  static_cast<unsigned>(hr));
        SafeRelease(*outTexture);
        return FrameResult::Error;
    }

    return FrameResult::Success;
}

// ---------------------------------------------------------------------------
// SeekToStart — rewind to the beginning for seamless video looping.
//
// Uses a PROPVARIANT with VT_I8 and value 0 to seek to position 0.
// GUID_NULL tells MF to interpret the position in the source's native
// time format (100-nanosecond units for MP4).
// ---------------------------------------------------------------------------
HRESULT VideoDecoder::SeekToStart() {
    if (!m_reader) return E_UNEXPECTED;

    PROPVARIANT pos = {};
    pos.vt = VT_I8;
    pos.hVal.QuadPart = 0;

    HR_CHECK(
        m_reader->SetCurrentPosition(GUID_NULL, pos),
        "VideoDecoder::SeekToStart — SetCurrentPosition failed"
    );

    LOG_INFO("VideoDecoder::SeekToStart — rewound to beginning");
    return S_OK;
}

// ---------------------------------------------------------------------------
// Shutdown — release all Media Foundation resources.
// ---------------------------------------------------------------------------
void VideoDecoder::Shutdown() {
    m_reader.Reset();
    m_dxgiManager.Reset();
    m_resetToken = 0;

    LOG_INFO("VideoDecoder::Shutdown — MF resources released");
}

} // namespace lw
