#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>

namespace veyra::sink {
// Piecewise media time for resampled PCM. Frame positions refer to output
// samples, while endpoints retain the original source timeline.
class AudioFrameTimeline {
    struct Span {uint64_t first,count;double begin,end;};
    std::deque<Span> spans_;
public:
    void clear(){spans_.clear();}
    void append(uint64_t first,uint64_t count,double begin,double end){
        if(!count||!std::isfinite(begin)||!std::isfinite(end)||end<begin)return;
        spans_.push_back({first,count,begin,end});
        while(spans_.size()>1024)spans_.pop_front();
    }
    std::optional<double> at(double frame)const{
        for(const auto& s:spans_)if(frame>=s.first&&frame<=s.first+s.count)
            return s.begin+(s.end-s.begin)*(frame-s.first)/s.count;
        return {};
    }
    void discardBefore(uint64_t frame){while(!spans_.empty()&&spans_.front().first+spans_.front().count<frame)spans_.pop_front();}
};
}
