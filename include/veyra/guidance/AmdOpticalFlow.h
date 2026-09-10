#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>

namespace veyra::guidance {
// Records FidelityFX OF on the application's queue. The caller drains that
// queue before destruction; input/output return to their incoming states.
class AmdOpticalFlow {
public:
    AmdOpticalFlow();
    ~AmdOpticalFlow();
    bool initialize(ID3D12Device*,uint32_t width,uint32_t height);
    bool dispatch(ID3D12GraphicsCommandList*,ID3D12Resource* color,bool reset);
    ID3D12Resource* vectors() const;
    ID3D12Resource* sceneChanges() const;
    bool warmedUp() const;
    uint32_t width() const;
    uint32_t height() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
