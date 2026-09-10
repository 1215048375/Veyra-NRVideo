#pragma once
#include "veyra/diagnostics/GpuTimer.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/engine/PreviewView.h"
#include <chrono>
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::pipeline { class EnhanceGraph; }
namespace veyra::sink { struct RgbaImage; }
namespace veyra::engine {
class VideoPresenter {
public:
    bool open(gfx::D3D12DeviceContext&, HWND, pipeline::EnhanceGraph&);
    bool present(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,unsigned slot,bool generated,bool referencesValid=true,int comparison=0,bool baseReference=false,float split=.5f,pipeline::FrameIdentity identity={},PreviewView view={});
    void close();
    // Explicit integration-test capture only; never called by playback/export.
    bool readPresentedFrameForTest(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,sink::RgbaImage&);
    uint64_t submittedCount() const {return sink_.presentCount();}
diagnostics::GpuSample blitTiming(ID3D12Fence* f,uint64_t revision=0){gpuTimer_.collect(f);if(revision&&gpuTimer_.last().identity.settingsRevision!=revision){diagnostics::GpuSample pending;pending.state=diagnostics::SampleState::Pending;return pending;}return gpuTimer_.last().gpu[size_t(diagnostics::GpuStage::Blit)];}
private:
    diagnostics::GpuTimer gpuTimer_;
    gfx::PresentSink sink_;
    pipeline::GraphicsPass pass_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvs_;
    unsigned inc_=0;
    unsigned lastBuffer_=0;bool hasPresented_=false;
    HWND window_=nullptr;
    std::chrono::steady_clock::time_point lastResize_{};
    void refresh(ID3D12Device*);
};
}
