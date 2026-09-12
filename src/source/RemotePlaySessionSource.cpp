#include "veyra/source/RemotePlaySessionSource.h"
#include "veyra/Log.h"
#include <cmath>
#include <format>
extern "C" {
#include <libavutil/frame.h>
}
namespace veyra::source {
bool RemotePlaySessionSource::connect(RemotePlayConnectDesc desc) {
    close();
    { std::lock_guard lock(mutex_); initialized_=started_=failed_=false; skipped_=0; rates_={}; feedback_={}; controller_={}; pendingControllers_.clear(); snapshot_={}; }
    owner_=std::jthread([this, request=std::move(desc)](std::stop_token stop) mutable { run(stop,std::move(request)); });
    std::unique_lock lock(mutex_);
    ready_.wait(lock,[&]{return initialized_;});
    info_=publishedInfo_;
    return started_;
}
void RemotePlaySessionSource::run(std::stop_token stop, RemotePlayConnectDesc desc) {
    RemotePlaySource source;
    std::jthread feeder;
    std::jthread monitor;
    try {
        const bool ok=source.connect(desc);
        desc.request.credentials=remoteplay::PairingCredentials{};
        {std::lock_guard lock(mutex_);publishedInfo_=source.info();started_=ok;failed_=!ok;initialized_=true;telemetryInbox_=source.telemetryInbox();}
        ready_.notify_all();
        if(ok) {
            monitor=std::jthread([inbox=source.telemetryInbox()](std::stop_token cancel){
                while(!cancel.stop_requested()){
                    const auto s=inbox->snapshot();const auto now=remoteplay::monotonic100ns();
                    const auto age=[&](auto stamp){return stamp>0?double(now-stamp)/10000:-1.0;};
                    log::info("remoteplay-progress",std::format("state={} received={} decoded={} inputFps={:.1f} decodeFps={:.1f} videoMbps={:.3f} receiveAgeMs={:.1f} decodeAgeMs={:.1f} decodeBusyMs={:.1f} queue={} waitingIdr={} idrRequests={} errors={} (complete-video callback; not network latency)",int(s.state),s.video.accessUnits,s.decodedFrames,s.receivedFps,s.decodedFps,s.videoMbps,age(s.lastVideo100ns),age(s.lastDecoded100ns),age(s.decodeStarted100ns),s.video.depth,s.video.waitingForIdr,s.video.idrRequests,s.errorCode));
                    for(int i=0;i<10&&!cancel.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
            WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;
            format.nSamplesPerSec=48000;format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=384000;
            if(audio_.configure(format)&&audio_.start()) {
                // Separate from both decoder and GPU owners. Source is closed
                // only after this reader and the WASAPI owner have joined.
                feeder=std::jthread([this,&source](std::stop_token cancel){
                    try {
                        float pcm[960]; double next=-1;
                        while(!cancel.stop_requested()) {
                            double pts=0;const auto count=source.pullAudio(pcm,480,&pts);
                            if(count) {
                                const bool gap=next>=0&&std::abs(pts-next)>0.5;
                                if(!audio_.push(pcm,count*2*sizeof(float),pts,gap))
                                    log::warn("remoteplay-audio","PCM queue rejected block");
                                next=pts+1000.0*double(count)/48000;
                            } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                    } catch(const std::exception& e) {log::error("remoteplay-audio",e.what());}
                });
            } else log::error("remoteplay-audio","output initialization failed; video remains available");
            auto lastFrame=std::chrono::steady_clock::now();
            auto nextController=lastFrame;
            auto rateStart=lastFrame;uint64_t previousReceived=0,previousDecoded=0;
            unsigned recoveryAttempts=0;
            auto nextRecovery=lastFrame+std::chrono::seconds(1);
            while(!stop.stop_requested()) {
                std::string pin;
                {std::lock_guard lock(mutex_);pin.swap(pin_);}
                if(!pin.empty()) {
                    const auto r=source.submitLoginPin(pin);SecureZeroMemory(pin.data(),pin.size());
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                }
                const auto now=std::chrono::steady_clock::now();
                if(!desc.request.viewOnly&&now>=nextController) {
                    remoteplay::ControllerState controller;
                    {std::lock_guard lock(mutex_);controller=takeControllerLocked(remoteplay::monotonic100ns());}
                    const auto r=source.submitController(controller);
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                    nextController=now+std::chrono::milliseconds(4);
                }
                pipeline::FramePacket packet; const AVFrame* frame=nullptr;
                const auto result=source.read(packet,&frame);
                auto feedback=source.takeFeedback();
                const auto snapshot=source.sessionSnapshot();
                {std::lock_guard lock(mutex_);snapshot_=snapshot;feedback_.merge(std::move(feedback));
                    rates_.received=snapshot.video.accessUnits;rates_.ingressDropped=snapshot.video.dropped;
                    const double elapsed=std::chrono::duration<double>(now-rateStart).count();
                    if(elapsed>=1){rates_.receivedFps=double(rates_.received-previousReceived)/elapsed;
                        rates_.decodedFps=double(rates_.decoded-previousDecoded)/elapsed;rates_.ready=true;
                        previousReceived=rates_.received;previousDecoded=rates_.decoded;rateStart=now;}
                }
                if(result==SourceReadStatus::Error) {std::lock_guard lock(mutex_);failed_=true;break;}
                if(result==SourceReadStatus::Frame && frame) {
                    publishDecoded(frame,packet,source.info());
                    lastFrame=now;
                    recoveryAttempts=0;nextRecovery=now+std::chrono::seconds(1);
                } else {
                    if(snapshot.state==remoteplay::SessionState::Streaming&&now>=nextRecovery&&recoveryAttempts<3){
                        ++recoveryAttempts;source.recoverVideo();nextRecovery=now+std::chrono::seconds(2);
                        log::warn("remoteplay-recovery",std::format("no decoded progress; request keyframe attempt={} received={} decoded={} lastReceiveAgeMs={:.1f}",recoveryAttempts,snapshot.video.accessUnits,snapshot.decodedFrames,snapshot.lastVideo100ns?double(remoteplay::monotonic100ns()-snapshot.lastVideo100ns)/10000:-1));
                    }
                    // A missing console or unrecoverable stream must not leave
                    // a perpetual opening spinner. Login PIN allows more time.
                    const auto deadline=snapshot.state==remoteplay::SessionState::LoginPinRequired?std::chrono::seconds(120):std::chrono::seconds(30);
                    if(now-lastFrame>deadline) {
                        log::error("remoteplay","no decoded frame before recovery deadline");
                        std::lock_guard lock(mutex_);failed_=true;break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
    } catch(const std::exception& e) {
        log::error("remoteplay",e.what());
        {std::lock_guard lock(mutex_);failed_=true;initialized_=true;}
        ready_.notify_all();
    }
    if(monitor.joinable()){monitor.request_stop();monitor.join();}
    if(feeder.joinable()){feeder.request_stop();feeder.join();}
    audio_.stop();
    source.close();
}
void RemotePlaySessionSource::publishDecoded(const AVFrame* frame,pipeline::FramePacket packet,const SourceInfo& info){
    auto* cloned=av_frame_clone(frame);if(!cloned)throw std::bad_alloc();
    Frame item{std::shared_ptr<AVFrame>(cloned,[](AVFrame* p){av_frame_free(&p);}),packet,info};
    std::lock_guard lock(mutex_);
    ++rates_.decoded;
    if(latest_){++skipped_;item.packet.flags|=latest_->packet.flags;
        item.packet.flags|=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);}
    latest_=std::move(item);
}
SourceReadStatus RemotePlaySessionSource::read(pipeline::FramePacket& packet,const AVFrame** frame) {
    if(frame)*frame=nullptr;
    std::lock_guard lock(mutex_);
    if(failed_)return SourceReadStatus::Error;
    if(!latest_)return started_?SourceReadStatus::Waiting:SourceReadStatus::Error;
    packet=latest_->packet;info_=latest_->info;view_=std::move(latest_->frame);latest_.reset();
    if(frame)*frame=view_.get();return SourceReadStatus::Frame;
}
void RemotePlaySessionSource::close() noexcept {
    if(owner_.joinable()){owner_.request_stop();owner_.join();}
    std::lock_guard lock(mutex_);latest_.reset();view_.reset();info_={};started_=false;
    SecureZeroMemory(pin_.data(),pin_.size());pin_.clear();
}
void RemotePlaySessionSource::controller(remoteplay::ControllerState state){
    std::lock_guard lock(mutex_);controllerStamp_=remoteplay::monotonic100ns();
    // A neutral input (focus/device loss) cancels queued actions immediately.
    if(!state.inputActive)pendingControllers_.clear();
    if(pendingControllers_.size()>=16){
        pendingControllers_.clear();pendingControllers_.push_back({controllerStamp_,{}});
        log::warn("remoteplay-input","Controller queue overflow; releasing before latest state");
    }
    pendingControllers_.push_back({controllerStamp_,std::move(state)});
}
remoteplay::ControllerState RemotePlaySessionSource::takeControllerLocked(remoteplay::HostTime now){
    if(now-controllerStamp_>1000000){pendingControllers_.clear();controller_={};return controller_;}
    if(!pendingControllers_.empty()&&now-pendingControllers_.front().first>1000000){
        while(!pendingControllers_.empty()&&now-pendingControllers_.front().first>1000000)pendingControllers_.pop_front();
        controller_={};return controller_;
    }
    if(!pendingControllers_.empty()){controller_=std::move(pendingControllers_.front().second);pendingControllers_.pop_front();}
    return controller_;
}
void RemotePlaySessionSource::loginPin(std::string pin){std::lock_guard lock(mutex_);SecureZeroMemory(pin_.data(),pin_.size());pin_=std::move(pin);}
remoteplay::SessionInbox::Snapshot RemotePlaySessionSource::sessionSnapshot()const{
    std::shared_ptr<const remoteplay::SessionInbox> inbox;{std::lock_guard lock(mutex_);inbox=telemetryInbox_;if(!inbox)return snapshot_;}
    return inbox->snapshot();
}
RemotePlaySessionSource::Rates RemotePlaySessionSource::rates()const{std::lock_guard lock(mutex_);return rates_;}
remoteplay::ControllerFeedback RemotePlaySessionSource::takeFeedback(){std::lock_guard lock(mutex_);auto result=std::move(feedback_);feedback_={};return result;}
uint64_t RemotePlaySessionSource::skipped()const{std::lock_guard lock(mutex_);return skipped_;}
}
