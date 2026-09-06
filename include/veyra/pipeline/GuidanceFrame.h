#pragma once

// Pipeline GuidanceFrame - the unified motion/depth/confidence contract
// consumed by SR (V1: explicit Zero), NR (Feature 18), and FG (DLSSG).
// Direction: current -> previous, post-SR workingExtent pixels.
#include <cstdint>

#include "veyra/pipeline/FramePacket.h"
#include "veyra/pipeline/ResetCoordinator.h"

namespace veyra::pipeline {

enum class GuidanceProvenance : uint8_t {
    None = 0,
    Zero,       // honest fallback: all-zero motion/depth
    Nvof,       // NVIDIA Optical Flow motion + cost-derived confidence
    Dav2,       // DAV2 depth only (motion zero) - diagnostics/A-B
    NvofDav2,   // NVOF motion + DAV2 depth
};

inline const char* guidanceProvenanceName(GuidanceProvenance p) {
    switch (p) {
    case GuidanceProvenance::Zero: return "Zero";
    case GuidanceProvenance::Nvof: return "Nvof";
    case GuidanceProvenance::Dav2: return "Dav2";
    case GuidanceProvenance::NvofDav2: return "NvofDav2";
    default: return "None";
    }
}

struct GuidanceFrame {
    GpuTextureHandle motion;      // R16G16_FLOAT, current->previous, workingExtent px
    GpuTextureHandle depth;       // R32_FLOAT normalized relative depth
    GpuTextureHandle confidence;  // R8_UNORM, 1 = trust, 0 = reject

    GuidanceProvenance provenance = GuidanceProvenance::Zero;
    uint64_t sourceSequence = 0;  // FramePacket::sequence guidance was built from
    uint64_t sourceEpoch = 0;     // epoch at production time (stale check)

    // Depth produced at interval > 1: frames since the last inference result.
    uint32_t depthAgeFrames = 0;

    // Set when this guidance follows a reset boundary (first frame of a new
    // epoch): providers must not consume pre-reset history.
    bool requiresReset = false;
    ResetReason resetReason = ResetReason::None;

    bool valid() const {
        return motion.present() && depth.present() && confidence.present()
            && sourceSequence > 0 && provenance != GuidanceProvenance::None;
    }

    // Motion must match the working extent exactly; consumers never rescale
    // silently (the NGX adapter applies the documented consumer-side scale).
    bool extentsMatch(uint32_t width, uint32_t height) const {
        return motion.width == width && motion.height == height
            && depth.width == width && depth.height == height
            && confidence.width == width && confidence.height == height;
    }
};

} // namespace veyra::pipeline
