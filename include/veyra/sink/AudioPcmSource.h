#pragma once
#include <cstddef>
#include <optional>
namespace veyra::sink {
class AudioPcmSource {
public:
    virtual ~AudioPcmSource() = default;
    virtual size_t pull(float* stereo, size_t frames, double* firstPtsMs) = 0;
    virtual std::optional<double> lastPullEndPtsMs()const{return {};}
    virtual bool padUnderruns()const{return true;}
};
}
