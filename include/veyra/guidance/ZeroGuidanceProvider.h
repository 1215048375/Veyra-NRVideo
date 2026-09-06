#pragma once

// ZeroGuidanceProvider - the honest fallback guidance producer (Product Spec
// 8.1: Zero is the LAST fallback, never a substitute for NVOF). Owns the three
// guidance textures at the working extent and zero-initializes them via an
// upload-buffer copy (the ClearUnorderedAccessViewFloat-segfault-free path
// proven in Phase 1). Every produced GuidanceFrame reports provenance=Zero so
// gates/UI/logs can never mistake it for real motion.
#include <cstdint>

#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/guidance/IGuidanceProvider.h"

namespace veyra::guidance {

namespace pipeline = veyra::pipeline; // qualified access to the data contracts

class ZeroGuidanceProvider final : public IGuidanceProvider {
public:
    ZeroGuidanceProvider(gfx::D3D12DeviceContext& context);
    ~ZeroGuidanceProvider() override;

    bool initialize(const GuidanceConfig& config) override;
    bool produce(const pipeline::FrameWindow& window, pipeline::GuidanceFrame& out) override;
    void reset(uint64_t epoch, pipeline::ResetReason reason) noexcept override;
    pipeline::GuidanceProvenance provenance() const noexcept override {
        return pipeline::GuidanceProvenance::Zero;
    }

    // Diagnostic (tests only): textures are valid and sized to the extent.
    bool resourcesReady() const { return ready_; }
    uint32_t workingWidth() const { return width_; }
    uint32_t workingHeight() const { return height_; }

private:
    bool createZeroedTexture(unsigned pixelBytesPerPixel, DXGI_FORMAT format,
        gfx::ComPtr<ID3D12Resource>& out);

    gfx::D3D12DeviceContext& context_;
    GuidanceConfig config_{};
    gfx::ComPtr<ID3D12Resource> motion_;
    gfx::ComPtr<ID3D12Resource> depth_;
    gfx::ComPtr<ID3D12Resource> confidence_;
    gfx::ComPtr<ID3D12Resource> upload_;
    bool ready_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t epoch_ = 1;
    uint64_t producedFrames_ = 0;
};

} // namespace veyra::guidance
