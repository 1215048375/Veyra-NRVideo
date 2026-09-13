// Synthetic pixels only. Diagnostic readback is intentionally outside all
// player/capture/export paths. The actual product EnhanceGraph is under test.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <cmath>
#include <filesystem>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace {
uint64_t hash(const veyra::sink::RgbaImage& im){uint64_t v=14695981039346656037ull;for(auto b:im.pixels){v^=b;v*=1099511628211ull;}return v;}
double center(const veyra::sink::RgbaImage& im){double total=0,sum=0;for(unsigned y=300;y<780;++y)for(unsigned x=0;x<im.width;++x){const auto v=im.pixels[(size_t(y)*im.width+x)*4];if(v>160){total+=1;sum+=x;}}return total?sum/total:-1;}
}
int main(int argc,char** argv){
    using namespace veyra;CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);
    Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;bool ok=ctx.initialize(dd,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);
    pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=1920;gd.sourceHeight=gd.workHeight=1080;gd.enableNr=false;gd.enableSr=false;gd.enableFg=true;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    const std::string mode=argc>1?argv[1]:"off";
    gd.enableNr=mode!="off";gd.enableNvofStandalone=gd.enableNr;
    if(mode.starts_with("dis"))gd.opticalFlowBackend=engine::OpticalFlowBackend::GpuDis;
    if(mode=="flowP")gd.flowQuality=engine::FlowQuality::Performance;
    if(mode=="flowQ")gd.flowQuality=engine::FlowQuality::Quality;
    if(mode=="dis-sr"){gd.workWidth=3840;gd.workHeight=2160;gd.nrWidth=1920;gd.nrHeight=1080;gd.enableSr=true;gd.videoSrQuality=2;}
    if(mode=="sr"){gd.workWidth=3840;gd.workHeight=2160;gd.nrWidth=1920;gd.nrHeight=1080;gd.enableSr=true;}
    if(mode=="mfg3"||mode=="mfg4"){gd.fgMultiplier=mode=="mfg3"?3:4;gd.enableNr=false;gd.enableNvofStandalone=false;}
    if(ok)ok=graph.initialize(gd)&&graph.createViews();
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=1920;f->height=1080;f->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(f,32)>=0;
    sink::RgbaImage previous;unsigned generated=0,valid=0;pipeline::FrameBatch retained;
    for(unsigned i=0;ok&&i<12;++i){
        for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;const bool square=x>=240+int(i)*16&&x<880+int(i)*16&&y>=220&&y<860;p[0]=p[1]=p[2]=square?240:20;p[3]=255;}
        pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(f,i*20.0,i==0,out)){ok=false;break;}
        sink::RgbaImage real,g;if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),real)){ok=false;break;}
        if(!ring.waitIdle()||!graph.resolveGeneration(out)){ok=false;break;}
        uint64_t previousGeneratedHash=0;
        for(uint32_t j=0;j<out.batch.count;++j){const auto& item=out.batch.frames[j];if(item.kind!=pipeline::FrameKind::Generated)continue;
            ++generated;if(!sink::readRgba8(ctx,ring,graph.generatedFrameResource(item.lease->slot),g)){ok=false;break;}
            const double t=double(item.subframe)/gd.fgMultiplier;
            double blendResidual=0;for(size_t k=0;k<g.pixels.size();k+=4)blendResidual+=std::abs(double(g.pixels[k])-(double(previous.pixels[k])*(1-t)+real.pixels[k]*t));blendResidual/=g.width*g.height;
            const double expected=center(previous)*(1-t)+center(real)*t;
            const bool good=item.validity==pipeline::GenerationValidity::Valid&&hash(g)!=hash(previous)&&hash(g)!=hash(real)&&hash(g)!=previousGeneratedHash&&blendResidual>0.1&&std::abs(center(g)-expected)<=3.0&&std::abs(item.pts100ns/10000.0-((i-1+t)*20.0))<=0.0001;
            valid+=good;
            previousGeneratedHash=hash(g);
            std::cout<<"CONTENT frame="<<i<<" subframe="<<item.subframe<<" pts="<<item.pts100ns<<" previousHash="<<hash(previous)<<" realHash="<<hash(real)<<" generatedHash="<<hash(g)<<" expectedCenter="<<expected<<" actualCenter="<<center(g)<<" blendResidual="<<blendResidual<<" valid="<<good<<std::endl;
        }
        previous=std::move(real);
        if(i==10)retained=out.batch;
    }
    std::cout<<"RESULT source="<<graph.metrics().nvofExecuteCount+graph.metrics().gpuDisExecuteCount+1<<" generated="<<generated<<" contentValid="<<valid<<" display=unmeasured"<<std::endl;
    pipeline::EnhanceGraph::FrameOutputs rejected;
    const bool protectedLease=ok&&!graph.process(f,240,false,rejected);
    std::cout<<"LEASE retained-slot-overwrite-rejected="<<protectedLease<<std::endl;
    retained={};rejected={};
    ok=ok&&generated==11*(gd.fgMultiplier-1)&&valid==generated&&protectedLease;ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
