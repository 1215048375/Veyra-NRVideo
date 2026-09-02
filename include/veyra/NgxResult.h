#pragma once

#include <cstdint>
#include <string>

namespace veyra {

// HRESULT (a 32-bit `long` on Windows) formatting:
// "0x887A0005 (DXGI_ERROR_DEVICE_REMOVED)" style for well-known D3D/DXGI/
// common codes, plain hex for anything else.
std::string hresultString(long hr);

// NVSDK_NGX_Result formatting. Only codes verified from the official public
// SDK headers get names (Phase 1 replaces this minimal table once the pinned
// DLSS SDK 310.7 headers are staged); everything else prints as hex only.
std::string ngxResultString(uint64_t result);

} // namespace veyra
