#pragma once
#include <array>
#include <optional>
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/pipeline/ResolutionPlan.h"
namespace veyra::diagnostics {
enum class GpuStage { Color, Sr, Flow, Nr, Residual, Fg1, Fg2, Fg3, FgBatch, Blit, Count };
enum class SampleState { NotExecuted, Pending, Measured, Unavailable };
enum class CpuStage { Decode, Submit, SlotWait, ReadyWait, DeadlineWait, Present, Count };
struct TimingAggregate {std::optional<double> mean,p95;uint64_t samples=0;};
struct GpuSample {SampleState state=SampleState::NotExecuted;std::optional<double> milliseconds;uint64_t begin=0,end=0,frequency=0;};
// Live presentation is asynchronous. Keep the identity of the window that
// owns these counters so a finished job from a prior reset cannot be reported
// as work performed by the current settings or temporal epoch.
struct FrameFlowIdentity {
    uint64_t sessionId=0;
    pipeline::FrameIdentity frame;
    uint64_t batchId=0,readyFence=0,consumerFence=0;
    bool sameWindow(uint64_t session,const pipeline::FrameIdentity& candidate)const{
        return sessionId==session&&frame.epoch==candidate.epoch&&frame.settingsRevision==candidate.settingsRevision;
    }
};
struct FrameFlowCounters {
    uint64_t captureReceived=0,mailboxOverwritten=0;
    uint64_t sourceAccepted=0,sourceSkippedBeforeGraph=0,realSubmitted=0;
    uint64_t fgCandidate=0,fgEvaluated=0,fgSkippedBeforeEval=0,fgReadyValid=0,fgInvalid=0,fgWarmup=0;
    uint64_t xessSdkGenerated=0,xessSdkPresented=0,realReady=0;
    uint64_t realPresented=0,generatedPresented=0,generatedExpiredAfterEval=0,cancelledBeforePresent=0;
    uint64_t historyResets=0,captureDropResets=0,settingsResets=0;
    uint32_t commandSlotsInFlight=0,commandSlotHighWater=0,presentationBatchHighWater=0;
};
struct FrameFlowMetrics {
    FrameFlowIdentity latest;
    FrameFlowCounters counters;
    std::optional<double> slotReuseWaitMs,captureArrivalToPresentReturnMs,gpuReadyWaitMs,deadlineWaitMs;
    uint64_t slotReuseWaitCount=0;
    double validGeneratedFps=0,presentSubmitFps=0,sourceCompletedFps=0,outputCompletedFps=0,xessSdkSubmitFps=0;
    bool rateWindowReady=false;
    std::optional<double> softwareLatencyMs,softwareLatencyP95Ms,slotWaitPerFrameMs;
    uint64_t latencySamples=0;
    std::array<TimingAggregate,size_t(GpuStage::Count)> gpuTiming;
    std::array<TimingAggregate,size_t(CpuStage::Count)> cpuTiming;
    uint64_t timingOverflow=0;
};
struct FrameMetrics {
    pipeline::FrameIdentity identity;
    pipeline::ResolutionPlan resolution;
    std::array<GpuSample,size_t(GpuStage::Count)> gpu{};
    std::optional<double> decodeCpuMs,submitCpuMs,gpuWaitCpuMs,deadlineWaitCpuMs,presentCpuMs,displayFps;
    uint64_t sourceFrames=0,validGenerated=0,submitted=0,expired=0;
    uint32_t queueWatermark=0;
    FrameFlowMetrics flow;
};
}
