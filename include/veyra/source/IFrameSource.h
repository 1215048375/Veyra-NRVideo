#pragma once

// IFrameSource - the unified input contract (Playbook R2/R7). Sources
// produce pipeline::FramePacket metadata plus the decoded frame view; the
// graph's ingress stage owns the single color conversion into the canonical
// linear RGBA16F working texture (Product Spec 7).
#include <cstdint>
#include <string>

#include "veyra/pipeline/FramePacket.h"

struct AVFrame;

namespace veyra::source {

namespace pipeline = veyra::pipeline;

struct SourceOpenDesc {
    std::wstring path;
    bool preferHardwareDecode = true; // D3D12VA first, software fallback explicit
    void* d3d12Device = nullptr;      // ID3D12Device* when hardware decode is wanted
    void* d3d12Queue = nullptr;       // ID3D12CommandQueue*
};

struct SourceInfo {
    bool opened = false;
    pipeline::SourceKind kind = pipeline::SourceKind::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    pipeline::Rational duration;
    double averageFps = 0.0;         // informational only; the pipeline is PTS-driven
    bool hardwareDecodeActive = false;
    pipeline::ColorDescription color;
};

enum class SourceReadStatus : uint8_t {
    Frame = 0,  // packet metadata + decoded frame valid
    Eos,        // source fully drained
    Error,
};

class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    virtual bool open(const SourceOpenDesc& desc) = 0;
    virtual const SourceInfo& info() const = 0;

    // Reads the next frame. On Frame, `out` carries the full packet metadata
    // and `decodedFrame` points at the FFmpeg frame (owned by the source,
    // valid until the next read). The ingress stage converts it.
    virtual SourceReadStatus read(pipeline::FramePacket& out, const AVFrame** decodedFrame) = 0;

    // Unsupported for capture/image sources. Atomic: demuxer seek + decoder
    // flush; the next read carries the Seek flag and a fresh epoch.
    virtual bool seek(const pipeline::Rational& targetSeconds) = 0;

    virtual void close() noexcept = 0;
};

} // namespace veyra::source
