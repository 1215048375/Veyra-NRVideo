#pragma once
#include "veyra/pipeline/FramePacket.h"
#include "veyra/pipeline/ResetCoordinator.h"
#include <utility>

namespace veyra::diagnostics {
inline pipeline::ResetReason resetCause(pipeline::FrameFlags flags,
    pipeline::ResetReason requested, pipeline::ResetReason detected) {
    using namespace pipeline;
    if(hasFrameFlag(flags,FrameFlagBits::DeviceLost))return ResetReason::DeviceLost;
    if(requested!=ResetReason::None)return requested;
    for(auto [flag,reason]:{std::pair{FrameFlagBits::Open,ResetReason::Open},
        {FrameFlagBits::Seek,ResetReason::Seek},{FrameFlagBits::Drop,ResetReason::FrameDrop},
        {FrameFlagBits::Resize,ResetReason::Resize},{FrameFlagBits::PauseResume,ResetReason::PauseResume},
        {FrameFlagBits::Discontinuity,ResetReason::PtsDiscontinuity},{FrameFlagBits::Cut,ResetReason::SceneCut}})
        if(hasFrameFlag(flags,flag))return reason;
    return detected;
}
}
