#pragma once

// Pipeline ResetCoordinator - single source of truth for reset epochs
// (Playbook R2). Events are staged with notifyReset() and the epoch advances
// exactly once per real-frame boundary in beginFrame(), so SR, NR, FG, NVOF,
// depth, and cadence all observe the SAME epoch for a given frame. Modules
// never decide resets on their own.
#include <cstdint>

namespace veyra::pipeline {

enum class ResetReason : uint8_t {
    None = 0,
    Open,          // source opened / started
    Seek,
    SceneCut,
    FrameDrop,     // capture mailbox drop / PTS gap
    Resize,        // extent or quality-mode change
    SourceSwitch,
    PauseResume,
    DeviceLost,
    _Count
};

inline const char* resetReasonName(ResetReason r) {
    switch (r) {
    case ResetReason::Open: return "Open";
    case ResetReason::Seek: return "Seek";
    case ResetReason::SceneCut: return "SceneCut";
    case ResetReason::FrameDrop: return "FrameDrop";
    case ResetReason::Resize: return "Resize";
    case ResetReason::SourceSwitch: return "SourceSwitch";
    case ResetReason::PauseResume: return "PauseResume";
    case ResetReason::DeviceLost: return "DeviceLost";
    default: return "None";
    }
}

class ResetCoordinator {
public:
    // Stage a reset event. Multiple events between frame boundaries collapse
    // into ONE epoch bump; every staged reason is still counted.
    void notifyReset(ResetReason reason);

    // Advance the frame boundary. Must be called exactly once per real frame,
    // before any consumer reads epoch() for that frame. Returns the epoch in
    // effect for the frame that is beginning.
    uint64_t beginFrame(uint64_t sequence);

    uint64_t epoch() const { return epoch_; }
    ResetReason lastReason() const { return lastReason_; }
    uint64_t lastResetSequence() const { return lastResetSequence_; }
    bool hasPendingReset() const { return pending_; }

    // True if `otherEpoch` predates the current epoch (stale history that
    // must not be consumed by NVOF/depth/NR/FG).
    bool isStale(uint64_t otherEpoch) const { return otherEpoch < epoch_; }

    uint32_t resetCount(ResetReason reason) const {
        return resetCount_[static_cast<uint8_t>(reason)];
    }
    uint32_t totalResets() const;

private:
    uint64_t epoch_ = 1;
    bool pending_ = false;
    ResetReason pendingReason_ = ResetReason::None;
    ResetReason lastReason_ = ResetReason::None;
    uint64_t lastResetSequence_ = 0;
    uint32_t resetCount_[static_cast<uint8_t>(ResetReason::_Count)] = {};
};

} // namespace veyra::pipeline
