#pragma once
#include <cstdint>
namespace veyra::remoteplay {
// Local monotonic milliseconds. Bounded for the entire user-started session;
// one recovered frame does not replenish reconnects and create a retry loop.
class StreamRecovery {
public:
    enum class Action {Wait,Keyframe,Reconnect,Fail};
    void beginAttempt(int64_t now){lastProgress_=now;keyframes_=0;currentVideo_=false;}
    void frame(int64_t now){lastProgress_=now;keyframes_=0;everVideo_=currentVideo_=true;}
    Action poll(int64_t now,bool loginPin,bool failed,bool retryAllowed=true,bool startupRetryAllowed=false){
        const auto silent=now-lastProgress_;
        if(loginPin)return silent>=120000?Action::Fail:Action::Wait;
        if(failed&&!retryAllowed)return Action::Fail;
        // Each rebuilt session needs its own handshake/first-frame allowance.
        // A previous session having video must not shorten a new handshake.
        if(failed||silent>=(currentVideo_?6000:30000)){
            if((!everVideo_&&!(failed&&startupRetryAllowed))||reconnects_>=3)return Action::Fail;
            ++reconnects_;return Action::Reconnect;
        }
        if(currentVideo_&&keyframes_<2&&silent>=1000+int64_t(keyframes_)*2000){++keyframes_;return Action::Keyframe;}
        return Action::Wait;
    }
    unsigned reconnects()const{return reconnects_;}
private:
    int64_t lastProgress_=0;unsigned keyframes_=0,reconnects_=0;
    bool everVideo_=false,currentVideo_=false;
};
}
