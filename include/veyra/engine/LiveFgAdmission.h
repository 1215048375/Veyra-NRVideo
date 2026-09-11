#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace veyra::engine {
// A pair is indivisible: NGX MFG indices share history.
// Admit while at least its last generated timestamp can still be useful.
inline bool admitLiveFg(int64_t now, int64_t lastDeadline, double elapsedMs,
                        std::optional<double> completionP95Ms, double presentP95Ms) {
    constexpr int64_t tolerance = 100000; // Same 10 ms grace as presentation.
    if(now > lastDeadline && now-lastDeadline > tolerance)return false;
    if(!completionP95Ms || !std::isfinite(*completionP95Ms))return true;
    const double remaining=(std::max)(0.0,*completionP95Ms-elapsedMs)+(std::max)(0.0,presentP95Ms);
    return double(lastDeadline-now)/10000.0+10.0 >= remaining;
}
}
