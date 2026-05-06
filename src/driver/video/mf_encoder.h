#pragma once

#include "video_encoder.h"

#if defined(_WIN32) || defined(WIN32)

#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <vector>
#include <chrono>

namespace opendriver::driver::video {

class MediaFoundationEncoder : public IVideoEncoder {
public:
    MediaFoundationEncoder();
    ~MediaFoundationEncoder() override;

    bool Initialize(const VideoConfig& config) override;
    void SetLogger(std::function<void(const char*)> logger) override;
    void Shutdown() override;
    bool EncodeFrame(void* texture_handle, std::vector<uint8_t>& out_packet) override;
    std::string GetLastError() const override;

private:
    void SetLastError(const std::string& error);
    bool EnsureSharedD3D11Device();
    bool InitializeD3D11AndDXGI(ID3D11Device* pDevice);
    bool SetupEncoderMFT(const VideoConfig& config);
    bool CreateSampleFromTexture(ID3D11Texture2D* texture, IMFSample** sampleOut);
    bool EnsureStagingTexture(const D3D11_TEXTURE2D_DESC& sourceDesc);
    bool ConvertTextureToNV12Sample(ID3D11Texture2D* sourceTexture,
                                    DXGI_FORMAT sourceFormat,
                                    IMFSample** sampleOut);
    std::vector<uint8_t>& GetPooledNV12Buffer(size_t required_size);
    bool ProcessOutput(std::vector<uint8_t>& out_packet);
    bool RefreshOutputMediaTypeState();
    bool AppendSampleAsAnnexB(IMFSample* sample, std::vector<uint8_t>& out_packet);
    void ResetDeviceResources();
    void RegisterEncodeFailure(const char* stage);
    void RegisterEncodeSuccess();
    bool IsInFailureCooldown() const;
    void MaybeLogTelemetry(double encode_ms, size_t packet_size_bytes, bool produced_output);

    VideoConfig m_config;
    bool m_initialized = false;
    uint64_t m_frameIndex = 0;

    // DXGI / MFT Resources
    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11Device1> m_d3dDevice1;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_dxManager;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTexture;
    Microsoft::WRL::ComPtr<ID3D11Query> m_query;
    UINT m_dxToken = 0;

    // The H264 Encoder Transform
    Microsoft::WRL::ComPtr<IMFTransform> m_encoderMFT;
    DWORD m_inputStreamId = 0;
    DWORD m_outputStreamId = 0;
    std::string m_lastError;
    std::vector<uint8_t> m_nv12Buffer;
    std::vector<uint8_t> m_nv12BufferPool[2];
    uint32_t m_poolIdx = 0;
    std::vector<uint8_t> m_sequenceHeaderAnnexB;
    uint32_t m_avccLengthFieldBytes = 4;
    bool m_sentSequenceHeader = false;
    bool m_isHardwareMFT = false;
    bool m_isNvidiaMFT = false;
    bool m_requiresNv12Input = false;
    bool m_allowZeroCopyInput = true;
    uint32_t m_zeroCopyFailures = 0;
    static constexpr uint32_t kZeroCopyFailureThreshold = 5;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_nv12StagingTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_nv12DefaultTexture;
    uint64_t m_inputFramesSubmitted = 0;
    uint64_t m_outputSamplesProduced = 0;
    uint64_t m_encodeAttempts = 0;
    uint64_t m_encodeFailures = 0;
    uint64_t m_processOutputFailures = 0;
    uint64_t m_processInputFailures = 0;
    uint64_t m_cooldownSkips = 0;
    uint64_t m_telemetryLogCounter = 0;
    uint32_t m_consecutiveEncodeFailures = 0;
    std::chrono::steady_clock::time_point m_cooldownUntil{};
    static constexpr uint32_t kFailureCooldownThreshold = 4;
    static constexpr uint32_t kFailureCooldownMs = 120;
    static constexpr uint32_t kTelemetryLogInterval = 240;
    std::function<void(const char*)> m_logger;
};

} // namespace opendriver::driver::video

#endif
