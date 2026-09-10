#pragma once
#include "veyra/pipeline/FrameBatch.h"
#include <optional>

namespace veyra::engine {
struct FrameArrival {
    pipeline::FrameIdentity identity;
    int64_t pts100ns=0,host100ns=0;
    bool captureCallback=false;
};
struct FrameLineage {FrameArrival a,b;};
class FrameLineageTracker {
    std::optional<FrameArrival> previous_;
public:
    std::optional<FrameLineage> observe(const pipeline::FrameBatch& batch,int64_t arrival,bool callback,bool cached){
        if(cached){previous_.reset();return {};}
        FrameArrival current{batch.identity,batch.b100ns,arrival,callback};
        std::optional<FrameLineage> pair;
        if(previous_&&previous_->identity.epoch==current.identity.epoch&&
           previous_->identity.settingsRevision==current.identity.settingsRevision&&
           previous_->identity.sourceFrameId!=current.identity.sourceFrameId&&
           previous_->pts100ns==batch.a100ns&&batch.b100ns>batch.a100ns&&
           previous_->host100ns>0&&arrival>=previous_->host100ns)
            pair=FrameLineage{*previous_,current};
        previous_=current;
        return pair;
    }
};
}
