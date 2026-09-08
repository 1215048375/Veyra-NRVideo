#include "veyra/pipeline/ColorMetadata.h"
// EnhanceGraph implementation - the real GPU chain migrated from
// tools/player_probe main.cpp (R3.2). Ordering constraints preserved from the
// injected-layer era evidence: committed resources and NGX/NVOF objects are
// created BEFORE descriptor views; FG warm-up evaluate precedes views; static
// views are created last. Per-frame execution uses the shared command slot
// ring (NR evaluates on a fresh list - snippet constraint).
#include "veyra/pipeline/EnhanceGraph.h"

#include <algorithm>
#include <bit>
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
using diagnostics::GpuStage;

namespace {

constexpr int64_t usPerSecond = 1000000;
float uintBits(uint32_t v) { return std::bit_cast<float>(v); }

} // namespace

EnhanceGraph::EnhanceGraph(gfx::D3D12DeviceContext& context, gfx::CommandSlotRing& ring)
    : context_(context)
    , ring_(ring)
{
}

EnhanceGraph::~EnhanceGraph()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// initialize: exact ordering of the proven probe sequence.
// ---------------------------------------------------------------------------
bool EnhanceGraph::initialize(const EnhanceGraphDesc& desc)
{
    desc_ = desc;tracker_={};prevValid_=false;cadence_.reset();scene_.reset();previousLuma_.clear();
    diagnostics::DiagnosticEvent initDiagnostic;initDiagnostic.stage="initialize";initDiagnostic.identity={epoch_+1,desc.settingsRevision,0};initDiagnostic.resolution.source={desc.sourceWidth,desc.sourceHeight};initDiagnostic.resolution.base=initDiagnostic.resolution.fg=initDiagnostic.resolution.output={desc.workWidth,desc.workHeight};initDiagnostic.resolution.nr={desc.nrWidth?desc.nrWidth:desc.workWidth,desc.nrHeight?desc.nrHeight:desc.workHeight};initDiagnostic.resolution.flow=initDiagnostic.resolution.source;initDiagnostic.flowApplied=std::to_string(unsigned(desc.flowQuality));initDiagnostic.runtimeHash="E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";Logger::diagnosticContext(initDiagnostic);
    srcW_ = desc.sourceWidth;
    srcH_ = desc.sourceHeight;
    workW_ = desc.workWidth;
    workH_ = desc.workHeight;
    nrW_=desc.nrWidth?desc.nrWidth:workW_; nrH_=desc.nrHeight?desc.nrHeight:workH_;
    nvofW_ = srcW_;
    nvofH_ = srcH_;
    if(!Extent{srcW_,srcH_}.valid()||!Extent{workW_,workH_}.valid()){
        veyra::log::error("resolution","source/output exceeds D3D12 texture dimensions or is empty");return false;
    }
    if(!Extent{nrW_,nrH_}.valid()||nrW_>workW_||nrH_>workH_){veyra::log::error("resolution","invalid NR extent");return false;}
    veyra::log::info("resolution",std::format("source={}x{} base={}x{} nr={}x{} flow={}x{} fg={}x{} output={}x{}",srcW_,srcH_,workW_,workH_,nrW_,nrH_,nvofW_,nvofH_,workW_,workH_,workW_,workH_));
    srEnabled_ = desc.enableSr;
    nrEnabled_ = desc.enableNr && !desc.noFeatures && !desc.noNgx;
    fgEnabled_ = desc.enableFg && !desc.noNgx && !desc.stillImage;
    nvofStandalone_ = desc.enableNvofStandalone && !desc.noFeatures;
    Status st = Status::Ok;

    lumaPitch_ = (static_cast<size_t>(srcW_) + 255) & ~size_t(255);
    chromaPitch_ = lumaPitch_;
    lumaSize_ = lumaPitch_ * srcH_;
    chromaSize_ = chromaPitch_ * ((srcH_ + 1) / 2);
    dPitch_ = (static_cast<size_t>(workW_) * 4 + 255) & ~size_t(255);
    const size_t dSize = dPitch_ * workH_;
    nv12Buf_.resize(lumaSize_ + chromaSize_);

    if(!gpuTimer_.initialize(context_.device(),context_.directQueue()))veyra::log::warn("gpu-timestamp","GPU timing unavailable");
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
    fgDisableInit_=makeUploadBuffer(context_.device(),4);
    if(!fgDisableInit_)return false;
    void* initial=nullptr;if(FAILED(fgDisableInit_->Map(0,nullptr,&initial)))return false;
    *static_cast<uint32_t*>(initial)=1;fgDisableInit_->Unmap(0,nullptr);
    for(unsigned i=0;i<6;++i){
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=4;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr=context_.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&fgDisable_[i]));
        if(FAILED(hr)){veyra::log::error("fg-status",std::format("allocate UAV hr=0x{:X}",unsigned(hr)));return false;}
        hp.Type=D3D12_HEAP_TYPE_READBACK;bd.Flags=D3D12_RESOURCE_FLAG_NONE;
        hr=context_.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&fgDisableReadback_[i]));
        if(FAILED(hr)){veyra::log::error("fg-status",std::format("allocate readback hr=0x{:X}",unsigned(hr)));return false;}
    }
    // NV12 CPU upload keeps UPLOAD-heap BUFFERS (upload-heap textures are
    // size-limited) with persistent mapping, but the GPU ingestion is now a
    // PLACED_FOOTPRINT CopyTextureRegion instead of the retired RAW-buffer
    // SRV dispatch - the RAW SRV was the one descriptor kind that tripped the
    // injected layer (device removal / blocked NVOF; r33b evidence 2026-09-06).
    upLuma_[0] = makeUploadBuffer(context_.device(), lumaSize_);
    upLuma_[1] = makeUploadBuffer(context_.device(), lumaSize_);
    upChroma_[0] = makeUploadBuffer(context_.device(), chromaSize_);
    upChroma_[1] = makeUploadBuffer(context_.device(), chromaSize_);
    upDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroMotion_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    lumaTex_ = makeTexture(context_.device(), srcW_, srcH_, DXGI_FORMAT_R8_UNORM, true);
    chromaTex_ = makeTexture(context_.device(), (srcW_+1) / 2, (srcH_+1) / 2, DXGI_FORMAT_R8G8_UNORM, true);
    if(desc_.rgbInput){
        rgbPitch_=(size_t(srcW_)*4+255)&~size_t(255);
        rgbTex_=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R8G8B8A8_UNORM,false);
        if(!rgbTex_)return false;
        for(unsigned i=0;i<2;++i){upRgb_[i]=makeUploadBuffer(context_.device(),rgbPitch_*srcH_);
            if(!upRgb_[i]||FAILED(upRgb_[i]->Map(0,nullptr,reinterpret_cast<void**>(&mappedRgb_[i]))))return false;
        }
    }
    srcRgba_ = makeTexture(context_.device(), srcW_, srcH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    workRgba_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    for(unsigned i=0;i<2;++i){sourceReferences_[i]=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R16G16B16A16_FLOAT,false);baseReferences_[i]=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,false);if(!sourceReferences_[i]||!baseReferences_[i])return false;}
    nrInput_=makeTexture(context_.device(),nrW_,nrH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    residualRgba_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    nrFlow_=makeTexture(context_.device(),nrW_,nrH_,DXGI_FORMAT_R16G16_FLOAT,true);
    baseFlow_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16_FLOAT,true);
    if(!nrInput_||!residualRgba_||!nrFlow_||!baseFlow_)return false;
    proxyTex_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    neuralTex_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    finalRgba_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    videoFrame_[0] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    videoFrame_[1] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    confTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R8_UNORM, true);
    flowTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R16G16_FLOAT, true);
    depthTex_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    genFrame_[0] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    genFrame_[1] = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    for(unsigned i=2;i<6;++i){genFrame_[i]=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R8G8B8A8_UNORM,true);if(!genFrame_[i])return false;}
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
    veyra::log::info("graph", "NV12 upload ring persistently mapped (2 buffer sets)");
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
    if(desc_.stillImage){mvecSource_="single-image (no temporal motion)";return true;}
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
    nd.gridSize = 4;nd.quality=uint32_t(desc_.flowQuality);
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
    if (desc_.noNgx || desc_.noFeatures) {
        veyra::log::info("graph", "NGX core/features skipped by disabled-feature configuration");
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

    fgCapsAvailable_ = false;
    fgMultiFrameMax_ = 0;
    if (!desc_.stillImage) {
    fgBackend_ = std::make_unique<ngx::DlssFgBackend>();
    ngx::DlssFgBackend::Capability fgCaps{};
    const bool fgAvailable = fgBackend_->queryCapability(*coreHost_, fgCaps, st);
    if (!fgAvailable) {
        veyra::log::error("graph", "FG unavailable; fail closed");
        return false;
    }
    if(desc_.enableFg&&(desc_.fgMultiplier<2||desc_.fgMultiplier>4||fgCaps.multiFrameCountMax<desc_.fgMultiplier-1)){veyra::log::error("graph","requested MFG multiplier unsupported");return false;}
    fgCapsAvailable_ = fgCaps.available;
    fgMultiFrameMax_ = fgCaps.multiFrameCountMax;
    veyra::log::info("graph", std::format("FG capability available={} multiFrameMax={}",
        fgCaps.available, fgCaps.multiFrameCountMax));
    }

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
        pb.setU32(p::kWidth, nrW_); pb.setU32(p::kHeight, nrH_);
        pb.setU32(p::kInputWidth, nrW_); pb.setU32(p::kInputHeight, nrH_);
        pb.setU32(p::kOutputWidth, nrW_); pb.setU32(p::kOutputHeight, nrH_);
        pb.setU32(p::kOutputDotWidth, nrW_); pb.setU32(p::kOutputDotHeight, nrH_);
        pb.setU32(p::kUpscaling, 0);
        pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
        pb.setVoid(p::kComputeScalingRatioCallback,
            reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        pb.setI32(p::kHintRenderPreset, 0);
        pb.setU32(p::kStdWidth, nrW_); pb.setU32(p::kStdHeight, nrH_);
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

    // Still images have no temporal pair and must not depend on FG support.
    // FG create + warm-up evaluate BEFORE descriptor views (the runtime
    // allocates internals at the first evaluate and would fail after views
    // exist on this system).
    if (!desc_.stillImage) {
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
        b[0].Transition.pResource = videoFrame_[0].Get();
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[1].Transition.pResource = nrZeroMotion_.Get();
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[2].Transition.pResource = depthTex_.Get();
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[3].Transition.pResource = genFrame_[0].Get();
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        wlist->ResourceBarrier(4, b);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.backbuffer = videoFrame_[0].Get();
        fe.depth = depthTex_.Get();
        fe.mvecs = nrZeroMotion_.Get();
        fe.outputInterpolated = genFrame_[0].Get();
        fe.reset = true;
        fe.multiFrameCount=desc_.enableFg?desc_.fgMultiplier-1:1;fe.multiFrameIndex=1;
        fe.frameId = 0; // warm-up has its own identity; product IDs start at 1
        fe.mvecScaleX = 1.0f;
        fe.mvecScaleY = 1.0f;
        bool warmOk = true;
        for(uint32_t sub=1;sub<=fe.multiFrameCount;++sub){
            fe.multiFrameIndex=sub;
            warmOk=fgBackend_->evaluate(wlist,ngxParams_,fe,st)&&warmOk;
            D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=fe.outputInterpolated;wlist->ResourceBarrier(1,&u);
        }
        if(!warmOk)return false;
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
    if(!downsamplePass_.loadShader("NrDownsample.dxil",cs)||!downsamplePass_.create(context_.device(),cs,2,1,1))return false;
    if(!residualPass_.loadShader("NrResidualComposite.dxil",cs)||!residualPass_.create(context_.device(),cs,4,3,1))return false;
    if(!flowAdaptPass_.loadShader("FlowAdapt.dxil",cs)||!flowAdaptPass_.create(context_.device(),cs,3,1,1))return false;
    if (!yuvPass_.loadShader("YuvToLinearRgb.dxil", cs) || !yuvPass_.create(context_.device(), cs, 8, 2, 1)) return false;
    if(desc_.rgbInput&&(!rgbPass_.loadShader("RgbToLinear.dxil",cs)||!rgbPass_.create(context_.device(),cs,4,1,1)))return false;
    if (!encPass_.loadShader("ParityEncode.dxil", cs) || !encPass_.create(context_.device(), cs, 8, 1, 1)) return false;
    if (!decPass_.loadShader("ParityDecode.dxil", cs) || !decPass_.create(context_.device(), cs, 8)) return false;
    if (!blitPass_.loadShader("ScaleBlit.dxil", cs) || !blitPass_.create(context_.device(), cs, 16, 1, 1)) return false;
    // Nv12Upload stays: frame-time CopyTextureRegion is poisoned by the
    // injected layer (SEH in NGX evaluate, r33-final3 evidence); the compute
    // upload is the proven frame-path ingestion on this system.
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
    // Descriptor initialization is DEFAULT ON since the RAW-upload-SRV removal
    // (2026-09-06): texture SRV/UAV views keep the device alive and NVOF
    // executing (r33b-tu: 599/599 executes, real motion/confidence, 0
    // removals) and eliminate the uninitialized-slot dispatches behind GBV
    // id=938. VEYRA_VIEWS_OFF preserves the legacy behavior for A/B.
    const bool viewsOn = GetEnvironmentVariableW(L"VEYRA_VIEWS_OFF", nullptr, 0) == 0;
    const bool viewsTex = viewsOn;
    const bool viewsUav = viewsOn;
    if (!viewsOn) {
        veyra::log::warn("graph", "static views DISABLED (VEYRA_VIEWS_OFF; legacy behavior)");
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

    stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 15);
    stagedSrv(workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,downsamplePass_,0);
    makeUav(context_.device(),nrInput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(downsamplePass_,1));
    stagedSrv(workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,0);
    stagedSrv(nrInput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,1);
    stagedSrv(finalRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,2);
    makeUav(context_.device(),residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(residualPass_,3));
    stagedSrv(flowTex_.Get(),DXGI_FORMAT_R16G16_FLOAT,flowAdaptPass_,0);
    makeUav(context_.device(),nrFlow_.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu(flowAdaptPass_,1));
    makeUav(context_.device(),baseFlow_.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu(flowAdaptPass_,2));
    // Immutable per-resource views.
    if(desc_.rgbInput){
        stagedSrv(rgbTex_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,rgbPass_,0);
        makeUav(context_.device(),srcRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(rgbPass_,1));
    }
    if (viewsTex) stagedSrv(lumaTex_.Get(), DXGI_FORMAT_R8_UNORM, yuvPass_, 0);
    if (viewsTex) stagedSrv(chromaTex_.Get(), DXGI_FORMAT_R8G8_UNORM, yuvPass_, 1);
    if (viewsUav) makeUav(context_.device(), srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(yuvPass_, 2));
    if (viewsTex) stagedSrv(nrInput_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, encPass_, 0);
    if (viewsUav) makeUav(context_.device(), proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpu(encPass_, 1));
    if (viewsTex) stagedSrv(nrInput_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, decPass_, 0);
    if (viewsTex) stagedSrv(proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 1);
    if (viewsTex) stagedSrv(neuralTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 2);
    if (viewsUav) makeUav(context_.device(), finalRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(decPass_, 3));
    if (viewsUav) makeUav(context_.device(), lumaTex_.Get(), DXGI_FORMAT_R8_UNORM, cpu(uploadPass_, 2));
    if (viewsUav) makeUav(context_.device(), chromaTex_.Get(), DXGI_FORMAT_R8G8_UNORM, cpu(uploadPass_, 3));
    // Software NV12 ingestion uses plane copies; no raw-SRV dispatch.
    // Blit pass layout (all static; per-use offsets chosen at bind time):
    //  0: srcRgba SRV        1: workRgba UAV      (SR bypass / NR-off blit)
    //  2: finalRgba SRV      3/4: videoFrame UAV  (section 5)
    //  5: nvofInB SRV        6: nvofInA UAV       (NVOF A:=B)
    //  7/8: videoFrame SRV   9: nvofInB UAV       (NVOF B:=video)
    // 10: genTex SRV        11/12: videoFrame SRV (presents)
    // 13/14: genTex SRV (slot 2)
    if (viewsTex) stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 0);
    if (viewsUav) makeUav(context_.device(), workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(blitPass_, 1));
    if (viewsTex) stagedSrv(nrEnabled_ ? residualRgba_.Get() : workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 2);
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
bool EnhanceGraph::process(const AVFrame* frame, double ptsMs, bool reset, FrameOutputs& out, uint64_t sourceFrameId, const ColorDescription* color)
{
    out = FrameOutputs{};
    out.ptsMs = ptsMs;
    if(!sourceFrameId)sourceFrameId=realFrameIndex_+1;
    diagnostics::DiagnosticEvent diagnostic;diagnostic.stage="frame";diagnostic.identity={epoch_+uint64_t(reset),desc_.settingsRevision,sourceFrameId};diagnostic.batch=realFrameIndex_+1;
    diagnostic.resolution.source={srcW_,srcH_};diagnostic.resolution.base={workW_,workH_};diagnostic.resolution.nr={nrW_,nrH_};diagnostic.resolution.flow={nvofW_,nvofH_};diagnostic.resolution.fg=diagnostic.resolution.output={workW_,workH_};diagnostic.runtimeHash="E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";diagnostic.flowApplied=std::to_string(actualFlowPerf());diagnostic.fallbackReason=mvecSource_;Logger::diagnosticContext(diagnostic);
    static const bool graphOff = GetEnvironmentVariableW(L"VEYRA_GRAPH_OFF", nullptr, 0) != 0;
    if (!frame || !initialized_ || !std::isfinite(ptsMs)) return false;
    const auto resolved=resolveFrameColor(*frame,color?*color:ColorDescription{});
    if(resolved.matrix==YuvMatrix::BT2020NCL||resolved.matrix==YuvMatrix::BT2020CL||resolved.transfer==TransferFunction::BT2020_10){veyra::log::error("graph","BT.2020 input is not supported by the BT.601/709 SDR conversion");return false;}
    if(reset||!realFrameIndex_)veyra::log::info("color",std::format("range={} assumed={} matrix={} assumed={} transfer={} assumed={}",int(resolved.range),resolved.rangeAssumed,int(resolved.matrix),resolved.matrixAssumed,int(resolved.transfer),resolved.transferAssumed));
    if (frame->color_trc == AVCOL_TRC_SMPTE2084 || frame->color_trc == AVCOL_TRC_ARIB_STD_B67) {
        veyra::log::error("graph", "HDR input is unsupported by the V1 SDR pipeline"); return false;
    }
    if(std::abs(ptsMs)>9e13){veyra::log::error("timeline","PTS outside representable range");return false;}
    if(prevValid_&&(std::llround(ptsMs*10000)-std::llround(prevPtsMs_*10000)<4||ptsMs-prevPtsMs_>1000)){
        reset=true;veyra::log::info("timeline","non-monotonic or discontinuous PTS; atomic history reset");
    }
    if (reset) { prevValid_ = false; scene_.reset(); previousLuma_.clear();cadence_.reset(); }

    const uint32_t parity = static_cast<uint32_t>(realFrameIndex_ % 2);
    if(!realLeases_[parity].expired()||!generatedLeases_[parity].expired()){
        veyra::log::error("frame-pool",std::format("slot={} still leased; refusing overwrite, batch={}",parity,realFrameIndex_+1));return false;
    }
    for(unsigned i=parity;i<6;i+=2)if(!generatedLeases_[i].expired()){veyra::log::error("frame-pool","generated subframe still leased; refusing overwrite");return false;}
    if (graphOff) {
        prevPtsMs_ = ptsMs;
        prevValid_ = true;
        ++realFrameIndex_;
        out.realFrameIndex = realFrameIndex_;
        out.videoSlot = parity;
        out.passthrough = true;
        return true;
    }
    uint32_t slot = 0;
    if (uploadFences_[parity] && !context_.waitForFenceValue(uploadFences_[parity])) return false;
    Status st = Status::Ok;

    // 1. Source NV12: D3D12VA texture directly (GPU) or CPU upload.
    ID3D12Resource* nv12Texture = nullptr;
    ID3D12GraphicsCommandList* list = ring_.acquireNext(slot, st);
    if (list == nullptr) { veyra::log::error("graph", "ring acquire"); return false; }
    gpuTimer_.frame({epoch_,desc_.settingsRevision,realFrameIndex_+1},context_.fence());gpuTimer_.mark(list,GpuStage::Color);

    if(desc_.rgbInput){
        if(frame->format!=AV_PIX_FMT_RGBA&&frame->format!=AV_PIX_FMT_BGRA){
            veyra::log::error("graph","direct RGB input contract requires RGBA/BGRA frame");return false;
        }
        if(frame->width!=int(srcW_)||frame->height!=int(srcH_))return false;
        for(uint32_t y=0;y<srcH_;++y){
            auto* dst=mappedRgb_[parity]+y*rgbPitch_;const auto* src=frame->data[0]+ptrdiff_t(y)*frame->linesize[0];
            if(frame->format==AV_PIX_FMT_RGBA)std::memcpy(dst,src,size_t(srcW_)*4);
            else for(uint32_t x=0;x<srcW_;++x){dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];}
        }
        tracker_.transition(list,rgbTex_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=rgbTex_.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upRgb_[parity].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,srcW_,srcH_,1,UINT(rgbPitch_)};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        tracker_.transition(list,rgbTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else if (frame->format == AV_PIX_FMT_D3D12) {
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
        const int colorSpace=resolved.matrix==YuvMatrix::BT601?SWS_CS_ITU601:SWS_CS_ITU709;
        const int* coefficients=sws_getCoefficients(colorSpace);
        const int full=resolved.range==ColorRange::Full?1:0;
        if(sws_setColorspaceDetails(nv12Ctx_,coefficients,full,coefficients,full,0,1<<16,1<<16)<0)return false;
        // Planar 8-bit YUV already exposes CPU luma for cadence analysis.
        // Convert directly into the fenced upload slot instead of writing and
        // copying another full NV12 frame. Never sample write-combined memory.
        const bool directUpload=frame->format==AV_PIX_FMT_YUV420P||frame->format==AV_PIX_FMT_YUVJ420P||frame->format==AV_PIX_FMT_NV12;
        uint8_t* planes[2] = { directUpload?mappedLuma_[parity]:nv12Buf_.data(), directUpload?mappedChroma_[parity]:nv12Buf_.data() + lumaSize_ };
        const int strides[2] = { static_cast<int>(lumaPitch_), static_cast<int>(chromaPitch_) };
        sws_scale(nv12Ctx_, frame->data, frame->linesize, 0, frame->height, planes, strides);
        std::vector<uint8_t> sample;sample.reserve(64*36);std::vector<double> hist(256,0);double sad=0;
        for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x){const uint8_t v=directUpload?frame->data[0][ptrdiff_t(y*srcH_/36)*frame->linesize[0]+x*srcW_/64]:planes[0][size_t(y*srcH_/36)*lumaPitch_+x*srcW_/64];hist[v]+=1.0/(64*36);sample.push_back(v);}
        if(previousLuma_.size()==sample.size())for(size_t i=0;i<sample.size();++i)sad+=std::abs(int(sample[i])-int(previousLuma_[i]))/(255.0*sample.size());
        cadence_.observe(ptsMs,sad,previousLuma_.size()==sample.size());
        out.measuredContentRate=cadence_.confirmedRate(desc_.contentRate);
        if(cadence_.conflicts(desc_.contentRate)&&realFrameIndex_%60==0)veyra::log::warn("cadence","requested content-rate identification conflicts with observed motion; preserving source timestamps");
        out.contentDuplicate=desc_.contentRate!=engine::ContentRate::Transport&&previousLuma_.size()==sample.size()&&sad<0.0001;
        const auto analysis=scene_.analyze(realFrameIndex_,hist,sad,static_cast<uint64_t>(std::max(0.0,ptsMs)*1000));
        if(analysis.isSceneCut||analysis.isCadenceBreak){reset=true;prevValid_=false;if(analysis.isSceneCut)++metrics_.sceneCutCount;}
        previousLuma_=std::move(sample);
        if(!directUpload){for (uint32_t y = 0; y < srcH_; ++y)
            std::memcpy(mappedLuma_[parity] + y * lumaPitch_, planes[0] + y * lumaPitch_, srcW_);
        for (uint32_t y = 0; y < (srcH_+1) / 2; ++y)
            std::memcpy(mappedChroma_[parity] + y * chromaPitch_, planes[1] + y * chromaPitch_, ((srcW_+1)/2)*2);}
        auto copyPlane = [&](ID3D12Resource* texture, ID3D12Resource* upload,
                             DXGI_FORMAT format, UINT width, UINT height, UINT pitch) {
            tracker_.transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
            dst.pResource = texture;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource = upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint = {format, width, height, 1, pitch};
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        };
        copyPlane(lumaTex_.Get(), upLuma_[parity].Get(), DXGI_FORMAT_R8_UNORM,
                  srcW_, srcH_, static_cast<UINT>(lumaPitch_));
        copyPlane(chromaTex_.Get(), upChroma_[parity].Get(), DXGI_FORMAT_R8G8_UNORM,
                  (srcW_+1)/2, (srcH_+1)/2, static_cast<UINT>(chromaPitch_));
        tracker_.transition(list, lumaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, chromaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("upload");

    // 2. YUV -> RGBA16F.
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(desc_.rgbInput){
        const float c[8]={uintBits(srcW_),uintBits(srcH_),uintBits(transferCode(resolved.transfer)),0,0,0,0,0};
        rgbPass_.bind(list,c,gpuHandleOf(rgbPass_,0).ptr,gpuHandleOf(rgbPass_,1).ptr);
        list->Dispatch((srcW_+15)/16,(srcH_+15)/16,1);
    }else{
        const float constants[8] = { resolved.range==ColorRange::Full?0.0f:1.0f,
            resolved.matrix==YuvMatrix::BT601?0.0f:1.0f,
            float(transferCode(resolved.transfer)), 0.0f,
            uintBits(srcW_), uintBits(srcH_), 0.0f, 0.0f };
        yuvPass_.bind(list, constants, gpuHandleOf(yuvPass_, 0).ptr, gpuHandleOf(yuvPass_, 2).ptr);
        list->Dispatch((srcW_ + 15) / 16, (srcH_ + 15) / 16, 1);
    }
    tracker_.uavBarrier(list, srcRgba_.Get());
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (nv12Texture != nullptr) {
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_COMMON);
    }
    gpuTimer_.mark(list,GpuStage::Color,true);
    if (desc_.stageMark) desc_.stageMark("yuv");

    // Guidance from original post-SR color BEFORE NR. Queue waits are GPU-side.
    bool haveFlow = false;
    const bool runMotion = nvofStandalone_ || fgEnabled_ || srEnabled_;
    if (runMotion && nvof_ && nvof_->initialized()) {
        const float dims[8] = {uintBits(nvofW_),uintBits(nvofH_),uintBits(nvofW_),uintBits(nvofH_),0,0,0,0};
        if (prevValid_) {
            tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            blitPass_.bind(list,dims,gpuHandleOf(blitPass_,5).ptr,gpuHandleOf(blitPass_,6).ptr);
            list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
            tracker_.uavBarrier(list,nvofInA_.Get());
            tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_COMMON);
        }
        tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float encodedDims[8] = {uintBits(nvofW_),uintBits(nvofH_),uintBits(nvofW_),uintBits(nvofH_),1,0,0,0};
        blitPass_.bind(list,encodedDims,gpuHandleOf(blitPass_,15).ptr,gpuHandleOf(blitPass_,9).ptr);
        list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
        tracker_.uavBarrier(list,nvofInB_.Get());
        tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if (prevValid_) {
            gpuTimer_.mark(list,GpuStage::Flow);
            if(!ring_.submitAndSignal(slot))return false;
            haveFlow=nvof_->execute(ring_.lastSignaledValue(),st);
            if(!haveFlow) { ++metrics_.nvofFrameFailures; mvecSource_="zero-motion-fallback (NVOF execute failed)"; }
            else {
                ++metrics_.nvofExecuteCount;
                if(FAILED(context_.directQueue()->Wait(nvofOutFence_.Get(),nvof_->nextOutValue()-1)))return false;
            }
            list=ring_.acquireNext(slot,st);if(!list)return false;
            gpuTimer_.mark(list,GpuStage::Flow,true);
            if(haveFlow) {
                tracker_.transition(list,nvofRawTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofCostTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                const float dc[8]={uintBits(rawW_),uintBits(rawH_),uintBits(nvofW_),uintBits(nvofH_),
                    uintBits(selectedGrid_),uintBits(0),uintBits(32),0};
                densifyPass_.bind(list,dc,gpuHandleOf(densifyPass_,0).ptr,gpuHandleOf(densifyPass_,2).ptr);
                list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
                tracker_.uavBarrier(list,flowTex_.Get());tracker_.uavBarrier(list,confTex_.Get());
                tracker_.transition(list,nvofRawTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofCostTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            }
        }
    }

    // 3. SR into workRgba (or 1:1 blit bypass).
    if(haveFlow){
        auto adapt=[&](ID3D12Resource* output,uint32_t w,uint32_t h,uint32_t descriptor){
            tracker_.transition(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float c[8]={uintBits(nvofW_),uintBits(nvofH_),uintBits(w),uintBits(h),0,0,0,0};
            flowAdaptPass_.bind(list,c,gpuHandleOf(flowAdaptPass_,0).ptr,gpuHandleOf(flowAdaptPass_,descriptor).ptr);
            list->Dispatch((w+15)/16,(h+15)/16,1);tracker_.uavBarrier(list,output);tracker_.transition(list,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };
        adapt(nrFlow_.Get(),nrW_,nrH_,1);adapt(baseFlow_.Get(),workW_,workH_,2);
    }
    if(reset){++metrics_.resetCount;++epoch_;}
    out.batch.batchId=realFrameIndex_+1;out.batch.identity={epoch_,desc_.settingsRevision,sourceFrameId};
    out.batch.a100ns=static_cast<int64_t>(std::llround(prevPtsMs_*10000));
    out.batch.b100ns=static_cast<int64_t>(std::llround(ptsMs*10000));gpuTimer_.identity(out.batch.identity);
    if (srEnabled_ && srBackend_ && srBackend_->created()) {
        gpuTimer_.mark(list,GpuStage::Sr);
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssSrBackend::EvalDesc ed{};
        ed.color = srcRgba_.Get();
        ed.output = workRgba_.Get();
        ed.depth = nrZeroDepth_.Get();
        ed.motionVectors = haveFlow?baseFlow_.Get():nrZeroMotion_.Get();
        ed.reset = reset;
        if (!srBackend_->evaluate(list, ngxParams_, ed, st)) {
            veyra::log::error("graph", "sr evaluate failed");
            return false;
        }
        ++metrics_.srEvaluateCount;gpuTimer_.mark(list,GpuStage::Sr,true);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            uintBits(srcW_), uintBits(srcH_),
            uintBits(workW_), uintBits(workH_), 0, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, 0).ptr, gpuHandleOf(blitPass_, 1).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("sr");
    // GPU copies retain exact pre-enhancement references under the real lease.
    auto retain=[&](ID3D12Resource* src,ID3D12Resource* dst){tracker_.transition(list,src,D3D12_RESOURCE_STATE_COPY_SOURCE);tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(dst,src);tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COMMON);tracker_.transition(list,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
    retain(srcRgba_.Get(),sourceReferences_[parity].Get());retain(workRgba_.Get(),baseReferences_[parity].Get());


    // 4a. Parity encode.
    if (nrEnabled_ && nrHandle_ != nullptr) {
        tracker_.transition(list,nrInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float down[8]={uintBits(workW_),uintBits(workH_),uintBits(nrW_),uintBits(nrH_),0,0,0,0};
        downsamplePass_.bind(list,down,gpuHandleOf(downsamplePass_,0).ptr,gpuHandleOf(downsamplePass_,1).ptr);
        list->Dispatch((nrW_+15)/16,(nrH_+15)/16,1);tracker_.uavBarrier(list,nrInput_.Get());tracker_.transition(list,nrInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
            uintBits(nrW_), uintBits(nrH_), 0.0f, 0.0f };
        encPass_.bind(list, constants, gpuHandleOf(encPass_, 0).ptr, gpuHandleOf(encPass_, 1).ptr);
        list->Dispatch((nrW_ + 15) / 16, (nrH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, proxyTex_.Get());
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (desc_.stageMark) desc_.stageMark("encode");

        // 4b. NR evaluate on a FRESH list (snippet constraint).
        if (!ring_.submitAndSignal(slot)) { veyra::log::error("graph", "submit before nr"); return false; }
        ID3D12GraphicsCommandList* nlist = ring_.acquireNext(slot, st);
        if (nlist == nullptr) { veyra::log::error("graph", "acquire nr list"); return false; }
        {
            namespace p = ngx::dlssnr;
            ngx::ParameterBlock pb(ngxParams_);
            pb.setD3D12Resource(p::kColor, proxyTex_.Get());
            pb.setD3D12Resource(p::kOutput, neuralTex_.Get());
            pb.setD3D12Resource(p::kMVec, haveFlow ? nrFlow_.Get() : nrZeroMotion_.Get());
            pb.setD3D12Resource(p::kDepth, nrZeroDepth_.Get());
            pb.setU32(p::kColorSubrectWidth, nrW_); pb.setU32(p::kColorSubrectHeight, nrH_);
            pb.setU32(p::kOutputSubrectWidth, nrW_); pb.setU32(p::kOutputSubrectHeight, nrH_);
            pb.setU32(p::kMVecSubrectWidth, nrW_); pb.setU32(p::kMVecSubrectHeight, nrH_);
            pb.setU32(p::kDepthSubrectWidth, nrW_); pb.setU32(p::kDepthSubrectHeight, nrH_);
            pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
            pb.setI32(p::kDepthInverted, 1);
            pb.setI32(p::kIndicatorInvertX, 0);
            pb.setI32(p::kIndicatorInvertY, 0);
            pb.setI32(p::kEnabled, 1);
            pb.setI32(p::kReset, reset ? 1 : 0);
            pb.setI32(p::kStyle, desc_.model.style);
            pb.setF32(p::kIntensity, desc_.model.intensity);
            pb.setF32(p::kLocalToneStrength, desc_.model.tone);
            pb.setF32(p::kLocalStructureStrength, desc_.model.structure);
            pb.setF32(p::kSkinStructureStrength, desc_.model.skin);
            pb.setI32(p::kUseAutoMask, desc_.model.autoMask);
            pb.setI32(p::kUICorrection, desc_.model.uiCorrection);
            static bool injectedFailureConsumed=false;
            if(!injectedFailureConsumed&&desc_.model.style==2&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_STYLE2",nullptr,0)){
                injectedFailureConsumed=true;veyra::log::error("settings-test","injected NR execution rejection before NGX; rollback exercise, not a hardware failure");return false;
            }
            gpuTimer_.mark(nlist,GpuStage::Nr);
            uint64_t er = 0; uint32_t es = 0;
            if (!nrAdapter_->snippetEvaluateFeature(nlist, nrHandle_, ngxParams_, er, es) ||
                er != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                diagnostic.stage="NR Evaluate";diagnostic.ngx=er;diagnostic.seh=es;Logger::diagnosticContext(diagnostic);veyra::log::error("graph", std::format("NR evaluate failed 0x{:X} seh={}", er, es));
                return false;
            }
            gpuTimer_.mark(nlist,GpuStage::Nr,true);
            ++metrics_.nrEvaluateCount;
            if(haveFlow)++metrics_.nrMotionFrames;
            tracker_.uavBarrier(nlist, neuralTex_.Get());
            tracker_.transition(nlist, neuralTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            wchar_t profileFlag[2]{};
            if(GetEnvironmentVariableW(L"VEYRA_PROFILE_SPLIT",profileFlag,2)&&profileFlag[0]==L'1'){
                ring_.tag(slot,"nr-only");
                if(!ring_.submitAndSignal(slot))return false;
                nlist=ring_.acquireNext(slot,st);if(!nlist)return false;
                ring_.tag(slot,"decode-output-fg");
            }
            // 4c. Parity decode.
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
                uintBits(nrW_), uintBits(nrH_), 0.0f, 0.0f };
            decPass_.bind(nlist, constants, gpuHandleOf(decPass_, 0).ptr, gpuHandleOf(decPass_, 3).ptr);
            nlist->Dispatch((nrW_ + 15) / 16, (nrH_ + 15) / 16, 1);
            tracker_.uavBarrier(nlist, finalRgba_.Get());
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        list = nlist; // continue recording on the NR list
    }
    if (desc_.stageMark) desc_.stageMark("nr");

    if(nrEnabled_&&nrHandle_){
        gpuTimer_.mark(list,GpuStage::Residual);
        tracker_.transition(list,residualRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto& r=desc_.residual;const float c[8]={r.total,r.darken,r.brighten,r.color,r.luminance,0,0,0};
        residualPass_.bind(list,c,gpuHandleOf(residualPass_,0).ptr,gpuHandleOf(residualPass_,3).ptr);
        list->Dispatch((workW_+15)/16,(workH_+15)/16,1);tracker_.uavBarrier(list,residualRgba_.Get());tracker_.transition(list,residualRgba_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);gpuTimer_.mark(list,GpuStage::Residual,true);
    }

    // 5. Working frame -> SDR RGBA8 videoFrame[parity] + NVOF chain.
    {
        const UINT srcSlot = 2;
        const UINT uavSlot = 3 + parity;
        tracker_.transition(list, (nrEnabled_ && nrHandle_ != nullptr) ? residualRgba_.Get() : workRgba_.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            uintBits(workW_), uintBits(workH_),
            uintBits(workW_), uintBits(workH_), 1, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, srcSlot).ptr, gpuHandleOf(blitPass_, uavSlot).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, videoFrame_[parity].Get());
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_COMMON);

    }
    if(!fgEnabled_)gpuTimer_.resolve(list);
    if (!ring_.submitAndSignal(slot)) return false;
    if(!fgEnabled_)gpuTimer_.submitted(ring_.lastSignaledValue());
    out.videoFenceValue = ring_.lastSignaledValue();

    // FG reads enhanced SDR color, not the pre-NR guidance input.
    if (fgEnabled_ && fgBackend_ && fgBackend_->created()) {
      for(uint32_t sub=1;sub<desc_.fgMultiplier;++sub){
        const uint32_t generatedSlot=parity+(sub-1)*2;
        auto* flist=ring_.acquireNext(slot,st); if(!flist)return false;
        auto* motion=haveFlow?baseFlow_.Get():nrZeroMotion_.Get();
        tracker_.transition(flist,videoFrame_[parity].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,depthTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,genFrame_[generatedSlot].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // One status resource for the whole input-frame pair, just like the
        // unchanged input parameters. Later MFG calls may retain its value.
        if(sub==1){
            tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
            flist->CopyBufferRegion(fgDisable_[parity].Get(),0,fgDisableInit_.Get(),0,4);
        }
        tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.backbuffer=videoFrame_[parity].Get(); fe.depth=depthTex_.Get(); fe.mvecs=motion;
        fe.outputInterpolated=genFrame_[generatedSlot].Get(); fe.reset=reset||!prevValid_;
        fe.outputDisableInterpolation=fgDisable_[parity].Get();
        fe.multiFrameCount=desc_.fgMultiplier-1;fe.multiFrameIndex=sub;
        fe.frameId=realFrameIndex_+1;
        // FG consumer adapter: current->previous pixel flow -> normalized reverse flow.
        fe.mvecScaleX=haveFlow?-1.0f/static_cast<float>(workW_):1.0f;
        fe.mvecScaleY=haveFlow?-1.0f/static_cast<float>(workH_):1.0f;
        if(sub==1)gpuTimer_.mark(flist,GpuStage::FgBatch);
        const auto timingStage=static_cast<GpuStage>(unsigned(GpuStage::Fg1)+sub-1);gpuTimer_.mark(flist,timingStage);
        if(!fgBackend_->evaluate(flist,ngxParams_,fe,st))return false;
        gpuTimer_.mark(flist,timingStage,true);if(sub==desc_.fgMultiplier-1)gpuTimer_.mark(flist,GpuStage::FgBatch,true);
        tracker_.uavBarrier(flist,genFrame_[generatedSlot].Get());
        tracker_.uavBarrier(flist,fgDisable_[parity].Get());
        tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        flist->CopyBufferRegion(fgDisableReadback_[generatedSlot].Get(),0,fgDisable_[parity].Get(),0,4);
        tracker_.transition(flist,videoFrame_[parity].Get(),D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist,genFrame_[generatedSlot].Get(),D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist,depthTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(sub==desc_.fgMultiplier-1)gpuTimer_.resolve(flist);
        if(!ring_.submitAndSignal(slot))return false;
        if(sub==desc_.fgMultiplier-1)gpuTimer_.submitted(ring_.lastSignaledValue());
        out.hasGenerated=prevValid_&&!reset;
        out.genSlot=parity; out.genFenceValue=ring_.lastSignaledValue();
        out.generatedPtsMs=(prevPtsMs_+ptsMs)*0.5;
        if(out.hasGenerated){
            ++metrics_.fgSubmittedCandidates;
            BatchFrame f;f.identity=out.batch.identity;f.kind=FrameKind::Generated;f.validity=GenerationValidity::Pending;f.subframe=sub;
            f.pts100ns=FrameBatch::interpolate(out.batch.a100ns,out.batch.b100ns,sub,desc_.fgMultiplier);
            f.lease=std::make_shared<FrameLease>();f.lease->texture=genFrame_[generatedSlot];f.lease->slot=generatedSlot;f.lease->readyFence=out.genFenceValue;generatedLeases_[generatedSlot]=f.lease;out.batch.append(std::move(f));
        }
      }
    }
    uploadFences_[parity]=ring_.lastSignaledValue();

    prevPtsMs_ = ptsMs;
    prevValid_ = true;
    ++realFrameIndex_;
    out.realFrameIndex = realFrameIndex_;
    out.videoSlot = parity;
    BatchFrame real;real.identity=out.batch.identity;real.pts100ns=out.batch.b100ns;real.lease=std::make_shared<FrameLease>();real.lease->texture=videoFrame_[parity];real.lease->sourceReference=sourceReferences_[parity];real.lease->baseReference=baseReferences_[parity];real.lease->slot=parity;real.lease->readyFence=out.videoFenceValue;realLeases_[parity]=real.lease;out.batch.append(std::move(real));
    veyra::log::info("frame-batch",std::format("batch={} epoch={} revision={} source={} a={} b={} count={} realSlot={} realFence={} genSlot={} genFence={}",out.batch.batchId,epoch_,out.batch.identity.settingsRevision,out.batch.identity.sourceFrameId,out.batch.a100ns,out.batch.b100ns,out.batch.count,parity,out.videoFenceValue,out.genSlot,out.genFenceValue));
    return true;
}

bool EnhanceGraph::resolveGeneration(FrameOutputs& out)
{
    if(!out.hasGenerated)return true;
    if(context_.fence()->GetCompletedValue()<out.genFenceValue)return false;
    bool anyValid=false;
    for(uint32_t i=0;i<out.batch.count;++i){
    auto& frame=out.batch.frames[i];if(frame.kind!=FrameKind::Generated)continue;
    if(frame.validity!=GenerationValidity::Pending){anyValid|=frame.validity==GenerationValidity::Valid;continue;}
    void* data=nullptr;D3D12_RANGE range{0,4};
    HRESULT hr=fgDisableReadback_[frame.lease->slot]->Map(0,&range,&data);
    if(FAILED(hr)){frame.validity=GenerationValidity::Failed;out.hasGenerated=false;veyra::log::error("fg-status",std::format("Map hr=0x{:X}",unsigned(hr)));return true;}
    const uint32_t disabled=*static_cast<uint8_t*>(data); // SDK writes the first byte only
    D3D12_RANGE written{0,0};fgDisableReadback_[frame.lease->slot]->Unmap(0,&written);
    const bool rejected=disabled||out.contentDuplicate;
    frame.validity=rejected?GenerationValidity::Disabled:GenerationValidity::Valid;
    anyValid|=!rejected;if(rejected)++metrics_.fgDisabledFrames;else ++metrics_.fgGeneratedFrames;
    veyra::log::info("fg-status",std::format("batch={} frame={} epoch={} revision={} subframe={} fence={} disable={} valid={}",out.batch.batchId,out.realFrameIndex,out.batch.identity.epoch,out.batch.identity.settingsRevision,frame.subframe,frame.lease->readyFence,disabled,!rejected));
    }
    out.hasGenerated=anyValid;return true;
}

bool EnhanceGraph::applySettings(const engine::EnhancementSettings& s){
    if(!s.validate().empty()||(s.multiplier>1&&(!fgCapsAvailable_||s.multiplier-1>uint32_t(fgMultiFrameMax_))))return false;
    desc_.contentRate=s.content;desc_.model=s.model;desc_.residual=s.residual;desc_.settingsRevision=s.revision;
    desc_.fgMultiplier=std::max(2u,s.multiplier);desc_.enableNvofStandalone=s.nr&&!desc_.stillImage;nvofStandalone_=desc_.enableNvofStandalone;
    setNrEnabled(s.nr);setFgEnabled(s.multiplier>1);
    veyra::log::info("settings",std::format("requested revision={} intensity={} tone={} structure={} skin={} style={} autoMask={} UI={} residual={}/{}/{}/{}/{} multiplier={}",s.revision,s.model.intensity,s.model.tone,s.model.structure,s.model.skin,s.model.style,s.model.autoMask,s.model.uiCorrection,s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance,s.multiplier));
    return true;
}
void EnhanceGraph::setNrEnabled(bool on)
{
    nrEnabled_ = on && nrHandle_ != nullptr;
    // Refresh the section-5 blit SRV exactly as the probe did at runtime
    // (direct write into the visible heap - proven safe for this refresh).
    makeSrv(context_.device(),
        nrEnabled_ ? residualRgba_.Get() : workRgba_.Get(),
        DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(blitPass_, 2));
}

ID3D12Fence* EnhanceGraph::contextFence()const{return context_.fence();}
uint32_t EnhanceGraph::actualFlowPerf()const{return nvof_?nvof_->caps().actualPerfLevel:0;}
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
    return slot < 6 ? genFrame_[slot].Get() : nullptr;
}

// ---------------------------------------------------------------------------
// shutdown: s10 ownership-order teardown of everything the graph owns.
// The NVOF out-fence drain must ALREADY have happened (caller), while the
// ring and fences were alive.
// ---------------------------------------------------------------------------
void EnhanceGraph::shutdown()
{
    if (!initialized_ && !nrAdapter_ && !nvof_ && !srcRgba_) return;
    (void)ring_.drainQueue();(void)ring_.discardRecording();
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
    decPass_ = ComputePass{};
    rgbPass_={};rgbTex_.Reset();
    for(unsigned i=0;i<2;++i){if(upRgb_[i]&&mappedRgb_[i])upRgb_[i]->Unmap(0,nullptr);mappedRgb_[i]=nullptr;upRgb_[i].Reset();}
    downsamplePass_={};residualPass_={};flowAdaptPass_={};
    nrInput_.Reset();residualRgba_.Reset();nrFlow_.Reset();baseFlow_.Reset();
    for(unsigned i=0;i<6;++i){fgDisable_[i].Reset();fgDisableReadback_[i].Reset();generatedLeases_[i].reset();genFrame_[i].Reset();}for(auto& lease:realLeases_)lease.reset();fgDisableInit_.Reset();
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
    for(auto& r:sourceReferences_)r.Reset();for(auto& r:baseReferences_)r.Reset();workRgba_.Reset();
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
    gpuTimer_.close();initialized_ = false;
    veyra::log::info("graph", "shutdown complete (ordered)");
    (void)st;
}

} // namespace veyra::pipeline
