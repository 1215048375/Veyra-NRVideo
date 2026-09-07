#include "veyra/engine/LivePresentationTiming.h"
#include <cmath>
#include <limits>
#include <cstdio>
int main(){
    using veyra::engine::livePairHoldMs;
    int failed=0;
    auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failed;};
    check(livePairHoldMs(false,1822.09,22.64)==0,"no-FG never waits on stale source PTS");
    check(livePairHoldMs(false,1e12,0)==0,"no-FG absolute PTS cannot add latency");
    check(livePairHoldMs(true,100,90)==10,"50fps FG pair half interval");
    check(std::abs(livePairHoldMs(true,100,100-1000./120)-1000./120)<1e-8,"60fps FG pair half interval");
    check(livePairHoldMs(true,1e12,0)<=1000./30,"timestamp jump has bounded hold");
    check(livePairHoldMs(true,10,20)==0,"backwards PTS cannot sleep negatively");
    check(livePairHoldMs(true,std::numeric_limits<double>::infinity(),0)==0,"nonfinite PTS rejected");
    return failed?1:0;
}
