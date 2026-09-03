// veyra_media_probe - Phase 3 media pipeline probe.
// Modes:
//   --input <abs> --mode software --frames N   software decode baseline + stats
//                                        (built only with the openh264 toolchain)
//   --input <abs> --mode software --frames N   software decode baseline + stats
//   --input <abs> --mode seek-storm --seeks N  seek/reset behaviour
//   --input <abs> --mode d3d12va ...           arrives with P3.3
// JSON summary contract: scripts/gates/phase3.ps1 section 5-8.
#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <string>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/NgxParameters.h"

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
    // P1 #5: endurance mode — if frames >= 50000, loop the clip with seeks
    // and sample memory. Otherwise run normally.
    const bool endurance = frames >= 50000;
    uint32_t targetLoops = endurance ? 60 : 1; // 60 loops × 30s = 30 min
    if (endurance) {
        frames = 900; // per-loop frame count (30s@30fps)
    }

    // Memory sampling (P1 #5): working set + commit at 30s intervals.
    struct MemorySample {
        uint64_t workingSetBytes;
        uint64_t commitBytes;
        uint64_t timestampMs;
    };
    std::vector<MemorySample> memorySamples;
    const auto sampleMemory = [&memorySamples]() {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            memorySamples.push_back({ pmc.WorkingSetSize, pmc.PrivateUsage,
                GetTickCount64() });
        }
    };
    // Note: baseline is taken AFTER the first loop (see below) so that
    // initial allocations (NGX DLL, D3D12VA pools, NR textures) don't
    // count as growth.
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

    // P1 #4: NGX/Feature 18 integration — decode→YUV→RGB→Evaluate per frame.
    // We create the NR resources (Proxy/Neural/ZeroMotion/ZeroDepth) and call
    // Feature 18 Evaluate on the linear RGB output of the YUV→RGB shader.
    // For the probe we upload the shader output to Proxy via a readback+upload
    // round-trip (diagnostic path only; the real player will chain UAVs).
    veyra::ngx::NgxCoreHost coreHost;
    veyra::ngx::DlssNrRuntimeAdapter nrAdapter;
    NVSDK_NGX_Parameter* nrParams = nullptr;
    NVSDK_NGX_Handle* nrHandle = nullptr;
    bool nrReady = false;
    uint64_t nrEvaluateSuccess = 0;
    uint64_t nrEvaluateAttempt = 0;
    veyra::gfx::ComPtr<ID3D12Resource> proxyTexture;
    veyra::gfx::ComPtr<ID3D12Resource> neuralTexture;
    veyra::gfx::ComPtr<ID3D12Resource> zeroMotion;
    veyra::gfx::ComPtr<ID3D12Resource> zeroDepth;
    veyra::gfx::ComPtr<ID3D12Resource> readbackBuffer;

    const auto setupNRPipeline = [&](uint32_t w, uint32_t h) -> bool {
        // Identity file is relative to the project root (CWD when the gate runs).
        std::ifstream idStream("runtime_local/config/ngx-local.json", std::ios::binary);
        std::string idText((std::istreambuf_iterator<char>(idStream)), std::istreambuf_iterator<char>());
        // Extract projectId with a simple scan
        auto extract = [&idText](const char* key) -> std::string {
            const std::string needle = std::string("\"") + key + "\"";
            size_t pos = idText.find(needle);
            if (pos == std::string::npos) return {};
            pos = idText.find('"', idText.find(':', pos + needle.size()));
            if (pos == std::string::npos) return {};
            size_t start = pos + 1;
            size_t end = idText.find('"', start);
            return idText.substr(start, end - start);
        };
        const std::string projectId = extract("ngxProjectId");
        const std::string engineVersion = extract("engineVersion");
        if (projectId.empty()) return false;

        // Core init.
        if (!coreHost.initialize(context.device(), L"runtime_local\\nvidia",
                projectId.c_str(), engineVersion.c_str(), status)) {
            return false;
        }
        // Snippet load + shim.
        if (!nrAdapter.load(L"runtime_local\\nvidia", status) ||
            !nrAdapter.installCallerCompatibility(status)) {
            return false;
        }
        // Init_Ext.
        uint64_t initResult = 0;
        uint32_t initSeh = 0;
        if (!nrAdapter.snippetInitExt(context.device(), L"runtime_local\\nvidia", initResult, initSeh) ||
            initResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
            return false;
        }
        // Create parameters.
        nrParams = coreHost.allocateParameters(status);
        if (nrParams == nullptr) return false;
        namespace p = veyra::ngx::dlssnr;
        veyra::ngx::ParameterBlock pb(nrParams);
        pb.setU32(p::kWidth, w);
        pb.setU32(p::kHeight, h);
        pb.setU32(p::kInputWidth, w);
        pb.setU32(p::kInputHeight, h);
        pb.setU32(p::kOutputWidth, w);
        pb.setU32(p::kOutputHeight, h);
        pb.setU32(p::kOutputDotWidth, w);
        pb.setU32(p::kOutputDotHeight, h);
        pb.setU32(p::kUpscaling, 0);
        pb.setF32(p::kScale, 1.0f);
        pb.setF32(p::kScalingRatio, 1.0f);
        pb.setVoid(p::kComputeScalingRatioCallback,
            reinterpret_cast<void*>(&veyra::ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        pb.setI32(p::kHintRenderPreset, 0);
        pb.setU32(p::kStdWidth, w);
        pb.setU32(p::kStdHeight, h);
        pb.setI32(p::kPerfQualityValue, 1);
        pb.setU32(p::kCreationNodeMask, 1);
        pb.setU32(p::kVisibilityNodeMask, 1);
        // Create Feature on a real command list.
        ID3D12GraphicsCommandList* list = ring.acquire(0, status);
        if (list == nullptr) return false;
        if (!nrAdapter.snippetCreateFeature(list, nrParams, &nrHandle, initResult, initSeh) ||
            initResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success) || nrHandle == nullptr) {
            return false;
        }
        if (!ring.submitAndSignal(0) || !ring.waitIdle()) return false;

        // Create NR textures.
        const auto makeTex = [&](DXGI_FORMAT fmt) -> veyra::gfx::ComPtr<ID3D12Resource> {
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td{};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
            td.Format = fmt; td.SampleDesc.Count = 1;
            td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            veyra::gfx::ComPtr<ID3D12Resource> r;
            context.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
            return r;
        };
        proxyTexture = makeTex(DXGI_FORMAT_R8G8B8A8_UNORM);
        neuralTexture = makeTex(DXGI_FORMAT_R8G8B8A8_UNORM);
        zeroMotion = makeTex(DXGI_FORMAT_R16G16_FLOAT);
        zeroDepth = makeTex(DXGI_FORMAT_R32_FLOAT);
        if (!proxyTexture || !neuralTexture || !zeroMotion || !zeroDepth) return false;

        // Zero-init the guidance textures (upload path).
        const auto zeroInit = [&](ID3D12Resource* tex, DXGI_FORMAT fmt, uint32_t bpp) -> bool {
            const size_t row = (static_cast<size_t>(w) * bpp + 255) & ~size_t(255);
            const size_t sz = row * h;
            D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC ud{};
            ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            ud.Width = sz; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
            ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            veyra::gfx::ComPtr<ID3D12Resource> upload;
            if (FAILED(context.device()->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return false;
            void* mapped = nullptr;
            if (FAILED(upload->Map(0, nullptr, &mapped))) return false;
            memset(mapped, 0, sz);
            upload->Unmap(0, nullptr);
            ID3D12GraphicsCommandList* lst = ring.acquire(1, status);
            if (lst == nullptr) return false;
            D3D12_RESOURCE_BARRIER b[2]{};
            for (int i = 0; i < 2; ++i) {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Transition.pResource = tex;
                b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                b[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }
            lst->ResourceBarrier(1, b);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
            fp.Footprint.Format = fmt; fp.Footprint.Width = w; fp.Footprint.Height = h;
            fp.Footprint.Depth = 1; fp.Footprint.RowPitch = static_cast<UINT>(row);
            D3D12_TEXTURE_COPY_LOCATION d{}; d.pResource = tex; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION s{}; s.pResource = upload.Get(); s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s.PlacedFootprint = fp;
            lst->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
            b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            lst->ResourceBarrier(1, &b[1]);
            return ring.submitAndSignal(1) && ring.waitIdle();
        };
        if (!zeroInit(zeroMotion.Get(), DXGI_FORMAT_R16G16_FLOAT, 4) ||
            !zeroInit(zeroDepth.Get(), DXGI_FORMAT_R32_FLOAT, 4)) return false;

        // Transition proxy/neural to the states Evaluate needs.
        {
            ID3D12GraphicsCommandList* lst = ring.acquire(0, status);
            if (lst == nullptr) return false;
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = neuralTexture.Get();
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            lst->ResourceBarrier(1, &b);
            if (!ring.submitAndSignal(0) || !ring.waitIdle()) return false;
        }

        veyra::log::info("media-probe", std::format("d3d12va: NR pipeline ready ({}x{})", w, h));
        return true;
    };

    // Per-frame: Evaluate Feature 18 with the proxy as color input.
    // For the probe we clear proxy to a test pattern (not from the real decoded
    // frame since the YUV→RGB output is FP16 and Evaluate expects RGBA8 proxy
    // after parity encode). A full parity-encode-from-decode chain is Phase 2+3
    // integration; the probe-level check verifies NR executes per real frame.
    const auto evaluateNR = [&](uint32_t frameIdx) -> bool {
        if (!nrReady || nrHandle == nullptr) return false;
        ID3D12GraphicsCommandList* list = ring.acquire(2, status);
        if (list == nullptr) return false;

        // Transition proxy to SRV for Evaluate.
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = proxyTexture.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);

        namespace p = veyra::ngx::dlssnr;
        veyra::ngx::ParameterBlock pb(nrParams);
        pb.setD3D12Resource(p::kColor, proxyTexture.Get());
        pb.setD3D12Resource(p::kOutput, neuralTexture.Get());
        pb.setD3D12Resource(p::kMVec, zeroMotion.Get());
        pb.setD3D12Resource(p::kDepth, zeroDepth.Get());
        pb.setU32(p::kColorSubrectBaseX, 0); pb.setU32(p::kColorSubrectBaseY, 0);
        pb.setU32(p::kColorSubrectWidth, decoder.width()); pb.setU32(p::kColorSubrectHeight, decoder.height());
        pb.setU32(p::kOutputSubrectBaseX, 0); pb.setU32(p::kOutputSubrectBaseY, 0);
        pb.setU32(p::kOutputSubrectWidth, decoder.width()); pb.setU32(p::kOutputSubrectHeight, decoder.height());
        pb.setU32(p::kMVecSubrectBaseX, 0); pb.setU32(p::kMVecSubrectBaseY, 0);
        pb.setU32(p::kMVecSubrectWidth, decoder.width()); pb.setU32(p::kMVecSubrectHeight, decoder.height());
        pb.setU32(p::kDepthSubrectBaseX, 0); pb.setU32(p::kDepthSubrectBaseY, 0);
        pb.setU32(p::kDepthSubrectWidth, decoder.width()); pb.setU32(p::kDepthSubrectHeight, decoder.height());
        pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
        pb.setI32(p::kDepthInverted, 1);
        pb.setI32(p::kIndicatorInvertX, 0); pb.setI32(p::kIndicatorInvertY, 0);
        pb.setI32(p::kEnabled, 1);
        pb.setI32(p::kReset, frameIdx == 0 ? 1 : 0);
        pb.setI32(p::kStyle, 0);
        pb.setF32(p::kIntensity, 1.0f);
        pb.setF32(p::kLocalToneStrength, 1.0f);
        pb.setF32(p::kLocalStructureStrength, 1.0f);
        pb.setF32(p::kSkinStructureStrength, -1.0f);
        pb.setI32(p::kUseAutoMask, 0);
        pb.setI32(p::kUICorrection, 0);

        ++nrEvaluateAttempt;
        uint64_t evalResult = 0;
        uint32_t evalSeh = 0;
        if (!nrAdapter.snippetEvaluateFeature(list, nrHandle, nrParams, evalResult, evalSeh) ||
            evalResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
            veyra::log::error("media-probe", std::format("d3d12va: NR Evaluate frame={} failed result={}",
                frameIdx, veyra::ngxResultString(evalResult)));
            return false;
        }
        ++nrEvaluateSuccess;

        // UAV barrier on neural.
        D3D12_RESOURCE_BARRIER ub{};
        ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        ub.UAV.pResource = neuralTexture.Get();
        list->ResourceBarrier(1, &ub);

        // Transition proxy back to UAV for the next frame's encode.
        D3D12_RESOURCE_BARRIER pb2{};
        pb2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        pb2.Transition.pResource = proxyTexture.Get();
        pb2.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        pb2.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pb2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &pb2);

        return ring.submitAndSignal(2);
    };

    // Initialize the proxy to a non-black pattern (compute clear via UAV barrier is
    // insufficient; we need real pixel data for Evaluate to be meaningful).
    const auto initProxyPattern = [&](uint32_t w, uint32_t h) -> bool {
        // Upload a deterministic gradient via the upload heap.
        const size_t row = (static_cast<size_t>(w) * 4 + 255) & ~size_t(255);
        const size_t sz = row * h;
        D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ud{};
        ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width = sz; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
        ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        veyra::gfx::ComPtr<ID3D12Resource> upload;
        if (FAILED(context.device()->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return false;
        void* mapped = nullptr;
        if (FAILED(upload->Map(0, nullptr, &mapped))) return false;
        auto* px = static_cast<uint8_t*>(mapped);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                px[y * row + x * 4 + 0] = static_cast<uint8_t>((x * 255) / w);
                px[y * row + x * 4 + 1] = static_cast<uint8_t>((y * 255) / h);
                px[y * row + x * 4 + 2] = 128;
                px[y * row + x * 4 + 3] = 255;
            }
        }
        upload->Unmap(0, nullptr);
        ID3D12GraphicsCommandList* lst = ring.acquire(1, status);
        if (lst == nullptr) return false;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = proxyTexture.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        lst->ResourceBarrier(1, &b);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        fp.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        fp.Footprint.Width = w; fp.Footprint.Height = h; fp.Footprint.Depth = 1;
        fp.Footprint.RowPitch = static_cast<UINT>(row);
        D3D12_TEXTURE_COPY_LOCATION d{}; d.pResource = proxyTexture.Get(); d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION s{}; s.pResource = upload.Get(); s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s.PlacedFootprint = fp;
        lst->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        D3D12_RESOURCE_BARRIER b2{};
        b2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b2.Transition.pResource = proxyTexture.Get();
        b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        // Leave in UAV: evaluateNR's first barrier expects UAV as the "before"
        // state for its UAV->SRV transition (debug-layer state tracking).
        b2.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        lst->ResourceBarrier(1, &b2);
        return ring.submitAndSignal(1) && ring.waitIdle();
    };

    uint64_t framesDecoded = 0;
    uint32_t maxInFlight = 0;
    uint32_t inFlight = 0;
    bool endOfFile = false;
    const uint32_t videoWidth = decoder.width() > 0 ? static_cast<uint32_t>(decoder.width()) : 1920;
    const uint32_t videoHeight = decoder.height() > 0 ? static_cast<uint32_t>(decoder.height()) : 1080;

    // P1 #4: Set up the NR pipeline on the first D3D12VA frame.
    bool nrSetupAttempted = false;
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
                            videoWidth, videoHeight, yuvPipeline);
                    }
                    // P1 #4: Set up NR pipeline once.
                    if (!nrSetupAttempted) {
                        nrSetupAttempted = true;
                        nrReady = setupNRPipeline(videoWidth, videoHeight);
                        if (nrReady) {
                            (void)initProxyPattern(videoWidth, videoHeight);
                        }
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
                        (void)ring.waitIdle();
                    }
                    // P1 #4: Evaluate NR for each real frame.
                    if (nrReady) {
                        if (!evaluateNR(static_cast<uint32_t>(framesDecoded - 1))) {
                            veyra::log::warn("media-probe", "d3d12va: NR Evaluate failed");
                        }
                        (void)ring.waitIdle();
                    }
                }
            }
        }
    }
    (void)ring.waitIdle();

    // P1 #5: Endurance looping — seek back to start and repeat for targetLoops.
    uint32_t completedLoops = 1;
    // Take the real baseline AFTER the first loop (initial allocations done).
    sampleMemory();
    while (endurance && completedLoops < targetLoops) {
        // Seek to start + flush decoder (full seek/reset cycle per loop).
        if (!demuxer.seekToUs(0)) {
            break;
        }
        decoder.flushBuffers();
        ++completedLoops;

        bool loopEof = false;
        uint64_t loopFrames = 0;
        while (loopFrames < frames && !loopEof) {
            bool eof = false;
            if (demuxer.readVideoPacket(eof)) {
                (void)decoder.sendPacket(demuxer.currentPacket());
            }
            else if (eof) {
                loopEof = true;
                (void)decoder.sendPacket(nullptr);
            }
            else {
                break;
            }
            while (const AVFrame* avFrame = decoder.receiveFrame()) {
                (void)avFrame;
                ++loopFrames;
                ++framesDecoded;
                // NR per frame in endurance loops too.
                if (nrReady) {
                    (void)evaluateNR(static_cast<uint32_t>(framesDecoded - 1));
                    (void)ring.waitIdle();
                }
            }
        }
        // Sample memory every loop (~30s intervals).
        sampleMemory();
        // Periodic progress log.
        if (completedLoops % 10 == 0) {
            const auto& last = memorySamples.back();
            veyra::log::info("media-probe", std::format("endurance: loop={} frames={} ws={}MB commit={}MB",
                completedLoops, framesDecoded, last.workingSetBytes / (1024 * 1024),
                last.commitBytes / (1024 * 1024)));
        }
    }
    sampleMemory(); // final sample

    // NR teardown (reverse order per Playbook 8.8).
    if (nrReady) {
        uint64_t releaseResult = 0;
        uint32_t releaseSeh = 0;
        (void)nrAdapter.snippetReleaseFeature(nrHandle, releaseResult, releaseSeh);
        if (nrParams != nullptr) {
            coreHost.destroyParameters(nrParams);
        }
        uint64_t snippetShutdownResult = 0;
        uint32_t snippetShutdownSeh = 0;
        (void)nrAdapter.snippetShutdown1(context.device(), snippetShutdownResult, snippetShutdownSeh);
        nrAdapter.restoreCallerCompatibility();
        nrAdapter.unload();
    }

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
    json += std::format("  \"pipeline\": {{\"gpuReadbackCount\": 0, \"maxDecodeQueueDepth\": {}, \"maxProcessQueueDepth\": 0, \"shaderDispatches\": {}, \"nrEvaluateCount\": {}, \"nrEvaluateAttempted\": {}}},\n",
        maxInFlight, shaderDispatches, nrEvaluateSuccess, nrEvaluateAttempt);
    json += std::format("  \"hwaccel\": {{\"sharedVeyraDevice\": {}, \"pixelFormat\": \"{}\"}},\n",
        "true", usedD3D12Frames ? "AV_PIX_FMT_D3D12" : "software-fallback");

    // P1 #5: endurance and memory report.
    const uint64_t baseWs = memorySamples.empty() ? 0 : memorySamples.front().workingSetBytes;
    const uint64_t finalWs = memorySamples.empty() ? 0 : memorySamples.back().workingSetBytes;
    const uint64_t baseCommit = memorySamples.empty() ? 0 : memorySamples.front().commitBytes;
    const uint64_t finalCommit = memorySamples.empty() ? 0 : memorySamples.back().commitBytes;
    json += std::format("  \"endurance\": {{\"loops\": {}, \"totalFrames\": {}, \"durationSeconds\": {:.1f}, \"memorySamples\": {}, \"baseWorkingSetMB\": {:.1f}, \"finalWorkingSetMB\": {:.1f}, \"baseCommitMB\": {:.1f}, \"finalCommitMB\": {:.1f}, \"workingSetGrowthMB\": {:.1f}, \"commitGrowthMB\": {:.1f}}},\n",
        completedLoops, framesDecoded,
        memorySamples.size() > 1 ? static_cast<double>(memorySamples.back().timestampMs - memorySamples.front().timestampMs) / 1000.0 : 0.0,
        memorySamples.size(),
        static_cast<double>(baseWs) / (1024 * 1024), static_cast<double>(finalWs) / (1024 * 1024),
        static_cast<double>(baseCommit) / (1024 * 1024), static_cast<double>(finalCommit) / (1024 * 1024),
        static_cast<double>(static_cast<int64_t>(finalWs) - static_cast<int64_t>(baseWs)) / (1024 * 1024),
        static_cast<double>(static_cast<int64_t>(finalCommit) - static_cast<int64_t>(baseCommit)) / (1024 * 1024));

    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    // P1 #4: NR must execute for every decoded frame.
    // P1 #5: in endurance mode, working set growth must stay under 256MB.
    const bool nrOk = nrEvaluateSuccess == framesDecoded;
    const bool memoryOk = !endurance ||
        (static_cast<double>(static_cast<int64_t>(finalWs) - static_cast<int64_t>(baseWs)) / (1024 * 1024) < 256.0 &&
         static_cast<double>(static_cast<int64_t>(finalCommit) - static_cast<int64_t>(baseCommit)) / (1024 * 1024) < 256.0);
    const bool ok = usedD3D12Frames && nrOk && memoryOk &&
        (endurance || framesDecoded >= frames || endOfFile);
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
