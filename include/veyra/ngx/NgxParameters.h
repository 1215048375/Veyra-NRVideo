#pragma once

#include <d3d12.h>
#include <nvsdk_ngx.h>

#include <cstdint>

namespace veyra::ngx {

// Strongly-typed setter wrapper over NVSDK_NGX_Parameter (Playbook 4.5).
// The C++ virtual Set overloads encode the exact wire type, which is how
// parameter-name-correct-but-type-wrong bugs are prevented.
class ParameterBlock {
public:
    explicit ParameterBlock(NVSDK_NGX_Parameter* parameters)
        : parameters_(parameters)
    {
    }

    NVSDK_NGX_Parameter* raw() const { return parameters_; }

    void setU32(const char* name, uint32_t value) { parameters_->Set(name, static_cast<unsigned int>(value)); }
    void setI32(const char* name, int32_t value) { parameters_->Set(name, static_cast<int>(value)); }
    void setF32(const char* name, float value) { parameters_->Set(name, value); }
    void setD3D12Resource(const char* name, ID3D12Resource* value) { parameters_->Set(name, value); }
    void setVoid(const char* name, void* value) { parameters_->Set(name, value); }

private:
    NVSDK_NGX_Parameter* parameters_ = nullptr;
};

} // namespace veyra::ngx
