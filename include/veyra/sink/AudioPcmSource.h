#pragma once
#include <cstddef>
namespace veyra::sink {
class AudioPcmSource {
public:
    virtual ~AudioPcmSource() = default;
    virtual size_t pull(float* stereo, size_t frames, double* firstPtsMs) = 0;
};
}
