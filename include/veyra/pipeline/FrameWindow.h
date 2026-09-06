#pragma once

// Pipeline FrameWindow - fixed temporal context (Playbook R2/R6).
// prev/current/next plus an explicit lookaheadFrames mode. The unbounded
// vector from the veyra::core draft is intentionally gone: capture never
// buffers more than the mode requires, and DLSSG never receives a third
// frame as an input (C is for Veyra-side validation only).
#include <cstdint>
#include <initializer_list>

#include "veyra/pipeline/FramePacket.h"

namespace veyra::pipeline {

struct FrameWindow {
    static constexpr uint32_t kMaxLookahead = 2;

    const FramePacket* prev = nullptr;    // A (null on first frame / after reset)
    const FramePacket* current = nullptr; // B
    const FramePacket* next = nullptr;    // C (validation only; null is not an error)

    // 0 = NR Low Latency (no deliberate wait for a future frame)
    // 1 = FG Low Latency (A/B pair; A-half generated after B arrives)
    // 2 = Buffered Quality (A/B/C; C validates A/B guidance/depth/cut)
    uint32_t lookaheadFrames = 0;

    bool valid() const { return current != nullptr && current->valid(); }

    bool consistentLookahead() const { return lookaheadFrames <= kMaxLookahead; }

    // All present frames must come from one reset epoch; mixing epochs means
    // history survived a reset and must be rejected by consumers.
    bool sameSourceEpoch() const {
        uint64_t epoch = 0;
        bool first = true;
        for (const FramePacket* p : { prev, current, next }) {
            if (p == nullptr) { continue; }
            if (first) { epoch = p->sourceEpoch; first = false; }
            else if (p->sourceEpoch != epoch) { return false; }
        }
        return true;
    }
};

} // namespace veyra::pipeline
