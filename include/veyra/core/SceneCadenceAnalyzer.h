#pragma once

// Scene/Cadence analyzer  detects hard cuts, flash frames, duplicate frames,
// and cadence changes using histogram + SAD + PTS sequence analysis.
// Feeds ResetCoordinator with the appropriate ResetReason.
#include <cstdint>
#include <vector>

#include "veyra/core/ResetCoordinator.h"

namespace veyra::core {

struct SceneAnalysisResult {
    bool isSceneCut = false;       // hard cut detected
    bool isFlash = false;          // bright flash (not a cut)
    bool isDuplicate = false;      // duplicate of previous frame
    bool isCadenceBreak = false;   // PTS irregularity
    double histogramDistance = 0.0; // Chi-square or L1 distance
    double sadScore = 0.0;        // Sum of Absolute Differences (normalized)
    double ptsDeltaUs = 0.0;      // PTS difference from previous frame
    uint64_t frameId = 0;
};

class SceneCadenceAnalyzer {
public:
    // Configure thresholds.
    struct Config {
        double sceneCutHistogramThreshold = 0.5;  // L1 distance for hard cut
        double sceneCutSadThreshold = 0.3;        // normalized SAD for hard cut
        double flashHistogramThreshold = 0.1;     // flash: high SAD, low hist distance
        double flashSadThreshold = 0.5;           // flash: SAD above this
        double duplicateSadThreshold = 0.001;     // near-zero SAD = duplicate
        double duplicatePtsWindowUs = 1000;       // PTS delta < 1ms = duplicate
        double cadenceBreakPtsJitterUs = 5000;    // PTS jitter beyond this = break
    };

    explicit SceneCadenceAnalyzer(const Config& config = {}) : config_(config) {}

    // Analyze one frame against the previous frame's histogram and SAD.
    // `histogram` is a 256-bin luma histogram (normalized to [0,1]).
    // `sad` is the normalized SAD score in [0,1].
    // `ptsUs` is this frame's PTS.
    SceneAnalysisResult analyze(uint64_t frameId,
                                const std::vector<double>& histogram,
                                double sad,
                                uint64_t ptsUs);

    // Reset baseline (called on seek/source switch/etc).
    void reset() { hasBaseline_ = false; prevHistogram_.clear(); }

    const SceneAnalysisResult& lastResult() const { return lastResult_; }
    uint64_t sceneCutCount() const { return sceneCutCount_; }
    uint64_t flashCount() const { return flashCount_; }
    uint64_t duplicateCount() const { return duplicateCount_; }

private:
    Config config_;
    bool hasBaseline_ = false;
    std::vector<double> prevHistogram_;
    uint64_t prevPtsUs_ = 0;
    uint64_t sceneCutCount_ = 0;
    uint64_t flashCount_ = 0;
    uint64_t duplicateCount_ = 0;
    SceneAnalysisResult lastResult_;

    double histogramDistance(const std::vector<double>& a, const std::vector<double>& b) const;
};

} // namespace veyra::core
