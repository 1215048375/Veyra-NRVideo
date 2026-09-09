#include "veyra/ngx/FrucBackend.h"
#include "veyra/ngx/FrucWorkerProtocol.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/Log.h"
#include <wrl/client.h>
#include <filesystem>
#include <format>
#include <vector>
namespace veyra::ngx {
using Microsoft::WRL::ComPtr;
namespace {
bool hrOk(HRESULT hr,const char* op) {
    if(FAILED(hr))log::error("fruc-ipc",std::format("{} hr=0x{:X}",op,unsigned(hr)));
    return SUCCEEDED(hr);
}
void barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value)CloseHandle(value);}
    void close(){if(value)CloseHandle(value);value=nullptr;}
};
}
struct FrucBackend::Impl {
    gfx::D3D12DeviceContext* ctx=nullptr;
    unsigned width=0,height=0,count=0;uint64_t serial=0;
    ComPtr<ID3D12Resource> shared[5];ComPtr<ID3D12Fence> fence;
    Handle textures[5],fenceHandle,mapping,request,response,process,job;
    FrucWorkerMessage* message=nullptr;
    ~Impl() {
        stop();if(message)UnmapViewOfFile(message);
    }
    void stop() {
        if(!process.value)return;
        message->command=2;SetEvent(request.value);
        if(WaitForSingleObject(process.value,3000)!=WAIT_OBJECT_0) {
            // Only this owned worker is terminated; no user processes are touched.
            TerminateJobObject(job.value,1);WaitForSingleObject(process.value,1000);
        }
        process.close();job.close();
    }
    bool acknowledge(DWORD timeout) {
        HANDLE events[]{response.value,process.value};
        const auto result=WaitForMultipleObjects(2,events,FALSE,timeout);
        if(result!=WAIT_OBJECT_0) {
            DWORD exitCode=0;GetExitCodeProcess(process.value,&exitCode);
            log::error("fruc-ipc",std::format("worker wait={} exit={}",result,exitCode));return false;
        }
        if(message->result||message->seh) {
            log::error("fruc-ipc",std::format("worker result={} seh={}",message->result,message->seh));return false;
        }
        return true;
    }
    bool start() {
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        if(!mapping.value) {
            mapping.value=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(FrucWorkerMessage),nullptr);
            if(!mapping.value)return false;
            message=static_cast<FrucWorkerMessage*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(FrucWorkerMessage)));
            if(!message)return false;
            request.value=CreateEventW(&security,FALSE,FALSE,nullptr);response.value=CreateEventW(&security,FALSE,FALSE,nullptr);
            if(!request.value||!response.value)return false;
        }
        ResetEvent(request.value);ResetEvent(response.value);
        *message=FrucWorkerMessage{};
        message->width=width;message->height=height;message->multiplier=count+1;message->adapter=ctx->device()->GetAdapterLuid();
        message->fence=reinterpret_cast<uint64_t>(fenceHandle.value);
        message->request=reinterpret_cast<uint64_t>(request.value);message->response=reinterpret_cast<uint64_t>(response.value);
        std::vector<HANDLE> inherited{mapping.value,request.value,response.value,fenceHandle.value};
        for(unsigned j=0;j<count+2;++j){message->textures[j]=reinterpret_cast<uint64_t>(textures[j].value);inherited.push_back(textures[j].value);}
        wchar_t exePath[32768]{};if(!GetModuleFileNameW(nullptr,exePath,32768))return false;
        const auto executable=std::filesystem::path(exePath).parent_path()/L"veyra_fruc_worker.exe";
        // Unique worker log survives worker crashes and reset/reopen cycles.
        wchar_t existingLog[1024]{};GetEnvironmentVariableW(L"VEYRA_LOG_FILE",existingLog,1024);
        auto logPath=(existingLog[0]?std::filesystem::path(existingLog).parent_path():std::filesystem::path(L"logs"))/
            std::format(L"fruc-worker-{}-{}.log",GetCurrentProcessId(),GetTickCount64());
        const auto absoluteLog=std::filesystem::absolute(logPath).wstring();
        if(absoluteLog.size()>=std::size(message->logPath))return false;
        wcscpy_s(message->logPath,absoluteLog.c_str());
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))return false;
        struct AttributesCleanup{LPPROC_THREAD_ATTRIBUTE_LIST list;~AttributesCleanup(){DeleteProcThreadAttributeList(list);}}cleanup{attributes};
        if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited.data(),inherited.size()*sizeof(HANDLE),nullptr,nullptr))return false;
        job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
        PROCESS_INFORMATION pi{};auto command=std::format(L"\"{}\" {}",executable.wstring(),reinterpret_cast<uint64_t>(mapping.value));
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&pi)) {
            log::error("fruc-ipc",std::format("CreateProcess error={}",GetLastError()));return false;
        }
        process.value=pi.hProcess;Handle thread;thread.value=pi.hThread;
        if(!AssignProcessToJobObject(job.value,process.value)) {TerminateProcess(process.value,1);return false;}
        if(ResumeThread(thread.value)==DWORD(-1))return false;
        log::info("fruc-ipc",std::format("worker pid={} streams={} extent={}x{}",pi.dwProcessId,count,width,height));
        return acknowledge(15000);
    }
};
FrucBackend::FrucBackend():p_(std::make_unique<Impl>()){}
FrucBackend::~FrucBackend()=default;
bool FrucBackend::initialize(gfx::D3D12DeviceContext& ctx,unsigned width,unsigned height,unsigned multiplier) {
    if(p_->ctx||!width||!height||multiplier<2||multiplier>4)return false;
    auto& p=*p_;p.ctx=&ctx;p.width=width;p.height=height;p.count=multiplier-1;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    if(!hrOk(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&p.fence)),"CreateFence")||
       !hrOk(ctx.device()->CreateSharedHandle(p.fence.Get(),&security,GENERIC_ALL,nullptr,&p.fenceHandle.value),"ShareFence"))return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for(unsigned j=0;j<p.count+2;++j) {
        if(!hrOk(ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&p.shared[j])),"SharedTexture")||
           !hrOk(ctx.device()->CreateSharedHandle(p.shared[j].Get(),&security,GENERIC_ALL,nullptr,&p.textures[j].value),"ShareTexture"))return false;
    }
    return p.start();
}
bool FrucBackend::reset() {
    // Fresh process avoids the pinned FRUC runtime's invalid-handle/SEH after
    // destruction/recreation. Resources stay on GPU; caller drains consumers.
    p_->stop();return p_->start();
}
void FrucBackend::recordInput(ID3D12GraphicsCommandList* list,ID3D12Resource* input,unsigned parity) {
    auto target=p_->shared[parity].Get();
    barrier(list,input,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list,target,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(target,input);
    barrier(list,target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    barrier(list,input,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
}
bool FrucBackend::execute(unsigned parity,double previousMs,double currentMs,std::array<bool,3>& repeated) {
    auto& p=*p_;auto& m=*p.message;
    m.command=1;m.result=1;m.seh=0;m.parity=parity;m.previousMs=previousMs;m.currentMs=currentMs;
    m.inputFence=++p.serial;
    if(!hrOk(p.ctx->directQueue()->Signal(p.fence.Get(),m.inputFence),"InputSignal")||!SetEvent(p.request.value))return false;
    // Wait for CPU API submission/results, not for GPU completion. CUDA/FRUC's
    // internal CPU blocking is unchanged; all pixel synchronization is GPU-side.
    if(!p.acknowledge(10000))return false;
    p.serial=m.outputFence;for(unsigned j=0;j<p.count;++j)repeated[j]=m.repeated[j];
    return hrOk(p.ctx->directQueue()->Wait(p.fence.Get(),m.outputFence),"OutputQueueWait");
}
void FrucBackend::recordOutput(ID3D12GraphicsCommandList* list,ID3D12Resource* target,unsigned subframe) {
    auto input=p_->shared[subframe+1].Get();
    barrier(list,input,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list,target,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(target,input);
    barrier(list,target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    barrier(list,input,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
}
}
