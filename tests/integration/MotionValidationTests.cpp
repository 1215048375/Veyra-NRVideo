#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <d3d12sdklayers.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace veyra;using namespace veyra::pipeline;
float half(uint16_t v){int e=(v>>10)&31;float n=e?std::ldexp(1.f+(v&1023)/1024.f,e-15):std::ldexp(float(v&1023),-24);return v&32768?-n:n;}
int run(bool vertical){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
    if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 2;
    auto flow=makeTexture(ctx.device(),8,8,DXGI_FORMAT_R16G16_SINT,false),cost=makeTexture(ctx.device(),8,8,DXGI_FORMAT_R8_UINT,false);
    auto previous=makeTexture(ctx.device(),32,32,DXGI_FORMAT_R32G32B32A32_FLOAT,false),current=makeTexture(ctx.device(),32,32,DXGI_FORMAT_R32G32B32A32_FLOAT,false);
    auto output=makeTexture(ctx.device(),32,32,DXGI_FORMAT_R16G16_FLOAT,true),confidence=makeTexture(ctx.device(),32,32,DXGI_FORMAT_R8_UNORM,true);
    if(!flow||!cost||!previous||!current||!output||!confidence)return 2;
    std::vector<ComPtr<ID3D12Resource>> uploads;StateTracker states;uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return 2;
    auto upload=[&](ID3D12Resource* tex,unsigned w,unsigned h,unsigned bpp,auto fill){
        unsigned pitch=(w*bpp+255)&~255u;auto data=makeUploadBuffer(ctx.device(),pitch*h);void* ptr=nullptr;if(!data||FAILED(data->Map(0,nullptr,&ptr)))return false;
        memset(ptr,0,pitch*h);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)fill(static_cast<uint8_t*>(ptr)+y*pitch+x*bpp,x,y);data->Unmap(0,nullptr);
        states.transition(list,tex,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=tex;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=data.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint.Footprint={tex->GetDesc().Format,w,h,1,pitch};list->CopyTextureRegion(&a,0,0,0,&b,nullptr);states.transition(list,tex,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(data);return true;
    };
    if(!upload(flow.Get(),8,8,4,[&](uint8_t* p,unsigned x,unsigned y){if(vertical)std::swap(x,y);int16_t v[2]={0,0};v[vertical?1:0]=int16_t(y>=6?-80:y>=4?80:64);memcpy(p,v,4);})||!upload(cost.Get(),8,8,1,[&](uint8_t* p,unsigned x,unsigned y){if(vertical)std::swap(x,y);*p=x==3?255:x==5?31:x==6?32:x==7?16:0;})||
       !upload(previous.Get(),32,32,16,[&](uint8_t* p,unsigned x,unsigned y){if(vertical)std::swap(x,y);float v[4]={x/40.f,x/40.f,x/40.f,1};memcpy(p,v,16);})||
       !upload(current.Get(),32,32,16,[&](uint8_t* p,unsigned x,unsigned y){if(vertical)std::swap(x,y);float shift=y>=24?-2.5f:y>=16?2.5f:2.f;float c=(x+shift)/40.f+(y>=8&&y<16?.3f:y>=16&&y<24?.08f:0);float v[4]={c,c,c,1};memcpy(p,v,16);}))return 2;
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;
    ComputePass pass;std::vector<uint8_t> shader;if(!pass.loadShader("NvofDensify.dxil",shader)||!pass.create(ctx.device(),shader,6,4,2))return 2;
    unsigned index=0;for(auto* r:{flow.Get(),cost.Get(),previous.Get(),current.Get()})makeSrv(ctx.device(),r,r->GetDesc().Format,cpuHandleOf(pass,index++));
    makeUav(ctx.device(),output.Get(),DXGI_FORMAT_R16G16_FLOAT,cpuHandleOf(pass,4));makeUav(ctx.device(),confidence.Get(),DXGI_FORMAT_R8_UNORM,cpuHandleOf(pass,5));
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=16384;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return 2;
    bool ok=true;
    for(unsigned flags:{0u,3u}){
        list=ring.acquireNext(slot,st);if(!list)return 2;states.transition(list,output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);states.transition(list,confidence.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        float c[8];unsigned values[8]={8,8,32,32,4,0,32,flags};memcpy(c,values,sizeof c);pass.bind(list,c,gpuHandleOf(pass,0).ptr,gpuHandleOf(pass,4).ptr);list->Dispatch(2,2,1);
        auto copy=[&](ID3D12Resource* tex,UINT64 offset){states.uavBarrier(list,tex);states.transition(list,tex,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=tex;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint.Offset=offset;b.PlacedFootprint.Footprint={tex->GetDesc().Format,32,32,1,256};list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};copy(output.Get(),0);copy(confidence.Get(),8192);
        if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;void* ptr=nullptr;D3D12_RANGE read{0,16384};if(FAILED(readback->Map(0,&read,&ptr)))return 2;
        unsigned rejected=0,retained=0,attenuated=0;bool passOk=true;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){auto* p=static_cast<uint8_t*>(ptr);auto* v=reinterpret_cast<uint16_t*>(p+y*256+x*4);float vx=half(v[0]),vy=half(v[1]);unsigned conf=p[8192+y*256+x];
            unsigned sx=vertical?y:x,sy=vertical?x:y;if(vertical)std::swap(vx,vy);
            float expected=sy>=24?-2.5f:sy>=16?2.5f:2.f;bool outside=sx+expected<0||sx+expected>31;
            unsigned expectedCost=sx/4==3?255:sx/4==5?31:sx/4==6?32:sx/4==7?16:0;
            bool reject=expectedCost>=32||(flags&&(outside||(sy>=8&&sy<16)));bool attenuate=flags&&!reject&&sy>=16&&sy<24;
            if(reject){++rejected;passOk=passOk&&vx==0&&vy==0&&conf==0;}
            else if(attenuate){++attenuated;passOk=passOk&&vx>0&&vx<expected&&vy==0&&conf>0&&conf<255-expectedCost;}
            else{++retained;passOk=passOk&&vx==expected&&vy==0&&conf==255-expectedCost;}
        }
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);ok=ok&&passOk&&retained>100&&rejected>100&&(!flags||attenuated>100);
        std::cout<<"MOTION_VALIDATION vertical="<<vertical<<" flags="<<flags<<" rejected="<<rejected<<" retained="<<retained<<" attenuated="<<attenuated<<" pass="<<passOk<<std::endl;
    }
    ring.drainQueue();ComPtr<ID3D12InfoQueue> iq;unsigned errors=0;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&iq))))return 2;
    for(UINT64 i=0;i<iq->GetNumStoredMessages();++i){SIZE_T n=0;iq->GetMessage(i,nullptr,&n);std::vector<uint8_t> b(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(b.data());if(FAILED(iq->GetMessage(i,m,&n)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    std::cout<<"MOTION_DEBUG errors="<<errors<<" pass="<<(ok&&!errors)<<std::endl;return ok&&!errors?0:1;
}

int main(){int result=run(false);return result?result:run(true);}
