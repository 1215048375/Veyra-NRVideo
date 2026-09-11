// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "VideoIngress.h"
#include <memory>
#include <stdexcept>
namespace veyra::remoteplay {
// Thread-safe callback boundary, NOT a replacement Remote Play protocol/session.
// Invalidate before stopping Chiaki, join all its callbacks, then finishStop().
// Tokens hold weak ownership so a saved callback cannot resurrect a destroyed inbox.
class SessionInbox {
    struct Shared;
public:
    struct Snapshot {
        Generation generation=0;SessionState state=SessionState::Idle;HostTime commonOrigin100ns=0;
        std::uint64_t staleCallbacks=0;int errorCode=0;
        VideoQueueStats video;AudioQueueStats audio;
    };
    class Token {
    public:
        bool video(VideoSample sample) const noexcept;
        bool audio(PcmBlock block) const noexcept;
        void connected() const noexcept;
        void loginPinRequired() const noexcept;
        void failed(int code) const noexcept;
        Generation generation()const noexcept{return generation_;}
    private:
        friend class SessionInbox;Token(std::weak_ptr<Shared> shared,Generation g):shared_(std::move(shared)),generation_(g){}
        std::weak_ptr<Shared> shared_;Generation generation_=0;
    };
    explicit SessionInbox(QueueLimits limits={});
    ~SessionInbox();
    SessionInbox(const SessionInbox&)=delete;SessionInbox& operator=(const SessionInbox&)=delete;
    Token begin(HostTime commonOrigin);
    void invalidate();
    void finishStop(); // Only after actual backend stop/join, not from a callback.
    bool decodedFrameReady(Generation);
    std::optional<QueuedVideo> tryVideo(HostTime now);
    std::optional<PcmBlock> tryAudio();
    bool takeIdrRequest(HostTime now);
    void decodeFailed(Generation);
    bool isCurrent(Generation)const;
    Snapshot snapshot()const;
private:
    std::shared_ptr<Shared> shared_;
};
} // namespace veyra::remoteplay
