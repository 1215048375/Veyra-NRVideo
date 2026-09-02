#include "veyra/NgxResult.h"

#include <format>

namespace veyra {

namespace {

struct KnownHResult {
    uint32_t value;
    const char* name;
};

// Well-known COM/DXGI/D3D12 codes only; unlisted values print as plain hex.
// DXGI/D3D12 failure codes exceed LONG_MAX so they are stored unsigned.
const KnownHResult kKnownHResults[] = {
    {0x00000000u, "S_OK"},
    {0x00000001u, "S_FALSE"},
    {0x80004005u, "E_FAIL"},
    {0x80070057u, "E_INVALIDARG"},
    {0x8007000Eu, "E_OUTOFMEMORY"},
    {0x80004002u, "E_NOINTERFACE"},
    {0x80004001u, "E_NOTIMPL"},
    {0x80070006u, "E_HANDLE"},
    {0x80040154u, "REGDB_E_CLASSNOTREG"},
    {0x887A0001u, "DXGI_ERROR_INVALID_CALL"},
    {0x887A0002u, "DXGI_ERROR_NOT_FOUND"},
    {0x887A0005u, "DXGI_ERROR_DEVICE_REMOVED"},
    {0x887A0006u, "DXGI_ERROR_DEVICE_HUNG"},
    {0x887A0007u, "DXGI_ERROR_DEVICE_RESET"},
    {0x887A0008u, "DXGI_ERROR_WAS_STILL_DRAWING"},
    {0x887A0009u, "DXGI_ERROR_FRAME_STATISTICS_DISJOINT"},
    {0x887A000Au, "DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE"},
    {0x887A000Bu, "DXGI_ERROR_DRIVER_INTERNAL_ERROR"},
    {0x887A000Cu, "DXGI_ERROR_NONEXCLUSIVE"},
    {0x887A000Du, "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE"},
    {0x887A000Eu, "DXGI_ERROR_TIMEOUT"},
    {0x887A0010u, "DXGI_ERROR_WAS_STILL_DRAWING_PENDING"},
    {0x887A0020u, "DXGI_ERROR_UNSUPPORTED"},
    {0x887A0022u, "DXGI_ERROR_ACCESS_LOST"},
    {0x887A0027u, "DXGI_ERROR_SESSION_DISCONNECTED"},
    {0x887E0001u, "D3D12_ERROR_ADAPTER_NOT_FOUND"},
    {0x887E0002u, "D3D12_ERROR_DRIVER_VERSION_MISMATCH"},
    {0x887E0003u, "D3D12_ERROR_INVALID_REDIST"},
    {0x887E0116u, "D3D12_ERROR_DRIVER_PROCESS_CRASHED"},
};

const KnownHResult* findKnownHResult(uint32_t hr)
{
    for (const auto& entry : kKnownHResults) {
        if (entry.value == hr) {
            return &entry;
        }
    }
    return nullptr;
}

struct KnownNgxResult {
    uint64_t value;
    const char* name;
};

// Complete table from the pinned official NVIDIA DLSS SDK 310.7.0 header
// (nvsdk_ngx_defs.h); NVSDK_NGX_Result_Success is 0x1, failures are
// 0xBAD00000 | n. Unknown values print hex-only, never a guessed name.
const KnownNgxResult kKnownNgxResults[] = {
    {0x1ull, "NVSDK_NGX_Result_Success"},
    {0xBAD00000ull, "NVSDK_NGX_Result_Fail"},
    {0xBAD00001ull, "NVSDK_NGX_Result_FAIL_FeatureNotSupported"},
    {0xBAD00002ull, "NVSDK_NGX_Result_FAIL_PlatformError"},
    {0xBAD00003ull, "NVSDK_NGX_Result_FAIL_FeatureAlreadyExists"},
    {0xBAD00004ull, "NVSDK_NGX_Result_FAIL_FeatureNotFound"},
    {0xBAD00005ull, "NVSDK_NGX_Result_FAIL_InvalidParameter"},
    {0xBAD00006ull, "NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall"},
    {0xBAD00007ull, "NVSDK_NGX_Result_FAIL_NotInitialized"},
    {0xBAD00008ull, "NVSDK_NGX_Result_FAIL_UnsupportedInputFormat"},
    {0xBAD00009ull, "NVSDK_NGX_Result_FAIL_RWFlagMissing"},
    {0xBAD0000Aull, "NVSDK_NGX_Result_FAIL_MissingInput"},
    {0xBAD0000Bull, "NVSDK_NGX_Result_FAIL_UnableToInitializeFeature"},
    {0xBAD0000Cull, "NVSDK_NGX_Result_FAIL_OutOfDate"},
    {0xBAD0000Dull, "NVSDK_NGX_Result_FAIL_OutOfGPUMemory"},
    {0xBAD0000Eull, "NVSDK_NGX_Result_FAIL_UnsupportedFormat"},
    {0xBAD0000Full, "NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath"},
    {0xBAD00010ull, "NVSDK_NGX_Result_FAIL_UnsupportedParameter"},
    {0xBAD00011ull, "NVSDK_NGX_Result_FAIL_Denied"},
    {0xBAD00012ull, "NVSDK_NGX_Result_FAIL_NotImplemented"},
};

} // namespace

std::string hresultString(long hr)
{
    const uint32_t value = static_cast<uint32_t>(hr);
    const KnownHResult* known = findKnownHResult(value);
    if (known != nullptr) {
        return std::format("0x{:08X} ({})", value, known->name);
    }
    return std::format("0x{:08X}", value);
}

std::string ngxResultString(uint64_t result)
{
    for (const auto& entry : kKnownNgxResults) {
        if (entry.value == result) {
            return std::format("0x{:X} ({})", result, entry.name);
        }
    }
    return std::format("0x{:X}", result);
}

} // namespace veyra
