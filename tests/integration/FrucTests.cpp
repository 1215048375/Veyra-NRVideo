// Diagnostic pixel readback only. Known 8-pixel pan provides exact fractional GT.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <string_view>
#include <chrono>
#include <deque>
#include <thread>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
int main(int argc,char** argv){using namespace veyra;if(argc<3)return 2;const unsigned mult=unsigned(std::stoi(argv[1]));if(mult<2||mult>4)return 2;
    const bool throughput=argc>3&&(std::string_view(argv[3])=="--throughput"||std::string_view(argv[3])=="--4k-throughput");
    const unsigned W=argc>3&&std::string_view(argv[3])=="--4k-throughput"?3840:1920,H=W*9/16;
    std::vector<unsigned char> base(1920*1080);std::ifstream input(argv[2],std::ios::binary);if(!input.read(reinterpret_cast<char*>(base.data()),base.size()))return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);Status st;
    gfx::DeviceContextDesc deviceDesc{};bool ok=ctx.initialize(deviceDesc,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);
    pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=W;gd.sourceHeight=gd.workHeight=H;gd.rgbInput=true;gd.enableNr=gd.enableSr=false;gd.enableFg=true;gd.fgMultiplier=mult;gd.frameGenerationBackend=engine::FrameGenerationBackend::Fruc;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();if(ok)ok=graph.initialize(gd)&&graph.createViews();
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=W;f->height=H;f->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(f,32)>=0;
    auto color=[&](int x,unsigned y,unsigned c){const unsigned v=base[size_t(y%1080)*1920+unsigned(std::clamp(x,0,int(W)-1))%1920];return c==0?v:c==1?v/2+32:255-v;};
    if(throughput){
        using Clock=std::chrono::steady_clock;std::deque<pipeline::EnhanceGraph::FrameOutputs> pending;
        std::vector<double> cpuMs,gpuMs;uint64_t completed=0,generated=0;const auto start=Clock::now();
        auto collect=[&]{
            if(!graph.resolveGeneration(pending.front()))return false;
            for(const auto& sample:pending.front().batch.frames)if(sample.kind==pipeline::FrameKind::Generated&&sample.validity==pipeline::GenerationValidity::Valid)++generated;
            ++completed;pending.pop_front();return true;
        };
        for(unsigned i=0;ok&&i<120;++i){
            while(pending.size()>=2){if(collect())break;if(Clock::now()-start>std::chrono::seconds(60)){ok=false;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            if(!ok)break;
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto* p=f->data[0]+y*f->linesize[0]+x*4;for(unsigned c=0;c<3;++c)p[c]=static_cast<unsigned char>(color(int(x)-int(i%30)*8,y,c));p[3]=255;}
            pipeline::EnhanceGraph::FrameOutputs output;const auto begin=Clock::now();
            ok=graph.process(f,i*1000.0/60,i%30==0,output);
            if(!ok)break;
            if(i>8)cpuMs.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
            pending.push_back(std::move(output));
            const auto timing=graph.gpuMetrics().gpu[size_t(diagnostics::GpuStage::FgBatch)];if(i>8&&timing.milliseconds)gpuMs.push_back(*timing.milliseconds);
        }
        while(ok&&!pending.empty()){if(!collect()){if(Clock::now()-start>std::chrono::seconds(60)){ok=false;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
        auto percentile=[](std::vector<double> values,unsigned pct){if(values.empty())return -1.0;std::sort(values.begin(),values.end());return values[(values.size()*pct+99)/100-1];};
        const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
        ok=ok&&completed==120&&generated>0;
        std::cout<<"FRUC_THROUGHPUT extent="<<W<<'x'<<H<<" multiplier="<<mult<<" completed="<<completed<<" generated="<<generated<<" wallSeconds="<<elapsed<<" sourceFps="<<completed/elapsed<<" processCpuMedianMs="<<percentile(cpuMs,50)<<" processCpuP95Ms="<<percentile(cpuMs,95)<<" observedFgGpuMedianMs="<<percentile(gpuMs,50)<<" pixelReadback=0 pass="<<ok<<std::endl;
        pending.clear();ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
    }
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
    // Opt-in reproducer for the unresolved post-reset pixel error. The standard
    // smoke above never claimed pixel correctness after its static reseeds.
    if(argc>3&&std::string_view(argv[3])=="--reset-pixels"){
        bool pixelsOk=true;
        for(unsigned epoch=1;ok&&epoch<=6;++epoch){const double start=epoch%2?10000+epoch*1000:500;const bool invert=epoch%2!=0;
            for(unsigned i=0;ok&&i<3;++i){const int shift=int(epoch)*16+int(i)*8;
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto* p=f->data[0]+y*f->linesize[0]+x*4;for(unsigned c=0;c<3;++c){const auto v=color(int(x)-shift,y,c);p[c]=static_cast<unsigned char>(invert?255-v:v);}p[3]=255;}
                pipeline::EnhanceGraph::FrameOutputs out;ok=graph.process(f,start+i*1000.0/15,i==0,out)&&ring.waitIdle()&&graph.resolveGeneration(out);
                if(i==0){ok=ok&&out.batch.count==1;continue;}
                ok=ok&&out.batch.count==mult;
                for(unsigned j=0;ok&&j<out.batch.count;++j){const auto& item=out.batch.frames[j];if(item.kind!=pipeline::FrameKind::Generated)continue;
                    sink::RgbaImage image;ok=sink::readRgba8(ctx,ring,item.lease->texture.Get(),image);if(!ok)break;
                    double error=0,repeatError=0;size_t samples=0;const double target=shift-8+8.0*item.subframe/mult;
                    for(unsigned y=32;y<H-32;++y)for(unsigned x=96;x<W-96;++x)for(unsigned c=0;c<3;++c){const double pos=x-target;const int left=int(std::floor(pos));const double t=pos-left;
                        double expected=color(left,y,c)*(1-t)+color(left+1,y,c)*t;double repeated=color(int(x)-shift+8,y,c);if(invert){expected=255-expected;repeated=255-repeated;}
                        error+=std::abs(image.pixels[(size_t(y)*W+x)*4+c]-expected);repeatError+=std::abs(repeated-expected);++samples;}
                    const bool pass=item.validity==pipeline::GenerationValidity::Valid&&error<repeatError;
                    pixelsOk=pixelsOk&&pass;std::cout<<"FRUC_RESET_GT epoch="<<epoch<<" frame="<<i<<" sub="<<item.subframe<<" mae="<<error/samples<<" repeatMae="<<repeatError/samples<<" pass="<<pass<<std::endl;
                    if(!pass){
                        double best=1e9;int bestOffset=0;
                        for(int offset=-24;offset<=24;++offset){double candidate=0;unsigned count=0;
                            for(unsigned y=32;y<H-32;y+=8)for(unsigned x=96;x<W-96;x+=8)for(unsigned c=0;c<3;++c){double expected=color(int(x)-shift-offset,y,c);if(invert)expected=255-expected;candidate+=std::abs(image.pixels[(size_t(y)*W+x)*4+c]-expected);++count;}
                            candidate/=count;if(candidate<best){best=candidate;bestOffset=offset;}
                        }
                        std::cout<<"FRUC_RESET_NEAREST epoch="<<epoch<<" frame="<<i<<" offsetFromCurrent="<<bestOffset<<" mae="<<best<<std::endl;
                    }
                }
            }
        }
        ok=ok&&pixelsOk;
    }
    ok=ok&&generated==5*(mult-1);std::cout<<"FRUC_RESULT multiplier="<<mult<<" generated="<<generated<<" worstMae="<<worst<<" pass="<<ok<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
