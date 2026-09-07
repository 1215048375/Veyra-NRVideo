#pragma once
#include <array>
#include <optional>
#include <string>
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/pipeline/ResolutionPlan.h"
namespace veyra::diagnostics {
struct DiagnosticEvent {
    std::string timestamp,severity;
    std::string fingerprint,component,stage,message,runtimeHash,flowApplied,fallbackReason;
    std::optional<uint64_t> hresult,ngx,nvof,seh;
    pipeline::ResolutionPlan resolution;
    pipeline::FrameIdentity identity;
    uint64_t batch=0,occurrenceCount=1;
    uint32_t subframe=0;
};
class DiagnosticHistory {
public:
    static constexpr size_t capacity=64;
    void add(DiagnosticEvent event) {
        for(size_t i=0;i<size_;++i)if(events_[i].fingerprint==event.fingerprint){event.occurrenceCount=events_[i].occurrenceCount+1;events_[i]=std::move(event);return;}
        events_[next_]=std::move(event);next_=(next_+1)%capacity;size_=std::min(size_+1,capacity);
    }
    size_t size() const{return size_;}
    const auto& events() const{return events_;}
private:
    std::array<DiagnosticEvent,capacity> events_{};
    size_t size_=0,next_=0;
};
}
