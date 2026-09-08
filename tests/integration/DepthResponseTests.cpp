#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/DlssNrParameters.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;
int experiment(bool temporal,const std::filesystem::path& directory){
    constexpr unsigned w=256,h=256;const unsigned count=temporal?12:1;
    std::vector<sink::RgbaImage> finalBaseline,rawBaseline;bool positive=false;
    for(unsigned mode=0;mode<(temporal?15u:9u);++mode){
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 2;
        auto depth=pipeline::makeTexture(ctx.device(),w,h,DXGI_FORMAT_R32_FLOAT,false);
        auto up=pipeline::makeUploadBuffer(ctx.device(),size_t(w)*h*4);void* ptr=nullptr;
        if(!depth||!up||FAILED(up->Map(0,nullptr,&ptr)))return 2;
        const unsigned pattern=mode>=11?mode-9:mode;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            float d=pattern==2?0:pattern==3?1:(pattern==4||pattern==6)?.01f+.98f*x/(w-1):pattern==5?(((x/32+y/32)%2)?.1f:.9f):.5f;
            static_cast<float*>(ptr)[y*w+x]=d;
        }
        up->Unmap(0,nullptr);unsigned slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return 2;
        pipeline::StateTracker states;states.transition(list,depth.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=depth.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=up.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R32_FLOAT,w,h,1,w*4};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!ring.submitAndSignal(slot)||!ring.drainQueue())return 2;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=w;gd.sourceHeight=gd.workHeight=h;
        gd.rgbInput=gd.enableNr=true;gd.stillImage=!temporal;gd.enableFg=false;gd.enableNvofStandalone=temporal;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        ID3D12Resource* raw=nullptr;ID3D12Resource* residentDepth=nullptr;bool binding=true;unsigned hookCalls=0;
        gd.nrParameterProbe=[&](NVSDK_NGX_Parameter* p,ID3D12Resource*,ID3D12Resource* output,uint32_t,uint32_t){
            raw=output;++hookCalls;ngx::ParameterBlock pb(p);
            if(mode>=1&&mode<=6){pb.setD3D12Resource(ngx::dlssnr::kDepth,depth.Get());ID3D12Resource* got=nullptr;binding=binding&&p->Get(ngx::dlssnr::kDepth,&got)==NVSDK_NGX_Result_Success&&got==depth.Get();}
            if(mode==6)pb.setI32(ngx::dlssnr::kDepthInverted,0);
            if(mode==7)pb.setF32(ngx::dlssnr::kIntensity,0.f);
            // Separate guidance-response control; depth stays at the default.
            if(mode==9||mode==10){const float scale=mode==9?0.f:-1.f;pb.setF32(ngx::dlssnr::kMVecScaleX,scale);pb.setF32(ngx::dlssnr::kMVecScaleY,scale);}
            if(mode>=11){ID3D12Resource* got=nullptr;binding=binding&&p->Get(ngx::dlssnr::kDepth,&got)==NVSDK_NGX_Result_Success&&got;
                if(binding){auto d=got->GetDesc();binding=d.Format==DXGI_FORMAT_R32_FLOAT&&d.Width==w&&d.Height==h&&(!residentDepth||residentDepth==got);residentDepth=got;}}
        };
        if(!graph.initialize(gd)||!graph.createViews())return 2;
        AVFrame* f=av_frame_alloc();if(!f)return 2;f->format=AV_PIX_FMT_RGBA;f->width=w;f->height=h;f->color_range=AVCOL_RANGE_JPEG;f->color_trc=AVCOL_TRC_IEC61966_2_1;f->colorspace=AVCOL_SPC_RGB;
        if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return 2;}
        bool ok=true;uint64_t rawChanged=0,finalChanged=0;unsigned rawMax=0,finalMax=0;
        for(unsigned frame=0;frame<count&&ok;++frame){
            if(mode>=11&&frame==1){
                // Change the original resource's contents after the first frame.
                // This rules out an Evaluate-time resource-pointer cache as the
                // explanation for an inert replacement depth binding.
                unsigned s=0;auto* l=ring.acquireNext(s,st);if(!l||!residentDepth){ok=false;break;}
                D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={residentDepth,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};l->ResourceBarrier(1,&b);
                D3D12_TEXTURE_COPY_LOCATION a{},source{};a.pResource=residentDepth;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;source.pResource=up.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint.Footprint={DXGI_FORMAT_R32_FLOAT,w,h,1,w*4};l->CopyTextureRegion(&a,0,0,0,&source,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);l->ResourceBarrier(1,&b);
                if(!ring.submitAndSignal(s)||!ring.drainQueue()){ok=false;break;}
            }
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;unsigned q=x+frame*2;p[0]=(q*7+y*3)%256;p[1]=(q*3+y*11)%256;p[2]=(q*5+y*17)%256;p[3]=255;}
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage finalPixels,rawPixels;
            ok=graph.process(f,frame*(1000.0/60),frame==0,out,frame+1)&&binding&&raw&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),finalPixels);
            auto transitionRaw=[&](bool restore){unsigned s=0;auto* l=ring.acquireNext(s,st);if(!l)return false;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={raw,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,restore?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,restore?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON};l->ResourceBarrier(1,&b);return ring.submitAndSignal(s);};
            if(ok){ok=transitionRaw(false);if(ok){bool read=sink::readRgba8(ctx,ring,raw,rawPixels);ok=transitionRaw(true)&&read;}}
            if(ok){
                if(mode==0){finalBaseline.push_back(finalPixels);rawBaseline.push_back(rawPixels);}
                else if(finalPixels.pixels.size()!=finalBaseline[frame].pixels.size()||rawPixels.pixels.size()!=rawBaseline[frame].pixels.size())ok=false;
                else for(size_t i=0;i<finalPixels.pixels.size();++i){if(i%4==3)continue;unsigned d=unsigned(std::abs(int(finalPixels.pixels[i])-int(finalBaseline[frame].pixels[i])));finalChanged+=d!=0;finalMax=std::max(finalMax,d);unsigned r=unsigned(std::abs(int(rawPixels.pixels[i])-int(rawBaseline[frame].pixels[i])));rawChanged+=r!=0;rawMax=std::max(rawMax,r);}
                if(ok&&frame+1==count)ok=sink::saveImage((directory/(std::string(temporal?"temporal":"still")+"-depth-"+std::to_string(mode)+".png")).wstring(),finalPixels);
            }
        }
        av_frame_free(&f);auto metrics=graph.metrics();ok=ok&&metrics.nrEvaluateCount==count&&hookCalls==count&&metrics.nvofExecuteCount==(temporal?count-1:0)&&metrics.nrMotionFrames==(temporal?count-1:0)&&metrics.resetCount==1;
        if(mode==1||mode==8)ok=ok&&rawChanged==0&&finalChanged==0; // explicit .5 and independent repeated baseline
        if(mode==7){positive=rawChanged>100&&finalChanged>100;ok=ok&&positive;}
        ok=ring.drainQueue()&&ok;graph.shutdown();depth.Reset();up.Reset();
        pipeline::ComPtr<ID3D12InfoQueue> iq;unsigned errors=0;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&iq))))return 2;
        for(UINT64 i=0;i<iq->GetNumStoredMessages();++i){SIZE_T n=0;if(FAILED(iq->GetMessage(i,nullptr,&n)))return 2;std::vector<uint8_t>b(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(b.data());if(FAILED(iq->GetMessage(i,m,&n)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
        std::cout<<"DEPTH_RESPONSE temporal="<<temporal<<" mode="<<mode<<" frames="<<count<<" nr="<<metrics.nrEvaluateCount<<" nvof="<<metrics.nvofExecuteCount<<" motionFrames="<<metrics.nrMotionFrames<<" resets="<<metrics.resetCount<<" rawChanged="<<rawChanged<<" rawMax="<<rawMax<<" finalChanged="<<finalChanged<<" finalMax="<<finalMax<<" binding="<<binding<<" debugErrors="<<errors<<" pass="<<(ok&&!errors)<<std::endl;
        if(!ok||errors)return 1;
    }
    return positive?0:1;
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);int r=experiment(false,dir);if(!r)r=experiment(true,dir);CoUninitialize();return r;}
