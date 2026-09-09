#pragma once
#include <windows.h>
#include <cstdint>
namespace veyra::ngx {
// Control messages only; pixel buffers and completion dependencies stay on GPU.
struct FrucWorkerMessage {
    uint32_t version=1,width=0,height=0,multiplier=2;
    LUID adapter{};
    uint64_t textures[5]{},fence=0,request=0,response=0;
    uint64_t inputFence=0,outputFence=0;
    double previousMs=0,currentMs=0;
    uint32_t parity=0,command=0,result=1,seh=0;
    bool repeated[3]{};
    wchar_t logPath[1024]{};
};
}
