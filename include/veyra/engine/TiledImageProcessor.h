#pragma once
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/ImageExportSink.h"
#include <atomic>
#include <functional>

namespace veyra::engine {
// Static images only: bounded GPU residency, full-resolution CPU result.
// Context padding and overlap reduce tile boundaries; this is not equivalent
// to running the neural model with whole-image context.
class TiledImageProcessor {
public:
    struct Stats { uint32_t tiles=0; uint64_t nrEvaluations=0; };
    static bool process(gfx::D3D12DeviceContext&, gfx::CommandSlotRing&,
        const sink::RgbaImage&, sink::RgbaImage&, pipeline::EnhanceGraphDesc,
        const std::atomic<bool>& cancel, Stats&,
        const std::function<void(uint32_t,uint32_t)>& progress={});
};
}
