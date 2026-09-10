#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>

namespace veyra::gfx {
class XessPresenter {
public:
    XessPresenter();
    ~XessPresenter();
    bool initialize(ID3D12Device*,ID3D12CommandQueue*,IDXGIFactory2*,HWND,const DXGI_SWAP_CHAIN_DESC1&,IDXGISwapChain3**);
    bool beginFrame();
    bool tag(ID3D12GraphicsCommandList*,ID3D12Resource* color,ID3D12Resource* motion,ID3D12Resource* depth,
             RECT region,bool enabled,bool reset,float elapsedMs);
    bool beforePresent();
    bool afterPresent();
    uint64_t generatedCount() const;
    uint64_t presentedCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
