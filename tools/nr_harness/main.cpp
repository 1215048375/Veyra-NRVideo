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

// P1.3 caller-compatibility boundary battery (Playbook 8.3). Runs against the
// installed shim without needing a D3D12 device or NGX core session.
int runShimTest(const std::wstring& runtimeDir)
{
    const std::wstring dllPath = runtimeDir + L"\\nvngx_dlssnr.dll";
    veyra::FileIdentity dllIdentity{};
    veyra::IdentityError dllError = veyra::IdentityError::None;
    if (!veyra::computeFileIdentity(dllPath, dllIdentity, dllError) ||
        dllIdentity.sizeBytes != kExpectedDllSize ||
        dllIdentity.sha256Upper != kExpectedDllSha256) {
        veyra::log::error("harness", "shim-test: staged runtime identity mismatch");
        return 3;
    }

    veyra::ngx::DlssNrRuntimeAdapter adapter;
    veyra::Status status = veyra::Status::Ok;
    if (!adapter.load(runtimeDir, status)) {
        veyra::log::error("harness", std::format("shim-test: adapter load failed status={}", veyra::statusString(status)));
        return 9;
    }
    if (!adapter.installCallerCompatibility(status)) {
        veyra::log::error("harness", std::format("shim-test: shim install failed status={}", veyra::statusString(status)));
        adapter.unload();
        return 10;
    }

    constexpr DWORD kSentinelError = 0x1234;
    int failures = 0;
    const auto check = [&failures](const char* name, bool ok, const std::string& detail) {
        veyra::log::info("harness", std::format("shim-test[{}] {} {}", ok ? "PASS" : "FAIL", name, detail));
        if (!ok) {
            ++failures;
        }
    };

    HMODULE caller = veyra::ngx::DlssNrRuntimeAdapter::testCallerModule();
    check("caller-module-resolved", caller != nullptr, std::format("module=0x{:016X}", reinterpret_cast<uintptr_t>(caller)));

    // 1. size == 0: return 0, buffer untouched, last-error unchanged.
    {
        wchar_t buffer[16]{};
        wmemset(buffer, 0xABCD, 16);
        SetLastError(kSentinelError);
        const DWORD ret = veyra::ngx::DlssNrRuntimeAdapter::testShimGetModuleFileNameW(caller, buffer, 0);
        check("zero-size", ret == 0 && buffer[0] == static_cast<wchar_t>(0xABCD) && GetLastError() == kSentinelError,
            std::format("ret={} buffer0=0x{:04X} lastError=0x{:X}", ret, static_cast<unsigned>(buffer[0]), GetLastError()));
    }

    // 2. too-small buffer (size 5): truncate, NUL at [size-1], return size,
    //    last-error ERROR_INSUFFICIENT_BUFFER.
    {
        wchar_t buffer[8]{};
        SetLastError(kSentinelError);
        const DWORD ret = veyra::ngx::DlssNrRuntimeAdapter::testShimGetModuleFileNameW(caller, buffer, 5);
        const bool ok = ret == 5 && wcsncmp(buffer, L"nvng", 4) == 0 && buffer[4] == L'\0' &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER;
        check("too-small-truncates", ok,
            std::format("ret={} buffer={} lastError=0x{:X} expectedError=0x{:X}", ret, narrow(buffer), GetLastError(), ERROR_INSUFFICIENT_BUFFER));
    }

    // 3. exact fit (10 wchar = 9 chars + NUL): return 9, full name, no error change.
    {
        wchar_t buffer[16]{};
        SetLastError(kSentinelError);
        const DWORD ret = veyra::ngx::DlssNrRuntimeAdapter::testShimGetModuleFileNameW(caller, buffer, 10);
        check("exact-10-wchars", ret == 9 && wcscmp(buffer, L"nvngx.dll") == 0 && GetLastError() == kSentinelError,
            std::format("ret={} buffer={} lastError=0x{:X}", ret, narrow(buffer), GetLastError()));
    }

    // 4. roomy buffer (64): same as exact fit.
    {
        wchar_t buffer[64]{};
        SetLastError(kSentinelError);
        const DWORD ret = veyra::ngx::DlssNrRuntimeAdapter::testShimGetModuleFileNameW(caller, buffer, 64);
        check("roomy-64", ret == 9 && wcscmp(buffer, L"nvngx.dll") == 0,
            std::format("ret={} buffer={}", ret, narrow(buffer)));
    }

    // 5. nullptr module forwards to the real API (current executable path).
    {
        wchar_t buffer[MAX_PATH]{};
        SetLastError(kSentinelError);
        const DWORD ret = veyra::ngx::DlssNrRuntimeAdapter::testShimGetModuleFileNameW(nullptr, buffer, MAX_PATH);
        check("null-module-forwards", ret > 0 && ret < MAX_PATH && wcscmp(buffer, L"nvngx.dll") != 0,
            std::format("ret={} path={}", ret, narrow(buffer)));
    }

    // 6. non-caller module (kernel32) forwards to the real API.
    {
        wchar_t buffer[MAX_PATH]{};
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const DWORD ret = veyra::ngx::DlssNrRuntimeAdapter::testShimGetModuleFileNameW(kernel32, buffer, MAX_PATH);
        check("other-module-forwards", kernel32 != nullptr && ret > 0 && wcsstr(buffer, L"KERNEL32") != nullptr,
            std::format("ret={} path={}", ret, narrow(buffer)));
    }

    // 7. restore must clear the installed flag (unload verifies the slot again).
    adapter.restoreCallerCompatibility();
    check("restore-reported-clean", !adapter.callerCompatibilityInstalled(), "flag cleared");

    // Reverse teardown (unload restores the IAT again as defense in depth).
    adapter.unload();

    if (failures != 0) {
        veyra::log::error("harness", std::format("shim-test: FAIL ({} failing checks)", failures));
        return 11;
    }
    veyra::log::info("harness", "shim-test: PASS (all boundary checks)");
    return 0;
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
    bool shimTest = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--load-only") {
            loadOnly = true;
        }
        else if (arg == L"--shim-test") {
            shimTest = true;
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
    if (shimTest && !runtimeDir.empty()) {
        exitCode = runShimTest(runtimeDir);
    }
    else if (loadOnly && !runtimeDir.empty()) {
        exitCode = runLoadOnly(runtimeDir);
    }
    else {
        veyra::log::info("harness", "no mode selected; use --load-only/--shim-test --runtime-dir <abs> (frame loop arrives in P1.4-P1.6)");
        exitCode = 1;
    }

    veyra::Logger::instance().closeFile();
    return exitCode;
}
