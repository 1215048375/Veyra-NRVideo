#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/DeadlineWait.h"
#include "veyra/engine/PresentationWorker.h"
#include "veyra/engine/LivePresentationResetPolicy.h"
#include "veyra/engine/TimingWindow.h"
#include "veyra/engine/CaptureHalfRate.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
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
int64_t monotonic100ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;}
struct OnExit {std::function<void()> action;~OnExit(){action();}};
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
    const bool captureReplay=opts.captureReplayForTest;
    {std::lock_guard lock(mutex_);snapshot_={};processingRate_.reset(monotonic100ns());previewView_={};snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;savePath_.clear();desired_=opts.snapshot();desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_);}
    opts.captureReplayForTest=captureReplay;
    post([this,video,path,opts]{paused_=false;seekSeconds_=-1;run(video,path,opts);});
}
void EngineController::stop(){std::lock_guard lock(mutex_);stop_=true;pending_={};snapshot_.transport=busy_?TransportState::Stopping:TransportState::Empty;}
void EngineController::pause(bool p){paused_=p;std::lock_guard lock(mutex_);processingRate_.reset(monotonic100ns());if(snapshot_.running)snapshot_.transport=p?TransportState::Paused:TransportState::Playing;}
void EngineController::setVolume(float gain,bool mute){if(!std::isfinite(gain))return;volume_=std::clamp(gain,0.0f,1.0f);muted_=mute;}
bool EngineController::requestSettings(EnhancementSettings s){
    if(!s.validate().empty()){veyra::log::warn("settings","invalid whole settings transaction rejected");status(L"整套设置无效，未应用任何字段",false);return false;}
    std::lock_guard lock(mutex_);if(snapshot_.image)s.multiplier=1;s.revision=++nextRevision_;desired_=s;snapshot_.desired=s;snapshot_.applying=true;return true;
}
void EngineController::saveFrame(const std::wstring& path){std::lock_guard lock(mutex_);savePath_=path;}
void EngineController::startExport(const std::wstring& input,const std::wstring& output,PlayerOptions opts,bool hevc){
    const auto frozen=opts.snapshot();post([this,input,output,frozen,hevc]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool ok=exportVideo(input,output,PlayerOptions::from(frozen),hevc,stop_,[this](double p,const std::wstring& s){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.position=p;snapshot_.duration=1;snapshot_.running=true;});{std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.failed=!ok&&!stop_;}CoUninitialize();});
}
PlayerSnapshot EngineController::snapshot()const{std::lock_guard lock(mutex_);auto copy=snapshot_;copy.volume=volume_;copy.muted=muted_;copy.fps=copy.running&&!copy.image&&copy.transport==TransportState::Playing?processingRate_.rate(monotonic100ns()):0;return copy;}
void EngineController::status(const std::wstring& s,bool failed){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.failed=failed;if(failed)snapshot_.transport=TransportState::Failed;}
void EngineController::run(HWND window,std::wstring path,PlayerOptions options){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    status(L"正在初始化GPU与本地运行时…");
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;
    sink::AudioPipeline audioPipe;sink::AudioRenderer audio;VideoPresenter presenter;
    pipeline::EnhanceGraph graph(ctx,ring);AVFrame* imageFrame=nullptr;AVFrame* cachedFrame=nullptr;pipeline::FramePacket cachedPacket;
    source::CaptureCardSource captureSource;const bool physicalCapture=path.rfind(L"capture:",0)==0;
    const bool isCapture=physicalCapture||options.captureReplayForTest;
    source::IFrameSource* activeSource=physicalCapture?static_cast<source::IFrameSource*>(&captureSource):&source;
    if(options.captureReplayForTest)veyra::log::info("capture-test","file replay exercises live scheduler; no physical capture device or latency measurement");
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
                if(!(physicalCapture?captureSource.configure(od):activeSource->open(od))){status(L"无法打开视频，请查看诊断",true);break;}
                width=activeSource->info().width;height=activeSource->info().height;duration=activeSource->info().duration.toDouble();
            }
            if(!pipeline::Extent{width,height}.valid()){status(L"图像尺寸超出单张GPU纹理能力，需要分块处理",true);break;}
            pipeline::EnhanceGraphDesc gd;gd.sourceWidth=width;gd.sourceHeight=height;
            gd.rgbInput=isImage||(isCapture&&activeSource->info().color.pixelFormat==pipeline::SourcePixelFormat::Bgra8);
            gd.yuy2Input=isCapture&&activeSource->info().color.pixelFormat==pipeline::SourcePixelFormat::Yuy2;gd.stillImage=isImage;
            const auto resolution=pipeline::ResolutionPlan::make({width,height},options.sr,options.realtime?pipeline::NrSizePolicy::Realtime:pipeline::NrSizePolicy::Native,isImage,1);
            gd.workWidth=resolution.base.width;gd.workHeight=resolution.base.height;gd.nrWidth=resolution.nr.width;gd.nrHeight=resolution.nr.height;gd.flowWidth=resolution.flow.width;gd.flowHeight=resolution.flow.height;
            gd.enableSr=resolution.srApplied;gd.videoSrQuality=options.settings.videoSrQuality;gd.enableNr=options.nr;gd.enableFg=options.fg;gd.fgMultiplier=options.fgMultiplier;gd.frameGenerationBackend=options.settings.frameGenerationBackend;gd.enableNvofStandalone=options.nr;
            gd.noFeatures=false;gd.model=options.settings.model;gd.residual=options.settings.residual;gd.protection=options.settings.protection;gd.settingsRevision=options.settings.revision;gd.flowQuality=options.settings.flow;gd.contentRate=options.settings.content;
            gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
            if(!graph.initialize(gd)||!presenter.open(ctx,window,graph)||!graph.createViews()){status(L"增强初始化失败，请核对本地运行时",true);break;}
            if(physicalCapture&&!captureSource.start()){status(L"无法启动采集，请查看诊断",true);break;}
            {std::lock_guard lock(mutex_);snapshot_.duration=duration;snapshot_.running=true;snapshot_.transport=TransportState::Playing;snapshot_.image=isImage;snapshot_.capture=isCapture;snapshot_.applied=options.snapshot();snapshot_.desired=desired_;}
            status(isImage?L"图片已增强，可保存PNG/JPEG":std::format(L"{} | 输入 {}×{} / 底图 {}×{} / NR {}×{} / 光流 {}×{} / FG与输出 {}×{} | {}",isCapture?L"实时采集":L"播放",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.flowWidth,gd.flowHeight,gd.workWidth,gd.workHeight,gd.nrWidth<gd.workWidth?L"实时内部处理并回填":L"原生NR（性能成本较高）"));
            pipeline::EnhanceGraph::FrameOutputs out;bool reset=true,hasOutput=false;std::deque<double> latenessSamples;
            TimingWindow captureAges,scheduleWaits,processTimes,presentTimes,decodeTimes,gpuReadyTimes;
            std::array<TimingWindow,size_t(diagnostics::GpuStage::Count)> gpuStageTimes;
            std::array<uint64_t,size_t(diagnostics::GpuStage::Count)> lastGpuSampleEnd{};
            auto nextTimingLog=Clock::now()+std::chrono::seconds(1);
            auto anchor=Clock::now(),statsStart=anchor;double anchorMs=0;uint64_t frames=0,sourceFrames=0;bool wasPaused=false;double discardBefore=0;
            PresentationScheduler liveTimeline;uint64_t submitted=0,expired=0;std::deque<int64_t> submissionTimes;
            DeadlineWait deadlineWait;
            CaptureHalfRate captureSampler;
            auto completedProcessing=[&](uint64_t revision){std::lock_guard lock(mutex_);if(snapshot_.applied.revision==revision){processingRate_.complete(monotonic100ns());++snapshot_.processedCompleted;}};
            auto host100ns=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;};
            // Only the live source may discard stale input. File playback keeps
            // its audio-clock scheduler and lossless source ordering.
            std::mutex gpuMutex,liveStatsMutex;
            struct LiveStats {uint64_t submitted=0,expired=0;double waitMs=0,presentMs=0,readyMs=0,ageMs=0,fps=0,ageP95=0,waitP95=0,presentP95=0,readyP95=0;diagnostics::GpuSample blit;};
            LiveStats liveStats;std::deque<int64_t> liveSubmissions;
            TimingWindow liveAges,liveWaits,livePresent,liveReady;
            std::atomic<uint64_t> historyResets=0,presentationDrains=0,presentationCompletedReal=0,presentationSkippedGenerated=0,presentationCancelledJobs=0;
            std::atomic<uint64_t> presentationGeneration{0};
            std::unique_ptr<PresentationWorker> liveWorker;
            if(isCapture)liveWorker=std::make_unique<PresentationWorker>([&]{return stop_.load()||paused_.load();});
            auto drainLivePresentation=[&]{if(liveWorker){++presentationDrains;liveWorker->cancelAndDrain();}};
            uint64_t metricsRevision=options.settings.revision,statsSourceBase=0,statsGeneratedBase=0;
            uint64_t slotWaitBase=ring.cpuWaitCount(),submitBase=ring.submitCount();double slotWaitMsBase=ring.cpuWaitMilliseconds();
            while(!stop_){
                if(liveWorker&&liveWorker->failed()){status(L"采集画面呈现失败，请查看诊断",true);break;}
                const float gain=muted_?0.0f:volume_.load();audio.setGain(gain);
                if(isCapture){const bool available=captureSource.setAudioGain(gain);std::lock_guard lock(mutex_);snapshot_.audioAvailable=available;}
                // Save the latest processed real frame before a settings transaction
                // invalidates it (live rendering can be one batch behind processing).
                std::wstring save;EnhancementSettings requested;{std::lock_guard lock(mutex_);requested=desired_;if(hasOutput)save.swap(savePath_);}
                if(!save.empty()){try{sink::RgbaImage result;const auto e=std::filesystem::path(save).extension().wstring();
                    drainLivePresentation();
                    if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result)||!sink::saveImage(save,result,e==L".jpg"||e==L".jpeg"))status(L"保存失败（目标文件可能已存在），播放已保留",false);else{status(L"图片已保存："+save);veyra::log::info("image-save",std::format("saved extent={}x{} revision={}",gd.workWidth,gd.workHeight,options.settings.revision));}}
                    catch(const std::exception& e){veyra::log::warn("image-save",std::format("save exception; retaining session: {}",e.what()));status(L"保存异常，播放已保留；可再次保存",false);}}
                if(isImage)requested.multiplier=1;
                const auto previous=options.snapshot();const auto previousDesc=gd;bool transaction=false;
                if(requested.revision!=previous.revision){
                    drainLivePresentation();
                    auto next=PlayerOptions::from(requested);auto nextDesc=gd;
                    const auto plan=pipeline::ResolutionPlan::make({width,height},next.sr,requested.nrPolicy,isImage,requested.revision);
                    nextDesc.workWidth=plan.base.width;nextDesc.workHeight=plan.base.height;nextDesc.nrWidth=plan.nr.width;nextDesc.nrHeight=plan.nr.height;nextDesc.flowWidth=plan.flow.width;nextDesc.flowHeight=plan.flow.height;
                    nextDesc.enableSr=plan.srApplied;nextDesc.videoSrQuality=next.settings.videoSrQuality;nextDesc.enableNr=next.nr;nextDesc.enableFg=next.fg;nextDesc.fgMultiplier=next.fgMultiplier;nextDesc.frameGenerationBackend=next.settings.frameGenerationBackend;nextDesc.enableNvofStandalone=next.nr;
                    nextDesc.model=requested.model;nextDesc.residual=requested.residual;nextDesc.protection=requested.protection;nextDesc.settingsRevision=requested.revision;nextDesc.flowQuality=requested.flow;nextDesc.contentRate=requested.content;
                    const bool rebuild=gd.frameGenerationBackend!=nextDesc.frameGenerationBackend||(nextDesc.frameGenerationBackend==FrameGenerationBackend::Fruc&&(gd.fgMultiplier!=nextDesc.fgMultiplier||gd.enableFg!=nextDesc.enableFg))||gd.videoSrQuality!=nextDesc.videoSrQuality||gd.flowQuality!=nextDesc.flowQuality||gd.workWidth!=nextDesc.workWidth||gd.workHeight!=nextDesc.workHeight||gd.nrWidth!=nextDesc.nrWidth||gd.nrHeight!=nextDesc.nrHeight||gd.flowWidth!=nextDesc.flowWidth||gd.flowHeight!=nextDesc.flowHeight;
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
                    drainLivePresentation();
                    if(audioStarted)audioPipe.setPaused(true);wasPaused=true;
                    const bool referencesValid=hasOutput&&out.batch.count&&out.batch.frames[out.batch.count-1].lease&&out.batch.frames[out.batch.count-1].lease->referencesValid;
                    if(hasOutput&&!presenter.present(ctx,ring,graph,out.videoSlot,false,referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,out.batch.identity,previewView())){status(L"画面呈现失败",true);break;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;
                }
                if(wasPaused&&!paused_){if(audioStarted)audioPipe.setPaused(false);anchor=Clock::now();anchorMs=out.ptsMs;reset=true;wasPaused=false;}
                // Backpressure before reading the capacity-one source mailbox:
                // when a lease frees we consume the newest available sample.
                if(liveWorker&&!transaction&&!liveWorker->waitForSlot())continue;
                const auto decodeStart=Clock::now();
                pipeline::FramePacket pkt;const AVFrame* frame=imageFrame;
                if(transaction&&cachedFrame&&(!isCapture||paused_)){frame=cachedFrame;pkt=cachedPacket;}
                else if(!isImage){
                    auto rs=activeSource->read(pkt,&frame);
                    // Keep the accepted transaction and rollback state alive until
                    // the next real capture sample arrives. Never bind old PTS to now.
                    while(transaction&&isCapture&&rs==source::SourceReadStatus::Waiting&&!stop_){
                        if(paused_&&cachedFrame){frame=cachedFrame;pkt=cachedPacket;rs=source::SourceReadStatus::Frame;break;}
                        rs=activeSource->read(pkt,&frame);
                    }
                    if(stop_)break;
                    if(rs==source::SourceReadStatus::Waiting)continue;
                    if(rs==source::SourceReadStatus::Eos){status(L"视频已播放完毕");paused_=true;{std::lock_guard lock(mutex_);snapshot_.transport=TransportState::Ended;}continue;}
                    if(rs!=source::SourceReadStatus::Frame||pkt.pts.isUnknown()){status(isCapture?L"采集信号中断，请检查设备连接或格式":L"视频解码或时间戳错误",true);break;}
                }
                const bool rereadCached=transaction&&frame==cachedFrame;
                const bool halfRate=isCapture&&options.settings.content==ContentRate::Capture60To30&&
                    CaptureHalfRate::supported(activeSource->info().averageFps);
                if(reset||pipeline::breaksHistory(pkt.flags))captureSampler.reset();
                if(halfRate&&!rereadCached){
                    const auto interval=liveSourceInterval100ns(pipeline::Rational::unknown(),activeSource->info().averageFps);
                    if(!pkt.pts.isUnknown()&&!captureSampler.accept(pkt.pts.to100ns(),interval,false)){
                        std::lock_guard lock(mutex_);++snapshot_.captureRateSkipped;continue;
                    }
                    pkt.duration={interval*2,10000000};
                }
                if(!halfRate)captureSampler.reset();
                if(isImage)pkt.sequence=1;
                if(!rereadCached)++sourceFrames;
                const double decodeMs=elapsedMs(decodeStart);decodeTimes.add(decodeMs);
                const double pts=isImage?0:pkt.pts.toDouble()*1000;
                if(isCapture&&frames==0){anchor=Clock::now();anchorMs=pts;}
                if(pts+0.1<discardBefore)continue;
                if(frame!=cachedFrame){av_frame_free(&cachedFrame);cachedFrame=av_frame_clone(frame);cachedPacket=pkt;}
                const auto processStart=Clock::now();
                const bool historyReset=reset||pipeline::breaksHistory(pkt.flags);
                if(historyReset){++historyResets;++presentationGeneration;}
                // A mailbox Drop must reset SR/NR/flow/FG history, but draining
                // the two-batch presenter here discarded completed real frames.
                if(liveWorker&&presentationDrainRequired(reset,pkt.flags))drainLivePresentation();
                const bool injectedReject=transaction&&!options.nr&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",nullptr,0)>0;
                if(injectedReject)veyra::log::error("settings-test","test-only reject NR-disable transaction before graph process; no driver failure");
                bool processed=false;{std::lock_guard gpuLock(gpuMutex);processed=!injectedReject&&graph.process(frame,pts,historyReset,out,pkt.sequence,&pkt.colorInfo,comparisonMode_!=0);}
                if(!processed){
                    if(transaction){
                        ring.drainQueue();out={};presenter.close();graph.shutdown();options=PlayerOptions::from(previous);
                        gd=previousDesc;
                        if(graph.initialize(gd)&&presenter.open(ctx,window,graph)&&graph.createViews()&&graph.process(frame,pts,true,out,pkt.sequence,&pkt.colorInfo,comparisonMode_!=0)){
                            std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desired_=previous;snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_.revision!=previous.revision;snapshot_.status=L"参数执行失败，已整套回滚";transaction=false;
                        }else{status(L"参数回滚失败，已停止",true);break;}
                    }else{status(L"增强执行失败；请查看日志",true);break;}
                }
                if(transaction){std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_.revision!=options.settings.revision;snapshot_.status=std::format(L"输入 {}×{} / 底图 {}×{} / NR {}×{} / 光流 {}×{} / FG与输出 {}×{} | {}",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.flowWidth,gd.flowHeight,gd.workWidth,gd.workHeight,gd.nrWidth<gd.workWidth?L"实时内部处理并回填":L"原生NR（性能成本较高）");veyra::log::info("settings",std::format("Applied revision={} sourcePtsMs={} backend={} multiplier={} (source kept open)",options.settings.revision,pts,options.settings.frameGenerationBackend==FrameGenerationBackend::Fruc?"FRUC":"DLSS",options.snapshot().multiplier));}
                if(veyra::log::verboseFrameLogs())veyra::log::info("source-identity",std::format("source={} totalRead={} graphProcessed={} cached={} revision={} nvofStandalone={}",pkt.sequence,sourceFrames,frames+1,rereadCached,options.settings.revision,gd.enableNvofStandalone));
                reset=false;hasOutput=true;
                const auto processDone=Clock::now();processTimes.add(elapsedMs(processStart));
                if(metricsRevision!=options.settings.revision){
                    metricsRevision=options.settings.revision;statsStart=Clock::now();statsSourceBase=sourceFrames;statsGeneratedBase=graph.metrics().fgGeneratedFrames;
                    captureAges.clear();scheduleWaits.clear();processTimes.clear();presentTimes.clear();decodeTimes.clear();gpuReadyTimes.clear();for(auto& window:gpuStageTimes)window.clear();lastGpuSampleEnd={};latenessSamples.clear();submissionTimes.clear();submitted=expired=0;
                    slotWaitBase=ring.cpuWaitCount();slotWaitMsBase=ring.cpuWaitMilliseconds();submitBase=ring.submitCount();
                    if(liveWorker){std::lock_guard statsLock(liveStatsMutex);liveStats={};liveSubmissions.clear();liveAges.clear();liveWaits.clear();livePresent.clear();liveReady.clear();historyResets=0;presentationDrains=0;presentationCompletedReal=0;presentationSkippedGenerated=0;presentationCancelledJobs=0;}
                    veyra::log::info("metrics",std::format("reset appliedRevision={} sourceBase={}",metricsRevision,statsSourceBase));
                }
                if(transaction||frames==0){
                    std::lock_guard lock(mutex_);processingRate_.reset(monotonic100ns());snapshot_.captureHalfRate=halfRate;
                    veyra::log::info("capture-rate",std::format("revision={} requested60To30={} active={} transportFps={} originalPtsPreserved=true",options.settings.revision,options.settings.content==ContentRate::Capture60To30,halfRate,isCapture?activeSource->info().averageFps:0));
                }
                const auto captureArrival=pkt.arrivalHost100ns?pkt.arrivalHost100ns:
                    std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100;
                if(isCapture&&!rereadCached&&!liveTimeline.anchored(out.batch.identity.epoch)){
                    const auto duration100ns=liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps);
                    veyra::log::info("capture-timeline",std::format("interval100ns={} packetDurationKnown={} packetDurationPositive={} nominalFps={} FG={}",duration100ns,!pkt.duration.isUnknown(),pkt.duration.num>0,activeSource->info().averageFps,options.fg));
                    liveTimeline.reset(out.batch.identity.epoch,out.batch.b100ns,captureArrival,options.fg?duration100ns:0);
                }
                if(!audioStarted&&!isImage&&!isCapture&&frames==0){if(audioPipe.open(path)&&audio.start()){audioPipe.startThread(&audio);audioStarted=true;{std::lock_guard lock(mutex_);snapshot_.audioAvailable=true;}}anchor=Clock::now();anchorMs=pts;}
                auto nowMs=[&](){const double a=audioStarted?audio.mediaTimeMs():-1;return a>=0?a:anchorMs+std::chrono::duration<double,std::milli>(Clock::now()-anchor).count();};
                double frameWaitMs=0,framePresentMs=0;
                double gpuWaitMs=0;
                if(liveWorker){
                    const auto timeline=liveTimeline;
                    const uint64_t jobGeneration=presentationGeneration.load();
                    if(!liveWorker->push([&,batch=out,timeline,captureArrival,jobGeneration,rereadCached](const PresentationWorker::Cancelled& cancelled)mutable{
                        auto& wait=deadlineWait;const auto readyStart=Clock::now();
                        for(;;){if(cancelled()){++presentationCancelledJobs;return true;}bool ready;{std::lock_guard gpuLock(gpuMutex);ready=graph.resolveGeneration(batch);}if(ready)break;
                            if(elapsedMs(readyStart)>2000){veyra::log::error("capture-present","GPU ready timeout");return false;}wait.slice(.2);}
                        if(!rereadCached)completedProcessing(batch.batch.identity.settingsRevision);
                        const double readyMs=elapsedMs(readyStart);double waitMs=0,presentMs=0,ageMs=0;uint64_t count=0,dropped=0;
                        diagnostics::GpuSample blit;
                        OnExit publish{[&]{if(!count&&!dropped)return;std::lock_guard statsLock(liveStatsMutex);liveStats.submitted+=count;liveStats.expired+=dropped;
                            // Accumulate at completion, not at producer polling: two
                            // completions between reads must not lose a timing sample.
                            if(count){liveStats.waitMs=waitMs;liveStats.presentMs=presentMs;liveStats.readyMs=readyMs;liveStats.ageMs=ageMs;liveStats.blit=blit;
                                liveAges.add(ageMs);liveWaits.add(waitMs);livePresent.add(presentMs);liveReady.add(readyMs);liveStats.ageP95=liveAges.p95();liveStats.waitP95=liveWaits.p95();liveStats.presentP95=livePresent.p95();liveStats.readyP95=liveReady.p95();}
                            liveStats.fps=liveSubmissions.size()>1?double(liveSubmissions.size()-1)*1e7/(liveSubmissions.back()-liveSubmissions.front()):0;}};
                        for(unsigned i=0;i<batch.batch.count;++i){auto& item=batch.batch.frames[i];const bool generated=item.kind==pipeline::FrameKind::Generated;
                            if(generated&&(item.validity!=pipeline::GenerationValidity::Valid||comparisonMode_!=0))continue;
                            if(generated&&!generatedPresentationCurrent(jobGeneration,presentationGeneration.load())){++presentationSkippedGenerated;continue;}
                            if(generated&&timeline.expired(item.pts100ns,host100ns(),100000)){++dropped;continue;}
                            const auto waitStart=Clock::now();while(!cancelled()&&host100ns()<timeline.deadline(item.pts100ns))wait.slice();waitMs+=elapsedMs(waitStart);if(cancelled()){++presentationCancelledJobs;return true;}
                            if(generated&&!generatedPresentationCurrent(jobGeneration,presentationGeneration.load())){++presentationSkippedGenerated;continue;}
                            const auto begin=Clock::now();bool didPresent=false;
                            {std::lock_guard gpuLock(gpuMutex);if(cancelled()){++presentationCancelledJobs;return true;}const auto before=presenter.submittedCount();
                                if(!presenter.present(ctx,ring,graph,item.lease->slot,generated,item.lease->referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView()))return false;
                                item.lease->consumerFence=ring.lastSignaledValue();didPresent=presenter.submittedCount()>before;blit=presenter.blitTiming(ctx.fence());}
                            presentMs+=elapsedMs(begin);
                            if(didPresent){++count;if(!generated)++presentationCompletedReal;ageMs=double(host100ns()-captureArrival)/10000;std::lock_guard statsLock(liveStatsMutex);const auto time=host100ns();liveSubmissions.push_back(time);while(liveSubmissions.size()>1&&time-liveSubmissions.front()>10000000)liveSubmissions.pop_front();}
                        }
                        return true;
                    })){status(L"采集呈现队列失败",true);break;}
                    LiveStats completed;{std::lock_guard statsLock(liveStatsMutex);completed=liveStats;}
                    frameWaitMs=completed.waitMs;framePresentMs=completed.presentMs;gpuWaitMs=completed.readyMs;submitted=completed.submitted;expired=completed.expired;
                }else{
                const auto readyStart=Clock::now();
                while(!stop_&&!paused_&&seekSeconds_<0&&!graph.resolveGeneration(out)){
                    if(elapsedMs(readyStart)>2000){status(L"补帧 GPU 就绪超时",true);stop_=true;break;}
                    deadlineWait.slice(.2);
                }
                if(stop_)break;
                if(paused_||seekSeconds_>=0){reset=true;if(veyra::log::verboseFrameLogs())veyra::log::info("submit",std::format("batch={} cancelled before submit (pause/seek)",out.batch.batchId));continue;}
                if(!rereadCached&&!isImage)completedProcessing(out.batch.identity.settingsRevision);
                gpuWaitMs=elapsedMs(readyStart);gpuReadyTimes.add(gpuWaitMs);
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
                    if(!presenter.present(ctx,ring,graph,item.lease->slot,generated,item.lease->referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView())){presentFailed=true;break;}
                    framePresentMs+=elapsedMs(begin);
                    item.lease->consumerFence=ring.lastSignaledValue();
                    if(presenter.submittedCount()>before){++submitted;const auto time=host100ns();submissionTimes.push_back(time);while(submissionTimes.size()>1&&time-submissionTimes.front()>10000000)submissionTimes.pop_front();if(veyra::log::verboseFrameLogs())veyra::log::info("submit",std::format("batch={} epoch={} revision={} subframe={} pts100ns={} host100ns={} fence={} (submission, display unmeasured)",out.batch.batchId,item.identity.epoch,item.identity.settingsRevision,item.subframe,item.pts100ns,host100ns(),item.lease->consumerFence));}
                }
                if(presentFailed){status(L"画面提交失败",true);break;}
                if(interrupted){reset=true;continue;}
                }
                if(!liveWorker){scheduleWaits.add(frameWaitMs);presentTimes.add(framePresentMs);}
                const double lateness=isCapture?0:nowMs()-pts;
                if(!paused_&&!isCapture){latenessSamples.push_back(std::abs(lateness));if(latenessSamples.size()>1200)latenessSamples.pop_front();}
                std::vector<double> sorted(latenessSamples.begin(),latenessSamples.end());std::sort(sorted.begin(),sorted.end());
                const auto captureStats=isCapture?captureSource.metrics():source::CaptureMetrics{};
                diagnostics::FrameMetrics measured;pipeline::EnhanceGraph::Metrics graphStats;uint64_t slotWaitCount=0,commandSubmits=0;double slotWaitMilliseconds=0;
                {std::lock_guard gpuLock(gpuMutex);measured=graph.gpuMetrics();graphStats=graph.metrics();measured.gpu[size_t(diagnostics::GpuStage::Blit)]=presenter.blitTiming(ctx.fence(),options.settings.revision);slotWaitCount=ring.cpuWaitCount()-slotWaitBase;slotWaitMilliseconds=ring.cpuWaitMilliseconds()-slotWaitMsBase;commandSubmits=ring.submitCount()-submitBase;}
                if(measured.identity.settingsRevision!=options.settings.revision){measured={};measured.identity=out.batch.identity;for(auto& sample:measured.gpu)sample.state=diagnostics::SampleState::Pending;}
                for(size_t stage=0;stage<measured.gpu.size();++stage){const auto& sample=measured.gpu[stage];if(sample.state==diagnostics::SampleState::Measured&&sample.milliseconds&&sample.end&&sample.end!=lastGpuSampleEnd[stage]){gpuStageTimes[stage].add(*sample.milliseconds);lastGpuSampleEnd[stage]=sample.end;}}
                measured.resolution=pipeline::ResolutionPlan::make({width,height},options.sr,options.realtime?pipeline::NrSizePolicy::Realtime:pipeline::NrSizePolicy::Native,isImage,options.settings.revision);measured.decodeCpuMs=decodeMs;measured.submitCpuMs=std::chrono::duration<double,std::milli>(processDone-processStart).count();measured.gpuWaitCpuMs=gpuWaitMs;measured.deadlineWaitCpuMs=frameWaitMs;measured.presentCpuMs=framePresentMs;measured.submitted=submitted;measured.expired=expired;measured.sourceFrames=sourceFrames-statsSourceBase;measured.validGenerated=graphStats.fgGeneratedFrames-statsGeneratedBase;measured.queueWatermark=out.batch.count;
                LiveStats completed;if(liveWorker){std::lock_guard statsLock(liveStatsMutex);completed=liveStats;}
                const double ageP95=liveWorker?completed.ageP95:captureAges.p95(),waitP95=liveWorker?completed.waitP95:scheduleWaits.p95(),presentP95=liveWorker?completed.presentP95:presentTimes.p95();
                ++frames;{std::lock_guard lock(mutex_);snapshot_.metrics=measured;if(liveWorker)snapshot_.submissionFps=completed.fps;else snapshot_.submissionFps=submissionTimes.size()>1?double(submissionTimes.size()-1)*1e7/(submissionTimes.back()-submissionTimes.front()):0;snapshot_.position=pts/1000;snapshot_.frames=sourceFrames;snapshot_.generated=graphStats.fgGeneratedFrames;snapshot_.lateMs=lateness;snapshot_.lateP95Ms=sorted.empty()?0:sorted[size_t((sorted.size()-1)*0.95)];snapshot_.fps=(sourceFrames-statsSourceBase)/std::max(0.001,std::chrono::duration<double>(Clock::now()-statsStart).count());
                    snapshot_.flowPerf=graph.actualFlowPerf();snapshot_.contentFps=out.measuredContentRate;
                    snapshot_.nrEvaluated=graphStats.nrEvaluateCount;snapshot_.nvofExecuted=graphStats.nvofExecuteCount;
                    snapshot_.captureReceived=captureStats.received;snapshot_.captureDropped=captureStats.dropped;snapshot_.captureFps=captureStats.callbackFps;snapshot_.captureReadAgeMs=captureStats.readAgeMs;snapshot_.captureAgeMs=liveWorker?completed.ageMs:captureStats.frameAgeMs;snapshot_.captureAgeP95Ms=ageP95;
                    snapshot_.schedulingWaitP95Ms=waitP95;snapshot_.processCpuP95Ms=processTimes.p95();snapshot_.presentCpuP95Ms=presentP95;
                }
                const auto gpuP95=[&](diagnostics::GpuStage stage){return gpuStageTimes[size_t(stage)].p95();};
                if(!isCapture&&Clock::now()>=nextTimingLog){veyra::log::info("player-timing",std::format("revision={} decodeP95Ms={:.3f} graphSubmitP95Ms={:.3f} gpuReadyP95Ms={:.3f} presentP95Ms={:.3f} gpuColorP95Ms={:.3f} gpuSrP95Ms={:.3f} gpuFlowP95Ms={:.3f} gpuNrP95Ms={:.3f} gpuResidualP95Ms={:.3f} gpuFgBatchP95Ms={:.3f} gpuBlitP95Ms={:.3f} slotWaits={} slotWaitMs={:.3f} commandSubmits={} displaySubmits={} expiredGenerated={} processed={}",options.settings.revision,decodeTimes.p95(),processTimes.p95(),gpuReadyTimes.p95(),presentTimes.p95(),gpuP95(diagnostics::GpuStage::Color),gpuP95(diagnostics::GpuStage::Sr),gpuP95(diagnostics::GpuStage::Flow),gpuP95(diagnostics::GpuStage::Nr),gpuP95(diagnostics::GpuStage::Residual),gpuP95(diagnostics::GpuStage::FgBatch),gpuP95(diagnostics::GpuStage::Blit),slotWaitCount,slotWaitMilliseconds,commandSubmits,submitted,expired,sourceFrames-statsSourceBase));nextTimingLog=Clock::now()+std::chrono::seconds(1);}
                if(isCapture&&Clock::now()>=nextTimingLog){
                    const auto rates=snapshot();
                    veyra::log::info("frame-rate",std::format("revision={} gpuCompletedFps={:.2f} completedReal={} capture60To30={} rateSkipped={} presentSubmitFps={:.2f} windowMs=1000 (not scanout FPS)",options.settings.revision,rates.fps,rates.processedCompleted,rates.captureHalfRate,rates.captureRateSkipped,rates.submissionFps.value_or(0)));
                    veyra::log::info("capture-timing",std::format("revision={} received={} processed={} dropped={} callbackFps={:.2f} readAgeMs={:.3f} callbackToPresentReturnP95Ms={:.3f} processCpuP95Ms={:.3f} gpuReadyP95Ms={:.3f} schedulingWaitP95Ms={:.3f} presentCpuP95Ms={:.3f} gpuColorP95Ms={:.3f} gpuSrP95Ms={:.3f} gpuFlowP95Ms={:.3f} gpuNrP95Ms={:.3f} gpuResidualP95Ms={:.3f} gpuFgBatchP95Ms={:.3f} gpuBlitP95Ms={:.3f} slotWaits={} slotWaitMs={:.3f} commandSubmits={} displaySubmits={} expiredGenerated={} nr={} nvof={} generated={} historyResets={} presentationDrains={} presentationCompletedReal={} presentationSkippedGenerated={} presentationCancelledJobs={} presentationWorker=1 batchCapacity=2 (not HDMI-to-display latency)",options.settings.revision,captureStats.received,sourceFrames-statsSourceBase,captureStats.dropped,captureStats.callbackFps,captureStats.readAgeMs,ageP95,processTimes.p95(),liveWorker?completed.readyP95:gpuReadyTimes.p95(),waitP95,presentP95,gpuP95(diagnostics::GpuStage::Color),gpuP95(diagnostics::GpuStage::Sr),gpuP95(diagnostics::GpuStage::Flow),gpuP95(diagnostics::GpuStage::Nr),gpuP95(diagnostics::GpuStage::Residual),gpuP95(diagnostics::GpuStage::FgBatch),gpuP95(diagnostics::GpuStage::Blit),slotWaitCount,slotWaitMilliseconds,commandSubmits,submitted,expired,graphStats.nrEvaluateCount,graphStats.nvofExecuteCount,graphStats.fgGeneratedFrames,historyResets.load(),presentationDrains.load(),presentationCompletedReal.load(),presentationSkippedGenerated.load(),presentationCancelledJobs.load()));
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
