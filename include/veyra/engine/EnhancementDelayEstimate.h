#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace veyra::engine {
// Ideal direct-play baseline, not a simultaneous bypass measurement or scanout.
// File: positive lateness on the running media clock (startup excluded).
// Live: decoded-frame age minus estimated ordinary color/blit/present work.
inline std::optional<double> enhancementDelayEstimate(bool enhanced,bool live,
    double observedMs,std::optional<double> basicMs={}) {
    if(!std::isfinite(observedMs))return {};
    if(!enhanced)return 0.0;
    if(live&&(!basicMs||!std::isfinite(*basicMs)||*basicMs<0))return {};
    return std::max(0.0,observedMs-(live?*basicMs:0.0));
}
}
