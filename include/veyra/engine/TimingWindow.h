#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <vector>
namespace veyra::engine {
class TimingWindow {
    std::deque<double> values_;
    mutable double cached_=0;
    mutable std::chrono::steady_clock::time_point refresh_{};
public:
    void add(double v){if(!std::isfinite(v)||v<0)return;values_.push_back(v);if(values_.size()>1200)values_.pop_front();}
    void clear(){values_.clear();cached_=0;refresh_={};}
    double p95()const{
        if(values_.empty())return 0;const auto now=std::chrono::steady_clock::now();
        if(now<refresh_)return cached_;
        std::vector<double> sorted(values_.begin(),values_.end());const size_t i=(sorted.size()-1)*95/100;
        std::nth_element(sorted.begin(),sorted.begin()+i,sorted.end());cached_=sorted[i];refresh_=now+std::chrono::milliseconds(250);return cached_;
    }
};
}
