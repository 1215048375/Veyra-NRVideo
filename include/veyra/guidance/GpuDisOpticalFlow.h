#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>

namespace veyra::guidance {
// GPU-only adapter. Two slots are retired by EnhanceGraph's upload fences.
// Caller supplies COMMON inputs/outputs and signals completion after submission.
class GpuDisOpticalFlow {
public:
    GpuDisOpticalFlow();
    ~GpuDisOpticalFlow();
    bool initialize(ID3D12Device*, uint32_t width, uint32_t height);
    bool dispatch(ID3D12GraphicsCommandList*, ID3D12CommandQueue*, ID3D12Fence*,
                  uint64_t completion, uint32_t slot, ID3D12Resource* previous,
                  ID3D12Resource* current, ID3D12Resource* motion, ID3D12Resource* confidence,
                  uint64_t previousId, uint64_t currentId);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
