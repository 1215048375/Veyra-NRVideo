#pragma once

// Frame window  the temporal context the EnhanceGraph and guidance
// providers operate on. DLSSG 2X requires next-frame lookahead.
#include <cstdint>
#include <vector>

#include "veyra/core/FramePacket.h"

namespace veyra::core {

struct FrameWindow {
    FramePacket* prev = nullptr;      // t-1 (may be null on first frame / after reset)
    FramePacket* current = nullptr;   // t
    FramePacket* next = nullptr;      // t+1 (needed for DLSSG 2X; null when unavailable)
    std::vector<FramePacket*> lookahead; // future frames for A/B/C bounded modes

    bool hasTemporalHistory() const { return prev != nullptr; }
    bool hasNextFrame() const { return next != nullptr; }
    uint32_t lookaheadCount() const { return static_cast<uint32_t>(lookahead.size()); }
    bool sameSourceEpoch() const {
        uint64_t epoch = 0;
        bool first = true;
        for (const FramePacket* p : { prev, current, next }) {
            if (p == nullptr) continue;
            if (first) { epoch = p->sourceEpoch; first = false; }
            else if (p->sourceEpoch != epoch) return false;
        }
        return true;
    }
};

} // namespace veyra::core
