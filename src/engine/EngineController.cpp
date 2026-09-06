#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <chrono>
#include <filesystem>
#include <format>
#include <cmath>
#include <deque>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
using Clock=std::chrono::steady_clock;
void EngineController::open(HWND video,const std::wstring& path,PlayerOptions opts){stop();{std::lock_guard lock(mutex_);snapshot_={};savePath_.clear();}stop_=false;paused_=false;seekSeconds_=-1;worker_=std::thread(&EngineController::run,this,video,path,opts);}
void EngineController::stop(){stop_=true;if(worker_.joinable())worker_.join();}
void EngineController::saveFrame(const std::wstring& path){std::lock_guard lock(mutex_);savePath_=path;}
void EngineController::startExport(const std::wstring& input,const std::wstring& output,PlayerOptions opts,bool hevc){stop();stop_=false;worker_=std::thread([this,input,output,opts,hevc]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool ok=exportVideo(input,output,opts,hevc,stop_,[this](double p,const std::wstring& s){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.position=p;snapshot_.duration=1;snapshot_.running=true;});{std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.failed=!ok&&!stop_;}CoUninitialize();});}
PlayerSnapshot EngineController::snapshot()const{std::lock_guard lock(mutex_);return snapshot_;}
void EngineController::status(const std::wstring& s,bool failed){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.failed=failed;}
void EngineController::run(HWND window,std::wstring path,PlayerOptions options){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    status(L"Initializing GPU and local runtime...");
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;
    sink::AudioPipeline audioPipe;sink::AudioRenderer audio;VideoPresenter presenter;
    pipeline::EnhanceGraph graph(ctx,ring);AVFrame* imageFrame=nullptr;
    source::CaptureCardSource captureSource;const bool isCapture=path.rfind(L"capture:",0)==0;
    source::IFrameSource* activeSource=isCapture?static_cast<source::IFrameSource*>(&captureSource):&source;
    bool audioStarted=false;bool failed=false;
    try {
        do {
            Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
            if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st)){status(L"D3D12 initialization failed; see logs",true);break;}
            auto ext=std::filesystem::path(path).extension().wstring();for(auto& c:ext)c=towlower(c);
            const bool isImage=ext==L".png"||ext==L".jpg"||ext==L".jpeg";
            sink::RgbaImage image;
            uint32_t width=0,height=0;double duration=0;
            if(isImage){
                if(!sink::loadImage(path,image)){status(L"Cannot decode PNG/JPEG",true);break;}
                imageFrame=av_frame_alloc();imageFrame->format=AV_PIX_FMT_RGBA;imageFrame->width=image.width;imageFrame->height=image.height;imageFrame->pts=0;imageFrame->color_range=AVCOL_RANGE_JPEG;
                if(av_frame_get_buffer(imageFrame,32)<0){status(L"Image allocation failed",true);break;}
                for(unsigned y=0;y<image.height;++y)memcpy(imageFrame->data[0]+size_t(y)*imageFrame->linesize[0],image.pixels.data()+size_t(y)*image.width*4,size_t(image.width)*4);
                width=image.width;height=image.height;options.fg=false;
            }else{
                source::SourceOpenDesc od;od.path=path;od.preferHardwareDecode=false;
                if(!activeSource->open(od)){status(L"Cannot open video; see logs",true);break;}
                width=activeSource->info().width;height=activeSource->info().height;duration=activeSource->info().duration.toDouble();
            }
            if(width>3840||height>2160||width%2||height%2){status(L"V1 supports even-sized SDR inputs up to 3840 x 2160",true);break;}
            pipeline::EnhanceGraphDesc gd;gd.sourceWidth=width;gd.sourceHeight=height;
            gd.workWidth=options.sr?3840:width;gd.workHeight=options.sr?2160:height;
            if(!isImage&&options.realtime&&!options.sr&&width>1920){gd.workWidth=1920;gd.workHeight=((uint64_t(height)*1920/width)+1)&~1u;}
            gd.enableSr=options.sr&&(width!=3840||height!=2160);gd.enableNr=options.nr;gd.enableFg=options.fg;gd.enableNvofStandalone=options.nr;
            gd.runtimeAbsPath=std::filesystem::path(VEYRA_PROJECT_ROOT).wstring()+L"\\runtime_local\\nvidia";
            if(!graph.initialize(gd)||!presenter.open(ctx,window,graph)||!graph.createViews()){status(L"Enhancement initialization failed; local runtime required",true);break;}
            {std::lock_guard lock(mutex_);snapshot_.duration=duration;snapshot_.running=true;snapshot_.image=isImage;}
            status(isImage?L"Image enhanced — Save image to export":std::format(L"{} | Input {}x{} / processing {}x{} | Depth unavailable",isCapture?L"Capture (hardware test pending)":L"Playing",width,height,gd.workWidth,gd.workHeight));
            pipeline::EnhanceGraph::FrameOutputs out;bool reset=true,hasOutput=false;std::deque<double> latenessSamples;
            auto anchor=Clock::now(),statsStart=anchor;double anchorMs=0;uint64_t frames=0;bool wasPaused=false;double discardBefore=0;
            while(!stop_){
                std::wstring save;{std::lock_guard lock(mutex_);save.swap(savePath_);}
                if(!save.empty()&&hasOutput){sink::RgbaImage result;const auto e=std::filesystem::path(save).extension().wstring();
                    if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result)||!sink::saveImage(save,result,e==L".jpg"||e==L".jpeg"))status(L"Save failed (destination may already exist)",true);else status(L"Saved image: "+save);}
                const double seek=seekSeconds_.exchange(-1);
                if(seek>=0&&!isImage&&!isCapture){
                    if(!ring.drainQueue()||!activeSource->seek({static_cast<int64_t>(seek*1000000),1000000})){status(L"Seek failed",true);break;}
                    discardBefore=seek*1000;reset=true;anchorMs=discardBefore;anchor=Clock::now();if(audioStarted)audioPipe.requestSeek(discardBefore);
                }
                if((paused_&&seek<0)||(isImage&&hasOutput)){
                    if(audioStarted)audioPipe.setPaused(true);wasPaused=true;
                    if(hasOutput&&!presenter.present(ctx,ring,graph,out.videoSlot,false)){status(L"Present failed",true);break;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;
                }
                if(wasPaused&&!paused_){if(audioStarted)audioPipe.setPaused(false);anchor=Clock::now();anchorMs=out.ptsMs;reset=true;wasPaused=false;}
                pipeline::FramePacket pkt;const AVFrame* frame=imageFrame;
                if(!isImage){auto rs=activeSource->read(pkt,&frame);if(rs==source::SourceReadStatus::Waiting)continue;if(rs==source::SourceReadStatus::Eos){status(L"End of video");paused_=true;continue;}if(rs!=source::SourceReadStatus::Frame||pkt.pts.isUnknown()){status(L"Video decode/timestamp error",true);break;}}
                const double pts=isImage?0:pkt.pts.toDouble()*1000;
                if(isCapture&&frames==0){anchor=Clock::now();anchorMs=pts;}
                if(pts+0.1<discardBefore)continue;
                if(!graph.process(frame,pts,reset||pipeline::breaksHistory(pkt.flags),out)){status(L"Enhancement failed; see logs",true);break;}reset=false;hasOutput=true;
                if(!audioStarted&&!isImage&&!isCapture&&frames==0){if(audioPipe.open(path)&&audio.start()){audioPipe.startThread(&audio);audioStarted=true;}anchor=Clock::now();anchorMs=pts;}
                auto nowMs=[&](){const double a=audioStarted?audio.mediaTimeMs():-1;return a>=0?a:anchorMs+std::chrono::duration<double,std::milli>(Clock::now()-anchor).count();};
                auto show=[&](double target,bool generated){while(!stop_&&!paused_&&seekSeconds_<0&&nowMs()+0.5<target)std::this_thread::sleep_for(std::chrono::milliseconds(1));return !stop_&&presenter.present(ctx,ring,graph,generated?out.genSlot:out.videoSlot,generated);};
                if(out.hasGenerated&&!show(out.generatedPtsMs,true))break;
                if(!show(pts,false)){status(L"Present failed",!stop_);break;}
                const double lateness=nowMs()-pts;
                if(!paused_){latenessSamples.push_back(std::abs(lateness));if(latenessSamples.size()>1200)latenessSamples.pop_front();}
                std::vector<double> sorted(latenessSamples.begin(),latenessSamples.end());std::sort(sorted.begin(),sorted.end());
                ++frames;{std::lock_guard lock(mutex_);snapshot_.position=pts/1000;snapshot_.frames=frames;snapshot_.generated=graph.metrics().fgGeneratedFrames;snapshot_.lateMs=lateness;snapshot_.lateP95Ms=sorted.empty()?0:sorted[size_t((sorted.size()-1)*0.95)];snapshot_.fps=frames/std::max(0.001,std::chrono::duration<double>(Clock::now()-statsStart).count());}
            }
        }while(false);
    }catch(const std::exception& e){veyra::log::error("engine",e.what());status(L"Engine exception; see logs",true);failed=true;}
    (void)failed;
    audioPipe.stopThread();audio.shutdown();ring.drainQueue();presenter.close();graph.shutdown();captureSource.close();source.close();av_frame_free(&imageFrame);ring.shutdown();ctx.shutdown();
    {std::lock_guard lock(mutex_);snapshot_.running=false;}
    CoUninitialize();
}
}
