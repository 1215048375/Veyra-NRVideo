#pragma once
#include <algorithm>
#include <cmath>

namespace veyra::engine {
// Live capture never waits on an absolute source PTS. Once B is available,
// display generated(A,B), then B up to half an input interval later. Anchor
// each pair to current host time, not to the first (possibly stale) sample.
inline double livePairHoldMs(bool hasGenerated,double realPtsMs,double generatedPtsMs) {
    if(!hasGenerated||!std::isfinite(realPtsMs)||!std::isfinite(generatedPtsMs))return 0;
    return std::clamp(realPtsMs-generatedPtsMs,0.0,1000.0/30.0);
}
}
