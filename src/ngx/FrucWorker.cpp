#include "veyra/ngx/FrucWorkerProtocol.h"

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <filesystem>
#include <format>
#include <mutex>
#pragma warning(push,0)
#include <NvOFFRUC.h>
#include <cuda.h>
#pragma warning(pop)
namespace veyra::ngx {
using Microsoft::WRL::ComPtr;
namespace {
bool hrOk(HRESULT hr,const char* op){log::info("fruc",std::format("{} hr=0x{:X}",op,unsigned(hr)));return SUCCEEDED(hr);}
template<class Fn, class... Args>
NvOFFRUC_STATUS callSafe(unsigned& seh, Fn fn, Args... args) {
    __try { return fn(args...); }
    __except(EXCEPTION_EXECUTE_HANDLER) { seh=GetExceptionCode(); return NvOFFRUC_ERR_GENERIC; }
}
template<class Fn, class... Args>
bool checkedCall(const char* operation, Fn fn, Args... args) {
    unsigned seh=0;
    const auto result=callSafe(seh,fn,args...);
    log::info("fruc",std::format("{} result={} seh={}",operation,int(result),seh));
    return result==NvOFFRUC_SUCCESS && !seh;
}
bool cudaOk(CUresult result,const char* op) {
    if(result!=CUDA_SUCCESS)log::error("fruc",std::format("{} cudaResult={}",op,int(result)));
    return result==CUDA_SUCCESS;
}

}
struct WorkerKernel {
    using GetContext = CUresult (CUDAAPI*)(CUcontext*);
    using SetContext = CUresult (CUDAAPI*)(CUcontext);
    HMODULE cuda=nullptr; GetContext getContext=nullptr; SetContext setContext=nullptr;
    CUcontext streamContext[3]={};
    HMODULE lib=nullptr;unsigned width=0,height=0,count=0;uint64_t serial=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Fence> fence11;
    ComPtr<ID3D11Texture2D> tex[5];
    NvOFFRUCHandle handles[3]={};bool registered[3]={};
    PtrToFuncNvOFFRUCCreate create=nullptr;PtrToFuncNvOFFRUCDestroy destroy=nullptr;
    PtrToFuncNvOFFRUCRegisterResource reg=nullptr;PtrToFuncNvOFFRUCUnregisterResource unreg=nullptr;PtrToFuncNvOFFRUCProcess process=nullptr;
    struct ContextScope {
        WorkerKernel& owner;
        CUcontext saved=nullptr;
        bool valid=false;
        explicit ContextScope(WorkerKernel& p):owner(p) {
            valid=p.getContext && cudaOk(p.getContext(&saved),"GetContext");
        }
        ~ContextScope() { if(valid)cudaOk(owner.setContext(saved),"RestoreContext"); }
    };
    void release() {
        ContextScope scope(*this);
        for(unsigned index=3;index>0;--index) {
            const unsigned j=index-1;
            if(!handles[j])continue;
            if(scope.saved==streamContext[j])scope.saved=nullptr;
            if(setContext&&streamContext[j])cudaOk(setContext(streamContext[j]),"ReleaseContext");
            if(registered[j]) {
                NvOFFRUC_UNREGISTER_RESOURCE_PARAM r{};
                r.uiCount=3;r.pArrResource[0]=tex[0].Get();r.pArrResource[1]=tex[1].Get();r.pArrResource[2]=tex[2+j].Get();
                checkedCall("Unregister",unreg,handles[j],&r);
                registered[j]=false;
            }
            checkedCall("Destroy",destroy,handles[j]);
            handles[j]=nullptr;streamContext[j]=nullptr;
        }
    }
    ~WorkerKernel() {
        release();
        for(auto& t:tex)t.Reset();
        fence11.Reset();context.Reset();device5.Reset();device.Reset();
        if(lib)FreeLibrary(lib);
        if(cuda)FreeLibrary(cuda);
    }
    bool initialize(FrucWorkerMessage&);
    bool execute(FrucWorkerMessage&);
    bool createStreams() {
        ContextScope scope(*this);
        if(!scope.valid)return false;
        for(unsigned j=0;j<count;++j) {
            NvOFFRUC_CREATE_PARAM c{};
            c.uiWidth=width;c.uiHeight=height;c.pDevice=device.Get();
            c.eResourceType=DirectX11Resource;c.eSurfaceFormat=ARGBSurface;c.eCUDAResourceType=CudaResourceTypeUndefined;
            if(!checkedCall("Create",create,&c,&handles[j])||!handles[j])return false;
            // This local runtime leaves each newly created stream's CUDA context
            // current. Preserve it and explicitly bind it for subsequent calls.
            if(!cudaOk(getContext(&streamContext[j]),"StreamContext")||!streamContext[j])return false;
            log::info("fruc",std::format("Context stream={} ptr={}",j,static_cast<void*>(streamContext[j])));
            NvOFFRUC_REGISTER_RESOURCE_PARAM r{};
            r.uiCount=3;r.pD3D11FenceObj=fence11.Get();
            r.pArrResource[0]=tex[0].Get();r.pArrResource[1]=tex[1].Get();r.pArrResource[2]=tex[j+2].Get();
            if(!checkedCall("Register",reg,handles[j],&r))return false;
            registered[j]=true;
            if(!cudaOk(setContext(scope.saved),"RestoreAfterCreate"))return false;
        }
        return true;
    }
};
bool WorkerKernel::initialize(FrucWorkerMessage& info) {
    auto& p=*this;const auto w=info.width,h=info.height;p.width=w;p.height=h;p.count=info.multiplier-1;
    const std::wstring path=(runtime::localRuntimeDirectory()/L"NvOFFRUC.dll").make_preferred().wstring();FileIdentity identity;IdentityError error;
    if(!computeFileIdentity(path,identity,error)||!identity.signatureValid||!identity.signerIsNvidia||identity.sha256Upper!="5A0B6701D30709E25E7E5B92CA46B18AAB1459160CECD4F629872369D85C8B0A"){log::error("fruc","local runtime identity mismatch or unavailable");return false;}
    log::info("fruc",std::format("runtime sha256={} extent={}x{} instances={}",identity.sha256Upper,w,h,p.count));
    p.lib=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!p.lib){log::error("fruc",std::format("LoadLibrary error={}",GetLastError()));return false;}
    p.create=reinterpret_cast<PtrToFuncNvOFFRUCCreate>(GetProcAddress(p.lib,CreateProcName));p.destroy=reinterpret_cast<PtrToFuncNvOFFRUCDestroy>(GetProcAddress(p.lib,DestroyProcName));p.reg=reinterpret_cast<PtrToFuncNvOFFRUCRegisterResource>(GetProcAddress(p.lib,RegisterResourceProcName));p.unreg=reinterpret_cast<PtrToFuncNvOFFRUCUnregisterResource>(GetProcAddress(p.lib,UnregisterResourceProcName));p.process=reinterpret_cast<PtrToFuncNvOFFRUCProcess>(GetProcAddress(p.lib,ProcessProcName));if(!p.create||!p.destroy||!p.reg||!p.unreg||!p.process)return false;
    wchar_t systemDir[MAX_PATH]{};if(!GetSystemDirectoryW(systemDir,MAX_PATH))return false;
    const auto cudaPath=std::filesystem::path(systemDir)/L"nvcuda.dll";
    p.cuda=LoadLibraryExW(cudaPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!p.cuda)return false;
    p.getContext=reinterpret_cast<WorkerKernel::GetContext>(GetProcAddress(p.cuda,"cuCtxGetCurrent"));
    p.setContext=reinterpret_cast<WorkerKernel::SetContext>(GetProcAddress(p.cuda,"cuCtxSetCurrent"));if(!p.getContext||!p.setContext)return false;
    using InitCuda=CUresult(CUDAAPI*)(unsigned);auto initCuda=reinterpret_cast<InitCuda>(GetProcAddress(p.cuda,"cuInit"));if(!initCuda||!cudaOk(initCuda(0),"InitCuda"))return false;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;if(!hrOk(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory")||!hrOk(factory->EnumAdapterByLuid(info.adapter,IID_PPV_ARGS(&adapter)),"SameAdapter"))return false;
    if(!hrOk(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&p.device,nullptr,&p.context),"D3D11Device")||!hrOk(p.device.As(&p.device5),"Device5"))return false;

    if(!hrOk(device5->OpenSharedFence(reinterpret_cast<HANDLE>(info.fence),IID_PPV_ARGS(&fence11)),"OpenFence"))return false;
    for(unsigned j=0;j<count+2;++j)
        if(!hrOk(device5->OpenSharedResource1(reinterpret_cast<HANDLE>(info.textures[j]),IID_PPV_ARGS(&tex[j])),"OpenTexture"))return false;
    return createStreams();
}
bool WorkerKernel::execute(FrucWorkerMessage& info) {
    ContextScope scope(*this);if(!scope.valid)return false;
    auto wait=info.inputFence;
    for(unsigned j=0;j<count;++j) {
        if(!cudaOk(setContext(streamContext[j]),"ProcessContext"))return false;
        NvOFFRUC_PROCESS_IN_PARAMS in{};NvOFFRUC_PROCESS_OUT_PARAMS out{};
        in.stFrameDataInput.pFrame=tex[info.parity].Get();in.stFrameDataInput.nTimeStamp=info.currentMs;
        in.uSyncWait.FenceWaitValue.uiFenceValueToWaitOn=wait;
        out.stFrameDataOutput.pFrame=tex[j+2].Get();
        out.stFrameDataOutput.nTimeStamp=info.previousMs+(info.currentMs-info.previousMs)*double(j+1)/(count+1);
        out.stFrameDataOutput.bHasFrameRepetitionOccurred=&info.repeated[j];
        out.uSyncSignal.FenceSignalValue.uiFenceValueToSignalOn=++wait;
        unsigned seh=0;auto result=callSafe(seh,process,handles[j],&in,&out);
        info.result=uint32_t(result);info.seh=seh;
        if(result!=NvOFFRUC_SUCCESS||seh||log::verboseFrameLogs())log::info("fruc",std::format("Process inputMs={} outputMs={} sub={} result={} seh={} repeated={}",info.currentMs,out.stFrameDataOutput.nTimeStamp,j+1,int(result),seh,info.repeated[j]));
        if(result!=NvOFFRUC_SUCCESS||seh)return false;
    }
    info.outputFence=wait;return true;
}
} // namespace veyra::ngx
int wmain(int argc,wchar_t** argv) {
    using namespace veyra;using namespace veyra::ngx;
    if(argc!=2)return 2;
    auto mapping=reinterpret_cast<HANDLE>(_wcstoui64(argv[1],nullptr,10));
    auto* info=static_cast<FrucWorkerMessage*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(FrucWorkerMessage)));
    if(!info||info->version!=1||info->multiplier<2||info->multiplier>4||!info->width||!info->height)return 2;
    Logger::instance().setConsoleEnabled(false);
    if(!Logger::instance().openFile(info->logPath))return 2;
    const auto response=reinterpret_cast<HANDLE>(info->response),request=reinterpret_cast<HANDLE>(info->request);
    int exitCode=0;
    {
        WorkerKernel kernel;
        bool ready=kernel.initialize(*info);info->result=ready?0:1;
        if(!SetEvent(response))return 2;
        if(ready)for(;;) {
            if(WaitForSingleObject(request,INFINITE)!=WAIT_OBJECT_0){exitCode=2;break;}
            if(info->command==2)break;
            if(info->command!=1||info->parity>1){exitCode=2;break;}
            info->result=1;info->seh=0;
            const bool ok=kernel.execute(*info);
            if(!ok&&info->result==0)info->result=1;
            if(!SetEvent(response)){exitCode=2;break;}
            if(!ok){exitCode=1;break;}
        }
        else exitCode=1;
    }
    Logger::instance().flush();UnmapViewOfFile(info);CloseHandle(mapping);return exitCode;
}
