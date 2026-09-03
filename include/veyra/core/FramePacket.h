#pragma once

// Unified frame packet  the single data unit flowing through the
// EnhanceGraph. Every stage (decodeSRparityNRparityFGcomposite)
// consumes and produces FramePacket instances.
#include <cstdint>
#include <d3d12.h>

namespace veyra::core {

enum class FrameSource : uint8_t {
    Unknown = 0,
    File,        // FFmpeg demux/decode
    CaptureCard, // physical capture
    TestPattern, // synthetic harness input
};

struct FramePacket {
    uint64_t frameId = 0;         // monotonic per source epoch
    uint64_t ptsUs = 0;           // presentation timestamp
    uint64_t durationUs = 0;      // frame duration from container/cadence
    uint32_t width = 0;
    uint32_t height = 0;
    FrameSource source = FrameSource::Unknown;
    uint64_t sourceEpoch = 0;     // increment on source switch/seek/device lost

    // GPU resources (owned by the graph's resource pool, not by the packet).
    ID3D12Resource* linearColor = nullptr; // linear BT.709 working RGB (FP16)
    ID3D12Resource* proxy = nullptr;       // parity-encoded RGBA8
    ID3D12Resource* neural = nullptr;      // Feature 18 raw output (RGBA8)
    ID3D12Resource* final = nullptr;       // parity-decoded final (FP16)

    // GPU timing (filled by the graph after submission).
    uint64_t gpuTimestampBegin = 0;
    uint64_t gpuTimestampEnd = 0;

    bool valid() const { return frameId > 0 && linearColor != nullptr && width > 0 && height > 0; }
};

} // namespace veyra::core
