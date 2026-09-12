#pragma once
#include <cmath>
#include <cstdint>

namespace veyra::engine {
// Realtime preview scheduling policies (REALTIME_AV_SCHEDULING_REPAIR P2/P3).
// The audio-master clock never pauses for slow enhancement; decoded source
// frames instead lose their enhancement/display opportunity when their real
// PTS window has passed. Export, images and paused single frames never use
// these policies and keep full source integrity.
inline constexpr double kPreviewExpiryEpsilonMs = 0.01;

// A decoded candidate may be dropped before upload/color/SR/NR once its whole
// display window [pts, pts+interval) is behind the master media clock and a
// newer decoded candidate is available. PTS stays truthful; only the preview
// opportunity is dropped.
inline bool previewCandidateExpired(double clockMs, double ptsMs, double intervalMs) {
    if (!std::isfinite(clockMs) || !std::isfinite(ptsMs) || !std::isfinite(intervalMs) || intervalMs <= 0)
        return false; // unknown cadence never drops frames
    return clockMs > ptsMs + intervalMs + kPreviewExpiryEpsilonMs;
}

// A completed generated frame is presented only while it can still be the
// freshest displayable image: once the master clock passed its own PTS by the
// same 10 ms grace the capture path uses, presenting it replays stale media
// between already-shown endpoints. Completed REAL frames are always presented:
// with one in-flight batch they are by construction the newest ready result
// (plan rule 3: never drop the only ready picture).
inline bool previewGeneratedExpired(double clockMs, double ptsMs) {
    if (!std::isfinite(clockMs) || !std::isfinite(ptsMs))
        return false;
    return clockMs > ptsMs + 10.0 + kPreviewExpiryEpsilonMs;
}

// XeSS-FG generation happens inside the SDK swapchain, so pair admission
// cannot gate an Evaluate. Suppress generation over stable intervals instead:
// sustained lateness disables it, sustained health re-enables it, and single
// fluctuations never toggle it. Re-enabling goes through the presenter's
// history reset (xefg resetHistory), not a per-frame switch.
class XessGenerationGate {
public:
    explicit XessGenerationGate(unsigned suppressAfter = 12, unsigned resumeAfter = 60)
        : suppressAfter_(suppressAfter), resumeAfter_(resumeAfter) {}

    // Observe the media-clock lateness (clock - pts) of one presented real
    // frame. Returns true when the suppression state changed.
    bool observe(double latenessMs, double intervalMs) {
        if (!std::isfinite(latenessMs) || !std::isfinite(intervalMs) || intervalMs <= 0)
            return false;
        if (suppressed_) {
            if (latenessMs < intervalMs * 0.5) {
                if (++healthy_ >= resumeAfter_) {
                    suppressed_ = false;
                    streak_ = healthy_ = 0;
                    return true;
                }
            } else {
                healthy_ = 0;
            }
            return false;
        }
        if (latenessMs > intervalMs * 1.5) {
            if (++streak_ >= suppressAfter_) {
                suppressed_ = true;
                streak_ = healthy_ = 0;
                return true;
            }
        } else {
            streak_ = 0;
        }
        return false;
    }

    bool suppressed() const { return suppressed_; }
    void reset() { suppressed_ = false; streak_ = healthy_ = 0; }

private:
    unsigned suppressAfter_, resumeAfter_;
    unsigned streak_ = 0, healthy_ = 0;
    bool suppressed_ = false;
};
}
