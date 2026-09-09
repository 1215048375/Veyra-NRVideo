#pragma once
#include <d3d12.h>
#include <memory>
#include <array>
namespace veyra::gfx {class D3D12DeviceContext;}
namespace veyra::ngx {
// Independent adapter to locally installed NvOFFRUC, with GPU-only shared RGBA8
// surfaces. Each fractional timestamp has its own temporal FRUC instance.
class FrucBackend {
public:
    FrucBackend();~FrucBackend();
    bool initialize(gfx::D3D12DeviceContext&,unsigned width,unsigned height,unsigned multiplier);
    bool reset(); // caller drains graph consumers before resetting history
    void recordInput(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned parity);
    bool execute(unsigned parity,double previousMs,double currentMs,std::array<bool,3>& repeated);
    void recordOutput(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned subframe);
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
