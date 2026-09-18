#include "veyra/engine/StillImageRenderer.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <format>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
pipeline::EnhanceGraphDesc stillImageDescription(uint32_t width,uint32_t height,const EnhancementSettings& s){
    auto p=pipeline::ResolutionPlan::make({width,height},s.sr,s.nrPolicy,true,s.revision,s.srTarget);
    pipeline::EnhanceGraphDesc d;d.sourceWidth=width;d.sourceHeight=height;d.rgbInput=d.stillImage=true;
    d.workWidth=p.base.width;d.workHeight=p.base.height;d.nrWidth=p.nr.width;d.nrHeight=p.nr.height;d.flowWidth=p.flow.width;d.flowHeight=p.flow.height;
    d.enableNr=s.nr;d.enableSr=p.srApplied;d.enableFg=false;d.enableNvofStandalone=s.nr;d.noFeatures=!s.nr&&!p.srApplied;
    d.nrRuntime=s.nrRuntime;d.model=s.model;d.residual=s.residual;d.protection=s.protection;d.settingsRevision=s.revision;
    d.videoSrQuality=s.videoSrQuality;d.flowQuality=s.flow;d.contentRate=s.content;d.opticalFlowBackend=s.opticalFlowBackend;d.amdFlowHalfResolution=s.amdFlowHalfResolution;
    d.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();return d;
}
bool renderStillImage(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,const sink::RgbaImage& source,sink::RgbaImage& result,const EnhancementSettings& settings,const std::atomic<bool>& cancel){
    if(cancel)return false;
    if(!pipeline::Extent{source.width,source.height}.valid()||uint64_t(source.width)*source.height>16777216){
        pipeline::EnhanceGraphDesc d;d.enableNr=settings.nr;d.nrRuntime=settings.nrRuntime;d.noFeatures=!settings.nr;d.enableFg=false;
        d.model=settings.model;d.residual=settings.residual;d.protection=settings.protection;d.settingsRevision=settings.revision;d.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        TiledImageProcessor::Stats stats;const bool ok=TiledImageProcessor::process(ctx,ring,source,result,d,cancel,stats);
        log::info("image-prerender",std::format("tiled completed={} tiles={} nrEvaluate={}",ok,stats.tiles,stats.nrEvaluations));
        return ok&&(!settings.nr||stats.nrEvaluations>0);
    }
    pipeline::EnhanceGraph graph(ctx,ring);
    struct Drain{gfx::CommandSlotRing& ring;pipeline::EnhanceGraph& graph;~Drain(){ring.drainQueue();graph.shutdown();}} drain{ring,graph};
    auto d=stillImageDescription(source.width,source.height,settings);
    if(!graph.initialize(d)||!graph.createViews()||cancel)return false;
    auto freeFrame=[](AVFrame* f){av_frame_free(&f);};std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);if(!frame)return false;
    frame->format=AV_PIX_FMT_RGBA;frame->width=source.width;frame->height=source.height;
    frame->color_range=AVCOL_RANGE_JPEG;frame->colorspace=AVCOL_SPC_RGB;frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    if(av_frame_get_buffer(frame.get(),32)<0)return false;
    for(unsigned y=0;y<source.height;++y)memcpy(frame->data[0]+size_t(y)*frame->linesize[0],source.pixels.data()+size_t(y)*source.width*4,size_t(source.width)*4);
    pipeline::EnhanceGraph::FrameOutputs out;
    if(cancel||!graph.process(frame.get(),0,true,out,1,nullptr,false)||cancel)return false;
    if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result)||cancel)return false;
    const auto metrics=graph.metrics();
    if((settings.nr&&metrics.nrEvaluateCount==0)||(d.enableSr&&metrics.srEvaluateCount==0)){log::warn("image-prerender","requested effect did not execute; result rejected");return false;}
    log::info("image-prerender",std::format("rendered extent={}x{} nrEvaluate={} srEvaluate={} source={}x{}",result.width,result.height,metrics.nrEvaluateCount,metrics.srEvaluateCount,source.width,source.height));
    return true;
}
}
