#pragma once
#include <windows.h>
#include <mmreg.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
namespace veyra::sink {
struct CaptureAudioState {
    bool available=false,running=false,clockEstimated=true,limited=false;
    double compensationMs=0,bufferedMs=0;
    std::optional<double> skewMs;
    uint64_t resets=0,overflows=0,underruns=0;
    std::wstring error;
};
class CaptureAudioSession {
public:
    CaptureAudioSession();
    ~CaptureAudioSession();
    bool configure(const WAVEFORMATEX&);
    bool start();
    void stop();
    bool push(const void* data,size_t bytes,double ptsMs,bool discontinuity);
    void videoPresented(double ptsMs,int64_t host100ns);
    void setGain(float);
    // 0 automatic, 1 manual, 2 off. Positive offset delays sound.
    void setSync(unsigned mode,int offsetMs);
    CaptureAudioState snapshot()const;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
