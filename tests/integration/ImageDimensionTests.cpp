#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/TiledImageProcessor.h"
#include "veyra/pipeline/ColorMetadata.h"
#include <filesystem>
#include <iostream>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;
bool run(unsigned width,unsigned height,bool nr,const std::filesystem::path& directory){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.workWidth=width;gd.sourceHeight=gd.workHeight=height;
    gd.rgbInput=true;gd.stillImage=true;gd.enableFg=false;gd.enableNr=nr;gd.noFeatures=!nr;
    gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(!graph.initialize(gd)||!graph.createViews())return false;
    if(graph.fgCapabilityAvailable()||graph.fgCreated())return false;
    graph.setFgEnabled(true);if(graph.fgEnabled())return false;
    engine::EnhancementSettings settings;settings.multiplier=2;
    if(graph.applySettings(settings))return false;
    AVFrame* f=av_frame_alloc();f->width=width;f->height=height;f->format=AV_PIX_FMT_RGBA;
    f->color_range=AVCOL_RANGE_JPEG;f->color_trc=AVCOL_TRC_IEC61966_2_1;f->colorspace=AVCOL_SPC_RGB;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;
        p[0]=uint8_t((x*13+y*7)%256);p[1]=uint8_t((x*3+y*11)%256);p[2]=uint8_t((x*5+y*17+113)%256);p[3]=255;}
    pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
    bool ok=graph.process(f,0,true,out,1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
    int maxError=0;uint64_t brightness=0;
    if(ok){ok=result.width==width&&result.height==height;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c){const int value=result.pixels[(size_t(y)*width+x)*4+c];brightness+=value;maxError=std::max(maxError,std::abs(value-int(f->data[0][size_t(y)*f->linesize[0]+x*4+c])));}
        ok=ok&&brightness>0&&(nr?graph.metrics().nrEvaluateCount==1:maxError<=1);
        const auto path=directory/(std::to_string(width)+"x"+std::to_string(height)+(nr?"-nr.png":"-off.png"));
        sink::RgbaImage decoded;ok=ok&&sink::saveImage(path.wstring(),result)&&sink::loadImage(path.wstring(),decoded);
        ok=ok&&decoded.width==width&&decoded.height==height&&decoded.pixels==result.pixels;
    }
    std::cout<<"IMAGE_DIMENSION "<<width<<'x'<<height<<" nr="<<nr<<" maxError8="<<maxError<<" nrEvaluations="<<graph.metrics().nrEvaluateCount<<" pass="<<ok<<std::endl;
    out={};ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
bool tiled(unsigned width,unsigned height,bool nr,const std::filesystem::path& directory){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    sink::RgbaImage source;source.width=width;source.height=height;source.pixels.resize(size_t(width)*height*4);
    for(size_t i=0;i<source.pixels.size();i+=4){source.pixels[i]=uint8_t(i/4%251);source.pixels[i+1]=uint8_t(i/width%239);source.pixels[i+2]=113;source.pixels[i+3]=255;}
    // Independent display contract: half-transparent white composites to
    // sRGB 188 over black; fully transparent color contributes nothing.
    if(!nr){source.pixels[0]=source.pixels[1]=source.pixels[2]=255;source.pixels[3]=128;source.pixels[7]=0;}
    pipeline::EnhanceGraphDesc gd;gd.enableNr=nr;gd.noFeatures=!nr;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    std::atomic<bool> cancel=false;engine::TiledImageProcessor::Stats stats;sink::RgbaImage result;
    bool ok=engine::TiledImageProcessor::process(ctx,ring,source,result,gd,cancel,stats);
    int maxError=0;uint64_t brightness=0;
    if(ok){ok=result.width==width&&result.height==height&&result.pixels.size()==source.pixels.size();
        for(size_t i=0;i<result.pixels.size();++i){int expected=source.pixels[i];if(!nr&&i<8)expected=i%4==3?255:(i<4?188:0);maxError=std::max(maxError,std::abs(int(result.pixels[i])-expected));if(i%4!=3)brightness+=result.pixels[i];}
        ok=ok&&brightness>0&&(nr?stats.nrEvaluations==stats.tiles:maxError<=1);
        sink::RgbaImage decoded;auto path=directory/(std::to_string(width)+"x"+std::to_string(height)+(nr?"-tiled-nr.png":"-tiled-off.png"));
        ok=ok&&sink::saveImage(path.wstring(),result)&&sink::loadImage(path.wstring(),decoded)&&decoded.pixels==result.pixels&&decoded.width==width&&decoded.height==height;
    }
    std::cout<<"IMAGE_TILED "<<width<<'x'<<height<<" nr="<<nr<<" tiles="<<stats.tiles<<" evaluations="<<stats.nrEvaluations<<" maxError8="<<maxError<<" pass="<<ok<<std::endl;
    if(ok&&!nr){cancel=false;sink::RgbaImage abandoned;engine::TiledImageProcessor::Stats partial;
        const bool accepted=engine::TiledImageProcessor::process(ctx,ring,source,abandoned,gd,cancel,partial,[&](uint32_t,uint32_t){cancel=true;});
        ok=!accepted&&abandoned.pixels.empty()&&partial.tiles==1;
        std::cout<<"IMAGE_TILED_CANCEL pass="<<ok<<std::endl;
    }
    ring.shutdown();ctx.shutdown();return ok;
}
bool codecBands(const std::filesystem::path& directory){
    sink::RgbaImage source;source.width=1024;source.height=3073;source.pixels.resize(size_t(source.width)*source.height*4);
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){auto* p=source.pixels.data()+(size_t(y)*source.width+x)*4;p[std::min(2u,y/1024)]=255;p[3]=255;}
    bool ok=true;
    for(bool jpeg:{false,true}){sink::RgbaImage decoded;const auto path=directory/(jpeg?"banded.jpg":"banded.png");
        ok=ok&&sink::saveImage(path.wstring(),source,jpeg)&&sink::loadImage(path.wstring(),decoded)&&decoded.width==source.width&&decoded.height==source.height;
        if(!ok)break;
        if(!jpeg)ok=decoded.pixels==source.pixels;
        else for(unsigned y:{512u,1536u,2560u})for(unsigned c=0;c<3;++c){auto i=(size_t(y)*source.width+512)*4+c;ok=ok&&std::abs(int(decoded.pixels[i])-int(source.pixels[i]))<=5;}
    }
    std::cout<<"IMAGE_CODEC_BANDS pass="<<ok<<std::endl;return ok;
}
bool colorFallback(){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=64;gd.stillImage=gd.noFeatures=true;gd.enableNr=gd.enableFg=false;
    if(!graph.initialize(gd)||!graph.createViews())return false;
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_YUV420P;f->width=f->height=64;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    bool ok=true;uint64_t sequence=0;
    auto test=[&](int y,int u,int v,AVColorTransferCharacteristic trc,int expectedR,int expectedG,int expectedB){
        f->color_trc=trc;f->color_range=AVCOL_RANGE_UNSPECIFIED;f->colorspace=AVCOL_SPC_UNSPECIFIED;
        for(int row=0;row<64;++row)memset(f->data[0]+row*f->linesize[0],y,64);
        for(int row=0;row<32;++row){memset(f->data[1]+row*f->linesize[1],u,32);memset(f->data[2]+row*f->linesize[2],v,32);}
        auto metadata=pipeline::resolveFrameColor(*f);pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        bool pass=graph.process(f,0,true,out,++sequence,&metadata)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        if(pass){auto* p=result.pixels.data()+(32*64+32)*4;pass=std::abs(int(p[0])-expectedR)<=2&&std::abs(int(p[1])-expectedG)<=2&&std::abs(int(p[2])-expectedB)<=2;std::cout<<"COLOR_PIXEL transfer="<<trc<<" rgb="<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<" pass="<<pass<<std::endl;}
        out={};return pass;
    };
    ok=test(128,128,128,AVCOL_TRC_UNSPECIFIED,142,142,142)&&test(128,128,128,AVCOL_TRC_IEC61966_2_1,130,130,130)&&test(128,128,128,AVCOL_TRC_LINEAR,189,189,189)&&test(81,90,240,AVCOL_TRC_UNSPECIFIED,255,0,0);
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
bool captureRgb(bool nr){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=256;gd.rgbInput=true;gd.enableNr=nr;gd.enableNvofStandalone=nr;gd.enableFg=false;gd.noFeatures=!nr;
    gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(!graph.initialize(gd)||!graph.createViews())return false;
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_BGR0;f->width=f->height=256;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    bool ok=true;int maxError=0;
    for(unsigned frame=0;frame<4&&ok;++frame){
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;
            // Adjacent saturated red/blue proves that no 4:2:0 averaging occurs.
            p[0]=frame<2?(x%2?255:0):255;p[1]=frame<2?0:255;p[2]=frame<2?(x%2?0:255):255;p[3]=0;}
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        ok=graph.process(f,frame*33.333333,frame==0,out,frame+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        if(ok&&!nr)for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto* expected=f->data[0]+size_t(y)*f->linesize[0]+x*4;auto* actual=result.pixels.data()+(size_t(y)*256+x)*4;
            for(unsigned c=0;c<3;++c)maxError=std::max(maxError,std::abs(int(actual[c])-int(expected[2-c])));ok=ok&&actual[3]==255;}
        out={};
    }
    const auto m=graph.metrics();ok=ok&&maxError<=1&&m.sceneCutCount==1&&(!nr||(m.nrEvaluateCount==4&&m.nvofExecuteCount>=2));
    std::cout<<"CAPTURE_RGB nr="<<nr<<" maxError8="<<maxError<<" cuts="<<m.sceneCutCount<<" nrEvaluations="<<m.nrEvaluateCount<<" nvof="<<m.nvofExecuteCount<<" pass="<<ok<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    bool ok=true;for(auto e:{pipeline::Extent{1,1},{257,513},{97,9001},{4097,257},{257,4097}}){if(!run(e.width,e.height,false,directory)){ok=false;break;}}
    if(ok)ok=run(257,513,true,directory);
    if(ok)ok=run(97,9001,true,directory);
    if(ok)ok=tiled(17001,17,false,directory);
    if(ok)ok=tiled(17,17001,false,directory);
    if(ok)ok=tiled(97,17001,true,directory);
    if(ok)ok=tiled(2561,2561,false,directory);
    if(ok)ok=tiled(2561,2561,true,directory);
    if(ok)ok=codecBands(directory);
    if(ok)ok=colorFallback();
    if(ok)ok=captureRgb(false)&&captureRgb(true);
    CoUninitialize();return ok?0:1;
}
