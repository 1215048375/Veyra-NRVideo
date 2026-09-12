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
enum class SrTarget : uint32_t { Qhd, Uhd4K, Uhd8K };
constexpr bool validSrTarget(SrTarget target) { return target<=SrTarget::Uhd8K; }
constexpr Extent srTargetExtent(SrTarget target) {
    switch(target) {
    case SrTarget::Qhd:return {2560,1440};
    case SrTarget::Uhd4K:return {3840,2160};
    case SrTarget::Uhd8K:return {7680,4320};
    }
    return {};
}
struct ResolutionPlan {
    Extent source,base,nr,flow,fg,output;
    bool srApplied=false;
    uint64_t settingsRevision=0;
    static ResolutionPlan make(Extent source,bool sr,NrSizePolicy policy,bool exporting,uint64_t revision=0,SrTarget target=SrTarget::Uhd4K,bool nrBeforeSr=false) {
        if(!source.valid())throw std::invalid_argument("invalid SDR source extent");
        if(!validSrTarget(target))throw std::invalid_argument("invalid SR target");
        ResolutionPlan p; p.source=source;p.base=source;
        if(sr){const auto limit=srTargetExtent(target);const double scale=std::min(double(limit.width)/source.width,double(limit.height)/source.height);if(scale>1.0)p.base={std::max(2u,uint32_t(source.width*scale+1e-6)&~1u),std::max(2u,uint32_t(source.height*scale+1e-6)&~1u)};}
        p.srApplied=sr&&p.base!=source;p.nr=p.base;
        if(nrBeforeSr&&!exporting)p.nr=source;
        if(!exporting&&policy==NrSizePolicy::Realtime) {
            const double scale=std::min({1.0,1920.0/p.nr.width,1080.0/p.nr.height});
            if(scale<1.0)p.nr={std::max(1u,uint32_t(p.nr.width*scale)&~1u),std::max(1u,uint32_t(p.nr.height*scale)&~1u)};
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
