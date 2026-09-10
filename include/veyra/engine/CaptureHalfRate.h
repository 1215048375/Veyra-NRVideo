#pragma once
#include <cmath>
#include <cstdint>
namespace veyra::engine {
// Explicit 60/59.94 transport -> 30/29.97 sampling before any GPU work.
// Keeps original PTS. It does not claim to detect unique pictures or repair
// irregular game cadence. Quarter-slot tolerance absorbs timestamp jitter.
class CaptureHalfRate {
public:
    void reset(){anchored_=false;}
    static bool supported(double fps){return std::isfinite(fps)&&fps>=59&&fps<=61;}
    bool accept(int64_t pts,int64_t transportInterval,bool boundary){
        if(boundary||!anchored_||pts<last_||pts-last_>transportInterval*6){
            anchored_=true;next_=pts+transportInterval*2;last_=pts;return true;
        }
        last_=pts;
        if(pts+transportInterval/4<next_)return false;
        const auto period=transportInterval*2;
        next_+=((pts+transportInterval/4-next_)/period+1)*period;
        return true;
    }
private:
    bool anchored_=false;int64_t next_=0,last_=0;
};
}
