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

// The single NGX result code whose numeric value is verified against the
// public NVIDIA NGX SDK headers at this stage. The full named table lands in
// Phase 1 together with the pinned DLSS SDK 310.7 headers; unknown values
// intentionally print hex-only instead of a guessed name.
constexpr uint64_t kNgxResultSuccess = 0x300000ull;

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
    if (result == kNgxResultSuccess) {
        return std::format("0x{:X} (NVSDK_NGX_Result_Success)", result);
    }
    return std::format("0x{:X}", result);
}

} // namespace veyra
