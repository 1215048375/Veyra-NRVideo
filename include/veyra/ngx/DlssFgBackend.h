#pragma once

// DLSS Frame Generation (DLSSG 2X) backend using the official NGX SDK 310.7
// DLSSG helpers (Playbook section 14). V1 requests 2X only: one generated
// frame per real-frame pair (MultiFrameCount = MultiFrameIndex = 1).
//
// Capability contract (Playbook 14.2): before Create, the caller must have
// queried FrameGeneration.Available and logged GPU/driver/HAGS/runtime
// identity; queryCapability() performs that through the device capability
// parameters of the already-initialized NGX core.
//
// Resource contract: backbuffer/depth/mvecs/output are always provided.
// HUDLess/UI/UIAlpha/BidirectionalDistortionField are never provided and are
// declared through ResourceNeverProvided flags at Create (no dangling
// textures are ever passed).

#include <d3d12.h>
#include <nvsdk_ngx.h>

#include <cstdint>

#include "veyra/Result.h"

namespace veyra::ngx {

class NgxCoreHost;

class DlssFgBackend {
public:
    DlssFgBackend() = default;
    ~DlssFgBackend();

    DlssFgBackend(const DlssFgBackend&) = delete;
    DlssFgBackend& operator=(const DlssFgBackend&) = delete;

    struct Capability {
        bool available = false;
        uint64_t availableGetResult = 0;
        int64_t featureInitResult = 0;      // FrameGeneration.FeatureInitResult
        bool needsUpdatedDriver = false;
        int minDriverVersionMajor = 0;
        int minDriverVersionMinor = 0;
        uint32_t multiFrameCountMax = 0;   // >=1 when multiframe supported (2X needs >=1)
        uint64_t commonGetResults = 0;     // bitmask of extra parameter Get() codes
        int hagsRegistryMode = -1;         // HwSchMode: 2=on, 1=off, 0/absent=default(unlogged)
    };

    struct CreateDesc {
        uint32_t width;              // backbuffer extent (post-SR working extent)
        uint32_t height;
        uint32_t renderWidth;        // internal render extent (V1: == width/height)
        uint32_t renderHeight;
        uint32_t backbufferFormat;   // DXGI_FORMAT of backbuffer (e.g. R8G8B8A8_UNORM)
        bool dynamicResolution;
    };

    struct EvalDesc {
        ID3D12Resource* backbuffer;              // current real frame (post-NR, pre-UI)
        ID3D12Resource* depth;                   // constant/ZeroDepth for video content
        ID3D12Resource* mvecs;                   // working-extent motion vectors
        ID3D12Resource* outputInterpolated;      // generated frame (same format as backbuffer)
        ID3D12Resource* outputDisableInterpolation = nullptr; // optional 4-byte UAV buffer
        bool reset;
        uint64_t frameId;                        // monotonically increasing real-frame id
        float mvecScaleX;                        // convention under test (Playbook 14.2)
        float mvecScaleY;
    };

    // Queries FrameGeneration capability through NVSDK_NGX_GetCapabilityParameters.
    // The NGX core must be initialized. Every Get() result code is logged.
    bool queryCapability(NgxCoreHost& coreHost, Capability& caps, Status& status);

    // Creates the DLSSG feature through NGX_D3D12_CREATE_DLSSG. The core host
    // must be initialized before calling this.
    bool create(NgxCoreHost& coreHost,
                ID3D12GraphicsCommandList* cmdList,
                NVSDK_NGX_Parameter* params,
                const CreateDesc& desc,
                Status& status);

    void release();

    // Evaluates DLSSG for one real frame on the given command list. With 2X,
    // the runtime writes the frame interpolated between the previous and the
    // current real frame into desc.outputInterpolated.
    bool evaluate(ID3D12GraphicsCommandList* cmdList,
                  NVSDK_NGX_Parameter* params,
                  const EvalDesc& desc,
                  Status& status);

    bool created() const { return handle_ != nullptr; }
    uint64_t createResult() const { return createResult_; }
    uint64_t evaluateCount() const { return evaluateCount_; }
    uint64_t resetCount() const { return resetCount_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    NVSDK_NGX_Handle* handle_ = nullptr;
    uint64_t createResult_ = 0;
    uint64_t evaluateCount_ = 0;
    uint64_t resetCount_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace veyra::ngx
