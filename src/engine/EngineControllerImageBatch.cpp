#include "veyra/engine/EngineController.h"
#include "veyra/engine/ImageRenderCache.h"
#include "veyra/engine/StillImageRenderer.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <format>
namespace veyra::engine {
void EngineController::renderImageFolder(std::vector<std::wstring> paths,EnhancementSettings settings){
    settings.multiplier=1;settings.protection={};
    if(!settings.validate().empty())return;
    prerenderImages(false);cacheRenderedImages(true);prefetchImages({});
    uint64_t generation;
    {std::lock_guard lock(mutex_);generation=++imageBatchGeneration_;imageBatch_={};imageBatch_.active=true;imageBatch_.total=paths.size();}
    post([this,paths=std::move(paths),settings,generation]{
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;
        struct Cleanup{gfx::D3D12DeviceContext& ctx;gfx::CommandSlotRing& ring;~Cleanup(){ring.drainQueue();ring.shutdown();ctx.shutdown();}} cleanup{ctx,ring};
        for(const auto& path:paths){
            if(stop_)break;
            {std::lock_guard lock(mutex_);if(generation!=imageBatchGeneration_)return;imageBatch_.current=std::filesystem::path(path).filename().wstring();}
            bool ok=false,hit=false;
            try{
                sink::RgbaImage original,result;
                if(sink::loadImage(path,original,512ull*1024*1024)&&!stop_){
                    const auto entry=ImageRenderCache::entry(path,ImageRenderCache::sourceKey(path,original),settings);
                    hit=ImageRenderCache::load(entry,result);ok=hit;
                    if(!hit&&!stop_){
                        Status status=Status::Ok;gfx::DeviceContextDesc desc;desc.commandSlotCount=6;
                        bool device=ctx.device()!=nullptr;
                        if(!device)device=ctx.initialize(desc,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status);
                        ok=device&&renderStillImage(ctx,ring,original,result,settings,stop_)&&!stop_&&ImageRenderCache::save(entry,result);
                    }
                }
            }catch(const std::exception& e){log::warn("image-folder-batch",e.what());}
            if(stop_)break;
            {std::lock_guard lock(mutex_);if(generation!=imageBatchGeneration_)return;++imageBatch_.completed;
                if(!ok)++imageBatch_.failed;else if(hit)++imageBatch_.skipped;else ++imageBatch_.rendered;
                log::info("image-folder-batch",std::format("processed={}/{} rendered={} skipped={} failed={}",imageBatch_.completed,imageBatch_.total,imageBatch_.rendered,imageBatch_.skipped,imageBatch_.failed));}
            Logger::instance().flush();
        }
        {std::lock_guard lock(mutex_);if(generation==imageBatchGeneration_){imageBatch_.active=false;imageBatch_.cancelled=stop_;imageBatch_.current.clear();}}
    },true);
}
}
