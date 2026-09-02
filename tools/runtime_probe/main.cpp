// veyra_runtime_probe - Phase 0 runtime/D3D12 probe.
// Modes (added phase by phase):
//   --self-test [--runtime-dir <abs>]   verify veyra_base file identity + strings
//   full probe mode arrives with P0.6/P0.7 (device context + runtime loading)
#include <windows.h>

#include <format>
#include <string>
#include <vector>

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"

namespace {

constexpr uint64_t kExpectedDllSize = 165840496ull;
const char* kExpectedDllSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";

std::wstring ownExePath()
{
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return {};
    }
    return std::wstring(buffer, length);
}

// Narrow copy for logging; only used for non-critical display strings.
std::string narrow(const std::wstring& text)
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

int runSelfTest(const std::wstring& runtimeDir)
{
    const std::wstring exe = ownExePath();
    if (exe.empty()) {
        veyra::log::error("probe", "self-test: GetModuleFileNameW failed");
        return 2;
    }

    // 1. HRESULT / NGX result string sanity.
    const std::string hrOk = veyra::hresultString(0);
    const std::string hrRemoved = veyra::hresultString(0x887A0005L);
    const std::string hrUnknown = veyra::hresultString(0x8000FFFFL);
    const std::string ngxOk = veyra::ngxResultString(0x300000ull);
    if (hrOk.find("S_OK") == std::string::npos ||
        hrRemoved.find("DXGI_ERROR_DEVICE_REMOVED") == std::string::npos ||
        hrUnknown.find("0x8000FFFF") == std::string::npos ||
        ngxOk.find("NVSDK_NGX_Result_Success") == std::string::npos) {
        veyra::log::error("probe", std::format("self-test: result-string mapping broken hrOk={} hrRemoved={} hrUnknown={} ngxOk={}",
            hrOk, hrRemoved, hrUnknown, ngxOk));
        return 3;
    }
    veyra::log::info("probe", std::format("self-test: result-strings ok ({}, {}, {})", hrOk, hrRemoved, ngxOk));

    // 2. File identity of the running exe (SHA256 + version + signature state).
    veyra::FileIdentity exeIdentity{};
    veyra::IdentityError exeError = veyra::IdentityError::None;
    if (!veyra::computeFileIdentity(exe, exeIdentity, exeError)) {
        veyra::log::error("probe", std::format("self-test: exe identity failed error={}", veyra::identityErrorString(exeError)));
        return 4;
    }
    if (exeIdentity.sha256Upper.size() != 64) {
        veyra::log::error("probe", std::format("self-test: exe sha256 malformed length={}", exeIdentity.sha256Upper.size()));
        return 4;
    }
    veyra::log::info("probe", std::format("self-test: exe identity ok size={} sha256={}", exeIdentity.sizeBytes, exeIdentity.sha256Upper));

    // 3. Staged DLSSNR runtime identity against the pinned contract.
    if (!runtimeDir.empty()) {
        std::wstring dllPath = runtimeDir;
        if (!dllPath.empty() && dllPath.back() != L'\\' && dllPath.back() != L'/') {
            dllPath.push_back(L'\\');
        }
        dllPath += L"nvngx_dlssnr.dll";

        veyra::FileIdentity dllIdentity{};
        veyra::IdentityError dllError = veyra::IdentityError::None;
        if (!veyra::computeFileIdentity(dllPath, dllIdentity, dllError)) {
            veyra::log::error("probe", std::format("self-test: runtime identity failed error={} path={}",
                veyra::identityErrorString(dllError), narrow(dllPath)));
            return 5;
        }
        if (dllIdentity.sizeBytes != kExpectedDllSize ||
            dllIdentity.sha256Upper != kExpectedDllSha256 ||
            !dllIdentity.signatureValid ||
            !dllIdentity.signerIsNvidia ||
            dllIdentity.fileVersion != L"310.8.0.0") {
            veyra::log::error("probe", std::format("self-test: runtime identity mismatch size={} sha256={} valid={} nvidia={} version={}",
                dllIdentity.sizeBytes, dllIdentity.sha256Upper, dllIdentity.signatureValid, dllIdentity.signerIsNvidia,
                narrow(dllIdentity.fileVersion)));
            return 5;
        }
        veyra::log::info("probe", "self-test: staged runtime identity matches the pinned contract");
    }

    veyra::log::info("probe", "self-test: PASS");
    return 0;
}

int runDeviceInfo(bool debugLayer)
{
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc desc{};
    desc.enableDebugLayer = debugLayer;
    desc.commandSlotCount = 4;

    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        veyra::log::error("probe", std::format("device-info: initialize failed status={}", veyra::statusString(status)));
        return 6;
    }

    const veyra::gfx::AdapterInfo& adapter = context.adapter();
    veyra::log::info("probe", std::format("device-info: adapter={} vendor={} nvidia={} luid={} vramMiB={} driver={} ({})",
        narrow(adapter.description), adapter.vendorIdHex, adapter.isNvidia, adapter.luidString,
        adapter.dedicatedVideoMemoryBytes / (1024 * 1024),
        narrow(adapter.driverVersion.empty() ? std::wstring(L"<none>") : adapter.driverVersion),
        narrow(adapter.driverVersionSource)));
    veyra::log::info("probe", std::format("device-info: featureLevel={} debugLayer={} slots={}",
        context.featureLevelString(), context.debugLayerEnabled(), context.commandSlotCount()));

    if (!context.exerciseSlotRing()) {
        veyra::log::error("probe", "device-info: slot ring exercise failed");
        return 7;
    }

    uint32_t removedReason = 0;
    const bool alive = context.checkDeviceAlive(removedReason);
    context.shutdown();
    if (!alive) {
        veyra::log::error("probe", std::format("device-info: device removed reason={}", veyra::hresultString(static_cast<long>(removedReason))));
        return 8;
    }
    veyra::log::info("probe", "device-info: PASS");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring runtimeDir;
    bool selfTest = false;
    bool deviceInfo = false;
    bool debugLayer = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--self-test") {
            selfTest = true;
        }
        else if (arg == L"--device-info") {
            deviceInfo = true;
        }
        else if (arg == L"--debug-layer") {
            debugLayer = true;
        }
        else if (arg == L"--runtime-dir" && i + 1 < argc) {
            runtimeDir = argv[++i];
        }
    }

    if (selfTest) {
        return runSelfTest(runtimeDir);
    }
    if (deviceInfo) {
        return runDeviceInfo(debugLayer);
    }

    veyra::log::info("probe", "no mode selected; use --self-test or --device-info (full probe arrives in P0.7)");
    return 0;
}
