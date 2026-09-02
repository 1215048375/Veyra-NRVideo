// veyra_nr_harness - Phase 1 Feature 18 native harness.
// Modes (added task by task per the backlog):
//   --load-only --runtime-dir <abs>   core init + snippet load + exports + teardown
// Full frame-loop mode (Proxy -> Feature 18 -> Raw, 300 evaluates, captures,
// JSON summary) arrives with P1.4-P1.6.
#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <string>

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/NgxCoreHost.h"

namespace {

constexpr uint64_t kExpectedDllSize = 165840496ull;
const char* kExpectedDllSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";
const wchar_t* kExpectedDllFileVersion = L"310.8.0.0";

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

// Minimal string scan for the tiny identity file written once by
// stage-runtime.ps1 ({"...": "value"} triplets only); not a general parser.
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
        veyra::log::error("harness", std::format("load-only: identity file missing path={}", narrow(configPath)));
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    identity.projectId = scanJsonStringField(text, "ngxProjectId");
    identity.engineVersion = scanJsonStringField(text, "engineVersion");
    if (identity.projectId.empty() || identity.engineVersion.empty()) {
        veyra::log::error("harness", "load-only: identity file missing ngxProjectId/engineVersion");
        return false;
    }
    veyra::log::info("harness", std::format("load-only: local identity projectId={} engineVersion={}",
        identity.projectId, identity.engineVersion));
    return true;
}

int runLoadOnly(const std::wstring& runtimeDir)
{
    // 1. Device context (debug layer follows the build configuration).
#if defined(VEYRA_D3D12_DEBUG)
    constexpr bool kDebugLayerByBuild = true;
#else
    constexpr bool kDebugLayerByBuild = false;
#endif
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc contextDesc{};
    contextDesc.enableDebugLayer = kDebugLayerByBuild;
    contextDesc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(contextDesc, status)) {
        veyra::log::error("harness", std::format("load-only: device context failed status={}", veyra::statusString(status)));
        return 6;
    }

    // 2. Local NGX identity (this machine only; not a distribution identity).
    LocalIdentity identity{};
    if (!loadLocalIdentity(runtimeDir, identity)) {
        context.shutdown();
        return 7;
    }

    // 3. Staged snippet identity against the pinned contract.
    const std::wstring dllPath = runtimeDir + L"\\nvngx_dlssnr.dll";
    veyra::FileIdentity dllIdentity{};
    veyra::IdentityError dllError = veyra::IdentityError::None;
    if (!veyra::computeFileIdentity(dllPath, dllIdentity, dllError) ||
        dllIdentity.sizeBytes != kExpectedDllSize ||
        dllIdentity.sha256Upper != kExpectedDllSha256 ||
        !dllIdentity.signatureValid ||
        !dllIdentity.signerIsNvidia ||
        dllIdentity.fileVersion != kExpectedDllFileVersion) {
        veyra::log::error("harness", std::format("load-only: staged runtime identity mismatch error={}",
            veyra::identityErrorString(dllError)));
        context.shutdown();
        return 3;
    }
    veyra::log::info("harness", "load-only: staged runtime identity matches the pinned contract");

    // 4. NGX core init (single session for this device).
    veyra::ngx::NgxCoreHost coreHost;
    if (!coreHost.initialize(context.device(), runtimeDir, identity.projectId.c_str(), identity.engineVersion.c_str(), status)) {
        veyra::log::error("harness", std::format("load-only: core init failed result={} status={}",
            veyra::ngxResultString(coreHost.initResult()), veyra::statusString(status)));
        context.shutdown();
        return 8;
    }

    // 5. Snippet load + exports.
    veyra::ngx::DlssNrRuntimeAdapter adapter;
    if (!adapter.load(runtimeDir, status)) {
        veyra::log::error("harness", std::format("load-only: adapter load failed status={}", veyra::statusString(status)));
        coreHost.shutdown();
        context.shutdown();
        return 9;
    }

    // 6. Reverse-order teardown: snippet module -> core -> device.
    adapter.unload();
    coreHost.shutdown();
    context.shutdown();
    veyra::log::info("harness", "load-only: PASS");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring runtimeDir;
    std::wstring logFile;
    bool loadOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--load-only") {
            loadOnly = true;
        }
        else if (arg == L"--runtime-dir" && i + 1 < argc) {
            runtimeDir = argv[++i];
        }
        else if (arg == L"--log-file" && i + 1 < argc) {
            logFile = argv[++i];
        }
    }

    if (!logFile.empty()) {
        (void)veyra::Logger::instance().openFile(logFile);
    }

    int exitCode = 0;
    if (loadOnly && !runtimeDir.empty()) {
        exitCode = runLoadOnly(runtimeDir);
    }
    else {
        veyra::log::info("harness", "no mode selected; use --load-only --runtime-dir <abs> (frame loop arrives in P1.4-P1.6)");
        exitCode = 1;
    }

    veyra::Logger::instance().closeFile();
    return exitCode;
}
