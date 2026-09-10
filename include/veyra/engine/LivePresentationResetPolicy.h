#pragma once

#include "veyra/pipeline/FramePacket.h"
#include <cstdint>

namespace veyra::engine {

// A capture mailbox overwrite breaks temporal processing history, but it does
// not invalidate already-produced real frames. Keep those real frames eligible
// for presentation while suppressing generated frames from the prior epoch.
constexpr bool presentationDrainRequired(bool explicitReset, pipeline::FrameFlags flags) {
    return explicitReset
        || pipeline::hasFrameFlag(flags, pipeline::FrameFlagBits::Resize)
        || pipeline::hasFrameFlag(flags, pipeline::FrameFlagBits::DeviceLost);
}

constexpr bool generatedPresentationCurrent(uint64_t jobGeneration, uint64_t currentGeneration) {
    return jobGeneration == currentGeneration;
}

} // namespace veyra::engine
