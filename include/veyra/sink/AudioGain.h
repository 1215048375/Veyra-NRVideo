#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace veyra::sink {
// 5 ms full-scale ramp at 48 kHz, independent of PTS and renderer clock.
inline void applyStereoGain(float* samples,size_t frames,float target,float& current){
    target=std::isfinite(target)?std::clamp(target,0.0f,1.0f):0;
    for(size_t i=0;i<frames;++i){current+=std::clamp(target-current,-1.0f/240,1.0f/240);samples[i*2]*=current;samples[i*2+1]*=current;}
}
}
