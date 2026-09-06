#pragma once

// IGuidanceProvider - the guidance producer contract (Playbook R2/R4).
// Providers run on the render thread's command context; motion is always
// current -> previous in post-SR workingExtent pixels, depth is R32F
// normalized relative depth, confidence is R8_UNORM (1 trust, 0 reject).
#include <cstdint>

#include "veyra/pipeline/FramePacket.h"
#include "veyra/pipeline/FrameWindow.h"
#include "veyra/pipeline/GuidanceFrame.h"
#include "veyra/pipeline/ResetCoordinator.h"

namespace veyra::guidance {

namespace pipeline = veyra::pipeline; // qualified access to the data contracts

struct GuidanceConfig {
    uint32_t workingWidth = 0;    // post-SR extent (== source extent when SR bypasses)
    uint32_t workingHeight = 0;
    uint32_t lookaheadFrames = 0; // 0/1/2 bounded mode
    bool enableDepth = false;     // DAV2; V1 zero provider reports depth as zero
    uint32_t depthIntervalFrames = 1;
};

class IGuidanceProvider {
public:
    virtual ~IGuidanceProvider() = default;

    // Creates GPU resources for the configured working extent. The device and
    // command context are supplied by the owning graph (render thread only).
    virtual bool initialize(const GuidanceConfig& config) = 0;

    // Produces guidance for window.current. Must not consume history from a
    // different epoch (first frame after reset yields zero motion).
    virtual bool produce(const pipeline::FrameWindow& window, pipeline::GuidanceFrame& out) = 0;

    // Reset temporal state; providers drop history at the epoch boundary.
    virtual void reset(uint64_t epoch, pipeline::ResetReason reason) noexcept = 0;

    // Honest identity for logging/UI/gates.
    virtual pipeline::GuidanceProvenance provenance() const noexcept = 0;
};

} // namespace veyra::guidance
