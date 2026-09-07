#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace veyra::pipeline {
struct Extent {
    uint32_t width=0,height=0;
    bool operator==(const Extent&) const = default;
    bool valid() const {return width>=2&&height>=2&&width<=3840&&height<=2160&&!(width%2)&&!(height%2);}
};
enum class NrSizePolicy { Realtime, Native };
struct ResolutionPlan {
    Extent source,base,nr,flow,fg,output;
    bool srApplied=false;
    uint64_t settingsRevision=0;
    static ResolutionPlan make(Extent source,bool sr,NrSizePolicy policy,bool exporting,uint64_t revision=0) {
        if(!source.valid())throw std::invalid_argument("invalid SDR source extent");
        ResolutionPlan p; p.source=source;p.base=sr?Extent{3840,2160}:source;
        p.srApplied=sr&&p.base!=source;p.nr=p.base;
        if(!exporting&&policy==NrSizePolicy::Realtime) {
            const double scale=std::min({1.0,1920.0/p.base.width,1080.0/p.base.height});
            p.nr={std::max(2u,uint32_t(p.base.width*scale)&~1u),std::max(2u,uint32_t(p.base.height*scale)&~1u)};
        }
        p.flow=source;p.fg=p.output=p.base;p.settingsRevision=revision;return p;
    }
};
}
