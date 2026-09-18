#pragma once
#include "veyra/engine/TiledImageProcessor.h"
namespace veyra::engine {
// Uses the product EnhanceGraph. No independent NR/SR implementation.
pipeline::EnhanceGraphDesc stillImageDescription(uint32_t width,uint32_t height,const EnhancementSettings&);
bool renderStillImage(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,const sink::RgbaImage&,sink::RgbaImage&,
    const EnhancementSettings&,const std::atomic<bool>& cancel);
}
