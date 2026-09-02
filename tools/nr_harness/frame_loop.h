#pragma once

// Phase 1 frame loop: Proxy (test pattern) -> Feature 18 Evaluate -> Raw
// output with statistics and captures (Playbook sections 8.6/8.7).
#include <cstdint>
#include <string>

namespace veyra::harness {

struct FrameLoopArgs {
    std::wstring runtimeDir;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t frames = 300;
    std::wstring runId;
    std::wstring jsonFile;
    uint32_t captureFrame = 0;
    std::wstring captureDir;
    int styleOverride = 0;
    float intensityOverride = 1.0f;
};

int runFrameLoop(const FrameLoopArgs& args);

} // namespace veyra::harness
