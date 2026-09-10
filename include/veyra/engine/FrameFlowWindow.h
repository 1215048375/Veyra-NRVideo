#pragma once
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/engine/FrameRateWindow.h"
#include <mutex>

namespace veyra::engine {
// Each asynchronous job retains its own window. Replacing the current window
// never lets an old job increment the new revision/epoch's counters.
class FrameFlowWindow {
    mutable std::mutex mutex_;
    diagnostics::FrameFlowMetrics metrics_;
    FrameRateWindow generated_,presented_;
public:
    FrameFlowWindow(uint64_t session,pipeline::FrameIdentity id,int64_t now){
        metrics_.latest.sessionId=session;metrics_.latest.frame=id;
        generated_.reset(now);presented_.reset(now);
    }
    template<class F> void update(F action){std::lock_guard lock(mutex_);action(metrics_);}
    void ready(unsigned valid,unsigned invalid,int64_t now){
        std::lock_guard lock(mutex_);metrics_.counters.fgReadyValid+=valid;metrics_.counters.fgInvalid+=invalid;
        for(unsigned i=0;i<valid;++i)generated_.complete(now);
    }
    void presented(bool generated,uint64_t fence,int64_t now){
        std::lock_guard lock(mutex_);
        if(generated)++metrics_.counters.generatedPresented;else ++metrics_.counters.realPresented;
        metrics_.latest.consumerFence=fence;presented_.complete(now);
    }
    diagnostics::FrameFlowMetrics snapshot(int64_t now)const{
        std::lock_guard lock(mutex_);auto m=metrics_;m.validGeneratedFps=generated_.rate(now);m.presentSubmitFps=presented_.rate(now);return m;
    }
};
}
