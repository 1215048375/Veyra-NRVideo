#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace veyra::engine {
// Revision-scoped costs survive temporal-history resets. Old GPU pressure
// expires by wall time, rather than requiring successful FG to replace it.
class FgRecoveryBudget {
    struct Sample {int64_t time;double ms;};
    std::deque<Sample> base_,fg_,warmup_;
    bool limited_=false;
    unsigned recoveryPairs_=0;
    int64_t retryAt_=0;
    static void add(std::deque<Sample>& values,int64_t now,double ms){
        if(!std::isfinite(ms)||ms<0)return;
        while(!values.empty()&&(values.size()>=64||values.front().time<now-10000000))values.pop_front();
        values.push_back({now,ms});
    }
    static std::optional<double> p95(const std::deque<Sample>& values,int64_t now,int64_t age){
        std::vector<double> current;
        for(const auto& sample:values)if(sample.time>=now-age)current.push_back(sample.ms);
        if(current.empty())return {};
        const auto index=(current.size()*95+99)/100-1;
        std::nth_element(current.begin(),current.begin()+index,current.end());return current[index];
    }
public:
    void reset(){base_.clear();fg_.clear();warmup_.clear();limited_=false;recoveryPairs_=0;retryAt_=0;}
    void fgCost(double ms,int64_t now){add(fg_,now,ms);}
    void complete(double ms,bool evaluated,bool warmup,int64_t now){
        if(warmup){add(warmup_,now,ms);return;}
        const auto extra=evaluated?p95(fg_,now,20000000):std::optional<double>(0);
        // If a generated batch lacks a GPU timing, retain its whole measured
        // completion as conservative base cost; never assume free FG.
        add(base_,now,std::max(0.0,ms-extra.value_or(0)));
    }
    std::optional<double> predicted(int64_t now)const{
        const auto base=p95(base_,now,10000000);
        if(!base)return {};
        return *base+p95(fg_,now,20000000).value_or(0);
    }
    bool admit(int64_t now,int64_t deadline,double elapsed,double present){
        const auto cost=predicted(now);
        const bool fits=double(deadline-now)/10000+10>=std::max(0.0,cost.value_or(elapsed)-elapsed)+std::max(0.0,present);
        if(!fits){
            if(!limited_||recoveryPairs_){retryAt_=now+2500000;}
            limited_=true;recoveryPairs_=0;return false;
        }
        if(limited_){
            if(now<retryAt_)return false;
            // A probe consists of consecutive admitted pairs, not an isolated
            // warmup. Every pair still checks its actual deadline above.
            if(++recoveryPairs_>=2){limited_=false;recoveryPairs_=0;}
        }
        return true;
    }
    bool recovering()const{return limited_&&recoveryPairs_>0;}
};
}
