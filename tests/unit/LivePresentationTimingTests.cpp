#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/source/CaptureTiming.h"
#include <cmath>
#include <limits>
#include <cstdio>
int main(){
    using veyra::engine::livePairHoldMs;
    int failed=0;
    auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failed;};
    using veyra::source::captureDuration;using veyra::engine::liveSourceInterval100ns;
    check(veyra::pipeline::FramePacket{}.duration.isUnknown(),"unset packet duration is unknown, not known zero");
    check(captureDuration(100,166767,true,333333).to100ns()==166667,"sample duration wins over nominal");
    check(captureDuration(100,100,false,333333).to100ns()==333333,"missing sample stop uses nominal30fps");
    check(captureDuration(100,90,true,0).isUnknown(),"invalid sample and absent nominal stay unknown");
    check(liveSourceInterval100ns({1,1000},60)==10000,"1000fps packet not clamped to120fps");
    check(liveSourceInterval100ns({0,1},1000)==10000,"1000fps nominal fallback accepted");
    check(liveSourceInterval100ns({0,1},60)==166667,"known zero cannot turn60fps into120fps");
    check(liveSourceInterval100ns(captureDuration(0,0,false,333333),60)==333333,"30fps packet retained by scheduler");
    check(liveSourceInterval100ns({INT64_MAX,1},60)==1000000,"huge duration clamps before integer conversion");
    check(livePairHoldMs(false,1822.09,22.64)==0,"no-FG never waits on stale source PTS");
    check(livePairHoldMs(false,1e12,0)==0,"no-FG absolute PTS cannot add latency");
    check(livePairHoldMs(true,100,90)==10,"50fps FG pair half interval");
    check(std::abs(livePairHoldMs(true,100,100-1000./120)-1000./120)<1e-8,"60fps FG pair half interval");
    check(livePairHoldMs(true,1e12,0)<=1000./30,"timestamp jump has bounded hold");
    check(livePairHoldMs(true,10,20)==0,"backwards PTS cannot sleep negatively");
    check(livePairHoldMs(true,std::numeric_limits<double>::infinity(),0)==0,"nonfinite PTS rejected");
    return failed?1:0;
}
