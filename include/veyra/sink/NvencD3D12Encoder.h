#pragma once
#include <d3d12.h>
#include <functional>
#include <memory>
#include <vector>
namespace veyra::gfx {class D3D12DeviceContext;class CommandSlotRing;}
namespace veyra::pipeline {class EnhanceGraph;}
namespace veyra::sink {
class NvencD3D12Encoder {
public:
    using PacketWriter=std::function<bool(const uint8_t*,size_t,int64_t,bool)>;
    NvencD3D12Encoder();~NvencD3D12Encoder();
    bool open(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,bool hevc,unsigned fpsNum,unsigned fpsDen,PacketWriter);
    bool encode(unsigned frameSlot,bool generated,int64_t pts);
    bool finish();
    void close();
    std::vector<uint8_t> headers() const;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
