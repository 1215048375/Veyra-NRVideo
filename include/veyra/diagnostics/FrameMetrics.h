#pragma once
#include <array>
#include <optional>
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/pipeline/ResolutionPlan.h"
namespace veyra::diagnostics {
enum class GpuStage { Color, Sr, Flow, Nr, Residual, Fg1, Fg2, Fg3, FgBatch, Blit, Count };
enum class SampleState { NotExecuted, Pending, Measured, Unavailable };
struct GpuSample {SampleState state=SampleState::NotExecuted;std::optional<double> milliseconds;uint64_t begin=0,end=0,frequency=0;};
struct FrameMetrics {
    pipeline::FrameIdentity identity;
    pipeline::ResolutionPlan resolution;
    std::array<GpuSample,size_t(GpuStage::Count)> gpu{};
    std::optional<double> decodeCpuMs,submitCpuMs,gpuWaitCpuMs,deadlineWaitCpuMs,presentCpuMs,displayFps;
    uint64_t sourceFrames=0,validGenerated=0,submitted=0,expired=0;
    uint32_t queueWatermark=0;
};
}
