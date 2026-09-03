#include "veyra/ngx/DlssSrBackend.h"

#include <windows.h>

// NGX SDK helper headers trigger /W4 warnings; suppress them for this TU.
#pragma warning(push, 0)
#include <nvsdk_ngx_helpers.h>
#pragma warning(pop)

#include <format>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/ngx/NgxCoreHost.h"

namespace veyra::ngx {

namespace {

__declspec(noinline) NVSDK_NGX_Result CallCreateDlss(
    ID3D12GraphicsCommandList* cmdList,
    unsigned int creationNodeMask,
    unsigned int visibilityNodeMask,
    NVSDK_NGX_Handle** handle,
    NVSDK_NGX_Parameter* params,
    NVSDK_NGX_DLSS_Create_Params* createParams,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NGX_D3D12_CREATE_DLSS_EXT(cmdList, creationNodeMask,
            visibilityNodeMask, handle, params, createParams);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallEvaluateDlss(
    ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Handle* handle,
    NVSDK_NGX_Parameter* params,
    NVSDK_NGX_D3D12_DLSS_Eval_Params* evalParams,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NGX_D3D12_EVALUATE_DLSS_EXT(cmdList, handle, params, evalParams);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

} // namespace

DlssSrBackend::~DlssSrBackend()
{
    release();
}

bool DlssSrBackend::create(NgxCoreHost& coreHost,
                           ID3D12GraphicsCommandList* cmdList,
                           NVSDK_NGX_Parameter* params,
                           const CreateDesc& desc,
                           Status& status)
{
    (void)coreHost; // NGX core already initialized; SR creates through the core API
    if (handle_ != nullptr) {
        release();
    }
    inputWidth_ = desc.inputWidth;
    outputWidth_ = desc.outputWidth;

    // 1:1 bypass: never create SR for same-resolution (Playbook section 11).
    if (shouldBypass(desc.inputWidth, desc.outputWidth)) {
        log::info("ngx", std::format("sr-backend: bypass ({}x{} == {}x{})",
            desc.inputWidth, desc.inputHeight, desc.outputWidth, desc.outputHeight));
        return true;
    }

    NVSDK_NGX_DLSS_Create_Params createParams{};
    createParams.Feature.InWidth = desc.inputWidth;
    createParams.Feature.InHeight = desc.inputHeight;
    createParams.Feature.InTargetWidth = desc.outputWidth;
    createParams.Feature.InTargetHeight = desc.outputHeight;
    createParams.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(desc.perfQuality);
    createParams.InEnableOutputSubrects = desc.enableOutputSubrects ? 1 : 0;

    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallCreateDlss(cmdList, 1, 1, &handle_,
        params, &createParams, sehCode);
    createResult_ = static_cast<uint64_t>(result);

    log::info("ngx", std::format("sr-backend: Create DLSS {}x{} -> {}x{} result={} handle={} seh={}",
        desc.inputWidth, desc.inputHeight, desc.outputWidth, desc.outputHeight,
        ngxResultString(createResult_), handle_ != nullptr ? "non-null" : "null", sehCode));

    if (result != NVSDK_NGX_Result_Success || handle_ == nullptr) {
        status = Status::DeviceFailure;
        return false;
    }
    return true;
}

void DlssSrBackend::release()
{
    if (handle_ != nullptr) {
        // SR is a core NGX feature; release through the core API.
        const NVSDK_NGX_Result result = NVSDK_NGX_D3D12_ReleaseFeature(handle_);
        log::info("ngx", std::format("sr-backend: ReleaseFeature result={}", ngxResultString(static_cast<uint64_t>(result))));
        handle_ = nullptr;
    }
}

bool DlssSrBackend::evaluate(ID3D12GraphicsCommandList* cmdList,
                             NVSDK_NGX_Parameter* params,
                             const EvalDesc& desc,
                             Status& status)
{
    // 1:1 bypass: skip Evaluate entirely (Playbook section 11).
    if (shouldBypass(inputWidth_, outputWidth_)) {
        ++bypassCount_;
        return true;
    }

    if (handle_ == nullptr) {
        status = Status::InvalidArgument;
        return false;
    }

    NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams{};
    evalParams.Feature.pInColor = desc.color;
    evalParams.Feature.pInOutput = desc.output;
    evalParams.pInDepth = desc.depth;
    evalParams.pInMotionVectors = desc.motionVectors;
    evalParams.InJitterOffsetX = desc.jitterOffsetX;
    evalParams.InJitterOffsetY = desc.jitterOffsetY;
    evalParams.Feature.InSharpness = desc.sharpness;
    evalParams.InReset = desc.reset ? 1 : 0;
    evalParams.InMVScaleX = 1.0f;
    evalParams.InMVScaleY = 1.0f;
    evalParams.InPreExposure = 1.0f;
    evalParams.InRenderSubrectDimensions.Width = inputWidth_;
    evalParams.InRenderSubrectDimensions.Height = 0; // set from create height

    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallEvaluateDlss(cmdList, handle_, params, &evalParams, sehCode);
    log::info("ngx", std::format("sr-backend: Evaluate #{} result={} seh={}",
        evaluateCount_ + 1, ngxResultString(static_cast<uint64_t>(result)), sehCode));

    if (result != NVSDK_NGX_Result_Success) {
        status = Status::DeviceFailure;
        return false;
    }
    ++evaluateCount_;
    return true;
}

bool DlssSrBackend::isBypass() const
{
    return shouldBypass(inputWidth_, outputWidth_);
}

} // namespace veyra::ngx
