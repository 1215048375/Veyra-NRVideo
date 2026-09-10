#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <limits>
namespace veyra::source {
inline bool captureDiscontinuity(bool pending,bool retained,bool driver,bool havePrevious,
                                 double previous,double current,double nominalFps){
    const double maxGap=nominalFps>0?2.5/nominalFps:0.1;
    return (pending&&retained)||driver||
        (havePrevious&&(current<=previous||current-previous>maxGap));
}
inline pipeline::Rational captureDuration(int64_t start,int64_t end,bool completeSampleTime,int64_t nominal100ns){
    if(completeSampleTime&&end>start){
        const auto delta=uint64_t(end)-uint64_t(start);
        if(delta<=uint64_t((std::numeric_limits<int64_t>::max)()))return {int64_t(delta),10000000};
    }
    return nominal100ns>0?pipeline::Rational{nominal100ns,10000000}:pipeline::Rational::unknown();
}
}
