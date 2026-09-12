// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
namespace veyra::remoteplay {
enum class SendStatus {Accepted,NeedDrain,End,Error};
enum class ReceiveStatus {Frame,NeedInput,End,Error};
enum class PumpStatus {Idle,Yield,End,Error,ProtocolViolation};
struct PumpStats {std::uint64_t accepted=0,frames=0,eagain=0;};
// Codec requirements: send(const Packet&) -> SendStatus, receive() -> ReceiveStatus.
// receive() must transfer/clone each actual decoded frame before the next call.
// EAGAIN NEVER consumes pending_. A small work budget bounds each invocation.
template<class Packet> class PacketPump {
public:
    bool submit(Packet packet) {
        if(pending_||ended_||failed_) return false;
        pending_.emplace(std::move(packet));
        return true;
    }
    template<class Codec> PumpStatus step(Codec& codec,std::size_t budget=64) {
        if(failed_) return PumpStatus::Error;
        if(ended_) return PumpStatus::End;
        if(!budget)return PumpStatus::Yield;
        for(std::size_t i=0;i<budget;++i){
            if(draining_){
                switch(codec.receive()){
                case ReceiveStatus::Frame:++stats_.frames;drainProgress_=true;break;
                case ReceiveStatus::NeedInput:
                    draining_=false;
                    if(retry_&&!drainProgress_){failed_=true;return PumpStatus::ProtocolViolation;}
                    retry_=false;drainProgress_=false;
                    if(!pending_)return PumpStatus::Idle;
                    break;
                case ReceiveStatus::End:ended_=true;pending_.reset();return PumpStatus::End;
                case ReceiveStatus::Error:failed_=true;return PumpStatus::Error;
                }
            }else{
                if(!pending_)return PumpStatus::Idle;
                switch(codec.send(*pending_)){
                case SendStatus::Accepted:++stats_.accepted;pending_.reset();draining_=true;drainProgress_=false;retry_=false;break;
                case SendStatus::NeedDrain:++stats_.eagain;draining_=true;drainProgress_=false;retry_=true;break;
                case SendStatus::End:ended_=true;pending_.reset();return PumpStatus::End;
                case SendStatus::Error:failed_=true;return PumpStatus::Error;
                }
            }
        }
        return PumpStatus::Yield;
    }
    bool hasPending() const noexcept {return pending_.has_value();}
    bool drained() const noexcept {return !pending_&&!draining_;}
    const PumpStats& stats() const noexcept {return stats_;}
    void reset(){pending_.reset();draining_=retry_=drainProgress_=ended_=failed_=false;stats_={};}
private:
    std::optional<Packet> pending_;bool draining_=false,retry_=false,drainProgress_=false,ended_=false,failed_=false;PumpStats stats_{};
};
} // namespace veyra::remoteplay
