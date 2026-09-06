#pragma once
#include "veyra/gfx/PresentSink.h"
#include "veyra/pipeline/GpuPassUtils.h"
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::pipeline { class EnhanceGraph; }
namespace veyra::engine {
class VideoPresenter {
public:
    bool open(gfx::D3D12DeviceContext&, HWND, pipeline::EnhanceGraph&);
    bool present(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,unsigned slot,bool generated);
    void close();
private:
    gfx::PresentSink sink_;
    pipeline::GraphicsPass pass_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvs_;
    unsigned inc_=0;
    HWND window_=nullptr;
    void refresh(ID3D12Device*);
};
}
