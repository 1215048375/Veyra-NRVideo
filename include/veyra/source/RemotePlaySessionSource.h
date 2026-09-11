#pragma once
#include "RemotePlaySource.h"
#include "veyra/sink/CaptureAudioSession.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace veyra::source {
// Network/decoder owner keeps consuming reference pictures while the GPU is
// busy. Only already-decoded presentation opportunities use the latest slot.
class RemotePlaySessionSource final : public IFrameSource {
public:
    ~RemotePlaySessionSource() override { close(); }
    bool connect(RemotePlayConnectDesc desc);
    bool open(const SourceOpenDesc&) override { return false; }
    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket&, const AVFrame**) override;
    bool seek(const pipeline::Rational&) override { return false; }
    void close() noexcept override;
    void controller(remoteplay::ControllerState state);
    void loginPin(std::string pin);
    remoteplay::ControllerFeedback takeFeedback();
    remoteplay::SessionInbox::Snapshot sessionSnapshot() const;
    uint64_t skipped() const;
    struct Rates {uint64_t received=0,decoded=0,ingressDropped=0;double receivedFps=0,decodedFps=0;bool ready=false;};
    Rates rates() const;
    sink::CaptureAudioState audioState() const { return audio_.snapshot(); }
    void setAudioGain(float value) { audio_.setGain(value); }
    void setAudioSync(unsigned mode, int offset) { audio_.setSync(mode, offset); }
    void videoPresented(double pts, int64_t host) { audio_.videoPresented(pts, host); }
    void videoReset() { audio_.videoReset(); }
private:
    friend struct RemotePlaySessionSourceTestAccess;
    void publishDecoded(const AVFrame*,pipeline::FramePacket,const SourceInfo&);
    void run(std::stop_token, RemotePlayConnectDesc);
    remoteplay::ControllerState takeControllerLocked(remoteplay::HostTime now);
    struct Frame { std::shared_ptr<AVFrame> frame; pipeline::FramePacket packet; SourceInfo info; };
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::jthread owner_;
    bool initialized_=false, started_=false, failed_=false;
    std::optional<Frame> latest_;
    std::shared_ptr<AVFrame> view_;
    SourceInfo info_, publishedInfo_;
    remoteplay::SessionInbox::Snapshot snapshot_;
    remoteplay::ControllerState controller_;
    std::deque<std::pair<remoteplay::HostTime,remoteplay::ControllerState>> pendingControllers_;
    remoteplay::HostTime controllerStamp_=0;
    std::string pin_;
    uint64_t skipped_=0;
    Rates rates_;
    remoteplay::ControllerFeedback feedback_;
    sink::CaptureAudioSession audio_;
};
}
