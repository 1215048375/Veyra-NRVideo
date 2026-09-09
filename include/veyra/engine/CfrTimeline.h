#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

namespace veyra::engine {
// A rate is only a candidate. All timestamps must fit one common quantizer
// phase; a one-tick-per-frame time base is legal, a missing whole tick is not.
class CfrTimeline {
public:
    CfrTimeline(int num, int den, double quantum, double origin)
        : num_(num), den_(den), quantum_(quantum), origin_(origin), low_(-quantum/2), high_(quantum/2) {}
    bool valid() const {
        return num_ > 0 && den_ > 0 && fps() <= 1000 && fps() >= 1 &&
            std::isfinite(origin_) && std::isfinite(quantum_) && quantum_ > 0 && quantum_ <= 1.0 / fps() + 1e-12;
    }
    double fps() const { return double(num_) / den_; }
    double expected(uint64_t index) const { return origin_ + double(index) * den_ / num_; }
    bool accepts(uint64_t index, double pts) {
        if(!valid() || !std::isfinite(pts) || (havePrevious_ && pts<=previous_))return false;
        const double error=pts-expected(index);
        const double ticksPerFrame=1.0/(fps()*quantum_);
        if(std::abs(ticksPerFrame-std::round(ticksPerFrame))<1e-9 && std::abs(error)>1e-9)return false;
        const double lo=std::max(low_,error-quantum_/2),hi=std::min(high_,error+quantum_/2);
        if(lo>hi+1e-9)return false;
        low_=lo;high_=hi;previous_=pts;havePrevious_=true;return true;
    }
    static std::pair<int,int> select(int nominalNum,int nominalDen,double quantum,std::span<const double> samples) {
        if(samples.empty())return {0,0};
        auto fits=[&](int n,int d){CfrTimeline t(n,d,quantum,samples.front());for(size_t i=0;i<samples.size();++i)if(!t.accepts(i,samples[i]))return false;return t.valid();};
        // Prefer a declared standard rate when consistent. Otherwise choose a
        // simple, timestamp-consistent standard candidate, then the declaration.
        // Quantized short clips cannot prove a unique exact original rate.
        const std::pair<int,int> standard[]={{24,1},{25,1},{30,1},{48,1},{50,1},{60,1},{100,1},{120,1},{24000,1001},{30000,1001},{60000,1001},{120000,1001}};
        for(auto [n,d]:standard)if(int64_t(n)*nominalDen==int64_t(nominalNum)*d && fits(n,d))return {n,d};
        for(auto [n,d]:standard)if(fits(n,d))return {n,d};
        if(fits(nominalNum,nominalDen))return {nominalNum,nominalDen};
        return {0,0};
    }
private:
    int num_, den_;
    double quantum_, origin_, low_, high_,previous_=0;
    bool havePrevious_=false;
};
}
