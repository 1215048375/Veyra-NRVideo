#pragma once
#include <deque>
#include "veyra/engine/EnhancementSettings.h"
#include <cmath>
#include <algorithm>
namespace veyra::engine {
// Only reports a rate after repeated moving observations. A static scene
// cannot establish a lower content frame rate merely through equality.
class ContentCadence {
public:
    void reset(){changes_.clear();deltas_.clear();last_=-1;rate_=0;}
    void observe(double ptsMs,double sad,bool comparable){
        if(!comparable||!std::isfinite(ptsMs)){reset();return;}
        if(last_>=0){const auto d=ptsMs-last_;if(d<=0||d>100){reset();last_=ptsMs;return;}deltas_.push_back(d);if(deltas_.size()>60)deltas_.pop_front();}
        last_=ptsMs;changes_.push_back(sad>0.002?1:0);if(changes_.size()>60)changes_.pop_front();rate_=0;
        if(changes_.size()<30||deltas_.empty())return;int changed=0;for(int c:changes_)changed+=c;if(changed<12)return;
        double elapsed=0;for(double d:deltas_)elapsed+=d;const double transport=1000*deltas_.size()/elapsed;const double candidate=transport*changed/changes_.size();
        for(int r:{30,50,60})if(std::abs(candidate-r)<1.6)rate_=r;
    }
    int measuredRate()const{return rate_;}
    int confirmedRate(ContentRate mode)const{
        const int requested=mode==ContentRate::Fps30?30:mode==ContentRate::Fps50?50:mode==ContentRate::Fps60?60:0;
        return mode==ContentRate::Transport?0:requested?(rate_==requested?rate_:0):rate_;
    }
    bool conflicts(ContentRate mode)const{return rate_!=0&&mode>=ContentRate::Fps30&&mode<=ContentRate::Fps60&&confirmedRate(mode)==0;}
private:std::deque<int> changes_;std::deque<double>deltas_;double last_=-1;int rate_=0;
};
}
