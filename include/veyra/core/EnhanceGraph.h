#pragma once

// EnhanceGraph  the unified processing graph interface that chains
// decode  optional SR  parity encode  NR (Feature 18)  parity decode
//  optional FG  composite, consuming FrameWindow + GuidanceFrame.
#include <cstdint>
#include <memory>

#include "veyra/core/FramePacket.h"
#include "veyra/core/FrameWindow.h"
#include "veyra/core/GuidanceFrame.h"
#include "veyra/core/ResetCoordinator.h"

namespace veyra::core {

// Guidance provider interface  implemented by Zero, NVOF, and DAV2 backends.
class IGuidanceProvider {
public:
    virtual ~IGuidanceProvider() = default;

    // Called per frame. The provider fills the GuidanceFrame's resources
    // and metadata. Returns false on hard failure (caller should fall back).
    virtual bool provide(const FrameWindow& window, GuidanceFrame& out) = 0;

    // Provider identity for logging and honest reporting.
    virtual GuidanceProvider providerType() const = 0;

    // Reset temporal state (called on epoch change).
    virtual void reset() = 0;
};

// The unified enhancement graph. Concrete implementation wires the D3D12
// resources, NGX features, and parity shaders; this interface defines the
// contract that unit tests verify.
class IEnhanceGraph {
public:
    virtual ~IEnhanceGraph() = default;

    // Process one frame window through the full chain.
    // The output packet's `final` resource contains the enhanced frame.
    virtual bool process(const FrameWindow& window, const GuidanceFrame& guidance,
                         FramePacket& outPacket) = 0;

    // Get the reset coordinator for this graph.
    virtual ResetCoordinator& resetCoordinator() = 0;

    // Metrics for the debug panel and gate.
    struct Metrics {
        uint64_t framesProcessed = 0;
        uint64_t nrEvaluateSuccess = 0;
        uint64_t srEvaluateSuccess = 0;
        uint64_t gpuReadbackCount = 0;
        uint64_t resetCount = 0;
    };
    virtual const Metrics& metrics() const = 0;
};

// Factory for the concrete implementation.
std::unique_ptr<IEnhanceGraph> createEnhanceGraph();

} // namespace veyra::core
