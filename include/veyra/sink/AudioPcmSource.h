#pragma once
#include <cstddef>
#include <optional>
#include "veyra/sink/AudioFormat.h"
namespace veyra::sink {
class AudioPcmSource {
public:
    virtual ~AudioPcmSource() = default;
    virtual AudioFormat pcmFormat()const{return {};}
    virtual size_t pull(float* interleaved, size_t frames, double* firstPtsMs) = 0;
    virtual std::optional<double> lastPullEndPtsMs()const{return {};}
    virtual bool padUnderruns()const{return true;}
};
}
