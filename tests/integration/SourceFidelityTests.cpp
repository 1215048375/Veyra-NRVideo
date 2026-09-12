#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/sink/ImageExportSink.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
namespace {
constexpr unsigned width=1920,height=1080;
// No game image, stream, credentials, enhancement, or visual judgement.
// Alternating single-pixel edges expose unintended downscale/re-upscale;
// code-value ramps expose range/transfer errors in the shared ingress.
unsigned luma(unsigned x,unsigned y) {
    if(y<height/3)return (x&1)?235:16;
    if(y<2*height/3)return (y&1)?235:16;
    return 16+x%220;
}
double expectedGray(unsigned code) {
    const double linear=std::pow((code-16)/219.0,2.4);
    return 255*(linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055);
}
double sampled(const sink::RgbaImage& image,unsigned x,unsigned y,unsigned channel,unsigned w,unsigned h) {
    const double sx=(x+.5)*image.width/w-.5,sy=(y+.5)*image.height/h-.5;
    const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
    const double fx=sx-ix,fy=sy-iy;
    auto at=[&](int px,int py){return double(image.pixels[(size_t(std::clamp(py,0,int(image.height)-1))*image.width+std::clamp(px,0,int(image.width)-1))*4+channel]);};
    return std::lerp(std::lerp(at(ix,iy),at(ix+1,iy),fx),std::lerp(at(ix,iy+1),at(ix+1,iy+1),fx),fy);
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    int failures=0;sink::RgbaImage planarReference;
    for(bool nv12:{false,true}) {
        auto deleter=[](AVFrame* f){av_frame_free(&f);};
        std::unique_ptr<AVFrame,decltype(deleter)> frame(av_frame_alloc(),deleter);
        if(!frame)return 3;
        frame->format=nv12?AV_PIX_FMT_NV12:AV_PIX_FMT_YUV420P;frame->width=width;frame->height=height;
        frame->color_range=AVCOL_RANGE_MPEG;frame->colorspace=AVCOL_SPC_BT709;frame->color_trc=AVCOL_TRC_BT709;frame->color_primaries=AVCOL_PRI_BT709;
        if(av_frame_get_buffer(frame.get(),32)<0)return 4;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)frame->data[0][size_t(y)*frame->linesize[0]+x]=uint8_t(luma(x,y));
        for(unsigned y=0;y<height/2;++y) {
            std::fill_n(frame->data[1]+size_t(y)*frame->linesize[1],nv12?width:width/2,uint8_t(128));
            if(!nv12)std::fill_n(frame->data[2]+size_t(y)*frame->linesize[2],width/2,uint8_t(128));
        }
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc desc;
        desc.sourceWidth=desc.workWidth=width;desc.sourceHeight=desc.workHeight=height;
        desc.enableNr=desc.enableSr=desc.enableFg=false;desc.noFeatures=true;
        if(!graph.initialize(desc)||!graph.createViews())return 5;
        pipeline::ColorDescription fallback;fallback.displayReferred709=true;
        auto color=pipeline::resolveFrameColor(*frame,fallback);
        pipeline::EnhanceGraph::FrameOutputs output;
        if(!graph.process(frame.get(),0,true,output,1,&color))return 6;
        sink::RgbaImage image;
        if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(output.videoSlot),image)||image.width!=width||image.height!=height)return 7;
        double grayError=0;unsigned clippedEdges=0;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c) {
            auto value=image.pixels[(size_t(y)*width+x)*4+c];
            grayError=std::max(grayError,std::abs(value-expectedGray(luma(x,y))));
            if(y<2*height/3&&value!=(luma(x,y)==16?0:255))++clippedEdges;
        }
        const bool identical=!nv12||image.pixels==planarReference.pixels;
        if(!nv12)planarReference=image;
        const bool ingressOk=grayError<1.1&&clippedEdges==0&&identical;
        std::cout<<"INGRESS_FIDELITY nv12="<<nv12<<" extent="<<image.width<<"x"<<image.height<<" grayMaxError="<<grayError<<" edgeErrors="<<clippedEdges<<" samePlanes="<<identical<<" pass="<<ingressOk<<std::endl;
        failures+=!ingressOk;
        for(bool compatible:{false,true})for(unsigned w:{1920u,2560u,3840u}) {
            const unsigned h=w*9/16;
            HWND window=CreateWindowExW(0,L"STATIC",L"Source fidelity diagnostic",WS_POPUP,0,0,w,h,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            engine::VideoPresenter presenter;
            if(!window||!presenter.open(ctx,window,graph,compatible)||!presenter.present(ctx,ring,graph,output.videoSlot,false))return 8;
            sink::RgbaImage displayed;
            if(!presenter.readPresentedFrameForTest(ctx,ring,displayed)||displayed.width!=w||displayed.height!=h)return 9;
            double maxError=0,sum=0;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<3;++c) {
                const auto error=std::abs(displayed.pixels[(size_t(y)*w+x)*4+c]-sampled(image,x,y,c,w,h));
                maxError=std::max(maxError,error);sum+=error;
            }
            const bool ok=maxError<2;
            std::cout<<"PRESENT_FIDELITY nv12="<<nv12<<" compatible="<<compatible<<" extent="<<w<<"x"<<h<<" maxError="<<maxError<<" meanError="<<sum/(double(w)*h*3)<<" pass="<<ok<<std::endl;
            failures+=!ok;ring.drainQueue();presenter.close();DestroyWindow(window);
        }
        output={};ring.drainQueue();graph.shutdown();
    }
    ring.shutdown();ctx.shutdown();CoUninitialize();return failures?1:0;
}
