// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Types.h"
#include <mutex>
#include <stdexcept>
namespace veyra::remoteplay {
// Device polling remains outside the GPU render loop. UI focus loss must call
// setFocused(false) immediately. A new session starts neutral and unfocused.
class InputGate {
public:
    void begin(Generation g) {if(!g)throw std::invalid_argument("Generation zero is invalid");std::lock_guard l(mutex_);generation_=g;focused_=false;state_={};stamp_=0;}
    void setFocused(bool value) {std::lock_guard l(mutex_);focused_=value;state_={};stamp_=0;}
    bool update(Generation g,const ControllerState& s,HostTime now) {
        std::lock_guard l(mutex_);if(g!=generation_||!focused_||now<0||now<stamp_)return false;
        state_=s;state_.buttons&=0xffffu;stamp_=now;return true;
    }
    ControllerState sample(Generation g,HostTime now) const {
        std::lock_guard l(mutex_);
        if(!focused_||g!=generation_||now<stamp_||now-stamp_>1000000)return {}; // 100 ms stale input fails neutral.
        return state_;
    }
private:
    mutable std::mutex mutex_;Generation generation_=0;bool focused_=false;ControllerState state_{};HostTime stamp_=0;
};
} // namespace veyra::remoteplay
