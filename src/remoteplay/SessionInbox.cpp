// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/SessionInbox.h"
#include <limits>
namespace veyra::remoteplay {
struct SessionInbox::Shared {
    explicit Shared(QueueLimits limits):video(limits){}
    mutable std::mutex mutex;Generation generation=0;SessionState state=SessionState::Idle;
    HostTime origin=0;bool accepting=false;std::uint64_t stale=0;int error=0;
    VideoIngress video;AudioIngress audio;
    bool current(Generation g){if(!accepting||g!=generation){++stale;return false;}return true;}
};
SessionInbox::SessionInbox(QueueLimits l):shared_(std::make_shared<Shared>(l)){}
SessionInbox::~SessionInbox(){invalidate();}
SessionInbox::Token SessionInbox::begin(HostTime origin){
    if(origin<0)throw std::invalid_argument("Negative local clock origin");
    auto& s=*shared_;std::lock_guard lock(s.mutex);
    if(s.state!=SessionState::Idle)throw std::logic_error("Stop and join previous backend before a new session");
    if(s.generation==std::numeric_limits<Generation>::max())throw std::overflow_error("Session generation exhausted");
    ++s.generation;s.video.begin(s.generation);s.audio.begin(s.generation);s.origin=origin;s.state=SessionState::Connecting;s.accepting=true;s.error=0;
    return Token{shared_,s.generation};
}
void SessionInbox::invalidate(){auto& s=*shared_;std::lock_guard l(s.mutex);s.accepting=false;if(s.state!=SessionState::Idle)s.state=SessionState::Stopping;s.video.close();s.audio.close();}
void SessionInbox::finishStop(){auto& s=*shared_;std::lock_guard l(s.mutex);if(s.accepting)throw std::logic_error("Invalidate before join/finishStop");s.state=SessionState::Idle;}
bool SessionInbox::isCurrent(Generation g)const{auto& s=*shared_;std::lock_guard l(s.mutex);return s.accepting&&s.generation==g;}
bool SessionInbox::decodedFrameReady(Generation g){
    auto& s=*shared_;std::lock_guard l(s.mutex);
    if(!s.accepting||s.generation!=g||(s.state!=SessionState::WaitingFirstFrame&&s.state!=SessionState::Streaming))return false;
    s.state=SessionState::Streaming;return true;
}
std::optional<QueuedVideo> SessionInbox::tryVideo(HostTime now){auto& s=*shared_;std::lock_guard l(s.mutex);if(!s.accepting)return {};return s.video.tryPop(now);}
std::optional<PcmBlock> SessionInbox::tryAudio(){auto& s=*shared_;std::lock_guard l(s.mutex);if(!s.accepting)return {};return s.audio.tryPop();}
bool SessionInbox::takeIdrRequest(HostTime now){auto& s=*shared_;std::lock_guard l(s.mutex);return s.accepting&&s.video.takeIdrRequest(now);}
void SessionInbox::decodeFailed(Generation g){auto& s=*shared_;std::lock_guard l(s.mutex);if(s.accepting&&s.generation==g)s.video.requireKeyframe();}
SessionInbox::Snapshot SessionInbox::snapshot()const{
    auto& s=*shared_;std::lock_guard l(s.mutex);return {s.generation,s.state,s.origin,s.stale,s.error,s.video.stats(),s.audio.stats()};
}
bool SessionInbox::Token::video(VideoSample sample)const noexcept{
    try{auto s=shared_.lock();if(!s)return false;std::lock_guard l(s->mutex);if(!s->current(generation_))return false;
        if(sample.generation!=generation_)return false;
        const auto r=s->video.push(std::move(sample),monotonic100ns());return r==PushStatus::Accepted||r==PushStatus::ConfigStored;
    }catch(...){failed(-1001);return false;}
}
bool SessionInbox::Token::audio(PcmBlock block)const noexcept{
    try{auto s=shared_.lock();if(!s)return false;std::lock_guard l(s->mutex);if(!s->current(generation_)||block.generation!=generation_)return false;return s->audio.push(std::move(block));}
    catch(...){failed(-1002);return false;}
}
void SessionInbox::Token::connected()const noexcept{
    try{auto s=shared_.lock();if(!s)return;std::lock_guard l(s->mutex);if(!s->current(generation_))return;
        if(s->state==SessionState::Connecting||s->state==SessionState::LoginPinRequired)s->state=SessionState::WaitingFirstFrame;
    }catch(...){}
}
void SessionInbox::Token::loginPinRequired()const noexcept{
    try{auto s=shared_.lock();if(!s)return;std::lock_guard l(s->mutex);if(s->current(generation_))s->state=SessionState::LoginPinRequired;}catch(...){}
}
void SessionInbox::Token::failed(int code)const noexcept{
    try{auto s=shared_.lock();if(!s)return;std::lock_guard l(s->mutex);if(!s->current(generation_))return;
        s->error=code;s->state=SessionState::Failed;s->accepting=false;s->video.close();s->audio.close();
    }catch(...){}
}
} // namespace veyra::remoteplay
