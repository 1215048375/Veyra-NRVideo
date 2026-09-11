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
    { std::lock_guard lock(mutex_); initialized_=started_=failed_=false; skipped_=0; controller_={}; snapshot_={}; }
    owner_=std::jthread([this, request=std::move(desc)](std::stop_token stop) mutable { run(stop,std::move(request)); });
    std::unique_lock lock(mutex_);
    ready_.wait(lock,[&]{return initialized_;});
    info_=publishedInfo_;
    return started_;
}
void RemotePlaySessionSource::run(std::stop_token stop, RemotePlayConnectDesc desc) {
    RemotePlaySource source;
    std::jthread feeder;
    try {
        const bool ok=source.connect(desc);
        desc.request.credentials=remoteplay::PairingCredentials{};
        {std::lock_guard lock(mutex_);publishedInfo_=source.info();started_=ok;failed_=!ok;initialized_=true;}
        ready_.notify_all();
        if(ok) {
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
            while(!stop.stop_requested()) {
                std::string pin; remoteplay::ControllerState controller;
                {std::lock_guard lock(mutex_);pin.swap(pin_);controller=remoteplay::monotonic100ns()-controllerStamp_<=1000000?controller_:remoteplay::ControllerState{};}
                if(!pin.empty()) {
                    const auto r=source.submitLoginPin(pin);SecureZeroMemory(pin.data(),pin.size());
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                }
                const auto now=std::chrono::steady_clock::now();
                if(now>=nextController) {
                    const auto r=source.submitController(controller);
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                    nextController=now+std::chrono::milliseconds(4);
                }
                pipeline::FramePacket packet; const AVFrame* frame=nullptr;
                const auto result=source.read(packet,&frame);
                const auto snapshot=source.sessionSnapshot();
                {std::lock_guard lock(mutex_);snapshot_=snapshot;}
                if(result==SourceReadStatus::Error) {std::lock_guard lock(mutex_);failed_=true;break;}
                if(result==SourceReadStatus::Frame && frame) {
                    publishDecoded(frame,packet,source.info());
                    lastFrame=now;
                } else {
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
    if(feeder.joinable()){feeder.request_stop();feeder.join();}
    audio_.stop();
    source.close();
}
void RemotePlaySessionSource::publishDecoded(const AVFrame* frame,pipeline::FramePacket packet,const SourceInfo& info){
    auto* cloned=av_frame_clone(frame);if(!cloned)throw std::bad_alloc();
    Frame item{std::shared_ptr<AVFrame>(cloned,[](AVFrame* p){av_frame_free(&p);}),packet,info};
    std::lock_guard lock(mutex_);
    if(latest_){++skipped_;item.packet.flags|=latest_->packet.flags;
        item.packet.flags|=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);}
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
void RemotePlaySessionSource::controller(remoteplay::ControllerState state){std::lock_guard lock(mutex_);controller_=state;controllerStamp_=remoteplay::monotonic100ns();}
void RemotePlaySessionSource::loginPin(std::string pin){std::lock_guard lock(mutex_);SecureZeroMemory(pin_.data(),pin_.size());pin_=std::move(pin);}
remoteplay::SessionInbox::Snapshot RemotePlaySessionSource::sessionSnapshot()const{std::lock_guard lock(mutex_);return snapshot_;}
uint64_t RemotePlaySessionSource::skipped()const{std::lock_guard lock(mutex_);return skipped_;}
}
