#pragma once
#include "veyra/source/IFrameSource.h"
#include <memory>
#include <vector>
namespace veyra::source {
struct CaptureFormat {int index=0;unsigned width=0,height=0;double fps=0;std::wstring label;};
struct CaptureMetrics {
    uint64_t received=0, delivered=0, dropped=0;
    double callbackFps=0, readAgeMs=0, frameAgeMs=0;
};
class CaptureCardSource final:public IFrameSource {
public:
    CaptureCardSource();~CaptureCardSource()override;
    static std::vector<std::wstring> devices(bool audio=false);
    static std::vector<CaptureFormat> formats(unsigned device);
    bool open(const SourceOpenDesc&)override;
    // Negotiate/allocate before GPU initialization, but do not queue frames
    // or start audio until the presenter and enhancement graph are ready.
    bool configure(const SourceOpenDesc&);
    bool start();
    CaptureMetrics metrics()const;
    const SourceInfo& info()const override;
    SourceReadStatus read(pipeline::FramePacket&,const AVFrame**)override;
    bool seek(const pipeline::Rational&)override{return false;}
    void close()noexcept override;
private:struct Impl;std::unique_ptr<Impl> p_;
};
}
