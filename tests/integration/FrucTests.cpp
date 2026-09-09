// Diagnostic pixel readback only. Known 8-pixel pan provides exact fractional GT.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
int main(int argc,char** argv){using namespace veyra;if(argc<3)return 2;const unsigned mult=unsigned(std::stoi(argv[1]));if(mult<2||mult>4)return 2;
    constexpr unsigned W=1920,H=1080;std::vector<unsigned char> base(W*H);std::ifstream input(argv[2],std::ios::binary);if(!input.read(reinterpret_cast<char*>(base.data()),base.size()))return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);Status st;
    gfx::DeviceContextDesc deviceDesc{};bool ok=ctx.initialize(deviceDesc,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);
    pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=W;gd.sourceHeight=gd.workHeight=H;gd.rgbInput=true;gd.enableNr=gd.enableSr=false;gd.enableFg=true;gd.fgMultiplier=mult;gd.frameGenerationBackend=engine::FrameGenerationBackend::Fruc;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();if(ok)ok=graph.initialize(gd)&&graph.createViews();
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=W;f->height=H;f->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(f,32)>=0;
    auto color=[&](int x,unsigned y,unsigned c){const unsigned v=base[size_t(y)*W+std::clamp(x,0,int(W)-1)];return c==0?v:c==1?v/2+32:255-v;};
    unsigned generated=0;double worst=0;for(unsigned i=0;ok&&i<6;++i){for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto* p=f->data[0]+y*f->linesize[0]+x*4;for(unsigned c=0;c<3;++c)p[c]=static_cast<unsigned char>(color(int(x)-int(i)*8,y,c));p[3]=255;}
        pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(f,i*1000.0/15,i==0,out)||!ring.waitIdle()||!graph.resolveGeneration(out)){ok=false;break;}
        for(unsigned j=0;j<out.batch.count;++j){auto& item=out.batch.frames[j];if(item.kind!=pipeline::FrameKind::Generated)continue;sink::RgbaImage image;if(!sink::readRgba8(ctx,ring,item.lease->texture.Get(),image)){ok=false;break;}
            double err=0,repeatError=0;size_t samples=0;double shift=(i-1+double(item.subframe)/mult)*8;
            for(unsigned y=32;y<H-32;++y)for(unsigned x=64;x<W-64;++x)for(unsigned c=0;c<3;++c){double pos=x-shift;int left=int(std::floor(pos));double t=pos-left;double expected=color(left,y,c)*(1-t)+color(left+1,y,c)*t;err+=std::abs(image.pixels[(size_t(y)*W+x)*4+c]-expected);repeatError+=std::abs(double(color(int(x)-int(i-1)*8,y,c))-expected);++samples;}
            err/=samples;repeatError/=samples;worst=std::max(worst,err);bool pass=item.validity==pipeline::GenerationValidity::Valid&&err<repeatError&&std::llabs(item.pts100ns-std::llround((i-1+double(item.subframe)/mult)*10000000.0/15))<=1;
            std::cout<<"FRUC_GT frame="<<i<<" sub="<<item.subframe<<" mae="<<err<<" repeatMae="<<repeatError<<" valid="<<pass<<std::endl;ok=ok&&pass;++generated;
        }
    }
    // Explicit discontinuity must reseed, never interpolate across the reset.
    for(unsigned epoch=1;ok&&epoch<=3;++epoch){
        pipeline::EnhanceGraph::FrameOutputs out;
        ok=graph.process(f,epoch*2000.0,true,out)&&ring.waitIdle()&&graph.resolveGeneration(out)&&out.batch.count==1;
        // A second frame after reseeding must run successfully, even for static input.
        if(ok)ok=graph.process(f,epoch*2000.0+1000.0/15,false,out)&&ring.waitIdle()&&graph.resolveGeneration(out);
    }
    ok=ok&&generated==5*(mult-1);std::cout<<"FRUC_RESULT multiplier="<<mult<<" generated="<<generated<<" worstMae="<<worst<<" pass="<<ok<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
