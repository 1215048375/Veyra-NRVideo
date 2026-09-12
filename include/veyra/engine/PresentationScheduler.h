#pragma once
#include <cstdint>
namespace veyra::engine {
// Maps a continuous source epoch to one monotonic host timeline. Processing
// completion must never re-anchor individual pairs. PS5 may explicitly reset
// per newly decoded input pair (before enhancement) because its PTS is locally
// estimated and decoder delivery jitter is not playback lateness. Units: 100 ns.
class PresentationScheduler {
public:
    void reset(uint64_t epoch,int64_t source,int64_t host,int64_t lookahead,bool paceSourcePts=true) {
        epoch_=epoch;source_=source;host_=host;delay_=lookahead;anchored_=true;paced_=paceSourcePts;
    }
    bool anchored(uint64_t epoch)const{return anchored_&&epoch==epoch_;}
    // Unbuffered capture is ready-driven. A late first callback or clock drift
    // must not turn its source timestamps into a persistent presentation hold.
    int64_t deadline(int64_t pts)const{return paced_?host_+(pts-source_)+delay_:host_;}
    bool expired(int64_t pts,int64_t now,int64_t tolerance)const{return now>deadline(pts)+tolerance;}
private:
    uint64_t epoch_=0;int64_t source_=0,host_=0,delay_=0;bool anchored_=false,paced_=true;
};
}
