#include "parity_compare.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <nvsdk_ngx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

#include "harness_util.h"
#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/parity/RenoDxParityCodec.h"

namespace veyra::harness {

namespace {

using util::RgbaStats;
using util::analyzeRgba;
using util::jsonEscape;
using util::narrowText;
using util::osBuildString;
using util::ownExePath;
using util::writeRgbaPng;
using util::writeTextFileUtf8;

// float -> half with round-to-nearest-even (matches GPU FP16 storage);
// used for BOTH the upload buffer and the CPU reference quantization.
uint16_t floatToHalf(float value)
{
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
    const uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = bits & 0x007FFFFF;
    if (((bits >> 23) & 0xFF) == 0xFF) {
        return static_cast<uint16_t>(sign | 0x7C00 | (mantissa != 0 ? 1 : 0)); // Inf/NaN
    }
    if (((bits >> 23) & 0xFF) == 0) {
        mantissa = 0; // flush denormal inputs (test values are normal)
    }
    if (exponent >= 0x1F) {
        return static_cast<uint16_t>(sign | 0x7BFF); // saturate (inputs are bounded test values)
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa |= 0x800000;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        const uint32_t halfMantissa = mantissa >> shift;
        const uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const uint32_t halfUlp = 1u << (shift - 1);
        uint32_t rounded = halfMantissa;
        if (remainder > halfUlp || (remainder == halfUlp && (halfMantissa & 1u) != 0)) {
            rounded = halfMantissa + 1;
        }
        return static_cast<uint16_t>(sign | rounded);
    }
    uint32_t halfMantissa = mantissa >> 13;
    const uint32_t remainder = mantissa & 0x1FFF;
    if (remainder > 0x1000 || (remainder == 0x1000 && (halfMantissa & 1u) != 0)) {
        ++halfMantissa;
        if (halfMantissa == 0x400) {
            halfMantissa = 0;
            ++exponent;
        }
    }
    if (exponent >= 0x1F) {
        return static_cast<uint16_t>(sign | 0x7BFF);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | halfMantissa);
}

float halfToFloat(uint16_t value)
{
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000) << 16;
    const uint32_t exponent = (value >> 10) & 0x1F;
    const uint32_t mantissa = value & 0x3FF;
    if (exponent == 0) {
        return *reinterpret_cast<const float*>(&sign);
    }
    if (exponent == 0x1F) {
        const uint32_t inf = sign | 0x7F800000;
        return *reinterpret_cast<const float*>(&inf);
    }
    const uint32_t result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    return *reinterpret_cast<const float*>(&result);
}

// Deterministic linear test content (Playbook 9.5 scenarios as full-frame
// zones): gray steps incl. 18% gray, gradients with highlights up to 4.0,
// color patches, skin tone and hard edges.
void fillOriginalPixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float out[4])
{
    const double u = static_cast<double>(x) / width;
    const double v = static_cast<double>(y) / height;
    if (y < height / 4) {
        // Gray steps incl. 18% gray and shoulder entries 1/2/4.
        const double steps[] = { 0.0, 0.18, 0.5, 0.75, 1.0, 2.0, 4.0, 0.05 };
        const int index = static_cast<int>(u * 8) % 8;
        out[0] = out[1] = out[2] = static_cast<float>(steps[index]);
    }
    else if (y < height / 2) {
        // Gradient with superwhite sweep in red.
        out[0] = static_cast<float>(u * 4.0);
        out[1] = static_cast<float>(0.1 * (1.0 - u));
        out[2] = static_cast<float>(v * 0.2);
    }
    else if (y < 3 * height / 4) {
        // Color patches: primaries + skin tone + checker.
        const int cell = static_cast<int>(u * 8) % 8;
        switch (cell) {
        case 0: out[0] = 1; out[1] = 0; out[2] = 0; break;
        case 1: out[0] = 0; out[1] = 1; out[2] = 0; break;
        case 2: out[0] = 0; out[1] = 0; out[2] = 1; break;
        case 3: out[0] = 0.847f; out[1] = 0.682f; out[2] = 0.552f; break;
        case 4: case 6: out[0] = 0.2f; out[1] = 0.4f; out[2] = 0.6f; break;
        default: out[0] = 0.05f; out[1] = 0.05f; out[2] = 0.05f; break;
        }
    }
    else {
        // Hard edge columns on a mid gray field.
        out[0] = out[1] = out[2] = (x == width / 2 || x == width / 2 + 1) ? 0.9f : 0.3f;
        if (x % 64 == 0) {
            out[0] = 2.0f;
            out[1] = 1.0f;
            out[2] = 0.5f;
        }
    }
    out[3] = 1.0f;
}

struct ParityGpu {
    gfx::ComPtr<ID3D12Resource> original; // R16G16B16A16_FLOAT
    gfx::ComPtr<ID3D12Resource> proxy;    // R8G8B8A8_UNORM (UAV)
    gfx::ComPtr<ID3D12Resource> neural;   // R8G8B8A8_UNORM (UAV)
    gfx::ComPtr<ID3D12Resource> final;    // R16G16B16A16_FLOAT (UAV)
};

bool createParityTextures(const gfx::D3D12DeviceContext& context, uint32_t width, uint32_t height, ParityGpu& out)
{
    const auto makeTexture = [&](DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, const wchar_t* name) {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = flags;
        gfx::ComPtr<ID3D12Resource> resource;
        if (FAILED(context.device()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
            log::error("harness", std::format("parity: create texture {} failed", narrowText(name)));
            return gfx::ComPtr<ID3D12Resource>{};
        }
        return resource;
    };
    out.original = makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Original");
    out.proxy = makeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Proxy");
    out.neural = makeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Neural");
    out.final = makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Final");
    return out.original != nullptr && out.proxy != nullptr && out.neural != nullptr && out.final != nullptr;
}

std::vector<uint8_t> loadShaderBytes(const std::string& name)
{
    const std::string path = std::string(VEYRA_SHADER_DIR "/") + name;
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log::error("harness", std::format("parity: shader missing path={}", path));
        return {};
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    return bytes;
}

} // namespace

int runParityCompare(const FrameLoopArgs& args)
{
    const uint32_t width = args.width;
    const uint32_t height = args.height;
    const uint32_t evaluateFrames = std::max<uint32_t>(args.frames, 8);

#if defined(VEYRA_D3D12_DEBUG)
    constexpr bool kDebugLayerByBuild = true;
#else
    constexpr bool kDebugLayerByBuild = false;
#endif
    gfx::D3D12DeviceContext context;
    gfx::DeviceContextDesc contextDesc{};
    contextDesc.enableDebugLayer = kDebugLayerByBuild;
    contextDesc.commandSlotCount = 4;
    Status status = Status::Ok;
    if (!context.initialize(contextDesc, status)) {
        return 6;
    }
    gfx::CommandSlotRing ring;
    if (!ring.initialize(context.device(), context.directQueue(), context.fence(), context.fenceEvent(), 4, status)) {
        context.shutdown();
        return 7;
    }

    // Debug-layer observability (same rationale as the frame loop).
    gfx::ComPtr<ID3D12InfoQueue> parityInfoQueue;
    uint64_t parityInfoQueueStored = 0;
    uint64_t parityInfoQueueErrors = 0;
    bool parityInfoQueueActive = false;
#if defined(VEYRA_D3D12_DEBUG)
    if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&parityInfoQueue)))) {
        parityInfoQueue->SetMuteDebugOutput(false);
        parityInfoQueueActive = true;
    }
#endif
    const auto drainInfoQueue = [&]() {
        if (parityInfoQueue == nullptr) {
            return;
        }
        const uint64_t stored = parityInfoQueue->GetNumStoredMessages();
        uint64_t reported = 0;
        uint64_t errors = 0;
        for (uint64_t i = 0; i < stored && reported < 50; ++i) {
            SIZE_T length = 0;
            if (FAILED(parityInfoQueue->GetMessage(i, nullptr, &length)) || length == 0) {
                continue;
            }
            std::vector<uint8_t> buffer(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (FAILED(parityInfoQueue->GetMessage(i, message, &length))) {
                continue;
            }
            ++reported;
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                ++errors;
            }
            log::error("harness", std::format("parity infoqueue id={} desc={}",
                static_cast<unsigned>(message->ID), message->pDescription));
        }
        parityInfoQueueStored = stored;
        parityInfoQueueErrors = errors;
        log::info("harness", std::format("parity: infoqueue stored={} reported={} errors={}", stored, reported, errors));
    };

    // Local identity + core + snippet (Playbook 8.4 order).
    const std::wstring runtimeDir = args.runtimeDir;
    std::ifstream identityStream(runtimeDir + L"\\..\\config\\ngx-local.json", std::ios::binary);
    std::string identityText((std::istreambuf_iterator<char>(identityStream)), std::istreambuf_iterator<char>());
    const std::string projectId = [&identityText] {
        const std::string needle = "\"ngxProjectId\"";
        size_t position = identityText.find(needle);
        if (position == std::string::npos) return std::string();
        position = identityText.find('"', identityText.find(':', position + needle.size()));
        const size_t start = position + 1;
        const size_t end = identityText.find('"', start);
        return identityText.substr(start, end - start);
    }();
    if (projectId.empty()) {
        ring.shutdown();
        context.shutdown();
        return 7;
    }
    ngx::NgxCoreHost coreHost;
    if (!coreHost.initialize(context.device(), runtimeDir, projectId.c_str(), "Veyra-Experimental-0.1.0", status)) {
        ring.shutdown();
        context.shutdown();
        return 8;
    }
    ngx::DlssNrRuntimeAdapter adapter;
    if (!adapter.load(runtimeDir, status) || !adapter.installCallerCompatibility(status)) {
        adapter.unload();
        coreHost.shutdown();
        ring.shutdown();
        context.shutdown();
        return 9;
    }
    uint64_t result = 0;
    uint32_t sehCode = 0;
    if (!adapter.snippetInitExt(context.device(), runtimeDir, result, sehCode) ||
        result != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
        adapter.restoreCallerCompatibility();
        adapter.unload();
        coreHost.shutdown();
        ring.shutdown();
        context.shutdown();
        return 10;
    }

    NVSDK_NGX_Parameter* params = coreHost.allocateParameters(status);
    NVSDK_NGX_Handle* handle = nullptr;
    ParityGpu gpu{};
    if (params == nullptr) {
        goto parity_teardown;
    }
    {
        namespace p = ngx::dlssnr;
        ngx::ParameterBlock pb(params);
        pb.setU32(p::kWidth, width);
        pb.setU32(p::kHeight, height);
        pb.setU32(p::kInputWidth, width);
        pb.setU32(p::kInputHeight, height);
        pb.setU32(p::kOutputWidth, width);
        pb.setU32(p::kOutputHeight, height);
        pb.setU32(p::kOutputDotWidth, width);
        pb.setU32(p::kOutputDotHeight, height);
        pb.setU32(p::kUpscaling, 0);
        pb.setF32(p::kScale, 1.0f);
        pb.setF32(p::kScalingRatio, 1.0f);
        pb.setVoid(p::kComputeScalingRatioCallback,
            reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        pb.setI32(p::kHintRenderPreset, 0);
        pb.setU32(p::kStdWidth, width);
        pb.setU32(p::kStdHeight, height);
        pb.setI32(p::kPerfQualityValue, 1);
        pb.setU32(p::kCreationNodeMask, 1);
        pb.setU32(p::kVisibilityNodeMask, 1);

        ID3D12GraphicsCommandList* list = ring.acquire(0, status);
        if (list == nullptr) {
            goto parity_teardown;
        }
        if (!adapter.snippetCreateFeature(list, params, &handle, result, sehCode) ||
            result != static_cast<uint64_t>(NVSDK_NGX_Result_Success) || handle == nullptr) {
            goto parity_teardown;
        }
        if (!ring.submitAndSignal(0) || !ring.waitIdle()) {
            goto parity_teardown;
        }
    }
    log::info("harness", "parity: [stage] before textures");
    if (!createParityTextures(context, width, height, gpu)) {
        goto parity_teardown;
    }

    {
        // --- Zero guidance textures (R16G16F / R32F) via upload copies.
        const auto makeZero = [&](DXGI_FORMAT format, uint32_t bytesPerPixel) -> gfx::ComPtr<ID3D12Resource> {
            const size_t row = (static_cast<size_t>(width) * bytesPerPixel + 255) & ~size_t(255);
            const size_t sizeBytes = row * height;
            D3D12_HEAP_PROPERTIES uploadProps{};
            uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC uploadDesc{};
            uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width = sizeBytes;
            uploadDesc.Height = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels = 1;
            uploadDesc.SampleDesc.Count = 1;
            uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            gfx::ComPtr<ID3D12Resource> upload;
            gfx::ComPtr<ID3D12Resource> texture;
            D3D12_HEAP_PROPERTIES defaultProps{};
            defaultProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC texDesc{};
            texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texDesc.Width = width;
            texDesc.Height = height;
            texDesc.DepthOrArraySize = 1;
            texDesc.MipLevels = 1;
            texDesc.Format = format;
            texDesc.SampleDesc.Count = 1;
            texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(context.device()->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))) ||
                FAILED(context.device()->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                    D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture)))) {
                return nullptr;
            }
            void* mapped = nullptr;
            if (FAILED(upload->Map(0, nullptr, &mapped))) {
                return nullptr;
            }
            memset(mapped, 0, sizeBytes);
            upload->Unmap(0, nullptr);

            ID3D12GraphicsCommandList* list = ring.acquire(1, status);
            if (list == nullptr) {
                return nullptr;
            }
            D3D12_RESOURCE_BARRIER b[2]{};
            b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b[0].Transition.pResource = texture.Get();
            b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, b);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            footprint.Footprint.Format = format;
            footprint.Footprint.Width = width;
            footprint.Footprint.Height = height;
            footprint.Footprint.Depth = 1;
            footprint.Footprint.RowPitch = static_cast<UINT>(row);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = texture.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b[1].Transition.pResource = texture.Get();
            b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &b[1]);
            if (!ring.submitAndSignal(1) || !ring.waitIdle()) {
                return nullptr;
            }
            return texture;
        };
        auto motion = makeZero(DXGI_FORMAT_R16G16_FLOAT, 4);
        auto depth = makeZero(DXGI_FORMAT_R32_FLOAT, 4);
        if (motion == nullptr || depth == nullptr) {
            goto parity_teardown;
        }

        // --- Host Original (fp32 -> fp16) + upload to GPU.
        const size_t originalRow = (static_cast<size_t>(width) * 8 + 255) & ~size_t(255);
        const size_t originalBytes = originalRow * height;
        std::vector<uint16_t> originalHalf(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                float pixel[4]{};
                fillOriginalPixel(x, y, width, height, pixel);
                const size_t index = (static_cast<size_t>(y) * width + x) * 4;
                originalHalf[index + 0] = floatToHalf(pixel[0]);
                originalHalf[index + 1] = floatToHalf(pixel[1]);
                originalHalf[index + 2] = floatToHalf(pixel[2]);
                originalHalf[index + 3] = floatToHalf(pixel[3]);
            }
        }
        {
            D3D12_HEAP_PROPERTIES uploadProps{};
            uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC uploadDesc{};
            uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width = originalBytes;
            uploadDesc.Height = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels = 1;
            uploadDesc.SampleDesc.Count = 1;
            uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            gfx::ComPtr<ID3D12Resource> upload;
            if (FAILED(context.device()->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
                goto parity_teardown;
            }
            void* mapped = nullptr;
            if (FAILED(upload->Map(0, nullptr, &mapped))) {
                goto parity_teardown;
            }
            for (uint32_t y = 0; y < height; ++y) {
                memcpy(static_cast<uint8_t*>(mapped) + y * originalRow,
                    originalHalf.data() + static_cast<size_t>(y) * width * 4,
                    static_cast<size_t>(width) * 8);
            }
            upload->Unmap(0, nullptr);

            ID3D12GraphicsCommandList* list = ring.acquire(0, status);
            if (list == nullptr) {
                goto parity_teardown;
            }
            D3D12_RESOURCE_BARRIER b[2]{};
            b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b[0].Transition.pResource = gpu.original.Get();
            b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, b);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            footprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            footprint.Footprint.Width = width;
            footprint.Footprint.Height = height;
            footprint.Footprint.Depth = 1;
            footprint.Footprint.RowPitch = static_cast<UINT>(originalRow);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = gpu.original.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b[1].Transition.pResource = gpu.original.Get();
            b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &b[1]);
            if (!ring.submitAndSignal(0) || !ring.waitIdle()) {
                goto parity_teardown;
            }
        }

        // --- Descriptor heap + parity pipelines.
        log::info("harness", "parity: [stage] original uploaded");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 8;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        gfx::ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(context.device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)))) {
            goto parity_teardown;
        }
        log::info("harness", "parity: [stage] heap created");
        const UINT increment = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heap->GetGPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1; // 0 is invalid; the debug layer removes the device
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        context.device()->CreateShaderResourceView(gpu.original.Get(), &srvDesc, cpu);
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        context.device()->CreateShaderResourceView(gpu.proxy.Get(), &srvDesc, { cpu.ptr + increment });
        context.device()->CreateShaderResourceView(gpu.neural.Get(), &srvDesc, { cpu.ptr + 2ull * increment });

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        context.device()->CreateUnorderedAccessView(gpu.proxy.Get(), nullptr, &uavDesc, { cpu.ptr + 4ull * increment });
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        context.device()->CreateUnorderedAccessView(gpu.final.Get(), nullptr, &uavDesc, { cpu.ptr + 5ull * increment });

        D3D12_DESCRIPTOR_RANGE1 ranges[3]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 3;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        // NGX's EvaluateFeature records its own barriers on shared resources;
        // volatile descriptors keep bind-time state validation honest there.
        ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].RegisterSpace = 0;
        // Table base IS the UAV descriptor: bind-time handles already point
        // at heap slot 4 (encode: proxy) or slot 5 (decode: final).
        ranges[1].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_ROOT_PARAMETER1 rootParameters[3]{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.RegisterSpace = 0;
        rootParameters[0].Constants.Num32BitValues = 8;
        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 3;
        rootDesc.Desc_1_1.pParameters = rootParameters;
        gfx::ComPtr<ID3DBlob> signature;
        gfx::ComPtr<ID3DBlob> signatureError;
        const HRESULT serializeResult = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &signatureError);
        if (FAILED(serializeResult)) {
            log::error("harness", std::format("parity: root signature serialize failed hr={} detail={}",
                hresultString(serializeResult),
                signatureError != nullptr ? std::string(static_cast<const char*>(signatureError->GetBufferPointer())) : std::string("<none>")));
            goto parity_teardown;
        }
        gfx::ComPtr<ID3D12RootSignature> rootSignature;
        const HRESULT rootResult = context.device()->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
        if (FAILED(rootResult)) {
            log::error("harness", std::format("parity: CreateRootSignature failed hr={}", hresultString(rootResult)));
            uint32_t removedReason = 0;
            (void)context.checkDeviceAlive(removedReason);
            log::error("harness", std::format("parity: deviceRemovedReason=0x{:08X}", removedReason));
            drainInfoQueue();
            goto parity_teardown;
        }

        log::info("harness", "parity: [stage] root signature created");
        const auto makePso = [&](const char* file) -> gfx::ComPtr<ID3D12PipelineState> {
            const std::vector<uint8_t> bytes = loadShaderBytes(file);
            if (bytes.empty()) {
                return nullptr;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = rootSignature.Get();
            psoDesc.CS.pShaderBytecode = bytes.data();
            psoDesc.CS.BytecodeLength = bytes.size();
            gfx::ComPtr<ID3D12PipelineState> pso;
            if (FAILED(context.device()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)))) {
                log::error("harness", std::format("parity: PSO {} failed", file));
                return nullptr;
            }
            return pso;
        };
        auto encodePso = makePso("ParityEncode.dxil");
        auto decodePso = makePso("ParityDecode.dxil");
        if (encodePso == nullptr || decodePso == nullptr) {
            goto parity_teardown;
        }

        const parity::ParitySettings neutral{};
        const float settingsConstants[8] = {
            static_cast<float>(neutral.paperWhiteScale),
            static_cast<float>(neutral.transferStrength),
            static_cast<float>(neutral.colorStrength),
            0.0f,
            static_cast<float>(width),
            static_cast<float>(height),
            0.0f,
            0.0f
        };
        ID3D12DescriptorHeap* heaps[] = { heap.Get() };

        const auto transition = [](ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                                   D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &barrier);
        };

        log::info("harness", "parity: [stage] PSOs created");
        // --- ParityEncode dispatch.
        {
            ID3D12GraphicsCommandList* list = ring.acquire(2, status);
            if (list == nullptr) {
                goto parity_teardown;
            }
            transition(list, gpu.proxy.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->SetDescriptorHeaps(1, heaps);
            list->SetPipelineState(encodePso.Get());
            list->SetComputeRootSignature(rootSignature.Get());
            list->SetComputeRoot32BitConstants(0, 8, settingsConstants, 0);
            list->SetComputeRootDescriptorTable(1, { gpuHandle.ptr });              // SRV: original at offset 0
            list->SetComputeRootDescriptorTable(2, { gpuHandle.ptr + 4ull * increment }); // UAV: proxy at offset 4
            list->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
            transition(list, gpu.proxy.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transition(list, gpu.neural.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (!ring.submitAndSignal(2) || !ring.waitIdle()) {
                goto parity_teardown;
            }
        }

        // --- Readback staging.
        const size_t rgba8Row = (static_cast<size_t>(width) * 4 + 255) & ~size_t(255);
        const size_t rgba8Bytes = rgba8Row * height;
        D3D12_HEAP_PROPERTIES readbackProps{};
        readbackProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC readbackDesc{};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        readbackDesc.Width = rgba8Bytes > originalBytes ? rgba8Bytes : originalBytes;
        gfx::ComPtr<ID3D12Resource> readback;
        if (FAILED(context.device()->CreateCommittedResource(&readbackProps, D3D12_HEAP_FLAG_NONE,
                &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
            goto parity_teardown;
        }

        const auto copyOut = [&](ID3D12Resource* texture, DXGI_FORMAT format,
                                 size_t rowPitch, D3D12_RESOURCE_STATES before, std::vector<uint8_t>& outBytes) -> bool {
            (void)format; // footprint format documented at call sites
            ID3D12GraphicsCommandList* list = ring.acquire(3, status);
            if (list == nullptr) {
                return false;
            }
            transition(list, texture, before, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            footprint.Footprint.Format = format;
            footprint.Footprint.Width = width;
            footprint.Footprint.Height = height;
            footprint.Footprint.Depth = 1;
            footprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = texture;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            transition(list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE, before);
            if (!ring.submitAndSignal(3) || !ring.waitIdle()) {
                return false;
            }
            void* mapped = nullptr;
            D3D12_RANGE range{ 0, rowPitch * height };
            if (FAILED(readback->Map(0, &range, &mapped))) {
                return false;
            }
            outBytes.assign(static_cast<const uint8_t*>(mapped), static_cast<const uint8_t*>(mapped) + rowPitch * height);
            readback->Unmap(0, nullptr);
            return true;
        };

        std::vector<uint8_t> proxyBytes;
        if (!copyOut(gpu.proxy.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, rgba8Row,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, proxyBytes)) {
            goto parity_teardown;
        }

        // GPU vs CPU encode comparison (Playbook 9.5, <= 1 code value).
        double maxCodeDelta = 0.0;
        double maxAlphaDelta = 0.0;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t halfIndex = (static_cast<size_t>(y) * width + x) * 4;
                const parity::RgbF original{
                    halfToFloat(originalHalf[halfIndex + 0]),
                    halfToFloat(originalHalf[halfIndex + 1]),
                    halfToFloat(originalHalf[halfIndex + 2]),
                };
                const parity::Rgba8 expected = parity::encodeProxy(original, 1.0, neutral);
                const uint8_t* row = proxyBytes.data() + y * rgba8Row;
                maxCodeDelta = std::max(maxCodeDelta, std::abs(static_cast<double>(row[x * 4 + 0]) - expected.r));
                maxCodeDelta = std::max(maxCodeDelta, std::abs(static_cast<double>(row[x * 4 + 1]) - expected.g));
                maxCodeDelta = std::max(maxCodeDelta, std::abs(static_cast<double>(row[x * 4 + 2]) - expected.b));
                maxAlphaDelta = std::max(maxAlphaDelta, std::abs(static_cast<double>(row[x * 4 + 3]) - expected.a));
            }
        }
        log::info("harness", std::format("parity: gpu-vs-cpu encode maxCodeDelta={} maxAlphaDelta={}", maxCodeDelta, maxAlphaDelta));

        // --- Feature 18 evaluates on the fixed proxy.
        uint64_t evaluateSucceeded = 0;
        for (uint32_t frame = 0; frame < evaluateFrames; ++frame) {
            ID3D12GraphicsCommandList* list = ring.acquire(frame % 4, status);
            if (list == nullptr) {
                goto parity_teardown;
            }
            namespace p = ngx::dlssnr;
            ngx::ParameterBlock pb(params);
            pb.setD3D12Resource(p::kColor, gpu.proxy.Get());
            pb.setD3D12Resource(p::kOutput, gpu.neural.Get());
            pb.setD3D12Resource(p::kMVec, motion.Get());
            pb.setD3D12Resource(p::kDepth, depth.Get());
            pb.setU32(p::kColorSubrectBaseX, 0);
            pb.setU32(p::kColorSubrectBaseY, 0);
            pb.setU32(p::kColorSubrectWidth, width);
            pb.setU32(p::kColorSubrectHeight, height);
            pb.setU32(p::kOutputSubrectBaseX, 0);
            pb.setU32(p::kOutputSubrectBaseY, 0);
            pb.setU32(p::kOutputSubrectWidth, width);
            pb.setU32(p::kOutputSubrectHeight, height);
            pb.setU32(p::kMVecSubrectBaseX, 0);
            pb.setU32(p::kMVecSubrectBaseY, 0);
            pb.setU32(p::kMVecSubrectWidth, width);
            pb.setU32(p::kMVecSubrectHeight, height);
            pb.setU32(p::kDepthSubrectBaseX, 0);
            pb.setU32(p::kDepthSubrectBaseY, 0);
            pb.setU32(p::kDepthSubrectWidth, width);
            pb.setU32(p::kDepthSubrectHeight, height);
            pb.setF32(p::kMVecScaleX, 1.0f);
            pb.setF32(p::kMVecScaleY, 1.0f);
            pb.setI32(p::kDepthInverted, 1);
            pb.setI32(p::kIndicatorInvertX, 0);
            pb.setI32(p::kIndicatorInvertY, 0);
            pb.setI32(p::kEnabled, 1);
            pb.setI32(p::kReset, frame == 0 ? 1 : 0);
            pb.setI32(p::kStyle, 0);
            pb.setF32(p::kIntensity, 1.0f);
            pb.setF32(p::kLocalToneStrength, 1.0f);
            pb.setF32(p::kLocalStructureStrength, 1.0f);
            pb.setF32(p::kSkinStructureStrength, -1.0f);
            pb.setI32(p::kUseAutoMask, 0);
            pb.setI32(p::kUICorrection, 0);

            uint64_t evalResult = 0;
            uint32_t evalSeh = 0;
            if (!adapter.snippetEvaluateFeature(list, handle, params, evalResult, evalSeh) ||
                evalResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                log::error("harness", std::format("parity: Evaluate frame={} failed result={}",
                    frame, ngxResultString(evalResult)));
                goto parity_teardown;
            }
            ++evaluateSucceeded;
            D3D12_RESOURCE_BARRIER uavBarrier{};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = gpu.neural.Get();
            list->ResourceBarrier(1, &uavBarrier);
            if (!ring.submitAndSignal(frame % 4)) {
                goto parity_teardown;
            }
        }
        if (!ring.waitIdle()) {
            goto parity_teardown;
        }

        // --- ParityDecode: pre-pass transitions, then the dispatch list.
        {
            ID3D12GraphicsCommandList* list = ring.acquire(2, status);
            if (list == nullptr) {
                goto parity_teardown;
            }
            transition(list, gpu.neural.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transition(list, gpu.final.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (!ring.submitAndSignal(2) || !ring.waitIdle()) {
                goto parity_teardown;
            }
        }
        {
            ID3D12GraphicsCommandList* list = ring.acquire(2, status);
            if (list == nullptr) {
                goto parity_teardown;
            }
            list->SetDescriptorHeaps(1, heaps);
            list->SetPipelineState(decodePso.Get());
            list->SetComputeRootSignature(rootSignature.Get());
            list->SetComputeRoot32BitConstants(0, 8, settingsConstants, 0);
            list->SetComputeRootDescriptorTable(1, { gpuHandle.ptr });              // SRVs: original, proxy, neural
            list->SetComputeRootDescriptorTable(2, { gpuHandle.ptr + 5ull * increment }); // UAV: final at offset 5
            list->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
            transition(list, gpu.final.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            if (!ring.submitAndSignal(2) || !ring.waitIdle()) {
                goto parity_teardown;
            }
        }

        // --- Readbacks: neural (rgba8) and final (fp16).
        std::vector<uint8_t> neuralBytes;
        std::vector<uint8_t> finalBytes;
        if (!copyOut(gpu.neural.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, rgba8Row,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, neuralBytes)) {
            goto parity_teardown;
        }
        {
            // final currently COPY_SOURCE after the decode transition above.
            ID3D12GraphicsCommandList* list = ring.acquire(3, status);
            if (list != nullptr) {
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                footprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                footprint.Footprint.Width = width;
                footprint.Footprint.Height = height;
                footprint.Footprint.Depth = 1;
                footprint.Footprint.RowPitch = static_cast<UINT>(originalRow);
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint = footprint;
                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = gpu.final.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                transition(list, gpu.final.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if (ring.submitAndSignal(3) && ring.waitIdle()) {
                    void* mapped = nullptr;
                    D3D12_RANGE range{ 0, originalRow * height };
                    if (SUCCEEDED(readback->Map(0, &range, &mapped))) {
                        finalBytes.assign(static_cast<const uint8_t*>(mapped),
                            static_cast<const uint8_t*>(mapped) + originalRow * height);
                        readback->Unmap(0, nullptr);
                    }
                }
            }
        }
        if (neuralBytes.empty() || finalBytes.empty()) {
            goto parity_teardown;
        }

        // --- GPU vs CPU decode comparison (Playbook 9.5). The bound is
        // abs <= 0.002 where FP16 represents it; above ~4.0 one stored ulp
        // (0.0039) exceeds 0.002, and highlight-amplified fp32-vs-double
        // intermediates can legitimately cross a bucket boundary. Same-intent
        // bound: no channel beyond ONE stored ulp, and <=0.002 wherever that
        // is representable.
        const auto halfUlp = [](uint16_t h) -> double {
            const uint32_t exponent = (h >> 10) & 0x1Fu;
            if (exponent == 0) {
                return 5.9604644775390625e-08; // 2^-24 subnormal step
            }
            return std::ldexp(1.0, static_cast<int>(exponent) - 25);
        };
        double maxAbsError = 0.0;
        uint64_t nanInfCount = 0;
        uint64_t beyondOneUlpCount = 0;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t index = (static_cast<size_t>(y) * width + x) * 4;
                const parity::RgbF original{
                    halfToFloat(originalHalf[index + 0]),
                    halfToFloat(originalHalf[index + 1]),
                    halfToFloat(originalHalf[index + 2]),
                };
                const uint8_t* proxyRow = proxyBytes.data() + y * rgba8Row;
                const uint8_t* neuralRow = neuralBytes.data() + y * rgba8Row;
                const parity::RgbF proxyLinear{
                    parity::srgbDecode(proxyRow[x * 4 + 0] / 255.0),
                    parity::srgbDecode(proxyRow[x * 4 + 1] / 255.0),
                    parity::srgbDecode(proxyRow[x * 4 + 2] / 255.0),
                };
                const parity::RgbF neuralLinear{
                    parity::srgbDecode(neuralRow[x * 4 + 0] / 255.0),
                    parity::srgbDecode(neuralRow[x * 4 + 1] / 255.0),
                    parity::srgbDecode(neuralRow[x * 4 + 2] / 255.0),
                };
                const parity::RgbF expected = parity::parityDecode(original, proxyLinear, neuralLinear, neutral);
                // The Final texture is FP16: quantize the CPU expectation to
                // FP16 first so the tolerance measures shader math error, not
                // storage quantization (1 ulp at [4,8) is 0.0039 by design).
                const float expectedFp16[3] = {
                    halfToFloat(floatToHalf(static_cast<float>(expected.r))),
                    halfToFloat(floatToHalf(static_cast<float>(expected.g))),
                    halfToFloat(floatToHalf(static_cast<float>(expected.b))),
                };
                const uint16_t* finalRow = reinterpret_cast<const uint16_t*>(finalBytes.data() + y * originalRow);
                const float actual[3] = {
                    halfToFloat(finalRow[x * 4 + 0]),
                    halfToFloat(finalRow[x * 4 + 1]),
                    halfToFloat(finalRow[x * 4 + 2]),
                };
                for (int channel = 0; channel < 3; ++channel) {
                    if (!std::isfinite(actual[channel])) {
                        ++nanInfCount;
                    }
                    const double channelDiff = std::abs(static_cast<double>(actual[channel]) - expectedFp16[channel]);
                    if (channelDiff > 0.0025) {
                        const double cpuValue = channel == 0 ? expected.r : (channel == 1 ? expected.g : expected.b);
                        log::warn("harness", std::format("parity: one-ulp pixel x={} y={} ch={} actual={:.9g} expectedFp16={:.9g} cpu={:.12g} actualBits={:04X} myExpectedBits={:04X}",
                            x, y, channel, actual[channel], expectedFp16[channel], cpuValue,
                            static_cast<unsigned>(finalRow[x * 4 + channel]),
                            static_cast<unsigned>(floatToHalf(static_cast<float>(cpuValue)))));
                    }
                    const double allowed = std::max(0.002, halfUlp(finalRow[x * 4 + channel]));
                    if (channelDiff > allowed) {
                        ++beyondOneUlpCount;
                    }
                    maxAbsError = std::max(maxAbsError, channelDiff);
                }
            }
        }
        log::info("harness", std::format("parity: gpu-vs-cpu decode maxAbsError={} nanInf={} beyondOneUlp={}", maxAbsError, nanInfCount, beyondOneUlpCount));

        // --- Four-stage captures (Playbook 9.1 / 17.2).
        const std::wstring captureDir = args.captureDir.empty() ? std::wstring(L"captures\\phase2") : args.captureDir;
        std::error_code mkdirError;
        std::filesystem::create_directories(captureDir, mkdirError);
        const bool capturesOk = [&] {
            const std::wstring originalBin = captureDir + L"\\00_original.rgba16f.bin";
            const std::wstring proxyPng = captureDir + L"\\01_proxy.png";
            const std::wstring rawPng = captureDir + L"\\02_raw_dlssnr.png";
            const std::wstring finalBin = captureDir + L"\\03_final.rgba16f.bin";
            const std::wstring previewPng = captureDir + L"\\03_final-preview.png";
            bool ok = writeTextFileUtf8(originalBin,
                std::string(reinterpret_cast<const char*>(originalHalf.data()), originalHalf.size() * 2));
            ok = writeRgbaPng(proxyPng, proxyBytes.data(), width, height, rgba8Row) && ok;
            ok = writeRgbaPng(rawPng, neuralBytes.data(), width, height, rgba8Row) && ok;
            ok = writeTextFileUtf8(finalBin,
                std::string(reinterpret_cast<const char*>(finalBytes.data()), finalBytes.size())) && ok;

            // sRGB preview of the FP16 final (preview only; the .bin is raw).
            std::vector<uint8_t> preview(static_cast<size_t>(width) * height * 4);
            for (uint32_t y = 0; y < height; ++y) {
                const uint16_t* finalRow = reinterpret_cast<const uint16_t*>(finalBytes.data() + y * originalRow);
                for (uint32_t x = 0; x < width; ++x) {
                    for (int channel = 0; channel < 3; ++channel) {
                        const double linear = halfToFloat(finalRow[x * 4 + channel]);
                        const double encoded = parity::srgbEncode(std::min(std::max(linear, 0.0), 1.0));
                        preview[(static_cast<size_t>(y) * width + x) * 4 + channel] =
                            static_cast<uint8_t>(std::lround(encoded * 255.0));
                    }
                    preview[(static_cast<size_t>(y) * width + x) * 4 + 3] = 255;
                }
            }
            ok = writeRgbaPng(previewPng, preview.data(), width, height, static_cast<size_t>(width) * 4) && ok;

            std::string runtimeSha;
            {
                FileIdentity dllIdentity{};
                IdentityError dllError = IdentityError::None;
                if (computeFileIdentity(args.runtimeDir + L"\\nvngx_dlssnr.dll", dllIdentity, dllError)) {
                    runtimeSha = dllIdentity.sha256Upper;
                }
            }
            const std::string stageJson = std::format(
                "{{\n  \"schema\": 1,\n  \"width\": {},\n  \"height\": {},\n"
                "  \"formats\": {{\"original\": \"R16G16B16A16_FLOAT\", \"proxy\": \"R8G8B8A8_UNORM\","
                " \"raw\": \"R8G8B8A8_UNORM\", \"final\": \"R16G16B16A16_FLOAT\"}},\n"
                "  \"colorSpace\": \"linear BT.709 working RGB\",\n"
                "  \"rowPitch\": {{\"rgba16f\": {}, \"rgba8\": {}}},\n"
                "  \"profile\": {{\"paperWhiteScale\": 1.0, \"transferStrength\": 1.0, \"colorStrength\": 1.0}},\n"
                "  \"runtimeSha256\": \"{}\",\n"
                "  \"pts\": null,\n"
                "  \"originalSource\": \"host-upload (byte-identical to the GPU texture upload)\",\n"
                "  \"frameId\": {},\n  \"runId\": \"{}\"\n}}\n",
                width, height, width * 8, width * 4, jsonEscape(runtimeSha),
                evaluateFrames - 1, jsonEscape(narrowText(args.runId)));
            ok = writeTextFileUtf8(captureDir + L"\\00_original.json", stageJson) && ok;
            ok = writeTextFileUtf8(captureDir + L"\\03_final.json", stageJson) && ok;
            return ok;
        }();
        log::info("harness", std::format("parity: four-stage captures written={}", capturesOk));

        const RgbaStats rawStats = analyzeRgba(neuralBytes.data(), width, height, rgba8Row);
        const RgbaStats proxyStats = analyzeRgba(proxyBytes.data(), width, height, rgba8Row);
        double finalMeanLuma = 0.0;
        {
            double lumaSum = 0.0;
            uint64_t lumaCount = 0;
            for (uint32_t y = 0; y < height; ++y) {
                const uint16_t* finalRow = reinterpret_cast<const uint16_t*>(finalBytes.data() + y * originalRow);
                for (uint32_t x = 0; x < width; ++x) {
                    const double r = halfToFloat(finalRow[x * 4 + 0]);
                    const double g = halfToFloat(finalRow[x * 4 + 1]);
                    const double b = halfToFloat(finalRow[x * 4 + 2]);
                    lumaSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
                    ++lumaCount;
                }
            }
            finalMeanLuma = lumaCount > 0 ? lumaSum / static_cast<double>(lumaCount) : 0.0;
        }
        const std::string finalSha = sha256Hex(reinterpret_cast<const uint8_t*>(finalBytes.data()), finalBytes.size());

        // --- Reverse teardown (Playbook 8.8).
        uint64_t releaseResultValue = 0;
        uint32_t releaseSeh = 0;
        const bool releaseOk = adapter.snippetReleaseFeature(handle, releaseResultValue, releaseSeh) &&
            releaseResultValue == static_cast<uint64_t>(NVSDK_NGX_Result_Success);
        coreHost.destroyParameters(params);
        params = nullptr;
        uint64_t snippetShutdownValue = 0;
        uint32_t snippetShutdownSeh = 0;
        (void)adapter.snippetShutdown1(context.device(), snippetShutdownValue, snippetShutdownSeh);
        adapter.restoreCallerCompatibility();
        adapter.unload();
        ring.shutdown();
        coreHost.shutdown();
        context.shutdown();

        const bool allOk = evaluateSucceeded == evaluateFrames && capturesOk &&
            maxCodeDelta <= 1.0 && beyondOneUlpCount == 0 && nanInfCount == 0 && releaseOk;

        // --- JSON summary (phase2 gate contract).
        std::string exeSha;
        {
            FileIdentity exeIdentity{};
            IdentityError exeError = IdentityError::None;
            if (computeFileIdentity(ownExePath(), exeIdentity, exeError)) {
                exeSha = exeIdentity.sha256Upper;
            }
        }
        std::string addonSha;
        {
            FileIdentity addonIdentity{};
            IdentityError addonError = IdentityError::None;
            if (computeFileIdentity(L"renodx-dlss5-1.addon64", addonIdentity, addonError)) {
                addonSha = addonIdentity.sha256Upper;
            }
        }
        std::string json;
        json += "{\n";
        json += "  \"probe\": \"veyra_nr_harness_parity\",\n";
        json += std::format("  \"runId\": \"{}\",\n", jsonEscape(narrowText(args.runId)));
        json += std::format("  \"exeSha256\": \"{}\",\n", jsonEscape(exeSha));
        json += std::format("  \"osBuild\": \"{}\",\n", jsonEscape(osBuildString()));
        json += std::format("  \"frames\": {},\n", evaluateSucceeded);
        json += std::format("  \"encode\": {{\"maxCodeDelta\": {:.4}, \"maxAlphaDelta\": {:.4}}},\n", maxCodeDelta, maxAlphaDelta);
        json += std::format("  \"decode\": {{\"maxAbsError\": {:.6}, \"nanInfCount\": {}, \"beyondOneUlpCount\": {}}},\n", maxAbsError, nanInfCount, beyondOneUlpCount);
        json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}},\n",
            parityInfoQueueActive ? "true" : "false", parityInfoQueueStored, parityInfoQueueErrors);
        json += std::format("  \"stageLuma\": {{\"proxy\": {:.4}, \"raw\": {:.4}, \"final\": {:.4}}},\n",
            proxyStats.meanLuma, rawStats.meanLuma, finalMeanLuma);
        json += std::format("  \"baseline\": {{\"paperWhiteScale\": 1.0, \"transferStrength\": 1.0, \"colorStrength\": 1.0, \"addonSha256\": \"{}\", \"source\": \"V1 neutral engineering baseline (no ReShade preset exists in this workspace; addon never loaded)\"}},\n",
            jsonEscape(addonSha));
        json += std::format("  \"captureHashes\": {{\"rawDlssnr\": \"{}\", \"final\": \"{}\"}}\n",
            jsonEscape(rawStats.sha256), jsonEscape(finalSha));
        json += "}\n";
        if (!args.jsonFile.empty()) {
            const bool jsonOk = writeTextFileUtf8(args.jsonFile, json);
            log::info("harness", std::format("parity: json written={} path={}", jsonOk, narrowText(args.jsonFile)));
        }

        log::info("harness", allOk ? "parity: PASS" : "parity: FAIL");
        // Always drain so a PASSING debug run carries real infoqueue numbers
        // into the log/JSON (Reviewer P1 fix).
        drainInfoQueue();
        return allOk ? 0 : 12;
    }

parity_teardown:
    if (params != nullptr) {
        coreHost.destroyParameters(params);
    }
    adapter.restoreCallerCompatibility();
    adapter.unload();
    ring.shutdown();
    coreHost.shutdown();
    context.shutdown();
    log::error("harness", "parity: FAIL (setup or execution error)");
    return 11;
}

} // namespace veyra::harness
