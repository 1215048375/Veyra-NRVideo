#pragma once

// Guidance frame  the unified motion/depth/confidence contract that
// NR (Feature 18), SR, and FG (DLSSG) all consume. Direction: CurrentPrevious.
#include <cstdint>
#include <d3d12.h>

namespace veyra::core {

enum class GuidanceProvider : uint8_t {
    Zero = 0,      // honest fallback: all-zero motion/depth
    Nvof = 1,      // NVIDIA Optical Flow (non-zero motion + confidence)
    Dav2 = 2,      // Depth Anything V2 via DirectML (non-zero depth)
    NvofDav2 = 3,  // NVOF motion + DAV2 depth
};

inline const char* guidanceProviderName(GuidanceProvider p) {
    switch (p) {
    case GuidanceProvider::Zero: return "Zero";
    case GuidanceProvider::Nvof: return "NVOF";
    case GuidanceProvider::Dav2: return "DAV2";
    case GuidanceProvider::NvofDav2: return "NVOF+DAV2";
    default: return "Unknown";
    }
}

struct GuidanceFrame {
    uint64_t frameId = 0;
    uint32_t width = 0;             // guidance extent (matches NR input extent)
    uint32_t height = 0;
    GuidanceProvider provider = GuidanceProvider::Zero;

    // Motion vectors: CurrentPrevious, source-pixel units, R16G16_FLOAT.
    ID3D12Resource* motion = nullptr;
    // Depth: R32_FLOAT, positive = near (inverted convention for NR).
    ID3D12Resource* depth = nullptr;
    // Confidence: R8_UNORM, 1.0 = high confidence, 0.0 = low.
    ID3D12Resource* confidence = nullptr;

    bool motionIsZero = true;       // true when provider==Zero
    bool depthIsZero = true;
    bool requiresHistoryReset = false;
    uint64_t sourceEpoch = 0;

    bool valid() const {
        return frameId > 0 && motion != nullptr && depth != nullptr &&
            width > 0 && height > 0;
    }
};

} // namespace veyra::core
