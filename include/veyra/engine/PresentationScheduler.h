#pragma once
#include <cstdint>
namespace veyra::engine {
// Maps a continuous source epoch to one monotonic host timeline. Processing
// completion must never re-anchor individual pairs. Units are 100 ns.
class PresentationScheduler {
public:
    void reset(uint64_t epoch,int64_t source,int64_t host,int64_t lookahead) {
        epoch_=epoch;source_=source;host_=host;delay_=lookahead;anchored_=true;
    }
    bool anchored(uint64_t epoch)const{return anchored_&&epoch==epoch_;}
    int64_t deadline(int64_t pts)const{return host_+(pts-source_)+delay_;}
    bool expired(int64_t pts,int64_t now,int64_t tolerance)const{return now>deadline(pts)+tolerance;}
private:
    uint64_t epoch_=0;int64_t source_=0,host_=0,delay_=0;bool anchored_=false;
};
}
