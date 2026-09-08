#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;
int experiment(bool fg,const std::filesystem::path& dir){
    constexpr unsigned sw=640,sh=360,n=12;const unsigned w=fg?sw:sw*2,h=fg?sh:sh*2;
    std::vector<sink::RgbaImage> baselineReal,baselineGenerated;
    for(unsigned mode=0;mode<(fg?10u:8u);++mode){
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 2;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=sw;gd.sourceHeight=sh;gd.workWidth=w;gd.workHeight=h;gd.rgbInput=true;gd.enableNr=false;gd.enableSr=!fg;gd.enableFg=fg;gd.enableNvofStandalone=true;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        if(!graph.initialize(gd)||!graph.createViews())return 2;
        auto* depth=graph.diagnosticDepthResource(fg);if(!depth)return 2;auto d=depth->GetDesc();if(d.Format!=DXGI_FORMAT_R32_FLOAT||d.Width!=w||d.Height!=h)return 2;
        auto up=pipeline::makeUploadBuffer(ctx.device(),size_t(w)*h*4);if(!up)return 2;
        auto uploadDepth=[&](unsigned frame){
            void* ptr=nullptr;if(FAILED(up->Map(0,nullptr,&ptr)))return false;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                bool object=x>=100+frame*6&&x<260+frame*6&&y>=90&&y<270;
                static_cast<float*>(ptr)[size_t(y)*w+x]=mode>=8?((object!=(mode==9))?.1f:.9f):mode==1?(fg?.9f:.5f):mode==2?0:mode==3?1:mode==4?.05f+.9f*x/(w-1):((x/32+y/32)%2)?.1f:.9f;
            }
            up->Unmap(0,nullptr);unsigned slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return false;
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={depth,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=depth;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=up.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R32_FLOAT,w,h,1,w*4};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
            return ring.submitAndSignal(slot)&&ring.drainQueue();
        };
        if(mode>=1&&mode<=5&&!uploadDepth(0))return 2;
        AVFrame* f=av_frame_alloc();if(!f)return 2;f->width=sw;f->height=sh;f->format=AV_PIX_FMT_RGBA;f->color_range=AVCOL_RANGE_JPEG;f->color_trc=AVCOL_TRC_IEC61966_2_1;f->colorspace=AVCOL_SPC_RGB;if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return 2;}
        bool ok=true;uint64_t realChanged=0,generatedChanged=0;unsigned realMax=0,generatedMax=0,generated=0;
        uint64_t gtError=0,gtSamples=0,edgeError=0,edgeSamples=0;
        auto scenePixel=[&](unsigned x,unsigned y,unsigned halfSteps){const bool object=x>=100+halfSteps*3&&x<260+halfSteps*3&&y>=90&&y<270;unsigned q=x+halfSteps;unsigned c=((q/12+y/12)%2)?70:100;return std::array<uint8_t,4>{uint8_t((object?210:c)+(mode==7?20:0)),uint8_t(object?130:c+20),uint8_t(object?60:c+40),255};};
        auto compare=[&](const sink::RgbaImage& a,const sink::RgbaImage& b,uint64_t& changed,unsigned& maximum){if(a.width!=b.width||a.height!=b.height||a.pixels.size()!=b.pixels.size())return false;for(size_t i=0;i<a.pixels.size();++i){if(i%4==3)continue;unsigned delta=unsigned(std::abs(int(a.pixels[i])-int(b.pixels[i])));changed+=delta!=0;maximum=std::max(maximum,delta);}return true;};
        for(unsigned frame=0;frame<n&&ok;++frame){
            if(mode>=8&&!uploadDepth(frame)){ok=false;break;}
            for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){auto p=scenePixel(x,y,frame*2);std::copy(p.begin(),p.end(),f->data[0]+size_t(y)*f->linesize[0]+x*4);}
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage real,gen;ok=graph.process(f,frame*(1000.0/60),frame==0,out,frame+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),real);
            if(ok)ok=ring.waitIdle()&&graph.resolveGeneration(out);
            if(ok){if(mode==0)baselineReal.push_back(real);else ok=compare(real,baselineReal[frame],realChanged,realMax);}
            if(ok&&fg&&frame>0){
                const unsigned generatedBefore=generated;
                for(unsigned i=0;i<out.batch.count;++i){auto& item=out.batch.frames[i];if(item.kind!=pipeline::FrameKind::Generated||item.validity!=pipeline::GenerationValidity::Valid)continue;
                    if(item.subframe!=1||std::llabs(item.pts100ns-std::llround((frame-.5)*(10000000.0/60)))>1){ok=false;break;}
                    ++generated;ok=sink::readRgba8(ctx,ring,graph.generatedFrameResource(item.lease->slot),gen);if(!ok)break;
                    if(mode==0)baselineGenerated.push_back(gen);else ok=compare(gen,baselineGenerated[frame-1],generatedChanged,generatedMax);
                    const unsigned halfSteps=frame*2-1;const int left=100+halfSteps*3,right=260+halfSteps*3;
                    for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){auto expected=scenePixel(x,y,halfSteps);const bool edge=(y>=82&&y<278&&(std::abs(int(x)-left)<=12||std::abs(int(x)-right)<=12))||(int(x)>=left-12&&int(x)<right+12&&(std::abs(int(y)-90)<=8||std::abs(int(y)-270)<=8));
                        for(unsigned c=0;c<3;++c){unsigned error=unsigned(std::abs(int(gen.pixels[(size_t(y)*sw+x)*4+c])-int(expected[c])));gtError+=error;++gtSamples;if(edge){edgeError+=error;++edgeSamples;}}
                    }
                }
                ok=ok&&generated==generatedBefore+1;
            }
            if(ok&&frame+1==n){std::string label=std::string(fg?"fg":"sr")+"-depth-"+std::to_string(mode);ok=sink::saveImage((dir/(label+"-real.png")).wstring(),real);if(ok&&!gen.pixels.empty())ok=sink::saveImage((dir/(label+"-generated.png")).wstring(),gen);}
        }
        av_frame_free(&f);auto m=graph.metrics();ok=ok&&m.nrEvaluateCount==0&&m.srEvaluateCount==(fg?0:n)&&m.nvofExecuteCount==n-1&&m.resetCount==1;
        if(fg)ok=ok&&generated==n-1&&gtSamples==uint64_t(n-1)*sw*sh*3&&edgeSamples>0&&(mode==7||realChanged==0);
        if(mode==0||mode==1||mode==6)ok=ok&&generated==(fg?n-1:0)&&realChanged==0&&generatedChanged==0;
        if(mode==7)ok=ok&&realChanged>100&&(!fg||generatedChanged>100);
        ok=ring.drainQueue()&&ok;graph.shutdown();up.Reset();
        pipeline::ComPtr<ID3D12InfoQueue> iq;unsigned errors=0;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&iq))))return 2;
        for(UINT64 i=0;i<iq->GetNumStoredMessages();++i){SIZE_T bytes=0;if(FAILED(iq->GetMessage(i,nullptr,&bytes)))return 2;std::vector<uint8_t>b(bytes);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(b.data());if(FAILED(iq->GetMessage(i,msg,&bytes)))return 2;if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<msg->pDescription<<std::endl;}}
        std::cout<<"SRFG_DEPTH fg="<<fg<<" mode="<<mode<<" sr="<<m.srEvaluateCount<<" nvof="<<m.nvofExecuteCount<<" resets="<<m.resetCount<<" generated="<<generated<<" disabled="<<m.fgDisabledFrames<<" realChanged="<<realChanged<<" realMax="<<realMax<<" generatedChanged="<<generatedChanged<<" generatedMax="<<generatedMax<<" gtComplete="<<(fg&&generated==n-1)<<" gtMae8="<<(gtSamples?double(gtError)/gtSamples:-1)<<" edgeMae8="<<(edgeSamples?double(edgeError)/edgeSamples:-1)<<" errors="<<errors<<" pass="<<(ok&&!errors)<<std::endl;
        if(!ok||errors)return 1;
    }
    return 0;
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);int r=experiment(false,dir);if(!r)r=experiment(true,dir);CoUninitialize();return r;}
