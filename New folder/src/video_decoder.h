#pragma once
// =============================================================================
// video_decoder.h — Media Foundation video decoder with DXVA2 acceleration
//
// Decodes H.264 MP4 video files using the Windows Media Foundation pipeline
// with DXVA2 hardware acceleration. Decoded frames arrive as ID3D11Texture2D
// objects in GPU memory, ready for direct blit to the swap chain.
//
// The decoder shares the D3D11 device with the renderer through the
// IMFDXGIDeviceManager, enabling zero-copy decode → present.
// =============================================================================

#include "utils.h"
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace lw {

// Result of a single frame read operation.
// The render loop uses this to decide what to do next.
enum class FrameResult {
    Success,      // Frame decoded and texture pointers are valid
    EndOfStream,  // Reached end of video — caller should seek to start for looping
    Error,        // Unrecoverable decode error
    NoSample      // No sample available this call (transient, try again next tick)
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    // Initialize the decoder pipeline.
    //   device:    the D3D11 device (shared with Renderer)
    //   videoPath: full path to the MP4 file
    //
    // Steps:
    //   1. Create DXGI device manager and bind the D3D11 device
    //   2. Configure source reader attributes for hardware decode
    //   3. Open the video file with MFCreateSourceReaderFromURL
    //   4. Select video stream only (no audio)
    //   5. Set output format to ARGB32 for direct B8G8R8A8 blit
    HRESULT Init(ID3D11Device* device, const wchar_t* videoPath, UINT targetWidth = 0, UINT targetHeight = 0);

    // Read the next decoded frame from the video.
    //   outTexture:      receives the ID3D11Texture2D pointer (caller does NOT release)
    //   outSubresource:  receives the subresource index within the texture array
    //
    // The texture is part of the DXVA2 decode pool and is only valid until
    // the next ReadFrame() call. The caller must copy/blit it before then.
    FrameResult ReadFrame(ID3D11Texture2D** outTexture, UINT* outSubresource);

    // Seek back to the beginning of the video for seamless looping.
    HRESULT SeekToStart();

    // Release all Media Foundation resources.
    void Shutdown();

private:
    ComPtr<IMFSourceReader>       m_reader;       // MF source reader
    ComPtr<IMFDXGIDeviceManager>  m_dxgiManager;  // Shares D3D11 device with MF
    UINT                          m_resetToken = 0; // Device manager reset token
};

} // namespace lw
