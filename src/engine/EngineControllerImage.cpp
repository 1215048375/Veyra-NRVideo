#include "veyra/engine/EngineController.h"
#include "veyra/engine/TiledImageProcessor.h"
#include "veyra/engine/ImageRenderCache.h"
#include "veyra/engine/ImageComparison.h"
#include "veyra/engine/ImagePrerenderQueue.h"
#include "veyra/engine/ImageDecodeCache.h"
#include "veyra/engine/StillImageRenderer.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
void EngineController::prerenderNext(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,const EnhancementSettings& applied){
    if(!prerenderQueue_->pending())return;
    EnhancementSettings settings;
    {std::lock_guard lock(mutex_);if(stop_||!prerenderImages_||!desired_.sameVideoConfiguration(applied))return;settings=applied;prerenderCancel_=false;}
    // Protection regions belong to the currently selected photo; browsing clears them.
    settings.protection={};const auto key=ImageRenderCache::settingsKey(settings);const auto path=prerenderQueue_->next(key);if(path.empty())return;
    const auto begin=std::chrono::steady_clock::now();bool saved=false;
    auto rendered=std::make_shared<sink::RgbaImage>();
    try{
        auto original=imageCache_->get(path);sink::RgbaImage decoded;
        const bool loaded=original||sink::loadImage(path,decoded,512ull*1024*1024);
        if(loaded&&!prerenderCancel_&&!stop_){
            const auto& source=original?*original:decoded;
            const auto sourceKey=ImageRenderCache::sourceKey(path,source);const auto entry=ImageRenderCache::entry(path,sourceKey,settings);
            if(ImageRenderCache::load(entry,*rendered))saved=true;
            else if(renderStillImage(ctx,ring,source,*rendered,settings,prerenderCancel_)&&!stop_&&!prerenderCancel_&&prerenderQueue_->wanted(path,key))saved=ImageRenderCache::save(entry,*rendered);
        }
    }catch(const std::exception& e){veyra::log::warn("image-prerender",e.what());}
    if(prerenderCancel_||stop_){prerenderQueue_->retry(path);return;}
    prerenderQueue_->finish(path,key,rendered,saved);
    veyra::log::info("image-prerender",std::format("completed={} elapsedMs={:.1f} ready={} total={} failed={}",saved,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count(),prerenderQueue_->progress().first,prerenderQueue_->progress().second,prerenderQueue_->failures()));Logger::instance().flush();
}
void EngineController::runLargeImage(HWND window,const sink::RgbaImage& original,PlayerOptions options,
    gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,const std::wstring& path,const std::string& sourceKey,sink::RgbaImage* cachedResult) {
    {std::lock_guard lock(mutex_);snapshot_.image=true;snapshot_.running=true;desired_.multiplier=1;snapshot_.desired=desired_;}
    // A bounded presentation texture is independent of the full-resolution
    // result retained for saving. Preview changes never rerun the NR tiles.
    RECT initialClient{};GetClientRect(window,&initialClient);
    const uint32_t width=uint32_t(std::clamp(initialClient.right,1L,4096L)),height=uint32_t(std::clamp(initialClient.bottom,1L,4096L));
    pipeline::EnhanceGraphDesc previewDesc;previewDesc.sourceWidth=previewDesc.workWidth=width;previewDesc.sourceHeight=previewDesc.workHeight=height;
    previewDesc.rgbInput=true;previewDesc.stillImage=true;previewDesc.noFeatures=true;previewDesc.enableNr=false;previewDesc.enableFg=false;
    pipeline::EnhanceGraph preview(ctx,ring);VideoPresenter presenter;
    struct Cleanup {
        gfx::CommandSlotRing& ring;VideoPresenter& presenter;pipeline::EnhanceGraph& graph;
        ~Cleanup(){ring.drainQueue();presenter.close();graph.shutdown();}
    } cleanup{ring,presenter,preview};
    bool captureCompatible=options.settings.captureCompatible;
    if(!preview.initialize(previewDesc)||!presenter.open(ctx,window,preview,captureCompatible)||!preview.createViews()){status(L"大图预览初始化失败",true);return;}
    const auto freeFrame=[](AVFrame* p){av_frame_free(&p);};std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);
    if(!frame){status(L"大图预览内存不足",true);return;}
    frame->format=AV_PIX_FMT_RGBA;frame->width=width;frame->height=height;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
    if(av_frame_get_buffer(frame.get(),32)<0){status(L"大图预览内存不足",true);return;}
    sink::RgbaImage enhanced;EnhancementSettings applied;applied.revision=0;
    std::filesystem::path savedEntry;uint64_t savedRevision=0;
    if(cachedResult){enhanced=std::move(*cachedResult);applied=options.snapshot();applied.multiplier=1;
        std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.imageDiskCached=true;snapshot_.frames=1;snapshot_.imageLoading=false;snapshot_.transport=TransportState::Playing;
        auto& plan=snapshot_.metrics.resolution;plan.source={original.width,original.height};plan.base=plan.output={enhanced.width,enhanced.height};
        snapshot_.status=L"已读取缓存（保留生成时效果）· 可按 V / 分屏对比原图";
    }
    pipeline::EnhanceGraph::FrameOutputs out;bool havePreview=false;
    int lastComparison=-1;float lastSplit=-1;uint64_t previewRevision=0;PreviewView lastView;RECT lastClient{};
    while(!stop_){
        EnhancementSettings requested;std::wstring save;
        {std::lock_guard lock(mutex_);requested=desired_;if(!enhanced.pixels.empty())save.swap(savePath_);}
        requested.multiplier=1;
        if(cachedResult&&(!cacheRenderedImages_||(requested.revision!=applied.revision&&ImageRenderCache::settingsKey(requested)!=ImageRenderCache::settingsKey(applied)))){
            veyra::log::info("image-disk-cache","settings changed or cache disabled; returning to original rendering");{std::lock_guard lock(mutex_);snapshot_.imageDiskCached=false;}return;
        }
        if(requested.captureCompatible!=captureCompatible){
            if(!ring.drainQueue()){status(L"显示切换排空失败",true);break;}
            presenter.close();
            if(!presenter.open(ctx,window,preview,requested.captureCompatible)){
                presenter.close();
                if(!presenter.open(ctx,window,preview,captureCompatible)){status(L"显示切换恢复失败",true);break;}
                std::lock_guard lock(mutex_);desired_.rejectVideoRequest(requested,applied);snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.backendWarning=L"直播兼容模式未能启用，已恢复";continue;
            }
            captureCompatible=requested.captureCompatible;
        }
        auto contentSettings=requested;contentSettings.captureCompatible=applied.captureCompatible;
        if(!enhanced.pixels.empty()&&(cachedResult||contentSettings.sameVideoConfiguration(applied))){
            applied=requested;
            std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.applying=desired_!=applied;
        }
        if(!save.empty()){
            try {
            auto ext=std::filesystem::path(save).extension().wstring();for(auto& c:ext)c=towlower(c);
            if(!sink::saveImage(save,enhanced,ext==L".jpg"||ext==L".jpeg"))status(L"图片保存失败（目标可能已存在）",false);
            else {status(L"图片已保存，完整结果仍可继续预览");veyra::log::info("image-save",std::format("tiled full-resolution saved extent={}x{} revision={}",enhanced.width,enhanced.height,applied.revision));}
            }catch(const std::exception& e){veyra::log::warn("image-save",std::format("save exception; retaining full result/session: {}",e.what()));status(L"保存异常，增强结果已保留；可再次保存",false);}
        }
        if(requested.revision==applied.revision&&requested!=applied){
            applied=requested;
            std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.applying=desired_!=applied;
        }
        if(!cachedResult&&requested.revision!=applied.revision){
            pipeline::EnhanceGraphDesc desc;desc.enableNr=requested.nr;desc.nrRuntime=requested.nrRuntime;desc.noFeatures=!requested.nr;
            desc.model=requested.model;desc.residual=requested.residual;desc.protection=requested.protection;desc.settingsRevision=requested.revision;
            desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
            sink::RgbaImage candidate;TiledImageProcessor::Stats stats;
            status(L"正在分块增强大图，保留完整输出尺寸…");
            bool processed=false;
            if(cacheRenderedImages_){auto key=sourceKey.empty()?ImageRenderCache::sourceKey(path,original):sourceKey;processed=ImageRenderCache::load(ImageRenderCache::entry(path,key,requested),candidate);}
            try{if(!processed)processed=TiledImageProcessor::process(ctx,ring,original,candidate,desc,stop_,stats,[this](uint32_t n,uint32_t total){status(std::format(L"大图分块增强 {}/{} · 每块独立上下文",n,total));});}
            catch(const std::exception& e){veyra::log::warn("image-tiles",std::format("settings exception; retaining previous result: {}",e.what()));}
            if(!processed){
                if(stop_)break;
                if(enhanced.pixels.empty()){status(L"大图分块增强失败，请查看诊断",true);break;}
                std::lock_guard lock(mutex_);desired_.rejectVideoRequest(requested,applied);
                snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_!=applied;
                snapshot_.status=L"大图参数应用失败，已保留上一结果";continue;
            }
            enhanced=std::move(candidate);applied=requested;lastComparison=-1;
            {std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.desired=desired_;snapshot_.applying=desired_!=applied;
                snapshot_.nrEvaluated=stats.nrEvaluations;snapshot_.frames=1;snapshot_.imageLoading=false;snapshot_.transport=TransportState::Playing;
                auto& plan=snapshot_.metrics.resolution;plan.source=plan.base=plan.output={original.width,original.height};plan.nr={std::min(1280u,original.width)+256,std::min(1280u,original.height)+256};plan.flow=plan.fg={};plan.srApplied=false;plan.settingsRevision=applied.revision;
                snapshot_.status=std::format(L"{}×{} · 分块NR（非整图上下文）· 保存保留完整尺寸",original.width,original.height);}
        }
        if(!cachedResult&&cacheRenderedImages_&&!enhanced.pixels.empty()&&(savedEntry.empty()||savedRevision!=applied.revision)){
            const auto key=sourceKey.empty()?ImageRenderCache::sourceKey(path,original):sourceKey;
            const auto entry=ImageRenderCache::entry(path,key,applied);savedRevision=applied.revision;
            if(entry!=savedEntry){savedEntry=entry;if(!ImageRenderCache::save(entry,enhanced))status(L"缓存保存失败，请检查目录写入权限；图片仍可查看",false);}
        }
        const int comparison=comparisonMode_.load();const float split=comparisonSplit_.load();
        const auto view=previewView();RECT client{};GetClientRect(window,&client);
        if(client.right<1||client.bottom<1){std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;}
        if(comparison!=lastComparison||split!=lastSplit||view!=lastView||client.right!=lastClient.right||client.bottom!=lastClient.bottom){
            // Source and enhanced use exactly the same coordinates. The full
            // image buffers, rather than the preview, remain the save source.
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
                const double fit=std::min(double(client.right)/original.width,double(client.bottom)/original.height)*view.zoom;
                const double u=((x+.5)/width-.5)*client.right/(original.width*fit)+view.centerX;
                const double v=((y+.5)/height-.5)*client.bottom/(original.height*fit)+view.centerY;
                auto* dst=frame->data[0]+size_t(y)*frame->linesize[0]+size_t(x)*4;
                sampleImageComparison(original,enhanced,u,v,comparison,split,dst);
            }
            out={};if(!preview.process(frame.get(),0,true,out,++previewRevision)){status(L"大图预览上传失败",true);break;}
            havePreview=true;lastComparison=comparison;lastSplit=split;lastView=view;lastClient=client;
        }
        if(havePreview&&!presenter.present(ctx,ring,preview,out.videoSlot,false,false,0,false,.5f,{},PreviewView{0,.5f,.5f})){status(L"大图呈现失败",true);break;}
        if(havePreview&&prerenderImages_)prerenderNext(ctx,ring,applied);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    out={}; // Cleanup drains before either presenter or graph releases GPU objects.
}
}
