// EnhanceGraph implementation - the real GPU chain migrated from
// tools/player_probe main.cpp (R3.2). Ordering constraints preserved from the
// injected-layer era evidence: committed resources and NGX/NVOF objects are
// created BEFORE descriptor views; FG warm-up evaluate precedes views; static
// views are created last. Per-frame execution uses the shared command slot
// ring (NR evaluates on a fresh list - snippet constraint).
#include "veyra/pipeline/EnhanceGraph.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/DlssFgBackend.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/DlssSrBackend.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/NvOfSession.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext_d3d12va.h>
#include <libswscale/swscale.h>
}

namespace veyra::pipeline {

namespace {

constexpr int64_t usPerSecond = 1000000;

} // namespace

EnhanceGraph::EnhanceGraph(gfx::D3D12DeviceContext& context, gfx::CommandSlotRing& ring)
    : context_(context)
    , ring_(ring)
{
}

EnhanceGraph::~EnhanceGraph()
{
    // shutdown() must have run (features/NVOF/NGX need ordered teardown while
    // the device is alive); ComPtr releases afterwards are then no-ops.
}

// ---------------------------------------------------------------------------
// initialize: exact ordering of the proven probe sequence.
// ---------------------------------------------------------------------------
bool EnhanceGraph::initialize(const EnhanceGraphDesc& desc)
{
    desc_ = desc;
    srcW_ = desc.sourceWidth;
    srcH_ = desc.sourceHeight;
    workW_ = desc.workWidth;
    workH_ = desc.workHeight;
    nvofW_ = workW_;
    nvofH_ = workH_;
    srEnabled_ = desc.enableSr;
    nrEnabled_ = desc.enableNr && !desc.noFeatures && !desc.noNgx;
    fgEnabled_ = desc.enableFg && !desc.noNgx;
    Status st = Status::Ok;

    lumaPitch_ = (static_cast<size_t>(srcW_) + 255) & ~size_t(255);
    chromaPitch_ = lumaPitch_;
    lumaSize_ = lumaPitch_ * srcH_;
    chromaSize_ = chromaPitch_ * (srcH_ / 2);
    dPitch_ = (static_cast<size_t>(workW_) * 4 + 255) & ~size_t(255);
    const size_t dSize = dPitch_ * workH_;
    nv12Buf_.resize(lumaSize_ + chromaSize_);

    if (!createResources()) return false;
    if (!initZeroAndDepthTextures()) return false;
    if (!initNvof()) return false;
    if (!initNgxFeatures()) return false;
    if (!createComputePasses()) return false;

    initialized_ = true;
    veyra::log::info("graph", std::format("initialized src={}x{} work={}x{} sr={} nr={} fg={} nvof={}",
        srcW_, srcH_, workW_, workH_, srEnabled_ ? 1 : 0, nrEnabled_ ? 1 : 0,
        fgEnabled_ ? 1 : 0, (nvof_ && nvof_->initialized()) ? 1 : 0));
    return true;
}

bool EnhanceGraph::createResources()
{
    upLuma_[0] = makeUploadBuffer(context_.device(), lumaSize_);
    upLuma_[1] = makeUploadBuffer(context_.device(), lumaSize_);
    upChroma_[0] = makeUploadBuffer(context_.device(), chromaSize_);
    upChroma_[1] = makeUploadBuffer(context_.device(), chromaSize_);
    upDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroMotion_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    lumaTex_ = makeTexture(context_.device(), srcW_, srcH_, DXGI_FORMAT_R8_UNORM, true);
    chromaTex_ = makeTexture(context_.device(), srcW_, srcH_ / 2, DXGI_FORMAT_R8G8_UNORM, true);
    srcRgba_ = makeTexture(context_.device(), srcW_, srcH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    workRgba_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    proxyTex_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    neuralTex_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    finalRgba_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    videoFrame_[0] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    videoFrame_[1] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    confTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R8_UNORM, true);
    flowTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R16G16_FLOAT, true);
    depthTex_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    genFrame_[0] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    genFrame_[1] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    nrZeroMotion_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R16G16_FLOAT, false);
    nrZeroDepth_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    rawW_ = (nvofW_ + nvofGrid_ - 1) / nvofGrid_;
    rawH_ = (nvofH_ + nvofGrid_ - 1) / nvofGrid_;
    nvofRawTex_ = makeTexture(context_.device(), rawW_, rawH_, DXGI_FORMAT_R16G16_SINT, false);
    nvofCostTex_ = makeTexture(context_.device(), rawW_, rawH_, DXGI_FORMAT_R8_UINT, false);
    nvofInA_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_B8G8R8A8_UNORM, true);
    nvofInB_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_B8G8R8A8_UNORM, true);

    if (!upLuma_[0] || !upLuma_[1] || !upChroma_[0] || !upChroma_[1] ||
        !upDepth_ || !upZeroDepth_ || !upZeroMotion_ ||
        !lumaTex_ || !chromaTex_ || !srcRgba_ || !workRgba_ || !proxyTex_ ||
        !neuralTex_ || !finalRgba_ || !videoFrame_[0] || !videoFrame_[1] ||
        !flowTex_ || !depthTex_ || !genFrame_[0] || !genFrame_[1] ||
        !nrZeroMotion_ || !nrZeroDepth_ || !nvofRawTex_ || !nvofCostTex_ ||
        !nvofInA_ || !nvofInB_ || !confTex_) {
        veyra::log::error("graph", "resource allocation failed");
        return false;
    }

    // Persistent mapping of the NV12 upload ring (before any views).
    bool uploadsMapped = true;
    for (int i = 0; i < 2; ++i) {
        if (FAILED(upLuma_[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedLuma_[i]))) ||
            FAILED(upChroma_[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedChroma_[i])))) {
            uploadsMapped = false;
        }
    }
    if (!uploadsMapped) {
        veyra::log::error("graph", "NV12 upload persistent map failed");
        return false;
    }
    veyra::log::info("graph", "NV12 upload ring persistently mapped (2 buffers)");
    return true;
}

bool EnhanceGraph::initZeroAndDepthTextures()
{
    // Depth constants uploaded once (copies are safe before views exist).
    uint8_t* d = nullptr; uint8_t* zd = nullptr; uint8_t* zm = nullptr;
    upDepth_->Map(0, nullptr, reinterpret_cast<void**>(&d));
    upZeroDepth_->Map(0, nullptr, reinterpret_cast<void**>(&zd));
    upZeroMotion_->Map(0, nullptr, reinterpret_cast<void**>(&zm));
    for (uint32_t y = 0; y < workH_; ++y) {
        float* dRow = reinterpret_cast<float*>(d + y * dPitch_);
        float* zdRow = reinterpret_cast<float*>(zd + y * dPitch_);
        uint16_t* zmRow = reinterpret_cast<uint16_t*>(zm + y * dPitch_);
        for (uint32_t x = 0; x < workW_; ++x) {
            dRow[x] = 0.9f;  // explicit constant far depth (video content)
            zdRow[x] = 0.5f; // NR zero-depth explicit fallback
            zmRow[x * 2] = 0; zmRow[x * 2 + 1] = 0;
        }
    }
    upDepth_->Unmap(0, nullptr);
    upZeroDepth_->Unmap(0, nullptr);
    upZeroMotion_->Unmap(0, nullptr);

    Status st = Status::Ok;
    ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
    if (list == nullptr) return false;
    auto uploadTex = [&](ID3D12Resource* tex, const ComPtr<ID3D12Resource>& up, DXGI_FORMAT fmt) {
        D3D12_RESOURCE_BARRIER b{};
        b.Transition.pResource = tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = up.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = fmt;
        src.PlacedFootprint.Footprint.Width = workW_;
        src.PlacedFootprint.Footprint.Height = workH_;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(dPitch_);
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER back{};
        back.Transition.pResource = tex;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &back);
    };
    uploadTex(depthTex_.Get(), upDepth_, DXGI_FORMAT_R32_FLOAT);
    uploadTex(nrZeroDepth_.Get(), upZeroDepth_, DXGI_FORMAT_R32_FLOAT);
    uploadTex(nrZeroMotion_.Get(), upZeroMotion_, DXGI_FORMAT_R16G16_FLOAT);
    list->Close();
    ID3D12CommandList* lists[] = { list };
    context_.directQueue()->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> initFence;
    context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initFence));
    context_.directQueue()->Signal(initFence.Get(), 1);
    if (nvofOutEvent_ == nullptr) {
        nvofOutEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    initFence->SetEventOnCompletion(1, nvofOutEvent_);
    WaitForSingleObject(nvofOutEvent_, 5000);
    veyra::log::info("graph", "depth/zero guidance textures initialized");
    return true;
}

bool EnhanceGraph::initNvof()
{
    if (desc_.noFeatures) {
        veyra::log::info("graph", "VEYRA_NO_FEATURES: NVOF session skipped");
        return true;
    }
    nvof_ = std::make_unique<ngx::NvOfSession>();
    if (nvofOutFence_ == nullptr) {
        if (FAILED(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&nvofOutFence_)))) {
            veyra::log::error("graph", "NVOF out fence creation failed");
            return false;
        }
    }
    if (nvofOutEvent_ == nullptr) {
        nvofOutEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ngx::NvOfSession::Desc nd{};
    nd.width = nvofW_; nd.height = nvofH_;
    nd.inFence = context_.fence();
    nd.outFence = nvofOutFence_.Get();
    nd.gridSize = 4;
    // s6 contract: B8G8R8A8 inputs, raw R16G16_SINT + R8_UINT cost at grid
    // extent, passed explicitly; allocation failure = fail closed.
    Status st = Status::Ok;
    if (!nvof_->initialize(context_.device(), nvofInA_.Get(), nvofInB_.Get(),
            nvofRawTex_.Get(), nvofCostTex_.Get(), nd, st)) {
        veyra::log::error("graph", "NVOF init failed");
        return false;
    }
    selectedGrid_ = nvof_->caps().selectedGrid;
    return true;
}

bool EnhanceGraph::initNgxFeatures()
{
    if (desc_.noNgx) {
        veyra::log::warn("graph", "VEYRA_NO_NGX: core+features skipped; NVOF only");
        return true;
    }
    // Read the local NGX identity (same loader contract as the probes).
    std::ifstream ids(std::wstring(desc_.runtimeAbsPath) + L"\\..\\config\\ngx-local.json",
        std::ios::binary);
    std::string idText((std::istreambuf_iterator<char>(ids)), std::istreambuf_iterator<char>());
    std::string projectId, engineVersion;
    {
        auto scan = [&idText](const char* key) {
            const std::string needle = std::string("\"") + key + "\"";
            size_t p = idText.find(needle);
            if (p == std::string::npos) return std::string();
            p = idText.find('"', idText.find(':', p + needle.size()));
            if (p == std::string::npos) return std::string();
            return idText.substr(p + 1, idText.find('"', p + 1) - p - 1);
        };
        projectId = scan("ngxProjectId");
        engineVersion = scan("engineVersion");
    }
    if (projectId.empty()) {
        veyra::log::error("graph", "ngx-local.json missing");
        return false;
    }

    coreHost_ = std::make_unique<ngx::NgxCoreHost>();
    Status st = Status::Ok;
    if (!coreHost_->initialize(context_.device(), desc_.runtimeAbsPath.c_str(),
            projectId.c_str(), engineVersion.c_str(), st)) {
        return false;
    }

    fgBackend_ = std::make_unique<ngx::DlssFgBackend>();
    ngx::DlssFgBackend::Capability fgCaps{};
    const bool fgAvailable = fgBackend_->queryCapability(*coreHost_, fgCaps, st);
    if (!fgAvailable) {
        veyra::log::error("graph", "FG unavailable; fail closed");
        return false;
    }
    fgCapsAvailable_ = fgCaps.available;
    fgMultiFrameMax_ = fgCaps.multiFrameCountMax;
    veyra::log::info("graph", std::format("FG capability available={} multiFrameMax={}",
        fgCaps.available, fgCaps.multiFrameCountMax));

    nrAdapter_ = std::make_unique<ngx::DlssNrRuntimeAdapter>();
    if (!nrAdapter_->load(desc_.runtimeAbsPath.c_str(), st) ||
        !nrAdapter_->installCallerCompatibility(st) ||
        !nrAdapter_->snippetInitExt(context_.device(), desc_.runtimeAbsPath.c_str(), nrResult_, nrSeh_) ||
        nrResult_ != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
        veyra::log::error("graph", std::format("NR snippet init failed 0x{:X}", nrResult_));
        return false;
    }
    ngxParams_ = coreHost_->allocateParameters(st);
    if (ngxParams_ == nullptr) return false;

    srBackend_ = std::make_unique<ngx::DlssSrBackend>();

    // NR feature create (8.5 create parameter contract).
    {
        namespace p = ngx::dlssnr;
        ngx::ParameterBlock pb(ngxParams_);
        pb.setU32(p::kWidth, workW_); pb.setU32(p::kHeight, workH_);
        pb.setU32(p::kInputWidth, workW_); pb.setU32(p::kInputHeight, workH_);
        pb.setU32(p::kOutputWidth, workW_); pb.setU32(p::kOutputHeight, workH_);
        pb.setU32(p::kOutputDotWidth, workW_); pb.setU32(p::kOutputDotHeight, workH_);
        pb.setU32(p::kUpscaling, 0);
        pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
        pb.setVoid(p::kComputeScalingRatioCallback,
            reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        pb.setI32(p::kHintRenderPreset, 0);
        pb.setU32(p::kStdWidth, workW_); pb.setU32(p::kStdHeight, workH_);
        pb.setI32(p::kPerfQualityValue, 1);
        pb.setU32(p::kCreationNodeMask, 1); pb.setU32(p::kVisibilityNodeMask, 1);
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) return false;
        if (!nrAdapter_->snippetCreateFeature(list, ngxParams_, &nrHandle_, nrResult_, nrSeh_) ||
            nrResult_ != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
            veyra::log::error("graph", std::format("NR create failed 0x{:X}", nrResult_));
            return false;
        }
        (void)ring_.submitAndSignal(0);
        (void)ring_.waitIdle();
    }

    if (srEnabled_ && !desc_.noFeatures) {
        ngx::DlssSrBackend::CreateDesc sd{};
        sd.inputWidth = srcW_; sd.inputHeight = srcH_;
        sd.outputWidth = workW_; sd.outputHeight = workH_;
        sd.perfQuality = 1; sd.enableOutputSubrects = false;
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) return false;
        if (!srBackend_->create(*coreHost_, list, ngxParams_, sd, st) || !srBackend_->created()) {
            veyra::log::error("graph", "SR create failed");
            return false;
        }
        (void)ring_.submitAndSignal(0);
        (void)ring_.waitIdle();
    }

    // FG create + warm-up evaluate BEFORE descriptor views (the runtime
    // allocates internals at the first evaluate and would fail after views
    // exist on this system).
    {
        ngx::DlssFgBackend::CreateDesc fd{};
        fd.width = workW_; fd.height = workH_;
        fd.renderWidth = workW_; fd.renderHeight = workH_;
        fd.backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) return false;
        if (!fgBackend_->create(*coreHost_, list, ngxParams_, fd, st) || !fgBackend_->created()) {
            veyra::log::error("graph", std::format("FG create failed 0x{:X}", fgBackend_->createResult()));
            return false;
        }
        (void)ring_.submitAndSignal(0);
        (void)ring_.waitIdle();

        ID3D12GraphicsCommandList* wlist = ring_.acquire(0, st);
        if (wlist == nullptr) return false;
        D3D12_RESOURCE_BARRIER b[4]{};
        for (int i = 0; i < 4; ++i) {
            b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        b[0].Transition.pResource = nvofInB_.Get();
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[1].Transition.pResource = nrZeroMotion_.Get();
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[2].Transition.pResource = depthTex_.Get();
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[3].Transition.pResource = genFrame_[0].Get();
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        wlist->ResourceBarrier(4, b);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.backbuffer = nvofInB_.Get();
        fe.depth = depthTex_.Get();
        fe.mvecs = nrZeroMotion_.Get();
        fe.outputInterpolated = genFrame_[0].Get();
        fe.reset = true;
        fe.frameId = 0;
        fe.mvecScaleX = 1.0f;
        fe.mvecScaleY = 1.0f;
        const bool warmOk = fgBackend_->evaluate(wlist, ngxParams_, fe, st);
        (void)warmOk;
        for (int i = 0; i < 4; ++i) b[i].Transition.StateBefore = b[i].Transition.StateAfter;
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        wlist->ResourceBarrier(4, b);
        if (!ring_.submitAndSignal(0) || !ring_.waitIdle()) return false;
        veyra::log::info("graph", std::format("FG warm-up evaluate done ok={} result=0x{:X}",
            warmOk ? 1 : 0, fgBackend_->createResult()));
    }
    return true;
}

bool EnhanceGraph::createComputePasses()
{
    std::vector<uint8_t> cs;
    if (!yuvPass_.loadShader("YuvToLinearRgb.dxil", cs) || !yuvPass_.create(context_.device(), cs, 8)) return false;
    if (!encPass_.loadShader("ParityEncode.dxil", cs) || !encPass_.create(context_.device(), cs, 8)) return false;
    if (!decPass_.loadShader("ParityDecode.dxil", cs) || !decPass_.create(context_.device(), cs, 8)) return false;
    if (!blitPass_.loadShader("ScaleBlit.dxil", cs) || !blitPass_.create(context_.device(), cs, 16)) return false;
    if (!uploadPass_.loadShader("Nv12Upload.dxil", cs) || !uploadPass_.create(context_.device(), cs, 4, 1, 2)) return false;
    if (!densifyPass_.loadShader("NvofDensify.dxil", cs) ||
        !densifyPass_.create(context_.device(), cs, 6, 2, 2)) return false;
    if (!stager_.initialize(context_.device(), 64)) {
        veyra::log::error("graph", "descriptor stager init failed");
        return false;
    }
    return true;
}

bool EnhanceGraph::createViews()
{
    // Original probe env-matrix semantics (default OFF): the proven runtime
    // configuration on this system runs the dispatches against
    // never-written descriptor slots (GBV id=938 - the known Phase 6 R6.1
    // blocker). Initializing the slots trips the injected-layer fabricated
    // device-removed on Present; solving that interaction is R6.1 scope.
    const bool viewsTex = GetEnvironmentVariableW(L"VEYRA_VIEWS_TEX", nullptr, 0) != 0;
    const bool viewsRaw = GetEnvironmentVariableW(L"VEYRA_VIEWS_RAW", nullptr, 0) != 0;
    const bool viewsUav = GetEnvironmentVariableW(L"VEYRA_VIEWS_UAV", nullptr, 0) != 0;
    if (!viewsTex && !viewsRaw && !viewsUav) {
        veyra::log::info("graph", "static views skipped (default; VEYRA_VIEWS_* opt-in)");
        return true;
    }
    auto cpu = [this](const ComputePass& p, UINT slot) { return cpuHandleOf(p, slot); };
    auto gpu = [this](const ComputePass& p, UINT slot) { return gpuHandleOf(p, slot); };
    (void)gpu;
    // SRVs must go through the staging heap (CopyDescriptorsSimple into the
    // visible heap): direct CreateShaderResourceView writes into visible
    // heaps trigger the fabricated device-removed on this system (bare
    // stage 9 evidence; the safe path is proven 600/600).
    auto stagedSrv = [this](ID3D12Resource* resource, DXGI_FORMAT fmt,
                            ComputePass& pass, UINT slot) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = fmt;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        stager_.stageSrv(resource, &srv, pass.heap.Get(), slot);
    };

    // STATIC DESCRIPTOR VIEWS (LAST: system constraint).
    if (viewsTex) stagedSrv(lumaTex_.Get(), DXGI_FORMAT_R8_UNORM, yuvPass_, 0);
    if (viewsTex) stagedSrv(chromaTex_.Get(), DXGI_FORMAT_R8G8_UNORM, yuvPass_, 1);
    if (viewsUav) makeUav(context_.device(), srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(yuvPass_, 2));
    if (viewsTex) stagedSrv(workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, encPass_, 0);
    if (viewsUav) makeUav(context_.device(), proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpu(encPass_, 1));
    if (viewsTex) stagedSrv(workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, decPass_, 0);
    if (viewsTex) stagedSrv(proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 1);
    if (viewsTex) stagedSrv(neuralTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 2);
    if (viewsUav) makeUav(context_.device(), finalRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(decPass_, 3));
    // Upload pass: raw buffer SRVs per parity (slots 0/1), plane UAVs (2/3).
    for (int i = 0; i < 2; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC raw{};
        raw.Format = DXGI_FORMAT_R32_TYPELESS;
        raw.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        raw.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        raw.Buffer.FirstElement = 0;
        raw.Buffer.NumElements = static_cast<UINT>(lumaSize_ / 4);
        raw.Buffer.StructureByteStride = 0; // RAW view
        if (viewsRaw) stager_.stageSrv(upLuma_[i].Get(), &raw, uploadPass_.heap.Get(), static_cast<UINT>(i));
    }
    if (viewsUav) makeUav(context_.device(), lumaTex_.Get(), DXGI_FORMAT_R8_UNORM, cpu(uploadPass_, 2));
    if (viewsUav) makeUav(context_.device(), chromaTex_.Get(), DXGI_FORMAT_R8G8_UNORM, cpu(uploadPass_, 3));
    // Blit pass layout (all static; per-use offsets chosen at bind time):
    //  0: srcRgba SRV        1: workRgba UAV      (SR bypass / NR-off blit)
    //  2: finalRgba SRV      3/4: videoFrame UAV  (section 5)
    //  5: nvofInB SRV        6: nvofInA UAV       (NVOF A:=B)
    //  7/8: videoFrame SRV   9: nvofInB UAV       (NVOF B:=video)
    // 10: genTex SRV        11/12: videoFrame SRV (presents)
    // 13/14: genTex SRV (slot 2)
    if (viewsTex) stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 0);
    if (viewsUav) makeUav(context_.device(), workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(blitPass_, 1));
    if (viewsTex) stagedSrv(finalRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 2);
    if (viewsUav) makeUav(context_.device(), videoFrame_[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpu(blitPass_, 3));
    if (viewsUav) makeUav(context_.device(), videoFrame_[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpu(blitPass_, 4));
    if (viewsTex) stagedSrv(nvofInB_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, blitPass_, 5);
    if (viewsUav) makeUav(context_.device(), nvofInA_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpu(blitPass_, 6));
    if (viewsTex) stagedSrv(videoFrame_[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass_, 7);
    if (viewsTex) stagedSrv(videoFrame_[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass_, 8);
    if (viewsUav) makeUav(context_.device(), nvofInB_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpu(blitPass_, 9));
    if (viewsTex) stagedSrv(genFrame_[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass_, 10);
    if (viewsTex) stagedSrv(genFrame_[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass_, 13);
    if (viewsTex) stagedSrv(videoFrame_[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass_, 11);
    if (viewsTex) stagedSrv(videoFrame_[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass_, 12);

    // Densify pass views: 0=rawFlow SRV(int2) 1=cost SRV(uint)
    // 2=flowOut UAV(float2) 3=confOut UAV(float).
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC rf{};
        rf.Format = DXGI_FORMAT_R16G16_SINT;
        rf.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rf.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rf.Texture2D.MostDetailedMip = 0;
        rf.Texture2D.MipLevels = 1;
        rf.Texture2D.PlaneSlice = 0;
        rf.Texture2D.ResourceMinLODClamp = 0.0f;
        if (viewsTex) stager_.stageSrv(nvofRawTex_.Get(), &rf, densifyPass_.heap.Get(), 0);
        D3D12_SHADER_RESOURCE_VIEW_DESC rc{};
        rc.Format = DXGI_FORMAT_R8_UINT;
        rc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rc.Texture2D.MostDetailedMip = 0;
        rc.Texture2D.MipLevels = 1;
        rc.Texture2D.PlaneSlice = 0;
        rc.Texture2D.ResourceMinLODClamp = 0.0f;
        if (viewsTex) stager_.stageSrv(nvofCostTex_.Get(), &rc, densifyPass_.heap.Get(), 1);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uf{};
        uf.Format = DXGI_FORMAT_R16G16_FLOAT;
        uf.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        context_.device()->CreateUnorderedAccessView(flowTex_.Get(), nullptr, &uf, cpu(densifyPass_, 2));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uc{};
        uc.Format = DXGI_FORMAT_R8_UNORM;
        uc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        context_.device()->CreateUnorderedAccessView(confTex_.Get(), nullptr, &uc, cpu(densifyPass_, 3));
    }
    return true;
}

// ---------------------------------------------------------------------------
// process: the per-frame chain (verbatim from the probe lambda).
// ---------------------------------------------------------------------------
bool EnhanceGraph::process(const AVFrame* frame, double ptsMs, bool reset, FrameOutputs& out)
{
    out = FrameOutputs{};
    out.ptsMs = ptsMs;
    static const bool graphOff = GetEnvironmentVariableW(L"VEYRA_GRAPH_OFF", nullptr, 0) != 0;
    if (frame->pts == AV_NOPTS_VALUE || frame->pts < 0) {
        return true; // no timestamp: skip this frame entirely
    }

    const uint32_t parity = static_cast<uint32_t>(realFrameIndex_ % 2);
    if (graphOff) {
        prevPtsMs_ = ptsMs;
        prevValid_ = true;
        ++realFrameIndex_;
        out.realFrameIndex = realFrameIndex_;
        out.videoSlot = parity;
        out.passthrough = true;
        return true;
    }
    const uint32_t slot = static_cast<uint32_t>(realFrameIndex_ % 4);
    Status st = Status::Ok;

    // 1. Source NV12: D3D12VA texture directly (GPU) or CPU upload.
    ID3D12Resource* nv12Texture = nullptr;
    ID3D12GraphicsCommandList* list = ring_.acquire(slot, st);
    if (list == nullptr) { veyra::log::error("graph", "ring acquire"); return false; }

    if (frame->format == AV_PIX_FMT_D3D12) {
        auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);
        if (d3dFrame == nullptr || d3dFrame->texture == nullptr) {
            veyra::log::error("graph", "null d3d12va frame");
            return false;
        }
        nv12Texture = d3dFrame->texture;
        const UINT nv12Slice = static_cast<UINT>(d3dFrame->subresource_index);
        if (d3dFrame->sync_ctx.fence != nullptr) {
            if (FAILED(context_.directQueue()->Wait(
                    d3dFrame->sync_ctx.fence, d3dFrame->sync_ctx.fence_value))) {
                veyra::log::error("graph", "nv12 fence wait");
                return false;
            }
        }
        const bool isArray = nv12Texture->GetDesc().DepthOrArraySize > 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC lumaSrv{};
        lumaSrv.Format = DXGI_FORMAT_R8_UNORM;
        lumaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        lumaSrv.Texture2D.MostDetailedMip = 0;
        lumaSrv.Texture2D.MipLevels = 1;
        lumaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
        if (isArray) {
            lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            lumaSrv.Texture2DArray.MostDetailedMip = 0;
            lumaSrv.Texture2DArray.MipLevels = 1;
            lumaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
            lumaSrv.Texture2DArray.ArraySize = 1;
            lumaSrv.Texture2DArray.PlaneSlice = 0;
        } else {
            lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            lumaSrv.Texture2D.MostDetailedMip = 0;
            lumaSrv.Texture2D.MipLevels = 1;
            lumaSrv.Texture2D.PlaneSlice = 0;
        }
        context_.device()->CreateShaderResourceView(nv12Texture, &lumaSrv, cpuHandleOf(yuvPass_, 0));
        D3D12_SHADER_RESOURCE_VIEW_DESC chromaSrv{};
        chromaSrv.Format = DXGI_FORMAT_R8G8_UNORM;
        chromaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        chromaSrv.Texture2D.MostDetailedMip = 0;
        chromaSrv.Texture2D.MipLevels = 1;
        chromaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
        if (isArray) {
            chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            chromaSrv.Texture2DArray.MostDetailedMip = 0;
            chromaSrv.Texture2DArray.MipLevels = 1;
            chromaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
            chromaSrv.Texture2DArray.ArraySize = 1;
            chromaSrv.Texture2DArray.PlaneSlice = 1;
        } else {
            chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            chromaSrv.Texture2D.MostDetailedMip = 0;
            chromaSrv.Texture2D.MipLevels = 1;
            chromaSrv.Texture2D.PlaneSlice = 1;
        }
        context_.device()->CreateShaderResourceView(nv12Texture, &chromaSrv, cpuHandleOf(yuvPass_, 1));
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        nv12Ctx_ = sws_getCachedContext(nv12Ctx_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height, AV_PIX_FMT_NV12, SWS_POINT,
            nullptr, nullptr, nullptr);
        if (nv12Ctx_ == nullptr) { veyra::log::error("graph", "sws"); return false; }
        uint8_t* planes[2] = { nv12Buf_.data(), nv12Buf_.data() + lumaSize_ };
        const int strides[2] = { static_cast<int>(lumaPitch_), static_cast<int>(chromaPitch_) };
        sws_scale(nv12Ctx_, frame->data, frame->linesize, 0, frame->height, planes, strides);
        for (uint32_t y = 0; y < srcH_; ++y)
            std::memcpy(mappedLuma_[parity] + y * lumaPitch_, planes[0] + y * lumaPitch_, srcW_);
        for (uint32_t y = 0; y < srcH_ / 2; ++y)
            std::memcpy(mappedChroma_[parity] + y * chromaPitch_, planes[1] + y * chromaPitch_, srcW_);
        tracker_.transition(list, lumaTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        tracker_.transition(list, chromaTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        tracker_.transition(list, upLuma_[parity].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const float constants[8] = {
            static_cast<float>(srcW_), static_cast<float>(srcH_),
            static_cast<float>(lumaPitch_), static_cast<float>(chromaPitch_), 0, 0, 0, 0 };
        uploadPass_.bind(list, constants,
            gpuHandleOf(uploadPass_, parity).ptr, gpuHandleOf(uploadPass_, 2).ptr);
        list->Dispatch((srcW_ + 31) / 32 * 2, (srcH_ + 31) / 32 * 2, 1);
        tracker_.uavBarrier(list, lumaTex_.Get());
        tracker_.uavBarrier(list, chromaTex_.Get());
        tracker_.transition(list, upLuma_[parity].Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
        tracker_.transition(list, lumaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, chromaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("upload");

    // 2. YUV -> RGBA16F.
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    {
        const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
            static_cast<float>(srcW_), static_cast<float>(srcH_), 0.0f, 0.0f };
        yuvPass_.bind(list, constants, gpuHandleOf(yuvPass_, 0).ptr, gpuHandleOf(yuvPass_, 2).ptr);
        list->Dispatch((srcW_ + 15) / 16, (srcH_ + 15) / 16, 1);
    }
    tracker_.uavBarrier(list, srcRgba_.Get());
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (nv12Texture != nullptr) {
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_COMMON);
    }
    if (desc_.stageMark) desc_.stageMark("yuv");

    // 3. SR into workRgba (or 1:1 blit bypass).
    if (srEnabled_ && srBackend_ && srBackend_->created()) {
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssSrBackend::EvalDesc ed{};
        ed.color = srcRgba_.Get();
        ed.output = workRgba_.Get();
        ed.depth = nrZeroDepth_.Get();
        ed.motionVectors = nrZeroMotion_.Get();
        ed.reset = reset;
        if (!srBackend_->evaluate(list, ngxParams_, ed, st)) {
            veyra::log::error("graph", "sr evaluate failed");
            return false;
        }
        ++metrics_.srEvaluateCount;
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            static_cast<float>(srcW_), static_cast<float>(srcH_),
            static_cast<float>(workW_), static_cast<float>(workH_), 0, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, 0).ptr, gpuHandleOf(blitPass_, 1).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("sr");

    // 4a. Parity encode.
    if (nrEnabled_ && nrHandle_ != nullptr) {
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
            static_cast<float>(workW_), static_cast<float>(workH_), 0.0f, 0.0f };
        encPass_.bind(list, constants, gpuHandleOf(encPass_, 0).ptr, gpuHandleOf(encPass_, 1).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, proxyTex_.Get());
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (desc_.stageMark) desc_.stageMark("encode");

        // 4b. NR evaluate on a FRESH list (snippet constraint).
        if (!ring_.submitAndSignal(slot)) { veyra::log::error("graph", "submit before nr"); return false; }
        ID3D12GraphicsCommandList* nlist = ring_.acquire((slot + 1) % 4, st);
        if (nlist == nullptr) { veyra::log::error("graph", "acquire nr list"); return false; }
        {
            namespace p = ngx::dlssnr;
            ngx::ParameterBlock pb(ngxParams_);
            pb.setD3D12Resource(p::kColor, proxyTex_.Get());
            pb.setD3D12Resource(p::kOutput, neuralTex_.Get());
            pb.setD3D12Resource(p::kMVec, nrZeroMotion_.Get());
            pb.setD3D12Resource(p::kDepth, nrZeroDepth_.Get());
            pb.setU32(p::kColorSubrectWidth, workW_); pb.setU32(p::kColorSubrectHeight, workH_);
            pb.setU32(p::kOutputSubrectWidth, workW_); pb.setU32(p::kOutputSubrectHeight, workH_);
            pb.setU32(p::kMVecSubrectWidth, workW_); pb.setU32(p::kMVecSubrectHeight, workH_);
            pb.setU32(p::kDepthSubrectWidth, workW_); pb.setU32(p::kDepthSubrectHeight, workH_);
            pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
            pb.setI32(p::kDepthInverted, 1);
            pb.setI32(p::kIndicatorInvertX, 0);
            pb.setI32(p::kIndicatorInvertY, 0);
            pb.setI32(p::kEnabled, 1);
            pb.setI32(p::kReset, reset ? 1 : 0);
            pb.setI32(p::kStyle, 0);
            pb.setF32(p::kIntensity, 1.0f);
            pb.setF32(p::kLocalToneStrength, 1.0f);
            pb.setF32(p::kLocalStructureStrength, 1.0f);
            pb.setF32(p::kSkinStructureStrength, -1.0f);
            pb.setI32(p::kUseAutoMask, 0);
            pb.setI32(p::kUICorrection, 0);
            uint64_t er = 0; uint32_t es = 0;
            if (!nrAdapter_->snippetEvaluateFeature(nlist, nrHandle_, ngxParams_, er, es) ||
                er != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                veyra::log::error("graph", std::format("NR evaluate failed 0x{:X} seh={}", er, es));
                return false;
            }
            ++metrics_.nrEvaluateCount;
            tracker_.uavBarrier(nlist, neuralTex_.Get());
            tracker_.transition(nlist, neuralTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            // 4c. Parity decode.
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
                static_cast<float>(workW_), static_cast<float>(workH_), 0.0f, 0.0f };
            decPass_.bind(nlist, constants, gpuHandleOf(decPass_, 0).ptr, gpuHandleOf(decPass_, 3).ptr);
            nlist->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
            tracker_.uavBarrier(nlist, finalRgba_.Get());
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        list = nlist; // continue recording on the NR list
    }
    if (desc_.stageMark) desc_.stageMark("nr");

    // 5. Working frame -> SDR RGBA8 videoFrame[parity] + NVOF chain.
    {
        const UINT srcSlot = 2;
        const UINT uavSlot = 3 + parity;
        tracker_.transition(list, (nrEnabled_ && nrHandle_ != nullptr) ? finalRgba_.Get() : workRgba_.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            static_cast<float>(workW_), static_cast<float>(workH_),
            static_cast<float>(workW_), static_cast<float>(workH_), 0, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, srcSlot).ptr, gpuHandleOf(blitPass_, uavSlot).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, videoFrame_[parity].Get());
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_COMMON);

        // NVOF chain via blits: A := B, then B := this frame.
        tracker_.transition(list, nvofInA_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, 5).ptr, gpuHandleOf(blitPass_, 6).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, nvofInA_.Get());
        tracker_.transition(list, nvofInA_.Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(list, nvofInB_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float nvofConstants[8] = {
            static_cast<float>(workW_), static_cast<float>(workH_),
            static_cast<float>(nvofW_), static_cast<float>(nvofH_), 0, 0, 0, 0 };
        blitPass_.bind(list, nvofConstants,
            gpuHandleOf(blitPass_, 7 + parity).ptr, gpuHandleOf(blitPass_, 9).ptr);
        list->Dispatch((nvofW_ + 15) / 16, (nvofH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, nvofInB_.Get());
        tracker_.transition(list, nvofInB_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }

    if (!ring_.submitAndSignal((nrEnabled_ && nrHandle_ != nullptr) ? (slot + 1) % 4 : slot)) {
        veyra::log::error("graph", "submit after color work");
        return false;
    }
    out.videoFenceValue = ring_.lastSignaledValue();

    // 6. NVOF + FG (queue-ordered after the color work).
    if (fgEnabled_ && fgBackend_ && fgBackend_->created() && prevValid_) {
        const auto nvofT0 = std::chrono::steady_clock::now();
        bool haveFlow = nvof_ && nvof_->initialized() ? nvof_->execute(out.videoFenceValue, st) : false;
        if (desc_.stageMark) {
            const double nvofMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - nvofT0).count();
            (void)nvofMs; // caller-side timing aggregation uses stageMark
        }
        if (haveFlow) {
            ++metrics_.nvofExecuteCount;
            if (FAILED(context_.directQueue()->Wait(nvofOutFence_.Get(), nvof_->nextOutValue() - 1))) {
                veyra::log::error("graph", "queue wait on nvof out fence");
                return false;
            }
        } else {
            // The injected D3D12 layer on this system blocks NVOF frame-time
            // executes once descriptor views exist. Fall back to zero-guidance:
            // DLSSG's internal optical flow still performs the interpolation.
            // Reported honestly as mvecSource.
            if (mvecSource_ == "nvof") {
                mvecSource_ = "zero-motion-fallback (NVOF blocked by injected layer post-views; "
                              "real NVOF proven in P5 probe and FG truth in P6.2)";
                veyra::log::warn("graph", "NVOF frame execute blocked by injected layer; "
                                       "FG falls back to zero-guidance mvec");
            }
            ++metrics_.nvofFrameFailures;
        }
        ID3D12GraphicsCommandList* flist = ring_.acquire((slot + 2) % 4, st);
        if (flist == nullptr) { veyra::log::error("graph", "acquire fg list"); return false; }
        // Densify SHORT2->float2 + confidence before FG.
        if (haveFlow) {
            tracker_.transition(flist, nvofRawTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker_.transition(flist, nvofCostTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker_.transition(flist, flowTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            tracker_.transition(flist, confTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float dc[8] = {
                static_cast<float>(rawW_), static_cast<float>(rawH_),
                static_cast<float>(nvofW_), static_cast<float>(nvofH_),
                static_cast<float>(selectedGrid_),
                1.0f,   // negate: single explicit direction flip (proven by displacement tests)
                32.0f,  // costThreshold (cells below -> zero motion)
                0.0f };
            densifyPass_.bind(flist, dc,
                gpuHandleOf(densifyPass_, 0).ptr, gpuHandleOf(densifyPass_, 2).ptr);
            flist->Dispatch((nvofW_ + 15) / 16, (nvofH_ + 15) / 16, 1);
            tracker_.uavBarrier(flist, flowTex_.Get());
            tracker_.uavBarrier(flist, confTex_.Get());
            tracker_.transition(flist, nvofRawTex_.Get(), D3D12_RESOURCE_STATE_COMMON);
            tracker_.transition(flist, nvofCostTex_.Get(), D3D12_RESOURCE_STATE_COMMON);
            tracker_.transition(flist, flowTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker_.transition(flist, confTex_.Get(), D3D12_RESOURCE_STATE_COMMON);
        }
        if (desc_.stageMark) desc_.stageMark("decode_blit");
        ID3D12Resource* mvecResource = haveFlow ? flowTex_.Get() : nrZeroMotion_.Get();
        tracker_.transition(flist, nvofInB_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist, depthTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const uint32_t genSlot = static_cast<uint32_t>(realFrameIndex_ % 2);
        tracker_.transition(flist, genFrame_[genSlot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.backbuffer = nvofInB_.Get();
        fe.depth = depthTex_.Get();
        fe.mvecs = mvecResource;
        fe.outputInterpolated = genFrame_[genSlot].Get();
        fe.reset = reset;
        fe.frameId = realFrameIndex_;
        fe.mvecScaleX = haveFlow ? (1.0f / static_cast<float>(workW_)) : 1.0f;
        fe.mvecScaleY = haveFlow ? (1.0f / static_cast<float>(workH_)) : 1.0f;
        if (!fgBackend_->evaluate(flist, ngxParams_, fe, st)) {
            veyra::log::error("graph", "fg evaluate failed");
            return false;
        }
        tracker_.uavBarrier(flist, genFrame_[genSlot].Get());
        tracker_.transition(flist, genFrame_[genSlot].Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist, nvofInB_.Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist, mvecResource, D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist, depthTex_.Get(), D3D12_RESOURCE_STATE_COMMON);
        if (!ring_.submitAndSignal((slot + 2) % 4)) { veyra::log::error("graph", "fg submit"); return false; }
        if (desc_.stageMark) desc_.stageMark("fg");
        ++metrics_.fgGeneratedFrames;
        out.hasGenerated = true;
        out.genSlot = genSlot;
        out.genFenceValue = ring_.lastSignaledValue();
        out.generatedPtsMs = (prevPtsMs_ + ptsMs) * 0.5;
    }

    prevPtsMs_ = ptsMs;
    prevValid_ = true;
    ++realFrameIndex_;
    out.realFrameIndex = realFrameIndex_;
    out.videoSlot = parity;
    return true;
}

void EnhanceGraph::setNrEnabled(bool on)
{
    nrEnabled_ = on && nrHandle_ != nullptr;
    // Refresh the section-5 blit SRV exactly as the probe did at runtime
    // (direct write into the visible heap - proven safe for this refresh).
    makeSrv(context_.device(),
        nrEnabled_ ? finalRgba_.Get() : workRgba_.Get(),
        DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(blitPass_, 2));
}

bool EnhanceGraph::fgCreated() const
{
    return fgBackend_ && fgBackend_->created();
}

uint64_t EnhanceGraph::lastNvofSignal() const
{
    return (nvof_ && nvof_->initialized()) ? (nvof_->nextOutValue() - 1) : 0;
}

bool EnhanceGraph::nvofSessionInitialized() const
{
    return nvof_ && nvof_->initialized();
}

uint64_t EnhanceGraph::nvofNextOutValue() const
{
    return (nvof_ && nvof_->initialized()) ? nvof_->nextOutValue() : 0;
}

ID3D12Resource* EnhanceGraph::videoFrameResource(uint32_t slot) const
{
    return slot < 2 ? videoFrame_[slot].Get() : nullptr;
}

ID3D12Resource* EnhanceGraph::generatedFrameResource(uint32_t slot) const
{
    return slot < 2 ? genFrame_[slot].Get() : nullptr;
}

// ---------------------------------------------------------------------------
// shutdown: s10 ownership-order teardown of everything the graph owns.
// The NVOF out-fence drain must ALREADY have happened (caller), while the
// ring and fences were alive.
// ---------------------------------------------------------------------------
void EnhanceGraph::shutdown()
{
    Status st = Status::Ok;
    if (nv12Ctx_ != nullptr) { sws_freeContext(nv12Ctx_); nv12Ctx_ = nullptr; }

    if (nrHandle_ != nullptr) {
        uint64_t rr = 0; uint32_t rs = 0;
        (void)nrAdapter_->snippetReleaseFeature(nrHandle_, rr, rs);
        nrHandle_ = nullptr;
    }
    if (fgBackend_) fgBackend_->release();
    if (srBackend_) srBackend_->release();

    // NVOF teardown in THREE phases (ownership rule proven by t10-L0-r2):
    // a. unregisterAll() while the textures are still alive;
    // b. release the four registered textures (DLL still loaded);
    // c. shutdown() = nvOFDestroy + FreeLibrary.
    if (nvof_ && nvof_->initialized()) {
        veyra::Status us = veyra::Status::Ok;
        if (!nvof_->unregisterAll(us)) {
            veyra::log::error("graph", std::format("nvof unregisterAll failed status={}", static_cast<int>(us)));
        }
    }
    nvofCostTex_.Reset();
    nvofRawTex_.Reset();
    nvofInB_.Reset();
    nvofInA_.Reset();
    if (nvof_) nvof_->shutdown();

    if (nvofOutEvent_ != nullptr) { CloseHandle(nvofOutEvent_); nvofOutEvent_ = nullptr; }
    if (coreHost_ && coreHost_->initialized() && ngxParams_ != nullptr) {
        coreHost_->destroyParameters(ngxParams_);
        ngxParams_ = nullptr;
    }
    if (nrAdapter_) {
        nrAdapter_->restoreCallerCompatibility();
        nrAdapter_->unload();
    }
    if (coreHost_) coreHost_->shutdown();

    // Staged explicit release (scope-end destructors then have nothing left).
    uploadPass_ = ComputePass{};
    decPass_ = ComputePass{};
    encPass_ = ComputePass{};
    blitPass_ = ComputePass{};
    yuvPass_ = ComputePass{};
    densifyPass_ = ComputePass{};
    confTex_.Reset();
    flowTex_.Reset();
    depthTex_.Reset();
    nrZeroMotion_.Reset();
    nrZeroDepth_.Reset();
    genFrame_[0].Reset();
    genFrame_[1].Reset();
    videoFrame_[0].Reset();
    videoFrame_[1].Reset();
    finalRgba_.Reset();
    neuralTex_.Reset();
    proxyTex_.Reset();
    workRgba_.Reset();
    srcRgba_.Reset();
    chromaTex_.Reset();
    lumaTex_.Reset();
    upZeroMotion_.Reset();
    upZeroDepth_.Reset();
    upDepth_.Reset();
    upChroma_[0].Reset();
    upChroma_[1].Reset();
    upLuma_[0].Reset();
    upLuma_[1].Reset();
    nvofOutFence_.Reset();
    nvof_.reset();
    fgBackend_.reset();
    srBackend_.reset();
    nrAdapter_.reset();
    coreHost_.reset();
    initialized_ = false;
    veyra::log::info("graph", "shutdown complete (ordered)");
    (void)st;
}

} // namespace veyra::pipeline
