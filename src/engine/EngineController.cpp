#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/DeadlineWait.h"
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
namespace {
double elapsedMs(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
struct TimingWindow {
    std::deque<double> values;
    void add(double v){values.push_back(v);if(values.size()>1200)values.pop_front();}
    double p95()const{if(values.empty())return 0;std::vector<double> sorted(values.begin(),values.end());std::sort(sorted.begin(),sorted.end());return sorted[size_t((sorted.size()-1)*.95)];}
};
}
EngineController::EngineController(){worker_=std::thread(&EngineController::dispatch,this);}
EngineController::~EngineController(){ {std::lock_guard lock(mutex_);shutdown_=true;pending_={};stop_=true;}wake_.notify_one();if(worker_.joinable())worker_.join(); }
void EngineController::dispatch(){
    for(;;){std::function<void()> task;{std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return shutdown_||bool(pending_);});if(shutdown_)break;task=std::move(pending_);pending_={};busy_=true;stop_=false;}
        try{task();}catch(const std::exception&){status(L"任务异常，已停止；请查看诊断",true);}
        {std::lock_guard lock(mutex_);busy_=false;snapshot_.running=false;if(snapshot_.transport==TransportState::Stopping)snapshot_.transport=TransportState::Empty;}
    }
}
void EngineController::post(std::function<void()> task){ {std::lock_guard lock(mutex_);stop_=true;pending_=std::move(task);}wake_.notify_one(); }
bool EngineController::idle()const{std::lock_guard lock(mutex_);return !busy_&&!pending_;}
void EngineController::open(HWND video,const std::wstring& path,PlayerOptions opts){
    {std::lock_guard lock(mutex_);snapshot_={};previewView_={};snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;savePath_.clear();desired_=opts.snapshot();desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_);}
    post([this,video,path,opts]{paused_=false;seekSeconds_=-1;run(video,path,opts);});
}
void EngineController::stop(){std::lock_guard lock(mutex_);stop_=true;pending_={};snapshot_.transport=busy_?TransportState::Stopping:TransportState::Empty;}
void EngineController::pause(bool p){paused_=p;std::lock_guard lock(mutex_);if(snapshot_.running)snapshot_.transport=p?TransportState::Paused:TransportState::Playing;}
void EngineController::setVolume(float gain,bool mute){if(!std::isfinite(gain))return;volume_=std::clamp(gain,0.0f,1.0f);muted_=mute;}
bool EngineController::requestSettings(EnhancementSettings s){
    if(!s.validate().empty()){veyra::log::warn("settings","invalid whole settings transaction rejected");status(L"整套设置无效，未应用任何字段",false);return false;}
    std::lock_guard lock(mutex_);if(snapshot_.image)s.multiplier=1;s.revision=++nextRevision_;desired_=s;snapshot_.desired=s;snapshot_.applying=true;return true;
}
void EngineController::saveFrame(const std::wstring& path){std::lock_guard lock(mutex_);savePath_=path;}
void EngineController::startExport(const std::wstring& input,const std::wstring& output,PlayerOptions opts,bool hevc){
    const auto frozen=opts.snapshot();post([this,input,output,frozen,hevc]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool ok=exportVideo(input,output,PlayerOptions::from(frozen),hevc,stop_,[this](double p,const std::wstring& s){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.position=p;snapshot_.duration=1;snapshot_.running=true;});{std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.failed=!ok&&!stop_;}CoUninitialize();});
}
PlayerSnapshot EngineController::snapshot()const{std::lock_guard lock(mutex_);auto copy=snapshot_;copy.volume=volume_;copy.muted=muted_;return copy;}
void EngineController::status(const std::wstring& s,bool failed){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.failed=failed;if(failed)snapshot_.transport=TransportState::Failed;}
void EngineController::run(HWND window,std::wstring path,PlayerOptions options){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    status(L"正在初始化GPU与本地运行时…");
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;
    sink::AudioPipeline audioPipe;sink::AudioRenderer audio;VideoPresenter presenter;
    pipeline::EnhanceGraph graph(ctx,ring);AVFrame* imageFrame=nullptr;AVFrame* cachedFrame=nullptr;pipeline::FramePacket cachedPacket;
    source::CaptureCardSource captureSource;const bool isCapture=path.rfind(L"capture:",0)==0;
    source::IFrameSource* activeSource=isCapture?static_cast<source::IFrameSource*>(&captureSource):&source;
    bool audioStarted=false;bool failed=false;
    try {
        do {
            Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
            if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st)){status(L"D3D12初始化失败，请查看诊断",true);break;}
            auto ext=std::filesystem::path(path).extension().wstring();for(auto& c:ext)c=towlower(c);
            const bool isImage=ext==L".png"||ext==L".jpg"||ext==L".jpeg";
            sink::RgbaImage image;
            uint32_t width=0,height=0;double duration=0;
            if(isImage){
                if(!sink::loadImage(path,image)){status(L"无法解码PNG/JPEG",true);break;}
                if(!pipeline::Extent{image.width,image.height}.valid()||uint64_t(image.width)*image.height>16777216){
                    runLargeImage(window,image,options,ctx,ring);break;
                }
                imageFrame=av_frame_alloc();imageFrame->format=AV_PIX_FMT_RGBA;imageFrame->width=image.width;imageFrame->height=image.height;imageFrame->pts=0;imageFrame->color_range=AVCOL_RANGE_JPEG;imageFrame->colorspace=AVCOL_SPC_RGB;imageFrame->color_trc=AVCOL_TRC_IEC61966_2_1;
                if(av_frame_get_buffer(imageFrame,32)<0){status(L"图片资源分配失败",true);break;}
                for(unsigned y=0;y<image.height;++y)memcpy(imageFrame->data[0]+size_t(y)*imageFrame->linesize[0],image.pixels.data()+size_t(y)*image.width*4,size_t(image.width)*4);
                width=image.width;height=image.height;options.fg=false;
                {std::lock_guard lock(mutex_);desired_.multiplier=1;snapshot_.image=true;snapshot_.desired=desired_;}
            }else{
                source::SourceOpenDesc od;od.path=path;od.preferHardwareDecode=false;
                if(!(isCapture?captureSource.configure(od):activeSource->open(od))){status(L"无法打开视频，请查看诊断",true);break;}
                width=activeSource->info().width;height=activeSource->info().height;duration=activeSource->info().duration.toDouble();
            }
            if(!pipeline::Extent{width,height}.valid()){status(L"图像尺寸超出单张GPU纹理能力，需要分块处理",true);break;}
            pipeline::EnhanceGraphDesc gd;gd.sourceWidth=width;gd.sourceHeight=height;gd.rgbInput=isImage||isCapture;gd.stillImage=isImage;
            const auto resolution=pipeline::ResolutionPlan::make({width,height},options.sr,options.realtime?pipeline::NrSizePolicy::Realtime:pipeline::NrSizePolicy::Native,isImage,1);
            gd.workWidth=resolution.base.width;gd.workHeight=resolution.base.height;gd.nrWidth=resolution.nr.width;gd.nrHeight=resolution.nr.height;
            gd.enableSr=resolution.srApplied;gd.enableNr=options.nr;gd.enableFg=options.fg;gd.fgMultiplier=options.fgMultiplier;gd.enableNvofStandalone=options.nr;
            gd.noFeatures=false;gd.model=options.settings.model;gd.residual=options.settings.residual;gd.settingsRevision=options.settings.revision;gd.flowQuality=options.settings.flow;gd.contentRate=options.settings.content;
            gd.runtimeAbsPath=std::filesystem::path(VEYRA_PROJECT_ROOT).wstring()+L"\\runtime_local\\nvidia";
            if(!graph.initialize(gd)||!presenter.open(ctx,window,graph)||!graph.createViews()){status(L"增强初始化失败，请核对本地运行时",true);break;}
            if(isCapture&&!captureSource.start()){status(L"无法启动采集，请查看诊断",true);break;}
            {std::lock_guard lock(mutex_);snapshot_.duration=duration;snapshot_.running=true;snapshot_.transport=TransportState::Playing;snapshot_.image=isImage;snapshot_.capture=isCapture;snapshot_.applied=options.snapshot();snapshot_.desired=desired_;}
            status(isImage?L"图片已增强，可保存PNG/JPEG":std::format(L"{} | 输入 {}×{} / 底图 {}×{} / NR {}×{} / 光流 {}×{} / FG与输出 {}×{} | {}",isCapture?L"实时采集":L"播放",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,width,height,gd.workWidth,gd.workHeight,gd.nrWidth<gd.workWidth?L"实时NR变化回填":L"原生NR（性能成本较高）"));
            pipeline::EnhanceGraph::FrameOutputs out;bool reset=true,hasOutput=false;std::deque<double> latenessSamples;
            TimingWindow captureAges,scheduleWaits,processTimes,presentTimes,decodeTimes,gpuReadyTimes;auto nextTimingLog=Clock::now()+std::chrono::seconds(1);
            auto anchor=Clock::now(),statsStart=anchor;double anchorMs=0;uint64_t frames=0,sourceFrames=0;bool wasPaused=false;double discardBefore=0;
            PresentationScheduler liveTimeline;uint64_t submitted=0,expired=0;std::deque<int64_t> submissionTimes;
            DeadlineWait deadlineWait;
            auto host100ns=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;};
            while(!stop_){
                const float gain=muted_?0.0f:volume_.load();audio.setGain(gain);
                if(isCapture){const bool available=captureSource.setAudioGain(gain);std::lock_guard lock(mutex_);snapshot_.audioAvailable=available;}
                // Save the currently displayed result before a settings transaction
                // invalidates it. Keep a request queued until a frame exists.
                std::wstring save;EnhancementSettings requested;{std::lock_guard lock(mutex_);requested=desired_;if(hasOutput)save.swap(savePath_);}
                if(!save.empty()){try{sink::RgbaImage result;const auto e=std::filesystem::path(save).extension().wstring();
                    if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result)||!sink::saveImage(save,result,e==L".jpg"||e==L".jpeg"))status(L"保存失败（目标文件可能已存在），播放已保留",false);else{status(L"图片已保存："+save);veyra::log::info("image-save",std::format("saved extent={}x{} revision={}",gd.workWidth,gd.workHeight,options.settings.revision));}}
                    catch(const std::exception& e){veyra::log::warn("image-save",std::format("save exception; retaining session: {}",e.what()));status(L"保存异常，播放已保留；可再次保存",false);}}
                if(isImage)requested.multiplier=1;
                const auto previous=options.snapshot();const auto previousDesc=gd;bool transaction=false;
                if(requested.revision!=previous.revision){
                    auto next=PlayerOptions::from(requested);auto nextDesc=gd;
                    const auto plan=pipeline::ResolutionPlan::make({width,height},next.sr,requested.nrPolicy,isImage,requested.revision);
                    nextDesc.workWidth=plan.base.width;nextDesc.workHeight=plan.base.height;nextDesc.nrWidth=plan.nr.width;nextDesc.nrHeight=plan.nr.height;
                    nextDesc.enableSr=plan.srApplied;nextDesc.enableNr=next.nr;nextDesc.enableFg=next.fg;nextDesc.fgMultiplier=next.fgMultiplier;nextDesc.enableNvofStandalone=next.nr;
                    nextDesc.model=requested.model;nextDesc.residual=requested.residual;nextDesc.settingsRevision=requested.revision;nextDesc.flowQuality=requested.flow;nextDesc.contentRate=requested.content;
                    const bool rebuild=gd.flowQuality!=nextDesc.flowQuality||gd.workWidth!=nextDesc.workWidth||gd.workHeight!=nextDesc.workHeight||gd.nrWidth!=nextDesc.nrWidth||gd.nrHeight!=nextDesc.nrHeight;
                    bool accepted=ring.drainQueue();out={};hasOutput=false;
                    if(accepted&&rebuild){presenter.close();graph.shutdown();accepted=graph.initialize(nextDesc)&&presenter.open(ctx,window,graph)&&graph.createViews();
                        if(!accepted){presenter.close();graph.shutdown();if(!graph.initialize(gd)||!presenter.open(ctx,window,graph)||!graph.createViews()){status(L"设置失败且旧资源恢复失败，已停止",true);break;}}
                    }else if(accepted)accepted=graph.applySettings(requested);
                    if(accepted){options=next;gd=nextDesc;transaction=true;reset=true;}
                    else {std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desired_=previous;snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_.revision!=previous.revision;snapshot_.status=L"设置应用失败，已恢复上一套参数";}
                }
                const double seek=seekSeconds_.exchange(-1);
                if(seek>=0&&!isImage&&!isCapture){
                    if(!ring.drainQueue()||!activeSource->seek({static_cast<int64_t>(seek*1000000),1000000})){status(L"跳转失败",true);break;}
                    discardBefore=seek*1000;reset=true;anchorMs=discardBefore;anchor=Clock::now();if(audioStarted)audioPipe.requestSeek(discardBefore);
                }
                if(!transaction&&((paused_&&seek<0)||(isImage&&hasOutput))){
                    if(audioStarted)audioPipe.setPaused(true);wasPaused=true;
                    if(hasOutput&&!presenter.present(ctx,ring,graph,out.videoSlot,false,comparisonMode_,comparisonBase_,comparisonSplit_,out.batch.identity,previewView())){status(L"画面呈现失败",true);break;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;
                }
                if(wasPaused&&!paused_){if(audioStarted)audioPipe.setPaused(false);anchor=Clock::now();anchorMs=out.ptsMs;reset=true;wasPaused=false;}
                const auto decodeStart=Clock::now();
                pipeline::FramePacket pkt;const AVFrame* frame=imageFrame;
                if(transaction&&cachedFrame){frame=cachedFrame;pkt=cachedPacket;}
                else if(!isImage){auto rs=activeSource->read(pkt,&frame);if(rs==source::SourceReadStatus::Waiting)continue;if(rs==source::SourceReadStatus::Eos){status(L"视频已播放完毕");paused_=true;{std::lock_guard lock(mutex_);snapshot_.transport=TransportState::Ended;}continue;}if(rs!=source::SourceReadStatus::Frame||pkt.pts.isUnknown()){status(isCapture?L"采集信号中断，请检查设备连接或格式":L"视频解码或时间戳错误",true);break;}}
                const bool rereadCached=transaction&&frame==cachedFrame;
                if(isImage)pkt.sequence=1;
                if(!rereadCached)++sourceFrames;
                const double decodeMs=elapsedMs(decodeStart);decodeTimes.add(decodeMs);
                const double pts=isImage?0:pkt.pts.toDouble()*1000;
                if(isCapture&&frames==0){anchor=Clock::now();anchorMs=pts;}
                if(pts+0.1<discardBefore)continue;
                if(frame!=cachedFrame){av_frame_free(&cachedFrame);cachedFrame=av_frame_clone(frame);cachedPacket=pkt;}
                const auto processStart=Clock::now();
                const bool injectedReject=transaction&&!options.nr&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",nullptr,0)>0;
                if(injectedReject)veyra::log::error("settings-test","test-only reject NR-disable transaction before graph process; no driver failure");
                if(injectedReject||!graph.process(frame,pts,reset||pipeline::breaksHistory(pkt.flags),out,pkt.sequence,&pkt.colorInfo)){
                    if(transaction){
                        ring.drainQueue();out={};presenter.close();graph.shutdown();options=PlayerOptions::from(previous);
                        gd=previousDesc;
                        if(graph.initialize(gd)&&presenter.open(ctx,window,graph)&&graph.createViews()&&graph.process(frame,pts,true,out,pkt.sequence,&pkt.colorInfo)){
                            std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desired_=previous;snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_.revision!=previous.revision;snapshot_.status=L"参数执行失败，已整套回滚";transaction=false;
                        }else{status(L"参数回滚失败，已停止",true);break;}
                    }else{status(L"增强执行失败；请查看日志",true);break;}
                }
                if(transaction){std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_.revision!=options.settings.revision;snapshot_.status=std::format(L"输入 {}×{} / 底图 {}×{} / NR {}×{} / 光流 {}×{} / FG与输出 {}×{} | {}",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,width,height,gd.workWidth,gd.workHeight,gd.nrWidth<gd.workWidth?L"实时NR变化回填":L"原生NR（性能成本较高）");veyra::log::info("settings",std::format("Applied revision={} sourcePtsMs={} (source kept open)",options.settings.revision,pts));}
                veyra::log::info("source-identity",std::format("source={} totalRead={} graphProcessed={} cached={} revision={} nvofStandalone={}",pkt.sequence,sourceFrames,frames+1,rereadCached,options.settings.revision,gd.enableNvofStandalone));
                reset=false;hasOutput=true;
                const auto processDone=Clock::now();processTimes.add(elapsedMs(processStart));
                if(isCapture&&!liveTimeline.anchored(out.batch.identity.epoch)){
                    const auto duration100ns=liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps);
                    veyra::log::info("capture-timeline",std::format("interval100ns={} packetDurationKnown={} packetDurationPositive={} nominalFps={} FG={}",duration100ns,!pkt.duration.isUnknown(),pkt.duration.num>0,activeSource->info().averageFps,options.fg));
                    liveTimeline.reset(out.batch.identity.epoch,out.batch.b100ns,std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100,options.fg?duration100ns:0);
                }
                if(!audioStarted&&!isImage&&!isCapture&&frames==0){if(audioPipe.open(path)&&audio.start()){audioPipe.startThread(&audio);audioStarted=true;{std::lock_guard lock(mutex_);snapshot_.audioAvailable=true;}}anchor=Clock::now();anchorMs=pts;}
                auto nowMs=[&](){const double a=audioStarted?audio.mediaTimeMs():-1;return a>=0?a:anchorMs+std::chrono::duration<double,std::milli>(Clock::now()-anchor).count();};
                double frameWaitMs=0,framePresentMs=0;
                const auto readyStart=Clock::now();
                while(!stop_&&!paused_&&seekSeconds_<0&&!graph.resolveGeneration(out)){
                    if(elapsedMs(readyStart)>2000){status(L"补帧 GPU 就绪超时",true);stop_=true;break;}
                    deadlineWait.slice(.2);
                }
                if(stop_)break;
                if(paused_||seekSeconds_>=0){reset=true;veyra::log::info("submit",std::format("batch={} cancelled before submit (pause/seek)",out.batch.batchId));continue;}
                const double gpuWaitMs=elapsedMs(readyStart);gpuReadyTimes.add(gpuWaitMs);
                bool interrupted=false,presentFailed=false;
                for(uint32_t i=0;i<out.batch.count;++i){
                    auto& item=out.batch.frames[i];const bool generated=item.kind==pipeline::FrameKind::Generated;
                    if(generated&&(item.validity!=pipeline::GenerationValidity::Valid||comparisonMode_!=0))continue;
                    if(isCapture&&generated&&liveTimeline.expired(item.pts100ns,host100ns(),100000)){++expired;continue;}
                    const auto waitStart=Clock::now();
                    while(!stop_&&!paused_&&seekSeconds_<0&&(isCapture?host100ns()<liveTimeline.deadline(item.pts100ns):nowMs()+0.25<item.pts100ns/10000.0))deadlineWait.slice();
                    frameWaitMs+=elapsedMs(waitStart);
                    if(stop_||paused_||seekSeconds_>=0){interrupted=true;break;}
                    const auto begin=Clock::now();const auto before=presenter.submittedCount();
                    if(!presenter.present(ctx,ring,graph,item.lease->slot,generated,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView())){presentFailed=true;break;}
                    framePresentMs+=elapsedMs(begin);
                    item.lease->consumerFence=ring.lastSignaledValue();
                    if(presenter.submittedCount()>before){++submitted;const auto time=host100ns();submissionTimes.push_back(time);while(submissionTimes.size()>1&&time-submissionTimes.front()>10000000)submissionTimes.pop_front();veyra::log::info("submit",std::format("batch={} epoch={} revision={} subframe={} pts100ns={} host100ns={} fence={} (submission, display unmeasured)",out.batch.batchId,item.identity.epoch,item.identity.settingsRevision,item.subframe,item.pts100ns,host100ns(),item.lease->consumerFence));}
                }
                if(presentFailed){status(L"画面提交失败",true);break;}
                if(interrupted){reset=true;continue;}
                scheduleWaits.add(frameWaitMs);presentTimes.add(framePresentMs);
                const double lateness=isCapture?0:nowMs()-pts;
                if(!paused_&&!isCapture){latenessSamples.push_back(std::abs(lateness));if(latenessSamples.size()>1200)latenessSamples.pop_front();}
                std::vector<double> sorted(latenessSamples.begin(),latenessSamples.end());std::sort(sorted.begin(),sorted.end());
                const auto captureStats=isCapture?captureSource.metrics():source::CaptureMetrics{};
                if(isCapture)captureAges.add(captureStats.frameAgeMs);
                auto measured=graph.gpuMetrics();measured.gpu[size_t(diagnostics::GpuStage::Blit)]=presenter.blitTiming(ctx.fence());measured.resolution=pipeline::ResolutionPlan::make({width,height},options.sr,options.realtime?pipeline::NrSizePolicy::Realtime:pipeline::NrSizePolicy::Native,isImage,options.settings.revision);measured.decodeCpuMs=decodeMs;measured.submitCpuMs=elapsedMs(processStart)-elapsedMs(processDone);measured.gpuWaitCpuMs=gpuWaitMs;measured.deadlineWaitCpuMs=frameWaitMs;measured.presentCpuMs=framePresentMs;measured.submitted=submitted;measured.expired=expired;measured.sourceFrames=sourceFrames;measured.validGenerated=graph.metrics().fgGeneratedFrames;measured.queueWatermark=out.batch.count;
                ++frames;{std::lock_guard lock(mutex_);snapshot_.metrics=measured;if(submissionTimes.size()>1)snapshot_.submissionFps=double(submissionTimes.size()-1)*1e7/(submissionTimes.back()-submissionTimes.front());snapshot_.position=pts/1000;snapshot_.frames=sourceFrames;snapshot_.generated=graph.metrics().fgGeneratedFrames;snapshot_.lateMs=lateness;snapshot_.lateP95Ms=sorted.empty()?0:sorted[size_t((sorted.size()-1)*0.95)];snapshot_.fps=sourceFrames/std::max(0.001,std::chrono::duration<double>(Clock::now()-statsStart).count());
                    snapshot_.flowPerf=graph.actualFlowPerf();snapshot_.contentFps=out.measuredContentRate;
                    snapshot_.nrEvaluated=graph.metrics().nrEvaluateCount;snapshot_.nvofExecuted=graph.metrics().nvofExecuteCount;
                    snapshot_.captureReceived=captureStats.received;snapshot_.captureDropped=captureStats.dropped;snapshot_.captureFps=captureStats.callbackFps;snapshot_.captureReadAgeMs=captureStats.readAgeMs;snapshot_.captureAgeMs=captureStats.frameAgeMs;snapshot_.captureAgeP95Ms=captureAges.p95();
                    snapshot_.schedulingWaitP95Ms=scheduleWaits.p95();snapshot_.processCpuP95Ms=processTimes.p95();snapshot_.presentCpuP95Ms=presentTimes.p95();
                }
                if(!isCapture&&Clock::now()>=nextTimingLog){veyra::log::info("player-timing",std::format("decodeP95Ms={:.3f} graphSubmitP95Ms={:.3f} gpuReadyP95Ms={:.3f} presentP95Ms={:.3f} processed={}",decodeTimes.p95(),processTimes.p95(),gpuReadyTimes.p95(),presentTimes.p95(),sourceFrames));nextTimingLog=Clock::now()+std::chrono::seconds(1);}
                if(isCapture&&Clock::now()>=nextTimingLog){
                    veyra::log::info("capture-timing",std::format("received={} processed={} dropped={} callbackFps={:.2f} readAgeMs={:.3f} callbackToPresentReturnP95Ms={:.3f} processCpuP95Ms={:.3f} schedulingWaitP95Ms={:.3f} presentCpuP95Ms={:.3f} nr={} nvof={} generated={} (not HDMI-to-display latency)",captureStats.received,frames,captureStats.dropped,captureStats.callbackFps,captureStats.readAgeMs,captureAges.p95(),processTimes.p95(),scheduleWaits.p95(),presentTimes.p95(),graph.metrics().nrEvaluateCount,graph.metrics().nvofExecuteCount,graph.metrics().fgGeneratedFrames));
                    nextTimingLog=Clock::now()+std::chrono::seconds(1);
                }
            }
        }while(false);
    }catch(const std::exception& e){veyra::log::error("engine",e.what());status(L"引擎异常，请查看诊断",true);failed=true;}
    (void)failed;
    audioPipe.stopThread();audio.shutdown();ring.drainQueue();presenter.close();graph.shutdown();captureSource.close();source.close();av_frame_free(&cachedFrame);av_frame_free(&imageFrame);ring.shutdown();ctx.shutdown();
    {std::lock_guard lock(mutex_);snapshot_.running=false;}
    CoUninitialize();
}
}
