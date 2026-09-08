// Isolated exact-source reconstruction diagnostic; never a product readback path.
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
namespace {
constexpr unsigned W=3840,H=2160,N=24;
bool foreground(int x,int y,unsigned frame){return x>=900+int(frame)*8&&x<1800+int(frame)*8&&y>=500&&y<1600;}
uint64_t hash(const sink::RgbaImage& image){uint64_t v=14695981039346656037ull;for(auto b:image.pixels){v^=b;v*=1099511628211ull;}return v;}
int experiment(const std::filesystem::path& dir,unsigned scene){
    std::array<float,256> linear{};for(unsigned i=0;i<256;++i){float s=i/255.f;linear[i]=s<=.04045f?s/12.92f:std::pow((s+.055f)/1.055f,2.4f);}
    auto encoded=[](float s){return uint8_t(std::clamp(std::lround((s<=.0031308f?s*12.92f:1.055f*std::pow(s,1/2.4f)-.055f)*255),0l,255l));};
    std::vector<uint64_t> srHashes;std::vector<uint8_t> gt(size_t(W)*H*4);
    for(unsigned mode=0;mode<3;++mode){
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
        if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=W/2;gd.sourceHeight=H/2;gd.workWidth=W;gd.workHeight=H;gd.rgbInput=true;gd.enableNr=false;gd.enableFg=false;gd.enableSr=mode!=0;gd.enableNvofStandalone=true;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        if(!graph.initialize(gd)||!graph.createViews())return 2;
        AVFrame* f=av_frame_alloc();if(!f)return 2;f->width=W/2;f->height=H/2;f->format=AV_PIX_FMT_RGBA;f->color_range=AVCOL_RANGE_JPEG;f->color_trc=AVCOL_TRC_IEC61966_2_1;f->colorspace=AVCOL_SPC_RGB;if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return 2;}
        std::vector<int16_t> previousError(size_t(W)*H*3),currentError(previousError.size());std::vector<double> srTimes;
        uint64_t absolute=0,squared=0,samples=0,temporal=0,temporalSamples=0;unsigned maximum=0;bool ok=true;
        for(unsigned frame=0;frame<N&&ok;++frame){
            // Every sample derives from the same exact high-resolution scene.
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                const int q=int(x)+int(frame)*4;const bool object=scene==1&&foreground(x,y,frame);
                unsigned value;
                if(object)value=((int(x)-int(frame)*8)/7+int(y)/11)%2?185:95;
                else if(scene==0){const unsigned band=y/(H/4);const unsigned period=4u<<band;value=((q/int(period)+int(y)/int(period))%2)?190:55;}
                else value=unsigned(110+35*std::sin(q*.037)+25*std::cos(y*.051));
                auto* p=&gt[(size_t(y)*W+x)*4];p[0]=uint8_t(value);p[1]=uint8_t(std::min(255u,value+20));p[2]=uint8_t(std::max(0,int(value)-20));p[3]=255;
            }
            for(unsigned y=0;y<H/2;++y)for(unsigned x=0;x<W/2;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;
                for(unsigned c=0;c<3;++c){float sum=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)sum+=linear[gt[(size_t(y*2+dy)*W+x*2+dx)*4+c]];p[c]=encoded(sum*.25f);}p[3]=255;}
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;ok=graph.process(f,frame*(1000.0/60),frame==0,out,frame+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
            if(!ok)break;ok=image.width==W&&image.height==H&&image.pixels.size()==gt.size();if(!ok)break;
            const auto h=hash(image);if(mode==1)srHashes.push_back(h);if(mode==2)ok=h==srHashes[frame];
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const bool object=scene==1&&foreground(x,y,frame);const int px=int(x)+(object?-8:4);const bool temporalValid=frame>0&&px>=0&&px<int(W)&&(scene==0||foreground(px,y,frame-1)==object);
                for(unsigned c=0;c<3;++c){const size_t i=(size_t(y)*W+x)*3+c;const int error=int(image.pixels[(size_t(y)*W+x)*4+c])-int(gt[(size_t(y)*W+x)*4+c]);currentError[i]=int16_t(error);const unsigned a=unsigned(std::abs(error));absolute+=a;squared+=uint64_t(a)*a;++samples;maximum=std::max(maximum,a);
                    if(temporalValid){temporal+=std::abs(error-previousError[(size_t(y)*W+px)*3+c]);++temporalSamples;}}
            }
            previousError.swap(currentError);const auto metrics=graph.gpuMetrics();const auto& timing=metrics.gpu[size_t(diagnostics::GpuStage::Sr)];
            if(frame>=4&&metrics.identity.sourceFrameId==frame+1&&timing.state==diagnostics::SampleState::Measured&&timing.milliseconds)srTimes.push_back(*timing.milliseconds);
            if(frame+1==N)ok=ok&&sink::saveImage((dir/("scene"+std::to_string(scene)+"-mode"+std::to_string(mode)+".png")).wstring(),image);
            if(frame+1==N&&mode==0){sink::RgbaImage reference{W,H,gt},input{W/2,H/2,{}};input.pixels.resize(size_t(W/2)*(H/2)*4);for(unsigned y=0;y<H/2;++y)std::copy_n(f->data[0]+size_t(y)*f->linesize[0],W/2*4,input.pixels.data()+size_t(y)*(W/2)*4);ok=ok&&sink::saveImage((dir/L"reference.png").wstring(),reference)&&sink::saveImage((dir/L"input.png").wstring(),input);}
        }
        av_frame_free(&f);auto m=graph.metrics();ok=ok&&m.nrEvaluateCount==0&&m.srEvaluateCount==(mode?N:0)&&m.nvofExecuteCount==N-1&&m.nvofFrameFailures==0&&m.resetCount==1&&samples==uint64_t(N)*W*H*3&&temporalSamples>0;
        ok=ring.drainQueue()&&ok;graph.shutdown();pipeline::ComPtr<ID3D12InfoQueue> iq;unsigned errors=0;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&iq))))return 2;
        for(UINT64 i=0;i<iq->GetNumStoredMessages();++i){SIZE_T size=0;if(FAILED(iq->GetMessage(i,nullptr,&size)))return 2;std::vector<uint8_t> bytes(size);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(FAILED(iq->GetMessage(i,msg,&size)))return 2;if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<msg->pDescription<<std::endl;}}
        std::sort(srTimes.begin(),srTimes.end());double mean=0;for(auto v:srTimes)mean+=v;mean=srTimes.empty()?-1:mean/srTimes.size();
        std::cout<<"SR_RECON scene="<<scene<<" mode="<<mode<<" source=1920x1080 output=3840x2160 frames="<<N<<" sr="<<m.srEvaluateCount<<" nvof="<<m.nvofExecuteCount<<" reset="<<m.resetCount<<" mae8="<<double(absolute)/samples<<" psnr="<<(squared?10*std::log10(255.*255.*samples/squared):999)<<" max="<<maximum<<" temporalErrorMae8="<<double(temporal)/temporalSamples<<" gpuSrSamples="<<srTimes.size()<<" gpuSrMeanMs="<<mean<<" gpuSrP95Ms="<<(srTimes.empty()?-1:srTimes[size_t(std::ceil(srTimes.size()*.95))-1])<<" errors="<<errors<<" pass="<<(ok&&!errors)<<std::endl;
        if(!ok||errors)return 1;
    }
    return 0;
}
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;unsigned scene=unsigned(std::wcstoul(argv[2],nullptr,10));if(scene>1)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);int result=experiment(dir,scene);CoUninitialize();return result;}
