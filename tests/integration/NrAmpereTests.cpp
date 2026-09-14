#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include <iostream>

// Exercise the actual scoped IAT installation and teardown on the local GPU.
// Passing on Blackwell proves routing/lifetime, not execution on Ampere.
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    veyra::Status status{};
    veyra::gfx::D3D12DeviceContext device;
    veyra::gfx::DeviceContextDesc desc;desc.requiredVendorId=0x10DE;
    if(!device.initialize(desc,status))return 1;
    for(int cycle=0;cycle<3;++cycle){
        veyra::ngx::DlssNrRuntimeAdapter nr;
        if(!nr.load(argv[1],status)||!nr.installCallerCompatibility(status)||
           !nr.installAmpereCompatibility(device.device(),status)||!nr.verifyAmpereCompatibilityForTest())return 1;
        if(nr.installAmpereCompatibility(device.device(),status))return 1;
        nr.unload();nr.unload();
    }
    std::cout<<"PASS scoped NVAPI routing, real selected adapter query, unchanged system entry and three reinstall cycles; RTX30 Evaluate NOT TESTED\n";
    return 0;
}
