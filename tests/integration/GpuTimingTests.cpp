#include "veyra/diagnostics/GpuTimer.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <iostream>

int main(){
    using namespace veyra;
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status=Status::Ok;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    diagnostics::GpuTimer timer;if(!timer.initialize(ctx.device(),ctx.directQueue()))return 2;timer.recordCompleted();
    Microsoft::WRL::ComPtr<ID3D12Fence> hold;
    if(FAILED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&hold))))return 2;
    if(FAILED(ctx.directQueue()->Wait(hold.Get(),1)))return 2;
    struct ReleaseHold {ID3D12Fence* fence;~ReleaseHold(){fence->Signal(1);}} release{hold.Get()};
    int failures=0;
    auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';failures+=!ok;};
    auto submit=[&](uint64_t sequence){
        uint32_t slot=0;auto* list=ring.acquireNext(slot,status);if(!list)return false;
        timer.frame({7,3,sequence},ctx.fence());
        timer.mark(list,diagnostics::GpuStage::Color);timer.mark(list,diagnostics::GpuStage::Color,true);timer.resolve(list);
        if(!ring.submitAndSignal(slot))return false;timer.submitted(ring.lastSignaledValue());return true;
    };
    // Five submissions share one source id, like generated frame blits. Keep
    // the GPU held so query-slot reuse cannot accidentally pass by completing.
    for(unsigned i=0;i<5;++i)if(!submit(8))return 2;
    timer.collect(ctx.fence());check(timer.takeCompleted().empty(),"incomplete GPU queries are never reported as completed");
    check(timer.skipped()==1,"four free query slots are used despite identical source ids; fifth is counted missing");
    if(FAILED(hold->Signal(1))||!ring.waitIdle())return 2;
    timer.collect(ctx.fence());const auto completed=timer.takeCompleted();
    bool all=completed.size()==4;
    for(const auto& sample:completed){const auto& color=sample.gpu[size_t(diagnostics::GpuStage::Color)];all&=sample.identity.epoch==7&&sample.identity.settingsRevision==3&&sample.identity.sourceFrameId==8&&color.state==diagnostics::SampleState::Measured&&color.frequency>0&&color.milliseconds.has_value();}
    check(all,"all four completed frames retain actual timestamps and identities");
    timer.collect(ctx.fence());check(timer.takeCompleted().empty(),"completed timing records are consumed exactly once");
    for(unsigned i=0;i<70;++i){if(!submit(20+i)||!ring.waitIdle())return 2;timer.collect(ctx.fence());}
    const auto bounded=timer.takeCompleted();
    check(bounded.size()==64&&timer.overflow()==6&&bounded.front().identity.sourceFrameId==26&&bounded.back().identity.sourceFrameId==89,"undrained completion records remain bounded with explicit overflow accounting");
    timer.close();check(timer.takeCompleted().empty()&&timer.skipped()==0&&timer.overflow()==0,"timer close clears old resources and delivery state");
    return failures?1:0;
}
