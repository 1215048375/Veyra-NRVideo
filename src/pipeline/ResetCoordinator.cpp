#include "veyra/pipeline/ResetCoordinator.h"

namespace veyra::pipeline {

void ResetCoordinator::notifyReset(ResetReason reason) {
    if (reason == ResetReason::None) { return; }
    ++resetCount_[static_cast<uint8_t>(reason)];
    if (!pending_) {
        pending_ = true;
        pendingReason_ = reason;
    }
}

uint64_t ResetCoordinator::beginFrame(uint64_t sequence) {
    if (pending_) {
        ++epoch_;
        lastReason_ = pendingReason_;
        lastResetSequence_ = sequence;
        pending_ = false;
        pendingReason_ = ResetReason::None;
    }
    return epoch_;
}

uint32_t ResetCoordinator::totalResets() const {
    uint32_t total = 0;
    for (int i = 1; i < static_cast<int>(ResetReason::_Count); ++i) {
        total += resetCount_[i];
    }
    return total;
}

} // namespace veyra::pipeline
