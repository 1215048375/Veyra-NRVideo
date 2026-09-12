#pragma once
#include <deque>
#include <algorithm>
namespace veyra::sink {
// Recent low-jitter mapping, not an all-time minimum. Device oscillator drift
// must not be reported as ever-growing software audio compensation.
class ArrivalClockMapping {
    struct Sample {double host,offset;};
    std::deque<Sample> minimum_;
public:
    void reset(){minimum_.clear();}
    double observe(double hostMs,double ptsMs){
        const double offset=hostMs-ptsMs;
        while(!minimum_.empty()&&(minimum_.front().host<=hostMs-2000||minimum_.size()>=4096))minimum_.pop_front();
        while(!minimum_.empty()&&minimum_.back().offset>=offset)minimum_.pop_back();
        minimum_.push_back({hostMs,offset});return minimum_.front().offset;
    }
};
}
