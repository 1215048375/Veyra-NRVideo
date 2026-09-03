// veyra_media_probe - Phase 3 media pipeline probe.
// Modes:
//   --input <abs> --mode software --frames N   software decode baseline + stats
//                                        (built only with the openh264 toolchain)
//   --input <abs> --mode software --frames N   software decode baseline + stats
//   --input <abs> --mode seek-storm --seeks N  seek/reset behaviour
//   --input <abs> --mode d3d12va ...           arrives with P3.3
// JSON summary contract: scripts/gates/phase3.ps1 section 5-8.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/gfx/CommandSlotRing.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext_d3d12va.h>
}

namespace {
// --- YUV→RGB GPU pipeline resources (P3.4) ---
struct YuvToRgbPipeline {
    veyra::gfx::ComPtr<ID3D12RootSignature> rootSignature;
    veyra::gfx::ComPtr<ID3D12PipelineState> pipelineState;
    veyra::gfx::ComPtr<ID3D12DescriptorHeap> srvHeap;
    veyra::gfx::ComPtr<ID3D12DescriptorHeap> uavHeap;
    veyra::gfx::ComPtr<ID3D12Resource> outputTexture; // linear RGB FP16
    UINT srvIncrement = 0;
    UINT uavIncrement = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t dispatchCount = 0;
};

bool createYuvToRgbPipeline(veyra::gfx::D3D12DeviceContext& context, uint32_t width, uint32_t height,
    YuvToRgbPipeline& pipeline)
{
    pipeline.width = width;
    pipeline.height = height;

    // Load the build-time compiled shader.
    const std::string shaderPath = VEYRA_SHADER_DIR "/YuvToLinearRgb.dxil";
    HANDLE shaderFile = CreateFileA(shaderPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (shaderFile == INVALID_HANDLE_VALUE) {
        veyra::log::error("media-probe", std::format("d3d12va: shader missing path={}", shaderPath));
        return false;
    }
    LARGE_INTEGER shaderSize{};
    GetFileSizeEx(shaderFile, &shaderSize);
    std::vector<uint8_t> shaderBytes(static_cast<size_t>(shaderSize.QuadPart));
    DWORD shaderRead = 0;
    ReadFile(shaderFile, shaderBytes.data(), static_cast<DWORD>(shaderBytes.size()), &shaderRead, nullptr);
    CloseHandle(shaderFile);
    if (shaderBytes.empty()) {
        return false;
    }

    // Root signature: b0 (8 constants) + SRV table (3) + UAV table (1).
    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    D3D12_DESCRIPTOR_RANGE1 uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    D3D12_ROOT_PARAMETER1 rootParams[3]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 8;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &uavRange;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootDesc.Desc_1_1.NumParameters = 3;
    rootDesc.Desc_1_1.pParameters = rootParams;
    veyra::gfx::ComPtr<ID3DBlob> signature;
    veyra::gfx::ComPtr<ID3DBlob> signatureError;
    if (FAILED(D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &signatureError))) {
        return false;
    }
    if (FAILED(context.device()->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&pipeline.rootSignature)))) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipeline.rootSignature.Get();
    psoDesc.CS.pShaderBytecode = shaderBytes.data();
    psoDesc.CS.BytecodeLength = shaderBytes.size();
    if (FAILED(context.device()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.pipelineState)))) {
        veyra::log::error("media-probe", "d3d12va: YuvToLinearRgb PSO creation failed");
        return false;
    }

    // Single heap: SRV slots 0-1 (luma+chroma), UAV slot 2 (output).
    // D3D12 allows exactly ONE CBV/SRV/UAV heap per command list.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 8;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(context.device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pipeline.srvHeap)))) {
        return false;
    }
    pipeline.srvIncrement = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Output texture: linear RGB FP16.
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC outputDesc{};
    outputDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    outputDesc.Width = width;
    outputDesc.Height = height;
    outputDesc.DepthOrArraySize = 1;
    outputDesc.MipLevels = 1;
    outputDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    outputDesc.SampleDesc.Count = 1;
    outputDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(context.device()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
            &outputDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&pipeline.outputTexture)))) {
        return false;
    }

    // UAV for the output (slot 2 in the single heap).
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = { pipeline.srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + 2ull * pipeline.srvIncrement };
    context.device()->CreateUnorderedAccessView(pipeline.outputTexture.Get(), nullptr, &uavDesc, uavCpu);

    veyra::log::info("media-probe", std::format("d3d12va: YuvToLinearRgb pipeline created ({}x{})", width, height));
    return true;
}

// Dispatch the YUV→RGB shader for one D3D12VA frame (NV12 texture).
bool dispatchYuvToRgb(veyra::gfx::D3D12DeviceContext& context,
    veyra::gfx::CommandSlotRing& ring,
    YuvToRgbPipeline& pipeline,
    ID3D12Resource* nv12Texture, int subresourceIndex,
    int slotIndex, veyra::Status& status)
{
    ID3D12GraphicsCommandList* list = ring.acquire(slotIndex, status);
    if (list == nullptr) {
        return false;
    }

    // Create per-frame SRVs for the NV12 planes (luma R8 + chroma R8G8).
    // Query the texture description to pick the correct SRV dimension
    // (texture array vs single 2D) and log what FFmpeg gave us.
    D3D12_RESOURCE_DESC texDesc = nv12Texture->GetDesc();
    const bool isArray = texDesc.DepthOrArraySize > 1;
    veyra::log::info("media-probe", std::format("d3d12va: NV12 tex desc {}x{} array={} depth={} fmt={} planes={}",
        texDesc.Width, texDesc.Height, isArray, texDesc.DepthOrArraySize,
        static_cast<int>(texDesc.Format), static_cast<int>(texDesc.Layout)));

    D3D12_CPU_DESCRIPTOR_HANDLE srvBase = pipeline.srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC lumaSrv{};
    lumaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lumaSrv.Format = DXGI_FORMAT_R8_UNORM;
    if (isArray) {
        lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        lumaSrv.Texture2DArray.MostDetailedMip = 0;
        lumaSrv.Texture2DArray.MipLevels = 1;
        lumaSrv.Texture2DArray.FirstArraySlice = subresourceIndex;
        lumaSrv.Texture2DArray.ArraySize = 1;
        lumaSrv.Texture2DArray.PlaneSlice = 0;
    }
    else {
        lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        lumaSrv.Texture2D.MostDetailedMip = 0;
        lumaSrv.Texture2D.MipLevels = 1;
        lumaSrv.Texture2D.PlaneSlice = 0;
    }
    context.device()->CreateShaderResourceView(nv12Texture, &lumaSrv, srvBase);

    D3D12_SHADER_RESOURCE_VIEW_DESC chromaSrv{};
    chromaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    chromaSrv.Format = DXGI_FORMAT_R8G8_UNORM;
    if (isArray) {
        chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        chromaSrv.Texture2DArray.MostDetailedMip = 0;
        chromaSrv.Texture2DArray.MipLevels = 1;
        chromaSrv.Texture2DArray.FirstArraySlice = subresourceIndex;
        chromaSrv.Texture2DArray.ArraySize = 1;
        chromaSrv.Texture2DArray.PlaneSlice = 1;
    }
    else {
        chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        chromaSrv.Texture2D.MostDetailedMip = 0;
        chromaSrv.Texture2D.MipLevels = 1;
        chromaSrv.Texture2D.PlaneSlice = 1;
    }
    context.device()->CreateShaderResourceView(nv12Texture, &chromaSrv, { srvBase.ptr + pipeline.srvIncrement });

    // Resource barriers: NV12 -> NON_PIXEL_SHADER_RESOURCE, output -> UAV.
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = nv12Texture;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = pipeline.outputTexture.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    // Only transition the NV12 (output stays UAV between frames).
    list->ResourceBarrier(1, barriers);

    // Bind and dispatch (single heap for both SRV and UAV tables).
    ID3D12DescriptorHeap* heaps[] = { pipeline.srvHeap.Get() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetPipelineState(pipeline.pipelineState.Get());
    list->SetComputeRootSignature(pipeline.rootSignature.Get());
    // colorParams: limitedRange=1, matrix709=1, transferSRGB=1, padding
    const float colorParams[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
    const uint32_t dims[4] = { pipeline.width, pipeline.height, 0, 0 };
    const float constants[8] = { colorParams[0], colorParams[1], colorParams[2], colorParams[3],
        static_cast<float>(dims[0]), static_cast<float>(dims[1]), 0.0f, 0.0f };
    list->SetComputeRoot32BitConstants(0, 8, constants, 0);
    list->SetComputeRootDescriptorTable(1, pipeline.srvHeap->GetGPUDescriptorHandleForHeapStart());
    // UAV table at slot 2 in the same heap.
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = { pipeline.srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + 2ull * pipeline.srvIncrement };
    list->SetComputeRootDescriptorTable(2, uavGpu);
    list->Dispatch((pipeline.width + 15) / 16, (pipeline.height + 15) / 16, 1);

    // Barrier NV12 back to COMMON (for FFmpeg's next decode use).
    D3D12_RESOURCE_BARRIER back{};
    back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    back.Transition.pResource = nv12Texture;
    back.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &back);

    if (!ring.submitAndSignal(slotIndex)) {
        return false;
    }
    ++pipeline.dispatchCount;
    return true;
}

} // namespace

#include "../nr_harness/harness_util.h"

namespace {

int g_infoQueueStored = 0;
int g_infoQueueErrors = 0;
bool g_infoQueueActive = false;

void drainInfoQueue(veyra::gfx::D3D12DeviceContext& context)
{
#if defined(VEYRA_D3D12_DEBUG)
    veyra::gfx::ComPtr<ID3D12InfoQueue> queue;
    if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&queue)))) {
        g_infoQueueActive = true;
        const uint64_t stored = queue->GetNumStoredMessages();
        for (uint64_t i = 0; i < stored && i < 50; ++i) {
            SIZE_T length = 0;
            if (FAILED(queue->GetMessage(i, nullptr, &length)) || length == 0) {
                continue;
            }
            std::vector<uint8_t> buffer(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (FAILED(queue->GetMessage(i, message, &length))) {
                continue;
            }
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                ++g_infoQueueErrors;
                veyra::log::error("media-probe", std::format("infoqueue id={} desc={}",
                    static_cast<unsigned>(message->ID), message->pDescription));
            }
            ++g_infoQueueStored;
        }
    }
#else
    (void)context;
#endif
}

std::string jsonEscapeLocal(const std::string& text)
{
    return veyra::harness::util::jsonEscape(text);
}

// Deterministic software-decode baseline (Phase 3A).
int runSoftwareDecode(const std::wstring& input, uint32_t frames, const std::string& runId,
    const std::wstring& jsonFile)
{
    veyra::gfx::D3D12DeviceContext context; // observability + the future upload target
    veyra::gfx::DeviceContextDesc desc{};
#if defined(VEYRA_D3D12_DEBUG)
    desc.enableDebugLayer = true;
#endif
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        return 6;
    }

    veyra::media::FFmpegDemuxer demuxer;
    if (!demuxer.open(input)) {
        context.shutdown();
        return 7;
    }
    veyra::media::FFmpegVideoDecoder decoder;
    if (!decoder.openSoftware(demuxer.videoCodecParameters(), demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen())) {
        context.shutdown();
        return 8;
    }

    // PTS-driven bounded pump: at most 4 packets in flight before a frame
    // must come out (Playbook: bounded queues, no fixed-FPS guessing).
    uint64_t framesDecoded = 0;
    uint32_t maxInFlight = 0;
    uint32_t inFlight = 0;
    bool endOfFile = false;
    while (framesDecoded < frames && !endOfFile) {
        if (inFlight < 4) {
            bool eof = false;
            if (demuxer.readVideoPacket(eof)) {
                if (!decoder.sendPacket(demuxer.currentPacket())) {
                    context.shutdown();
                    return 9;
                }
                ++inFlight;
            }
            else if (eof) {
                endOfFile = true;
                (void)decoder.sendPacket(nullptr);
            }
            else {
                context.shutdown();
                return 9;
            }
        }
        while (const AVFrame* frame = decoder.receiveFrame()) {
            (void)frame;
            ++framesDecoded;
            if (inFlight > 0) {
                --inFlight;
            }
        }
        maxInFlight = std::max(maxInFlight, inFlight);
    }

    const auto& ds = decoder.stats();
    const auto& ms = demuxer.stats();
    drainInfoQueue(context);
    context.shutdown();

    veyra::log::info("media-probe", std::format("software: frames={} maxInFlight={} packets={} ptsNonMono(demux={} dec={})",
        framesDecoded, maxInFlight, ms.packetsRead, ms.ptsNonMonotonicCount, ds.ptsNonMonotonicCount));

    std::string json;
    json += "{\n";
    json += "  \"probe\": \"veyra_media_probe\",\n";
    json += std::format("  \"runId\": \"{}\",\n", jsonEscapeLocal(runId));
    json += std::format("  \"frames\": {},\n", framesDecoded);
    json += std::format("  \"decode\": {{\"framesDecoded\": {}, \"ptsNonMonotonicCount\": {}}},\n",
        ds.framesDecoded, ds.ptsNonMonotonicCount + ms.ptsNonMonotonicCount);
    json += std::format("  \"pipeline\": {{\"gpuReadbackCount\": 0, \"maxDecodeQueueDepth\": {}, \"maxProcessQueueDepth\": 0}},\n",
        maxInFlight);
    json += std::format("  \"hwaccel\": {{\"sharedVeyraDevice\": false, \"pixelFormat\": \"software\"}},\n");
    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    const bool ok = framesDecoded >= frames || endOfFile;
    veyra::log::info("media-probe", ok ? "software: PASS" : "software: FAIL");
    return ok ? 0 : 10;
}

// D3D12VA hardware decode on the shared Veyra device (Playbook 13.2).
int runD3d12VADecode(const std::wstring& input, uint32_t frames, const std::string& runId,
    const std::wstring& jsonFile)
{
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc desc{};
#if defined(VEYRA_D3D12_DEBUG)
    desc.enableDebugLayer = true;
#endif
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        return 6;
    }

    veyra::media::FFmpegDemuxer demuxer;
    if (!demuxer.open(input)) {
        context.shutdown();
        return 7;
    }
    veyra::media::FFmpegVideoDecoder decoder;
    if (!decoder.openD3D12VA(demuxer.videoCodecParameters(),
            demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen(),
            context.device(), context.directQueue())) {
        context.shutdown();
        return 8;
    }

    // P3.4: YUV→RGB GPU pipeline (shader dispatch per frame, zero readback).
    veyra::gfx::CommandSlotRing ring;
    if (!ring.initialize(context.device(), context.directQueue(), context.fence(), context.fenceEvent(), 4, status)) {
        context.shutdown();
        return 7;
    }
    YuvToRgbPipeline yuvPipeline;
    bool yuvPipelineReady = false;
    uint32_t frameSlot = 0;

    uint64_t framesDecoded = 0;
    uint32_t maxInFlight = 0;
    uint32_t inFlight = 0;
    bool endOfFile = false;
    while (framesDecoded < frames && !endOfFile) {
        bool eof = false;
        if (demuxer.readVideoPacket(eof)) {
            if (!decoder.sendPacket(demuxer.currentPacket())) {
                ring.shutdown();
                context.shutdown();
                return 9;
            }
            ++inFlight;
        }
        else if (eof) {
            endOfFile = true;
            (void)decoder.sendPacket(nullptr);
        }
        else {
            ring.shutdown();
            context.shutdown();
            return 9;
        }
        while (const AVFrame* avFrame = decoder.receiveFrame()) {
            ++framesDecoded;
            if (inFlight > 0) {
                --inFlight;
            }
            maxInFlight = std::max(maxInFlight, inFlight);
            // P3.4: dispatch YUV→RGB shader for each D3D12VA frame.
            if (avFrame->format == AV_PIX_FMT_D3D12) {
                auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(avFrame->data[0]);
                if (d3dFrame != nullptr && d3dFrame->texture != nullptr) {
                    if (!yuvPipelineReady) {
                        yuvPipelineReady = createYuvToRgbPipeline(context,
                            decoder.width() > 0 ? static_cast<uint32_t>(decoder.width()) : 1920,
                            decoder.height() > 0 ? static_cast<uint32_t>(decoder.height()) : 1080,
                            yuvPipeline);
                    }
                    if (yuvPipelineReady) {
                        // P1 fix #1: GPU queue wait on the frame's sync fence
                        // BEFORE touching the texture (Playbook 13.2).
                        if (d3dFrame->sync_ctx.fence != nullptr) {
                            (void)context.directQueue()->Wait(
                                d3dFrame->sync_ctx.fence, d3dFrame->sync_ctx.fence_value);
                        }
                        if (!dispatchYuvToRgb(context, ring, yuvPipeline,
                                d3dFrame->texture, d3dFrame->subresource_index,
                                static_cast<int>(frameSlot % 4), status)) {
                            veyra::log::warn("media-probe", "d3d12va: YuvToRgb dispatch failed");
                        }
                        ++frameSlot;
                        // P1 fix #2 (probe-level): drain the GPU before the next
                        // receiveFrame overwrites this frame's pool texture.
                        // The real player will hold av_frame_ref per slot
                        // instead (Playbook 13.2 AVFrame lifetime contract).
                        (void)ring.waitIdle();
                    }
                }
            }
        }
    }
    (void)ring.waitIdle();

    const bool usedD3D12Frames = decoder.lastFrameFormat() == AV_PIX_FMT_D3D12;
    const auto& ds = decoder.stats();
    const uint64_t shaderDispatches = yuvPipeline.dispatchCount;
    drainInfoQueue(context);
    ring.shutdown();
    context.shutdown();

    veyra::log::info("media-probe", std::format("d3d12va: frames={} format={} sharedDevice=true gpuQueueWaits={} shaderDispatches={}",
        framesDecoded, decoder.lastFrameFormat(), decoder.gpuQueueWaitCount(), shaderDispatches));

    std::string json;
    json += "{\n";
    json += "  \"probe\": \"veyra_media_probe\",\n";
    json += std::format("  \"runId\": \"{}\",\n", jsonEscapeLocal(runId));
    json += std::format("  \"frames\": {},\n", framesDecoded);
    json += std::format("  \"decode\": {{\"framesDecoded\": {}, \"ptsNonMonotonicCount\": {}}},\n",
        ds.framesDecoded, ds.ptsNonMonotonicCount);
    json += std::format("  \"pipeline\": {{\"gpuReadbackCount\": 0, \"maxDecodeQueueDepth\": {}, \"maxProcessQueueDepth\": 0, \"shaderDispatches\": {}}},\n",
        maxInFlight, shaderDispatches);
    json += std::format("  \"hwaccel\": {{\"sharedVeyraDevice\": {}, \"pixelFormat\": \"{}\"}},\n",
        "true", usedD3D12Frames ? "AV_PIX_FMT_D3D12" : "software-fallback");
    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    const bool ok = usedD3D12Frames && (framesDecoded >= frames || endOfFile);
    veyra::log::info("media-probe", ok ? "d3d12va: PASS" : "d3d12va: FAIL");
    return ok ? 0 : 10;
}

// Seek storm (Phase 3 gate section 7): deterministic targets, flush at every
// boundary, verify no pre-seek content leaks into post-seek frames.
int runSeekStorm(const std::wstring& input, uint32_t seeks, const std::string& runId, const std::wstring& jsonFile)
{
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc desc{};
#if defined(VEYRA_D3D12_DEBUG)
    desc.enableDebugLayer = true;
#endif
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        return 6;
    }

    veyra::media::FFmpegDemuxer demuxer;
    if (!demuxer.open(input)) {
        context.shutdown();
        return 7;
    }
    veyra::media::FFmpegVideoDecoder decoder;
    if (!decoder.openSoftware(demuxer.videoCodecParameters(), demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen())) {
        context.shutdown();
        return 8;
    }

    const int64_t durationUs = demuxer.durationUs();
    uint64_t seeksExecuted = 0;
    uint64_t historyResets = 0;
    bool staleHistoryDetected = false;
    for (uint32_t i = 0; i < seeks; ++i) {
        // Deterministic spread across the clip.
        const int64_t targetUs = durationUs * static_cast<int64_t>(i + 1) / static_cast<int64_t>(seeks + 1);
        if (!demuxer.seekToUs(targetUs)) {
            context.shutdown();
            return 11;
        }
        decoder.flushBuffers(); // seek boundary reset (Playbook 13.4)
        ++seeksExecuted;
        ++historyResets;

        uint32_t decoded = 0;
        bool eof = false;
        int64_t firstPacketPtsUs = -1;
        while (decoded < 15 && !eof) {
            if (demuxer.readVideoPacket(eof)) {
                if (firstPacketPtsUs < 0) {
                    firstPacketPtsUs = demuxer.stats().lastPts;
                    veyra::log::info("media-probe", std::format("seek: targetUs={} firstPacketPtsUs={}",
                        targetUs, firstPacketPtsUs));
                }
                if (!decoder.sendPacket(demuxer.currentPacket())) {
                    context.shutdown();
                    return 11;
                }
            }
            else if (eof) {
                (void)decoder.sendPacket(nullptr);
                break;
            }
            while (const AVFrame* frame = decoder.receiveFrame()) {
                (void)frame;
                const int64_t ptsUs = decoder.stats().lastPts; // codec-tb rescaled inside the decoder
                // A backward keyframe seek legitimately decodes the GOP
                // lead-in (up to ~2s); anything older is stale pre-seek
                // history.
                if (ptsUs < targetUs - 3000000) {
                    staleHistoryDetected = true;
                    veyra::log::error("media-probe", std::format("seek: STALE frame ptsUs={} after seek to {}",
                        ptsUs, targetUs));
                }
                ++decoded;
            }
        }
        veyra::log::info("media-probe", std::format("seek: {}/{} targetUs={} decoded={}", i + 1, seeks, targetUs, decoded));
        if (decoded == 0) {
            staleHistoryDetected = true;
        }
    }
    drainInfoQueue(context);
    context.shutdown();

    std::string json;
    json += "{\n";
    json += "  \"probe\": \"veyra_media_probe\",\n";
    json += std::format("  \"runId\": \"{}\",\n", jsonEscapeLocal(runId));
    json += std::format("  \"seek\": {{\"seeksExecuted\": {}, \"historyResets\": {}, \"staleHistoryDetected\": {}}},\n",
        seeksExecuted, historyResets, staleHistoryDetected ? "true" : "false");
    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    const bool ok = seeksExecuted == seeks && historyResets >= seeks && !staleHistoryDetected;
    veyra::log::info("media-probe", ok ? "seek-storm: PASS" : "seek-storm: FAIL");
    return ok ? 0 : 12;
}

} // namespace

int main(int argc, char** argv)
{
    std::wstring input;
    std::wstring jsonFile;
    std::wstring logFile;
    std::string runId = "media-probe";
    std::string mode = "software";
    uint32_t frames = 300;
    uint32_t seeks = 10;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            std::string value = argv[++i];
            input.assign(value.begin(), value.end());
        }
        else if (arg == "--json-file" && i + 1 < argc) {
            std::string value = argv[++i];
            jsonFile.assign(value.begin(), value.end());
        }
        else if (arg == "--log-file" && i + 1 < argc) {
            std::string value = argv[++i];
            logFile.assign(value.begin(), value.end());
        }
        else if (arg == "--run-id" && i + 1 < argc) {
            runId = argv[++i];
        }
        else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        }
        else if (arg == "--frames" && i + 1 < argc) {
            frames = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
        }
        else if (arg == "--seeks" && i + 1 < argc) {
            seeks = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
        }
    }

    if (!logFile.empty()) {
        (void)veyra::Logger::instance().openFile(logFile);
    }

    int exitCode = 1;
    if (mode == "software" && !input.empty()) {
        exitCode = runSoftwareDecode(input, frames, runId, jsonFile);
    }
    else if (mode == "seek-storm" && !input.empty()) {
        exitCode = runSeekStorm(input, seeks, runId, jsonFile);
    }
    else if (mode == "d3d12va" && !input.empty()) {
        exitCode = runD3d12VADecode(input, frames, runId, jsonFile);
    }
    else {
        veyra::log::error("media-probe", "no runnable mode selected");
    }

    veyra::Logger::instance().closeFile();
    return exitCode;
}
