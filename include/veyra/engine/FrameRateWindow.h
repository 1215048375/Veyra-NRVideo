#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
namespace veyra::engine {
// Completion events, not source arrivals or generated-frame multipliers.
// Call under the owning controller's mutex. All times use monotonic 100 ns.
class FrameRateWindow {
public:
    void reset(int64_t now){start_=now;times_.clear();}
    void complete(int64_t now){
        times_.push_back(now);
        while(times_.size()>8192||(!times_.empty()&&times_.front()<=now-window))times_.pop_front();
    }
    double rate(int64_t now)const{
        const auto elapsed=(std::min)(window,(std::max<int64_t>)(0,now-start_));
        if(elapsed<1000000)return 0;
        const auto first=std::upper_bound(times_.begin(),times_.end(),now-window);
        return double(times_.end()-first)*10000000.0/double(elapsed);
    }
private:
    static constexpr int64_t window=10000000;
    int64_t start_=0;
    std::deque<int64_t> times_;
};
}
