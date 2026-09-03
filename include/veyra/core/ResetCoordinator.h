#pragma once

// Reset coordinator  tracks the current reset epoch and reasons.
// All temporal consumers (NR history, NVOF temporal hints, DLSSG history,
// scene analyzer baselines) must check the epoch before reusing state.
#include <cstdint>

namespace veyra::core {

enum class ResetReason : uint8_t {
    None = 0,
    Seek,           // playback seek
    SceneCut,       // detected hard cut
    FrameDrop,      // significant frame drop (history discontinuity)
    Resize,         // output/resource extent change
    SourceSwitch,   // new file / new capture source
    PauseResume,    // long pause then resume
    DeviceLost,     // D3D12 device removed/recreated
    _Count
};

inline const char* resetReasonName(ResetReason r) {
    switch (r) {
    case ResetReason::None: return "None";
    case ResetReason::Seek: return "Seek";
    case ResetReason::SceneCut: return "SceneCut";
    case ResetReason::FrameDrop: return "FrameDrop";
    case ResetReason::Resize: return "Resize";
    case ResetReason::SourceSwitch: return "SourceSwitch";
    case ResetReason::PauseResume: return "PauseResume";
    case ResetReason::DeviceLost: return "DeviceLost";
    default: return "Unknown";
    }
}

class ResetCoordinator {
public:
    uint64_t epoch() const { return epoch_; }
    ResetReason lastReason() const { return lastReason_; }
    uint64_t lastResetFrameId() const { return lastResetFrameId_; }

    // Called when a reset-worthy event occurs. Returns true if the epoch
    // actually advanced (i.e., this is a new reset, not a duplicate).
    bool reset(ResetReason reason, uint64_t frameId) {
        if (reason == ResetReason::None) return false;
        ++epoch_;
        lastReason_ = reason;
        lastResetFrameId_ = frameId;
        resetCount_[static_cast<uint8_t>(reason)]++;
        return true;
    }

    // True if `otherEpoch` is from before the current reset (stale history).
    bool isStale(uint64_t otherEpoch) const { return otherEpoch < epoch_; }

    uint32_t resetCount(ResetReason reason) const {
        return resetCount_[static_cast<uint8_t>(reason)];
    }

    uint32_t totalResets() const {
        uint32_t total = 0;
        for (int i = 0; i < static_cast<int>(ResetReason::_Count); ++i) total += resetCount_[i];
        return total;
    }

private:
    uint64_t epoch_ = 1;
    ResetReason lastReason_ = ResetReason::None;
    uint64_t lastResetFrameId_ = 0;
    uint32_t resetCount_[static_cast<uint8_t>(ResetReason::_Count)] = {};
};

} // namespace veyra::core
