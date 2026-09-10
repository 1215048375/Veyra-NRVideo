#include "veyra/guidance/AmdOpticalFlow.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/Log.h"
#ifdef VEYRA_HAS_AMD_OF
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#endif

namespace veyra::guidance {
struct AmdOpticalFlow::Impl {
    uint32_t w=0,h=0;
    uint32_t framesSinceReset=0;
    ID3D12Device* device=nullptr;
    pipeline::ComPtr<ID3D12Resource> flow,scd;
#ifdef VEYRA_HAS_AMD_OF
    FfxOpticalflowContext context{};
    std::vector<uint8_t> scratch;
    bool created=false;
    ~Impl(){if(created)log::info("amd-of",std::format("Destroy result={}",unsigned(ffxOpticalflowContextDestroy(&context))));}
    bool check(FfxErrorCode r,const char* operation){log::info("amd-of",std::format("{} result={}",operation,unsigned(r)));return r==FFX_OK;}
#endif
};
AmdOpticalFlow::AmdOpticalFlow():p_(std::make_unique<Impl>()){}
AmdOpticalFlow::~AmdOpticalFlow()=default;
bool AmdOpticalFlow::initialize(ID3D12Device* device,uint32_t w,uint32_t h){
#ifdef VEYRA_HAS_AMD_OF
    auto& p=*p_;
    if(!device||!w||!h||w>16384||h>16384||p.created){log::error("amd-of","invalid initialization contract");return false;}
    p.w=w;p.h=h;p.device=device;
    p.scratch.resize(ffxGetScratchMemorySizeDX12(1));
    FfxOpticalflowContextDescription desc{};desc.resolution={w,h};
    if(!p.check(ffxGetInterfaceDX12(&desc.backendInterface,ffxGetDeviceDX12(device),p.scratch.data(),p.scratch.size(),1),"Interface"))return false;
    if(!p.check(ffxOpticalflowContextCreate(&p.context,&desc),"Create"))return false;p.created=true;
    FfxOpticalflowSharedResourceDescriptions resources{};
    if(!p.check(ffxOpticalflowGetSharedResourceDescriptions(&p.context,&resources),"Resources"))return false;
    auto allocate=[&](const FfxCreateResourceDescription& r){return pipeline::makeTexture(device,r.resourceDescription.width,r.resourceDescription.height,ffxGetDX12FormatFromSurfaceFormat(r.resourceDescription.format),true);};
    p.flow=allocate(resources.opticalFlowVector);p.scd=allocate(resources.opticalFlowSCD);
    log::info("amd-of",std::format("input={}x{} raw={}x{} grid=8 integer-pixel motion",w,h,width(),height()));
    return p.flow&&p.scd;
#else
    (void)device;(void)w;(void)h;log::error("amd-of","FidelityFX SDK not compiled into this build");return false;
#endif
}
bool AmdOpticalFlow::dispatch(ID3D12GraphicsCommandList* list,ID3D12Resource* color,bool reset){
#ifdef VEYRA_HAS_AMD_OF
    auto& p=*p_;if(!p.created||!list||!color||!p.flow||!p.scd)return false;
    const auto cd=color->GetDesc();pipeline::ComPtr<ID3D12Device> owner,listOwner;
    if(cd.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||cd.Width!=p.w||cd.Height!=p.h||cd.DepthOrArraySize!=1||cd.MipLevels!=1||cd.SampleDesc.Count!=1||cd.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||FAILED(color->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=p.device||FAILED(list->GetDevice(IID_PPV_ARGS(&listOwner)))||listOwner.Get()!=p.device){
        log::error("amd-of","dispatch input extent/format/device mismatch");return false;
    }
    FfxOpticalflowDispatchDescription d{};d.commandList=ffxGetCommandListDX12(list);
    d.color=ffxGetResourceDX12(color,ffxGetResourceDescriptionDX12(color),L"Veyra OF color",FFX_RESOURCE_STATE_COMMON);
    d.opticalFlowVector=ffxGetResourceDX12(p.flow.Get(),ffxGetResourceDescriptionDX12(p.flow.Get()),L"Veyra OF vectors",FFX_RESOURCE_STATE_COMMON);
    d.opticalFlowSCD=ffxGetResourceDX12(p.scd.Get(),ffxGetResourceDescriptionDX12(p.scd.Get()),L"Veyra OF cut",FFX_RESOURCE_STATE_COMMON);
    d.reset=reset;d.backbufferTransferFunction=0;d.minMaxLuminance={0,1};
    const auto result=ffxOpticalflowContextDispatch(&p.context,&d);
    if(result==FFX_OK)p.framesSinceReset=reset?1:std::min(p.framesSinceReset+1,7u);
    if(result!=FFX_OK||log::verboseFrameLogs()||reset)log::info("amd-of",std::format("Dispatch reset={} result={}",reset,unsigned(result)));
    return result==FFX_OK;
#else
    (void)list;(void)color;(void)reset;return false;
#endif
}
ID3D12Resource* AmdOpticalFlow::vectors()const{return p_->flow.Get();}
ID3D12Resource* AmdOpticalFlow::sceneChanges()const{return p_->scd.Get();}
bool AmdOpticalFlow::warmedUp()const{return p_->framesSinceReset>=7;}
uint32_t AmdOpticalFlow::width()const{return p_->flow?uint32_t(p_->flow->GetDesc().Width):0;}
uint32_t AmdOpticalFlow::height()const{return p_->flow?p_->flow->GetDesc().Height:0;}
}
