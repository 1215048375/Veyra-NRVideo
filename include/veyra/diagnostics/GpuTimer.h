#pragma once
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/Log.h"
#include <format>
namespace veyra::diagnostics {
// Timestamp query ring, resolved only after the producing fence is complete.
// No image readback, no CPU waits. Samples carry the identity they measured.
class GpuTimer {
    static constexpr unsigned stages=unsigned(GpuStage::Count),slots=4,stride=stages*2;
    struct Pending{pipeline::FrameIdentity id;uint32_t mask=0;uint64_t fence=0;};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    std::array<Pending,slots> pending_{};uint64_t frequency_=0;int active_=-1;
    FrameMetrics last_;
public:
    bool initialize(ID3D12Device* device,ID3D12CommandQueue* queue){
        close();HRESULT hr=queue->GetTimestampFrequency(&frequency_);if(FAILED(hr)||!frequency_)return false;
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=slots*stride;hr=device->CreateQueryHeap(&q,IID_PPV_ARGS(&heap_));if(FAILED(hr)){log::error("gpu-timestamp",std::format("query heap hr=0x{:X}",unsigned(hr)));return false;}
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=slots*stride*8;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr=device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_));if(FAILED(hr)){log::error("gpu-timestamp",std::format("readback hr=0x{:X}",unsigned(hr)));return false;}return true;
    }
    void collect(ID3D12Fence* fence){if(!readback_||!fence)return;const auto completed=fence->GetCompletedValue();
        for(unsigned slot=0;slot<slots;++slot){auto& p=pending_[slot];if(!p.fence||p.fence>completed)continue;void* data=nullptr;D3D12_RANGE range{slot*stride*8,(slot+1)*stride*8};auto hr=readback_->Map(0,&range,&data);if(FAILED(hr)){log::error("gpu-timestamp",std::format("Map hr=0x{:X}",unsigned(hr)));p.fence=0;continue;}
            FrameMetrics m;m.identity=p.id;auto values=static_cast<const uint64_t*>(data)+slot*stride;
            for(unsigned stage=0;stage<stages;++stage)if(p.mask&(1u<<stage)){auto& v=m.gpu[stage];v.begin=values[stage*2];v.end=values[stage*2+1];v.frequency=frequency_;if(v.end>=v.begin){v.state=SampleState::Measured;v.milliseconds=double(v.end-v.begin)*1000/frequency_;}else v.state=SampleState::Unavailable;
                log::info("gpu-timestamp",std::format("frame={} epoch={} revision={} stage={} begin={} end={} frequency={} ms={} (GPU queue timestamps)",p.id.sourceFrameId,p.id.epoch,p.id.settingsRevision,stage,v.begin,v.end,frequency_,v.milliseconds.value_or(-1)));
            }
            D3D12_RANGE written{0,0};readback_->Unmap(0,&written);if(m.identity.sourceFrameId>=last_.identity.sourceFrameId)last_=m;p.fence=0;
        }
    }
    void frame(pipeline::FrameIdentity id,ID3D12Fence* fence){collect(fence);active_=-1;if(!heap_||!readback_)return;auto i=unsigned(id.sourceFrameId%slots);if(pending_[i].fence)return;active_=int(i);pending_[i]={id,0,0};}
    void identity(pipeline::FrameIdentity id){if(active_>=0)pending_[active_].id=id;}
    void mark(ID3D12GraphicsCommandList* list,GpuStage stage,bool end=false){if(active_<0)return;unsigned i=unsigned(stage);list->EndQuery(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,active_*stride+i*2+unsigned(end));if(end)pending_[active_].mask|=1u<<i;}
    void resolve(ID3D12GraphicsCommandList* list){if(active_<0)return;auto mask=pending_[active_].mask;for(unsigned i=0;i<stages;++i)if(mask&(1u<<i)){unsigned index=active_*stride+i*2;list->ResolveQueryData(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,index,2,readback_.Get(),index*8);}}
    void submitted(uint64_t fence){if(active_>=0)pending_[active_].fence=fence;active_=-1;}
    const FrameMetrics& last()const{return last_;}
    void close(){heap_.Reset();readback_.Reset();pending_={};last_={};active_=-1;frequency_=0;}
};
}
