#pragma once
#include <cstdint>
namespace veyra::ngx {
// The experimental profile only changes a successful query for the exact
// selected Ampere adapter. Other GPUs and driver failures retain their result.
constexpr bool rewriteNrAmpereArchitecture(uint32_t& architecture, bool selected, int result) {
    if(result!=0||!selected||(architecture&0xFFFFFFF0u)!=0x170u)return false;
    architecture=0x1B0u;
    return true;
}
}
