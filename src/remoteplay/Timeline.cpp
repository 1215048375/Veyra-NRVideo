// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Timeline.h"
#include <limits>
#include <stdexcept>
namespace veyra::remoteplay {
Sequence16Extender::Result Sequence16Extender::observe(std::uint16_t n) noexcept {
    if(!initialized_){initialized_=true;previous_=n;extended_=n;return {extended_,true,true,0};}
    const auto delta=static_cast<std::uint16_t>(n-previous_);
    if(delta==0 || delta>=0x8000u)return {extended_,false,false,0};
    if(extended_>std::numeric_limits<std::uint64_t>::max()-delta)return {};
    extended_+=delta;previous_=n;return {extended_,true,false,static_cast<std::uint32_t>(delta-1)};
}
namespace {
std::optional<std::int64_t> rescale(std::uint64_t n,std::uint32_t den) noexcept {
    if(den==0)return {};
    constexpr auto max=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto whole=n/den,rem=n%den;
    if(whole>max/10000000u)return {};
    const auto a=whole*10000000u,b=rem*10000000u/den;
    if(a>max-b) return {};
    return static_cast<std::int64_t>(a+b);
}
}
void RemotePlayClock::reset(HostTime origin,std::uint32_t fps) {
    if(origin<0 || (fps!=30&&fps!=60))throw std::invalid_argument("Invalid local clock origin/rate");
    *this={};origin_=origin;fps_=fps;
}
std::optional<EstimatedStamp> RemotePlayClock::video(std::uint64_t index,HostTime arrival) {
    if(!fps_ || arrival<origin_)return {};
    if(anchored_ && (index<=lastIndex_ || arrival<lastArrival_))return {};
    bool gap=false;
    if(!anchored_){videoAnchor_=arrival-origin_;firstIndex_=index;anchored_=true;}
    else if(arrival-lastArrival_>2000000 || index-lastIndex_>fps_/2u){
        videoAnchor_=arrival-origin_;firstIndex_=index;gap=true;
    }
    const auto delta=rescale(index-firstIndex_,fps_);
    if(!delta || videoAnchor_>std::numeric_limits<std::int64_t>::max()-*delta)return {};
    lastArrival_=arrival;lastIndex_=index;
    return EstimatedStamp{videoAnchor_+*delta,10000000/static_cast<std::int64_t>(fps_),gap};
}
std::optional<std::int64_t> RemotePlayClock::audioPts(std::uint64_t first,std::uint32_t rate,HostTime start) const noexcept {
    if(!fps_ || start<origin_ || rate<8000 || rate>192000)return {};
    const auto delta=rescale(first,rate);const auto base=start-origin_;
    if(!delta || base>std::numeric_limits<std::int64_t>::max()-*delta) return {};
    return base+*delta;
}
} // namespace veyra::remoteplay
