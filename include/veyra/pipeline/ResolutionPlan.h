#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace veyra::pipeline {
struct Extent {
    uint32_t width=0,height=0;
    bool operator==(const Extent&) const = default;
    // D3D12 texture extent, not a product aspect-ratio or video-format policy.
    bool valid() const {return width>=1&&height>=1&&width<=16384&&height<=16384;}
};
enum class NrSizePolicy { Realtime, Native };
struct ResolutionPlan {
    Extent source,base,nr,flow,fg,output;
    bool srApplied=false;
    uint64_t settingsRevision=0;
    static ResolutionPlan make(Extent source,bool sr,NrSizePolicy policy,bool exporting,uint64_t revision=0) {
        if(!source.valid())throw std::invalid_argument("invalid SDR source extent");
        ResolutionPlan p; p.source=source;p.base=source;
        if(sr){const double scale=std::min(3840.0/source.width,2160.0/source.height);if(scale>1.0)p.base={std::max(2u,uint32_t(source.width*scale+1e-6)&~1u),std::max(2u,uint32_t(source.height*scale+1e-6)&~1u)};}
        p.srApplied=sr&&p.base!=source;p.nr=p.base;
        if(!exporting&&policy==NrSizePolicy::Realtime) {
            const double scale=std::min({1.0,1920.0/p.base.width,1080.0/p.base.height});
            if(scale<1.0)p.nr={std::max(1u,std::min(p.base.width,uint32_t(p.base.width*scale)&~1u)),std::max(1u,std::min(p.base.height,uint32_t(p.base.height*scale)&~1u))};
        }
        p.flow=source;
        // NVOF reads source-space color. In the explicitly labelled realtime
        // path it must not silently remain at native 4K after NR was reduced.
        // Inputs smaller than the realtime NR extent stay at their native size.
        if(!exporting&&policy==NrSizePolicy::Realtime&&
           (p.nr.width<p.source.width||p.nr.height<p.source.height))p.flow=p.nr;
        p.fg=p.output=p.base;p.settingsRevision=revision;return p;
    }
};
}
