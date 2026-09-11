#pragma once
#include <chrono>
#include <cmath>
#include <optional>

namespace veyra::sink {
// Audio-owner policy. A presentation deadline is not a hard PCM stop boundary:
// short GPU jitter must leave a continuous endpoint running. Explicit transport
// resets still hold immediately; persistent lag and a hard lead bound rebuffer.
class AudioVideoContinuity {
public:
    using Clock=std::chrono::steady_clock;
    bool blocked(bool explicitHold,double audioPts,double coverage,Clock::time_point now) {
        if(explicitHold){lateSince_.reset();buffering_=false;return true;}
        if(!std::isfinite(audioPts)||!std::isfinite(coverage)){reset();return false;}
        const double lead=audioPts-coverage;
        if(buffering_){
            if(lead<=0)reset();
            return buffering_;
        }
        if(lead<=20){lateSince_.reset();return false;}
        if(!lateSince_)lateSince_=now;
        buffering_=lead>=80||now-*lateSince_>=std::chrono::milliseconds(100);
        return buffering_;
    }
    void reset(){lateSince_.reset();buffering_=false;}
private:
    bool buffering_=false;
    std::optional<Clock::time_point> lateSince_;
};
}
