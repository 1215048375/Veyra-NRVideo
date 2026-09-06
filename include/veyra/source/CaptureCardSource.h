#pragma once
#include "veyra/source/IFrameSource.h"
#include <memory>
#include <vector>
namespace veyra::source {
struct CaptureFormat {int index=0;unsigned width=0,height=0;double fps=0;std::wstring label;};
class CaptureCardSource final:public IFrameSource {
public:
    CaptureCardSource();~CaptureCardSource()override;
    static std::vector<std::wstring> devices(bool audio=false);
    static std::vector<CaptureFormat> formats(unsigned device);
    bool open(const SourceOpenDesc&)override;
    const SourceInfo& info()const override;
    SourceReadStatus read(pipeline::FramePacket&,const AVFrame**)override;
    bool seek(const pipeline::Rational&)override{return false;}
    void close()noexcept override;
private:struct Impl;std::unique_ptr<Impl> p_;
};
}
