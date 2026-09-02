#include "frame_loop.h"

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <nvsdk_ngx.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

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

namespace veyra::harness {

namespace {

std::string narrowText(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string converted(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), converted.data(), length, nullptr, nullptr);
    return converted;
}

std::string scanJsonStringField(const std::string& text, const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t position = text.find(needle);
    if (position == std::string::npos) {
        return {};
    }
    position = text.find(':', position + needle.size());
    if (position == std::string::npos) {
        return {};
    }
    position = text.find('"', position);
    if (position == std::string::npos) {
        return {};
    }
    const size_t start = position + 1;
    const size_t end = text.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return text.substr(start, end - start);
}

struct LocalIdentity {
    std::string projectId;
    std::string engineVersion;
};

bool loadLocalIdentity(const std::wstring& runtimeDir, LocalIdentity& identity)
{
    const std::wstring configPath = runtimeDir + L"\\..\\config\\ngx-local.json";
    std::ifstream stream(configPath, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    identity.projectId = scanJsonStringField(text, "ngxProjectId");
    identity.engineVersion = scanJsonStringField(text, "engineVersion");
    return !identity.projectId.empty() && !identity.engineVersion.empty();
}

// ---------------------------------------------------------------------------
// Minimal PNG writer (uncompressed deflate stored blocks): a real PNG any
// viewer opens, without linking a compression library (harness-only).
// ---------------------------------------------------------------------------
uint32_t crc32Of(const uint8_t* data, size_t size)
{
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32Of(const uint8_t* data, size_t size)
{
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

bool writeRgbaPng(const std::wstring& path, const uint8_t* pixels, uint32_t width, uint32_t height, size_t sourceRowPitch)
{
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw((rowBytes + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw[y * (rowBytes + 1)] = 0; // filter: none
        memcpy(raw.data() + y * (rowBytes + 1) + 1, pixels + y * sourceRowPitch, rowBytes);
    }

    std::vector<uint8_t> zlibStream;
    zlibStream.push_back(0x78);
    zlibStream.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(raw.size() - offset, 65535);
        const bool finalBlock = offset + chunk >= raw.size();
        zlibStream.push_back(finalBlock ? 1 : 0);
        zlibStream.push_back(static_cast<uint8_t>(chunk & 0xFF));
        zlibStream.push_back(static_cast<uint8_t>((chunk >> 8) & 0xFF));
        zlibStream.push_back(static_cast<uint8_t>(~chunk & 0xFF));
        zlibStream.push_back(static_cast<uint8_t>((~chunk >> 8) & 0xFF));
        zlibStream.insert(zlibStream.end(), raw.begin() + static_cast<ptrdiff_t>(offset),
            raw.begin() + static_cast<ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    const uint32_t adler = adler32Of(raw.data(), raw.size());
    zlibStream.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    zlibStream.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    zlibStream.push_back(static_cast<uint8_t>((adler >> 8) & 0xFF));
    zlibStream.push_back(static_cast<uint8_t>(adler & 0xFF));

    std::vector<uint8_t> png{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    const auto appendChunk = [&png](const char type[5], const std::vector<uint8_t>& payload) {
        const uint32_t length = static_cast<uint32_t>(payload.size());
        png.push_back(static_cast<uint8_t>((length >> 24) & 0xFF));
        png.push_back(static_cast<uint8_t>((length >> 16) & 0xFF));
        png.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
        png.push_back(static_cast<uint8_t>(length & 0xFF));
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), payload.begin(), payload.end());
        png.insert(png.end(), body.begin(), body.end());
        const uint32_t crc = crc32Of(body.data(), body.size());
        png.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));
        png.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
        png.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
        png.push_back(static_cast<uint8_t>(crc & 0xFF));
    };
    std::vector<uint8_t> ihdr(13);
    ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8_t>(width & 0xFF);
    ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8_t>(height & 0xFF);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // RGBA
    appendChunk("IHDR", ihdr);
    appendChunk("IDAT", zlibStream);
    appendChunk("IEND", {});

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, png.data(), static_cast<DWORD>(png.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == png.size();
}

struct RgbaStats {
    double meanLuma = 0.0;
    double minLuma = 1.0;
    double maxLuma = 0.0;
    double stddev = 0.0;
    bool allZero = true;
    bool constant = true;
    std::string sha256;
};

std::string jsonEscape(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                escaped += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            }
            else {
                escaped.push_back(c);
            }
            break;
        }
    }
    return escaped;
}

bool writeTextFileUtf8(const std::wstring& path, const std::string& content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == content.size();
}

std::wstring ownExePath()
{
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return {};
    }
    return std::wstring(buffer, length);
}

std::string osBuildString()
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return {};
    }
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (rtlGetVersion == nullptr) {
        return {};
    }
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) {
        return {};
    }
    return std::format("{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}

RgbaStats analyzeRgba(const uint8_t* pixels, uint32_t width, uint32_t height, size_t rowPitch)
{
    RgbaStats stats;
    double sum = 0.0;
    double sumSquares = 0.0;
    uint64_t count = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels + y * rowPitch;
        for (uint32_t x = 0; x < width; ++x) {
            const double r = row[x * 4 + 0] / 255.0;
            const double g = row[x * 4 + 1] / 255.0;
            const double b = row[x * 4 + 2] / 255.0;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            sum += luma;
            sumSquares += luma * luma;
            stats.minLuma = std::min(stats.minLuma, luma);
            stats.maxLuma = std::max(stats.maxLuma, luma);
            if (row[x * 4] != 0 || row[x * 4 + 1] != 0 || row[x * 4 + 2] != 0) {
                stats.allZero = false;
            }
            ++count;
        }
    }
    const double mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
    const double variance = count > 0 ? std::max(0.0, sumSquares / static_cast<double>(count) - mean * mean) : 0.0;
    stats.meanLuma = mean;
    stats.stddev = std::sqrt(variance);
    stats.constant = stats.maxLuma == stats.minLuma;
    std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(packed.data() + static_cast<size_t>(y) * width * 4, pixels + y * rowPitch, static_cast<size_t>(width) * 4);
    }
    stats.sha256 = sha256Hex(packed.data(), packed.size());
    return stats;
}

} // namespace

int runFrameLoop(const FrameLoopArgs& args)
{
    const uint32_t width = args.width;
    const uint32_t height = args.height;
    uint64_t evaluateAttempted = 0;
    uint64_t evaluateSucceeded = 0;
    uint64_t evaluateFailed = 0;
    bool deviceRemoved = false;
    uint32_t deviceRemovedReason = 0;
    RgbaStats finalStats;
    uint64_t initExtResult = 0;
    uint64_t createResult = 0;
    bool handleNonNull = false;
    uint64_t releaseResultValue = 0;
    uint64_t snippetShutdownResultValue = 0;
    std::string baselineSha;
    std::string variantStyleSha;
    std::string variantIntensitySha;
    bool timestampsNonZero = false;
    double gpuAvgMs = 0.0;

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

    // Real debug-layer observability (Reviewer P1 fix): capture the D3D12
    // info queue so "no state errors" is an assertion over retrieved
    // messages, not an empty grep. Debug builds only.
    gfx::ComPtr<ID3D12InfoQueue> infoQueue;
    bool infoQueueActive = false;
    uint64_t infoQueueStored = 0;
    uint64_t infoQueueErrors = 0;
#if defined(VEYRA_D3D12_DEBUG)
    if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetMuteDebugOutput(false);
        infoQueue->ClearStoredMessages();
        infoQueueActive = true;
        log::info("harness", "frame-loop: ID3D12InfoQueue attached and recording");
    }
    else {
        log::error("harness", "frame-loop: ID3D12InfoQuery unavailable while debug layer is expected");
    }
#endif
    gfx::CommandSlotRing ring;
    if (!ring.initialize(context.device(), context.directQueue(), context.fence(), context.fenceEvent(), 4, status)) {
        context.shutdown();
        return 7;
    }

    LocalIdentity identity{};
    if (!loadLocalIdentity(args.runtimeDir, identity)) {
        ring.shutdown();
        context.shutdown();
        return 7;
    }
    ngx::NgxCoreHost coreHost;
    if (!coreHost.initialize(context.device(), args.runtimeDir, identity.projectId.c_str(), identity.engineVersion.c_str(), status)) {
        ring.shutdown();
        context.shutdown();
        return 8;
    }
    ngx::DlssNrRuntimeAdapter adapter;
    if (!adapter.load(args.runtimeDir, status) || !adapter.installCallerCompatibility(status)) {
        adapter.unload();
        coreHost.shutdown();
        ring.shutdown();
        context.shutdown();
        return 9;
    }
    uint64_t result = 0;
    uint32_t sehCode = 0;
    if (!adapter.snippetInitExt(context.device(), args.runtimeDir, result, sehCode)) {
        initExtResult = result;
        adapter.restoreCallerCompatibility();
        adapter.unload();
        coreHost.shutdown();
        ring.shutdown();
        context.shutdown();
        return 10;
    }
    initExtResult = result;
    if (result != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
        adapter.restoreCallerCompatibility();
        adapter.unload();
        coreHost.shutdown();
        ring.shutdown();
        context.shutdown();
        return 10;
    }

    NVSDK_NGX_Parameter* params = coreHost.allocateParameters(status);
    NVSDK_NGX_Handle* handle = nullptr;
    if (params == nullptr) {
        goto teardown_fail;
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
            goto teardown_fail;
        }
        if (!adapter.snippetCreateFeature(list, params, &handle, result, sehCode) ||
            result != static_cast<uint64_t>(NVSDK_NGX_Result_Success) || handle == nullptr) {
            createResult = result;
            handleNonNull = handle != nullptr;
            goto teardown_fail;
        }
        createResult = result;
        handleNonNull = true;
        if (!ring.submitAndSignal(0) || !ring.waitIdle()) {
            goto teardown_fail;
        }
    }

    {
        const auto makeTexture = [&](DXGI_FORMAT format, const wchar_t* name) -> gfx::ComPtr<ID3D12Resource> {
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
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            gfx::ComPtr<ID3D12Resource> resource;
            const HRESULT hr = context.device()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
            if (FAILED(hr)) {
                log::error("harness", std::format("frame-loop: create {} failed hr={}",
                    narrowText(name), hresultString(hr)));
                return nullptr;
            }
            return resource;
        };

        auto proxy = makeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, L"Proxy");
        auto neural = makeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, L"Neural");
        auto motion = makeTexture(DXGI_FORMAT_R16G16_FLOAT, L"ZeroMotion");
        auto depth = makeTexture(DXGI_FORMAT_R32_FLOAT, L"ZeroDepth");
        auto confidence = makeTexture(DXGI_FORMAT_R8_UNORM, L"Confidence");
        if (proxy == nullptr || neural == nullptr || motion == nullptr || depth == nullptr || confidence == nullptr) {
            goto teardown_fail;
        }

        D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc{};
        uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uavHeapDesc.NumDescriptors = 8;
        uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        gfx::ComPtr<ID3D12DescriptorHeap> uavHeap;
        if (FAILED(context.device()->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap)))) {
            goto teardown_fail;
        }
        const UINT handleSize = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = uavHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = uavHeap->GetGPUDescriptorHandleForHeapStart();

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        context.device()->CreateUnorderedAccessView(proxy.Get(), nullptr, &uavDesc, cpuBase);
        context.device()->CreateUnorderedAccessView(neural.Get(), nullptr, &uavDesc, { cpuBase.ptr + handleSize });
        uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        context.device()->CreateUnorderedAccessView(motion.Get(), nullptr, &uavDesc, { cpuBase.ptr + 2ull * handleSize });
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        context.device()->CreateUnorderedAccessView(depth.Get(), nullptr, &uavDesc, { cpuBase.ptr + 3ull * handleSize });
        uavDesc.Format = DXGI_FORMAT_R8_UNORM;
        context.device()->CreateUnorderedAccessView(confidence.Get(), nullptr, &uavDesc, { cpuBase.ptr + 4ull * handleSize });

        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = 0;
        D3D12_ROOT_PARAMETER1 rootParameters[2]{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.RegisterSpace = 0;
        rootParameters[0].Constants.Num32BitValues = 4;
        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = &uavRange;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 2;
        rootDesc.Desc_1_1.pParameters = rootParameters;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        gfx::ComPtr<ID3DBlob> signatureBlob;
        gfx::ComPtr<ID3DBlob> errorBlob;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rootDesc, &signatureBlob, &errorBlob))) {
            log::error("harness", std::format("frame-loop: root signature serialize failed: {}",
                errorBlob != nullptr ? std::string(static_cast<const char*>(errorBlob->GetBufferPointer())) : std::string("?")));
            goto teardown_fail;
        }
        gfx::ComPtr<ID3D12RootSignature> rootSignature;
        if (FAILED(context.device()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) {
            goto teardown_fail;
        }

        const std::string shaderPath8 = VEYRA_SHADER_DIR "/GenerateTestPattern.dxil";
        HANDLE shaderFile = CreateFileA(shaderPath8.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (shaderFile == INVALID_HANDLE_VALUE) {
            log::error("harness", std::format("frame-loop: shader missing path={}", shaderPath8));
            goto teardown_fail;
        }
        LARGE_INTEGER shaderSize{};
        GetFileSizeEx(shaderFile, &shaderSize);
        std::vector<uint8_t> shaderBytes(static_cast<size_t>(shaderSize.QuadPart));
        DWORD shaderRead = 0;
        ReadFile(shaderFile, shaderBytes.data(), static_cast<DWORD>(shaderBytes.size()), &shaderRead, nullptr);
        CloseHandle(shaderFile);
        if (shaderBytes.empty()) {
            goto teardown_fail;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature.Get();
        psoDesc.CS.pShaderBytecode = shaderBytes.data();
        psoDesc.CS.BytecodeLength = shaderBytes.size();
        gfx::ComPtr<ID3D12PipelineState> pipelineState;
        if (FAILED(context.device()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
            log::error("harness", "frame-loop: compute pipeline state creation failed");
            goto teardown_fail;
        }
        log::info("harness", "frame-loop: test-pattern compute pipeline created");

        // One-time zero-init of the guidance textures via an upload buffer
        // copy (deterministic and validation-friendly; Playbook 6.4 zero
        // contract). Neural moves COMMON->UAV for Evaluate.
        {
            const auto zeroInitTexture = [&](ID3D12Resource* texture, DXGI_FORMAT format, uint32_t bytesPerPixel) -> bool {
                const size_t uploadRow = (static_cast<size_t>(width) * bytesPerPixel + 255) & ~size_t(255);
                const size_t uploadSize = uploadRow * height;
                static gfx::ComPtr<ID3D12Resource> upload; // reused across the three textures
                if (upload == nullptr) {
                    D3D12_HEAP_PROPERTIES uploadProps{};
                    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
                    D3D12_RESOURCE_DESC uploadDesc{};
                    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    uploadDesc.Width = uploadSize;
                    uploadDesc.Height = 1;
                    uploadDesc.DepthOrArraySize = 1;
                    uploadDesc.MipLevels = 1;
                    uploadDesc.SampleDesc.Count = 1;
                    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    if (FAILED(context.device()->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE,
                            &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
                        return false;
                    }
                }
                void* mapped = nullptr;
                if (FAILED(upload->Map(0, nullptr, &mapped))) {
                    return false;
                }
                memset(mapped, 0, uploadSize);
                upload->Unmap(0, nullptr);

                ID3D12GraphicsCommandList* list = ring.acquire(1, status);
                if (list == nullptr) {
                    return false;
                }
                D3D12_RESOURCE_BARRIER toDest{};
                toDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toDest.Transition.pResource = texture;
                toDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                toDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                toDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &toDest);

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                footprint.Footprint.Format = format;
                footprint.Footprint.Width = width;
                footprint.Footprint.Height = height;
                footprint.Footprint.Depth = 1;
                footprint.Footprint.RowPitch = static_cast<UINT>(uploadRow);
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = texture;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = upload.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint = footprint;
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                D3D12_RESOURCE_BARRIER toSrv{};
                toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSrv.Transition.pResource = texture;
                toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &toSrv);
                return ring.submitAndSignal(1) && ring.waitIdle();
            };

            if (!zeroInitTexture(motion.Get(), DXGI_FORMAT_R16G16_FLOAT, 4) ||
                !zeroInitTexture(depth.Get(), DXGI_FORMAT_R32_FLOAT, 4) ||
                !zeroInitTexture(confidence.Get(), DXGI_FORMAT_R8_UNORM, 1)) {
                log::error("harness", "frame-loop: zero-init upload failed");
                goto teardown_fail;
            }
            log::info("harness", "frame-loop: zero guidance textures initialized via upload copy");

            // Neural moves to UAV once for the whole loop.
            {
                ID3D12GraphicsCommandList* list = ring.acquire(1, status);
                if (list == nullptr) {
                    goto teardown_fail;
                }
                D3D12_RESOURCE_BARRIER toUav{};
                toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toUav.Transition.pResource = neural.Get();
                toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &toUav);
                if (!ring.submitAndSignal(1) || !ring.waitIdle()) {
                    goto teardown_fail;
                }
            }
        }

        const size_t rowPitch = (static_cast<size_t>(width) * 4 + 255) & ~size_t(255);
        const size_t bufferSize = rowPitch * height;
        D3D12_HEAP_PROPERTIES readbackProps{};
        readbackProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        gfx::ComPtr<ID3D12Resource> readback;
        if (FAILED(context.device()->CreateCommittedResource(&readbackProps, D3D12_HEAP_FLAG_NONE,
                &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
            goto teardown_fail;
        }

        const auto readTextureStats = [&](ID3D12Resource* texture, D3D12_RESOURCE_STATES before,
                                          const std::wstring& pngPath, RgbaStats& statsOut) -> bool {
            ID3D12GraphicsCommandList* list = ring.acquire(2, status);
            if (list == nullptr) {
                return false;
            }
            D3D12_RESOURCE_BARRIER toSource{};
            toSource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toSource.Transition.pResource = texture;
            toSource.Transition.StateBefore = before;
            toSource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            toSource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &toSource);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            footprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
            D3D12_RESOURCE_BARRIER back{};
            back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            back.Transition.pResource = texture;
            back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            back.Transition.StateAfter = before;
            back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &back);
            if (!ring.submitAndSignal(2) || !ring.waitIdle()) {
                return false;
            }
            void* mapped = nullptr;
            D3D12_RANGE readRange{ 0, bufferSize };
            if (FAILED(readback->Map(0, &readRange, &mapped))) {
                return false;
            }
            statsOut = analyzeRgba(static_cast<const uint8_t*>(mapped), width, height, rowPitch);
            bool pngOk = true;
            if (!pngPath.empty()) {
                pngOk = writeRgbaPng(pngPath, static_cast<const uint8_t*>(mapped), width, height, rowPitch);
            }
            readback->Unmap(0, nullptr);
            return pngOk;
        };

        log::info("harness", std::format("frame-loop: starting frames={} size={}x{} style={} intensity={}",
            args.frames, width, height, args.styleOverride, args.intensityOverride));
        D3D12_RESOURCE_STATES proxyState = D3D12_RESOURCE_STATE_COMMON;
        bool loopBroken = false;
        for (uint32_t frame = 0; frame < args.frames; ++frame) {
            ID3D12GraphicsCommandList* list = ring.acquire(frame % 4, status);
            if (list == nullptr) {
                loopBroken = true;
                break;
            }

            list->EndQuery(ring.timestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP,
                ring.timestampQueryIndex(frame % 4, false));

            D3D12_RESOURCE_BARRIER toUav{};
            toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toUav.Transition.pResource = proxy.Get();
            toUav.Transition.StateBefore = proxyState;
            toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &toUav);

            ID3D12DescriptorHeap* heaps[] = { uavHeap.Get() };
            list->SetDescriptorHeaps(1, heaps);
            list->SetPipelineState(pipelineState.Get());
            list->SetComputeRootSignature(rootSignature.Get());
            const UINT constants[4] = { frame, width, height, 0 };
            list->SetComputeRoot32BitConstants(0, 4, constants, 0);
            list->SetComputeRootDescriptorTable(1, { gpuBase.ptr });
            list->Dispatch((width + 15) / 16, (height + 15) / 16, 1);

            D3D12_RESOURCE_BARRIER toSrv{};
            toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toSrv.Transition.pResource = proxy.Get();
            toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &toSrv);
            proxyState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

            namespace p = ngx::dlssnr;
            ngx::ParameterBlock pb(params);
            pb.setD3D12Resource(p::kColor, proxy.Get());
            pb.setD3D12Resource(p::kOutput, neural.Get());
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
            pb.setI32(p::kStyle, args.styleOverride);
            pb.setF32(p::kIntensity, args.intensityOverride);
            pb.setF32(p::kLocalToneStrength, 1.0f);
            pb.setF32(p::kLocalStructureStrength, 1.0f);
            pb.setF32(p::kSkinStructureStrength, -1.0f);
            pb.setI32(p::kUseAutoMask, 0);
            pb.setI32(p::kUICorrection, 0);

            ++evaluateAttempted;
            uint64_t evalResult = 0;
            uint32_t evalSeh = 0;
            if (!adapter.snippetEvaluateFeature(list, handle, params, evalResult, evalSeh)) {
                ++evaluateFailed;
                loopBroken = true;
                break;
            }
            if (evalResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                ++evaluateFailed;
                log::error("harness", std::format("frame-loop: Evaluate frame={} result={} seh={}",
                    frame, ngxResultString(evalResult), evalSeh));
                loopBroken = true;
                break;
            }
            ++evaluateSucceeded;

            D3D12_RESOURCE_BARRIER uavBarrier{};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = neural.Get();
            list->ResourceBarrier(1, &uavBarrier);

            list->EndQuery(ring.timestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP,
                ring.timestampQueryIndex(frame % 4, true));

            if (!ring.submitAndSignal(frame % 4)) {
                loopBroken = true;
                break;
            }

            if (frame == args.captureFrame && !args.captureDir.empty()) {
                if (!ring.waitIdle()) {
                    loopBroken = true;
                    break;
                }
                RgbaStats proxyStats{};
                const bool proxyOk = readTextureStats(proxy.Get(), proxyState,
                    args.captureDir + L"\\frame0000_proxy.png", proxyStats);
                const bool rawOk = readTextureStats(neural.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    args.captureDir + L"\\frame0000_raw.png", finalStats);
                log::info("harness", std::format("frame-loop: captures frame={} proxy={} raw={} proxySha={}",
                    frame, proxyOk, rawOk, proxyStats.sha256));
            }
        }
        (void)ring.waitIdle();
        if (loopBroken) {
            log::error("harness", "frame-loop: stopped early after error");
        }

        if (!loopBroken || evaluateSucceeded > 0) {
            RgbaStats endStats{};
            const std::wstring noPng;
            if (readTextureStats(neural.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, noPng, endStats)) {
                finalStats = endStats;
            }
        }
        baselineSha = finalStats.sha256;

        // --- Variant segments: same feature, changed Style / Intensity.
        const auto runVariant = [&](int style, float intensity, uint32_t count, std::string& shaOut) -> bool {
            for (uint32_t i = 0; i < count; ++i) {
                ID3D12GraphicsCommandList* list = ring.acquire(i % 4, status);
                if (list == nullptr) {
                    return false;
                }
                D3D12_RESOURCE_BARRIER toUav2{};
                toUav2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toUav2.Transition.pResource = proxy.Get();
                toUav2.Transition.StateBefore = proxyState;
                toUav2.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                toUav2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &toUav2);
                ID3D12DescriptorHeap* heaps2[] = { uavHeap.Get() };
                list->SetDescriptorHeaps(1, heaps2);
                list->SetPipelineState(pipelineState.Get());
                list->SetComputeRootSignature(rootSignature.Get());
                const UINT constants2[4] = { i, width, height, 0 };
                list->SetComputeRoot32BitConstants(0, 4, constants2, 0);
                list->SetComputeRootDescriptorTable(1, { gpuBase.ptr });
                list->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
                D3D12_RESOURCE_BARRIER toSrv2{};
                toSrv2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSrv2.Transition.pResource = proxy.Get();
                toSrv2.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                toSrv2.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                toSrv2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &toSrv2);
                proxyState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

                namespace p = ngx::dlssnr;
                ngx::ParameterBlock pb(params);
                pb.setD3D12Resource(p::kColor, proxy.Get());
                pb.setD3D12Resource(p::kOutput, neural.Get());
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
                pb.setI32(p::kReset, i == 0 ? 1 : 0);
                pb.setI32(p::kStyle, style);
                pb.setF32(p::kIntensity, intensity);
                pb.setF32(p::kLocalToneStrength, 1.0f);
                pb.setF32(p::kLocalStructureStrength, 1.0f);
                pb.setF32(p::kSkinStructureStrength, -1.0f);
                pb.setI32(p::kUseAutoMask, 0);
                pb.setI32(p::kUICorrection, 0);

                uint64_t evalResult2 = 0;
                uint32_t evalSeh2 = 0;
                if (!adapter.snippetEvaluateFeature(list, handle, params, evalResult2, evalSeh2) ||
                    evalResult2 != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                    return false;
                }
                D3D12_RESOURCE_BARRIER uav2{};
                uav2.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                uav2.UAV.pResource = neural.Get();
                list->ResourceBarrier(1, &uav2);
                if (!ring.submitAndSignal(i % 4)) {
                    return false;
                }
            }
            if (!ring.waitIdle()) {
                return false;
            }
            RgbaStats variantStats{};
            const std::wstring noPng;
            if (!readTextureStats(neural.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, noPng, variantStats)) {
                return false;
            }
            shaOut = variantStats.sha256;
            return true;
        };

        if (!loopBroken) {
            const bool v1 = runVariant(1, 1.0f, 8, variantStyleSha);
            const bool v2 = runVariant(0, 0.5f, 8, variantIntensitySha);
            log::info("harness", std::format("frame-loop: variants style1={} intensity05={}",
                v1 ? "ok" : "FAILED", v2 ? "ok" : "FAILED"));
        }

        // --- Timestamp resolve (8 queries into a readback buffer).
        {
            D3D12_HEAP_PROPERTIES tsProps{};
            tsProps.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC tsDesc{};
            tsDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            tsDesc.Width = 64;
            tsDesc.Height = 1;
            tsDesc.DepthOrArraySize = 1;
            tsDesc.MipLevels = 1;
            tsDesc.SampleDesc.Count = 1;
            tsDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            gfx::ComPtr<ID3D12Resource> tsReadback;
            if (SUCCEEDED(context.device()->CreateCommittedResource(&tsProps, D3D12_HEAP_FLAG_NONE,
                    &tsDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tsReadback)))) {
                ID3D12GraphicsCommandList* list = ring.acquire(3, status);
                if (list != nullptr) {
                    list->ResolveQueryData(ring.timestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 8, tsReadback.Get(), 0);
                    if (ring.submitAndSignal(3) && ring.waitIdle()) {
                        void* mapped = nullptr;
                        D3D12_RANGE range{ 0, 64 };
                        if (SUCCEEDED(tsReadback->Map(0, &range, &mapped))) {
                            const auto* stamps = static_cast<const uint64_t*>(mapped);
                            UINT64 frequency = 0;
                            UINT64 totalNs = 0;
                            uint32_t usedPairs = 0;
                            bool allNonZero = true;
                            if (SUCCEEDED(context.directQueue()->GetTimestampFrequency(&frequency)) && frequency > 0) {
                                for (uint32_t slot = 0; slot < 4; ++slot) {
                                    const uint64_t begin = stamps[slot * 2];
                                    const uint64_t end = stamps[slot * 2 + 1];
                                    if (begin == 0 || end == 0) {
                                        allNonZero = false;
                                        continue;
                                    }
                                    totalNs += (end - begin) * 1000000000ull / frequency;
                                    ++usedPairs;
                                }
                            }
                            timestampsNonZero = allNonZero && usedPairs == 4;
                            if (usedPairs > 0) {
                                gpuAvgMs = static_cast<double>(totalNs) / usedPairs / 1000000.0;
                            }
                            tsReadback->Unmap(0, nullptr);
                        }
                    }
                }
            }
            log::info("harness", std::format("frame-loop: gpu timestamps nonZero={} avgMs={:.4}", timestampsNonZero, gpuAvgMs));
        }

        uint32_t removedReason = 0;
        deviceRemoved = !context.checkDeviceAlive(removedReason);
        deviceRemovedReason = removedReason;

        // Drain the debug info queue into the log/JSON (Reviewer P1 fix).
        if (infoQueue != nullptr) {
            infoQueueStored = infoQueue->GetNumStoredMessages();
            uint64_t reported = 0;
            for (uint64_t i = 0; i < infoQueueStored && reported < 200; ++i) {
                SIZE_T length = 0;
                if (FAILED(infoQueue->GetMessage(i, nullptr, &length)) || length == 0) {
                    continue;
                }
                std::vector<uint8_t> buffer(length);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
                if (FAILED(infoQueue->GetMessage(i, message, &length))) {
                    continue;
                }
                ++reported;
                const int severity = static_cast<int>(message->Severity);
                if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                    message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                    ++infoQueueErrors;
                    log::error("harness", std::format("D3D12 infoqueue[{}] id={} desc={}",
                        severity, static_cast<unsigned>(message->ID), message->pDescription));
                }
                else {
                    log::warn("harness", std::format("D3D12 infoqueue[{}] id={} desc={}",
                        severity, static_cast<unsigned>(message->ID), message->pDescription));
                }
            }
            log::info("harness", std::format("frame-loop: infoqueue stored={} reported={} errors={}",
                infoQueueStored, reported, infoQueueErrors));
        }

        log::info("harness", std::format("frame-loop: done attempted={} succeeded={} failed={} deviceRemoved={}",
            evaluateAttempted, evaluateSucceeded, evaluateFailed, deviceRemoved));
        log::info("harness", std::format("frame-loop: output stats meanLuma={:.5} minLuma={:.5} maxLuma={:.5} stddev={:.5} allZero={} constant={} sha256={}",
            finalStats.meanLuma, finalStats.minLuma, finalStats.maxLuma, finalStats.stddev,
            finalStats.allZero, finalStats.constant, finalStats.sha256));

        uint64_t releaseResult = 0;
        uint32_t releaseSeh = 0;
        const bool releaseOk = adapter.snippetReleaseFeature(handle, releaseResult, releaseSeh) &&
            releaseResult == static_cast<uint64_t>(NVSDK_NGX_Result_Success);
        releaseResultValue = releaseResult;
        coreHost.destroyParameters(params);
        params = nullptr;
        uint64_t snippetShutdownResult = 0;
        uint32_t snippetShutdownSeh = 0;
        (void)adapter.snippetShutdown1(context.device(), snippetShutdownResult, snippetShutdownSeh);
        snippetShutdownResultValue = snippetShutdownResult;
        adapter.restoreCallerCompatibility();
        adapter.unload();
        ring.shutdown();
        coreHost.shutdown();
        context.shutdown();
        log::info("harness", std::format("frame-loop: teardown release={} snippetShutdown={}",
            ngxResultString(releaseResult), ngxResultString(snippetShutdownResult)));

        const bool allOk = !loopBroken && evaluateSucceeded == args.frames && !deviceRemoved &&
            !finalStats.allZero && !finalStats.constant &&
            !variantStyleSha.empty() && !variantIntensitySha.empty() &&
            variantStyleSha != baselineSha && variantIntensitySha != baselineSha &&
            variantStyleSha != variantIntensitySha && timestampsNonZero && releaseOk;

        // --- JSON summary (phase1 gate contract).
        std::string exeSha;
        {
            FileIdentity exeIdentity{};
            IdentityError exeError = IdentityError::None;
            if (computeFileIdentity(ownExePath(), exeIdentity, exeError)) {
                exeSha = exeIdentity.sha256Upper;
            }
        }
        std::string json;
        json += "{\n";
        json += "  \"probe\": \"veyra_nr_harness\",\n";
        json += std::format("  \"runId\": \"{}\",\n", jsonEscape(narrowText(args.runId)));
        json += std::format("  \"exeSha256\": \"{}\",\n", jsonEscape(exeSha));
        json += std::format("  \"osBuild\": \"{}\",\n", jsonEscape(osBuildString()));
        json += std::format("  \"debugLayer\": {},\n", context.debugLayerEnabled() ? "true" : "false");
        json += std::format("  \"initExt\": {{\"result\": {}, \"success\": {}}},\n",
            initExtResult, initExtResult == 1ull ? "true" : "false");
        json += std::format("  \"createFeature\": {{\"result\": {}, \"success\": {}, \"handleNonNull\": {}}},\n",
            createResult, createResult == 1ull ? "true" : "false", handleNonNull ? "true" : "false");
        json += std::format("  \"evaluate\": {{\"attempted\": {}, \"succeeded\": {}, \"failed\": {}}},\n",
            evaluateAttempted, evaluateSucceeded, evaluateFailed);
        json += std::format("  \"output\": {{\"meanLuma\": {:.6}, \"minLuma\": {:.6}, \"maxLuma\": {:.6}, \"stddev\": {:.6}, \"sha256\": \"{}\", \"allZero\": {}, \"constant\": {}}},\n",
            finalStats.meanLuma, finalStats.minLuma, finalStats.maxLuma, finalStats.stddev,
            jsonEscape(finalStats.sha256), finalStats.allZero ? "true" : "false",
            finalStats.constant ? "true" : "false");
        json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}},\n",
            infoQueueActive ? "true" : "false", infoQueueStored, infoQueueErrors);
        json += "  \"variants\": [\n";
        json += std::format("    {{\"name\": \"style1\", \"sha256\": \"{}\"}},\n", jsonEscape(variantStyleSha));
        json += std::format("    {{\"name\": \"intensity05\", \"sha256\": \"{}\"}}\n", jsonEscape(variantIntensitySha));
        json += "  ],\n";
        json += std::format("  \"gpu\": {{\"timestampNonZero\": {}, \"avgMs\": {:.6}}},\n",
            timestampsNonZero ? "true" : "false", gpuAvgMs);
        json += std::format("  \"release\": {{\"featureReleased\": {}, \"shutdownResult\": {}, \"cleanExit\": {}}},\n",
            releaseOk ? "true" : "false", releaseResultValue, releaseOk ? "true" : "false");
        json += std::format("  \"deviceRemoved\": {},\n", deviceRemoved ? "true" : "false");
        json += std::format("  \"deviceRemovedReason\": {}\n", deviceRemovedReason);
        json += "}\n";
        if (!args.jsonFile.empty()) {
            const bool jsonOk = writeTextFileUtf8(args.jsonFile, json);
            log::info("harness", std::format("frame-loop: json written={} path={}", jsonOk, narrowText(args.jsonFile)));
        }

        log::info("harness", allOk ? "frame-loop: PASS" : "frame-loop: FAIL");
        return allOk ? 0 : 12;
    }

teardown_fail:
    if (params != nullptr) {
        coreHost.destroyParameters(params);
    }
    adapter.restoreCallerCompatibility();
    adapter.unload();
    ring.shutdown();
    coreHost.shutdown();
    context.shutdown();
    log::error("harness", std::format("frame-loop: FAIL setup attempted={} succeeded={} failed={}",
        evaluateAttempted, evaluateSucceeded, evaluateFailed));
    return 11;
}

} // namespace veyra::harness
