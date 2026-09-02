// veyra_runtime_probe - Phase 0 runtime/D3D12 probe (Playbook section 16).
//
// Modes:
//   --self-test [--runtime-dir <abs>]   verify veyra_base primitives
//   --device-info [--debug-layer]       adapter/device/slot-ring report
//   --runtime-dir <abs> [--window-seconds N] [--run-id id]
//     [--log-file <abs>] [--json-file <abs>]
//     full probe: staged DLL identity + restricted load + exports +
//     System32 nvofapi64 probe + fixed-size window with flip-model swapchain
//     loop on the 4-slot command ring. Writes a JSON summary consumed by
//     scripts/gates/phase0.ps1; the JSON embeds runId and the probe exe's own
//     SHA-256 so stale artifacts cannot pass the gate.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"

namespace {

constexpr uint64_t kExpectedDllSize = 165840496ull;
const char* kExpectedDllSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";
const wchar_t* kExpectedDllFileVersion = L"310.8.0.0";

const char* kRequiredExports[] = {
    "NVSDK_NGX_D3D12_Init_Ext",
    "NVSDK_NGX_D3D12_CreateFeature",
    "NVSDK_NGX_D3D12_EvaluateFeature",
    "NVSDK_NGX_D3D12_ReleaseFeature",
    "NVSDK_NGX_D3D12_Shutdown1",
};
constexpr size_t kRequiredExportCount = 5;

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

std::wstring fileVersionOf(const std::wstring& path, bool& present)
{
    present = false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return {};
    }
    present = true;
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return {};
    }
    std::vector<unsigned char> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) {
        return {};
    }
    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT length = 0;
    if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &length) && length >= sizeof(VS_FIXEDFILEINFO)) {
        return std::format(L"{}.{}.{}.{}",
            HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS),
            HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
    }
    return {};
}

std::wstring nvofapi64Path()
{
    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(systemDir, length) + L"\\nvofapi64.dll";
}

bool writeTextFile(const std::wstring& path, const std::string& content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == content.size();
}

struct ProbeArgs {
    std::wstring runtimeDir;
    uint32_t windowSeconds = 3;
    std::wstring runId;
    std::wstring logFile;
    std::wstring jsonFile;
};

struct ProbeSummary {
    std::string runId;
    std::string exeSha256;
    std::string osBuild;
    // adapter
    std::string adapterName;
    std::string adapterVendorId;
    std::string adapterLuid;
    uint64_t adapterDedicatedVideoMemoryMiB = 0;
    bool adapterIsNvidia = false;
    std::string driverVersion;
    std::string featureLevel;
    uint32_t commandSlots = 0;
    // runtime
    std::string runtimePath;
    uint64_t runtimeSizeBytes = 0;
    std::string runtimeSha256;
    std::string runtimeFileVersion;
    std::string runtimeSignature;
    std::string runtimeSigner;
    bool exports[kRequiredExportCount] = {};
    // nvofapi
    std::string nvofapiPath;
    std::string nvofapiFileVersion;
    bool nvofapiPresent = false;
    bool experimentalDlssnr = false;
    // window loop
    uint32_t windowDurationSeconds = 0;
    uint64_t windowFramesPresented = 0;
    bool windowDeviceRemoved = false;
    uint32_t windowDeviceRemovedReason = 0;
};

std::string buildJson(const ProbeSummary& s)
{
    std::string json;
    json += "{\n";
    json += std::format("  \"probe\": \"veyra_runtime_probe\",\n");
    json += std::format("  \"runId\": \"{}\",\n", jsonEscape(s.runId));
    json += std::format("  \"exeSha256\": \"{}\",\n", jsonEscape(s.exeSha256));
    json += std::format("  \"osBuild\": \"{}\",\n", jsonEscape(s.osBuild));
    json += "  \"adapter\": {\n";
    json += std::format("    \"name\": \"{}\",\n", jsonEscape(s.adapterName));
    json += std::format("    \"vendorId\": \"{}\",\n", jsonEscape(s.adapterVendorId));
    json += std::format("    \"luid\": \"{}\",\n", jsonEscape(s.adapterLuid));
    json += std::format("    \"dedicatedVideoMemoryMiB\": {},\n", s.adapterDedicatedVideoMemoryMiB);
    json += std::format("    \"isNvidia\": {}\n", s.adapterIsNvidia ? "true" : "false");
    json += "  },\n";
    json += std::format("  \"driverVersion\": \"{}\",\n", jsonEscape(s.driverVersion));
    json += std::format("  \"featureLevel\": \"{}\",\n", jsonEscape(s.featureLevel));
    json += std::format("  \"commandSlots\": {},\n", s.commandSlots);
    json += "  \"runtime\": {\n";
    json += std::format("    \"path\": \"{}\",\n", jsonEscape(s.runtimePath));
    json += std::format("    \"sizeBytes\": {},\n", s.runtimeSizeBytes);
    json += std::format("    \"sha256\": \"{}\",\n", jsonEscape(s.runtimeSha256));
    json += std::format("    \"fileVersion\": \"{}\",\n", jsonEscape(s.runtimeFileVersion));
    json += std::format("    \"signature\": \"{}\",\n", jsonEscape(s.runtimeSignature));
    json += std::format("    \"signer\": \"{}\"\n", jsonEscape(s.runtimeSigner));
    json += "  },\n";
    json += "  \"exports\": {\n";
    for (size_t i = 0; i < kRequiredExportCount; ++i) {
        json += std::format("    \"{}\": {}{}\n", kRequiredExports[i], s.exports[i] ? "true" : "false",
            i + 1 < kRequiredExportCount ? "," : "");
    }
    json += "  },\n";
    json += "  \"nvofapi\": {\n";
    json += std::format("    \"path\": \"{}\",\n", jsonEscape(s.nvofapiPath));
    json += std::format("    \"fileVersion\": \"{}\",\n", jsonEscape(s.nvofapiFileVersion));
    json += std::format("    \"present\": {}\n", s.nvofapiPresent ? "true" : "false");
    json += "  },\n";
    json += std::format("  \"experimentalDlssnr\": {},\n", s.experimentalDlssnr ? "true" : "false");
    json += "  \"windowLoop\": {\n";
    json += std::format("    \"durationSeconds\": {},\n", s.windowDurationSeconds);
    json += std::format("    \"framesPresented\": {},\n", s.windowFramesPresented);
    json += std::format("    \"deviceRemoved\": {},\n", s.windowDeviceRemoved ? "true" : "false");
    json += std::format("    \"deviceRemovedReason\": {}\n", s.windowDeviceRemovedReason);
    json += "  }\n";
    json += "}\n";
    return json;
}

// ---------------------------------------------------------------------------
// Window plumbing (fixed-size window; no resize => no ResizeBuffers needed)
// ---------------------------------------------------------------------------
constexpr wchar_t kWindowClassName[] = L"VeyraRuntimeProbeWindow";
bool gCloseRequested = false;

LRESULT CALLBACK probeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CLOSE:
    case WM_DESTROY:
        gCloseRequested = true;
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            gCloseRequested = true;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int runSelfTest(const std::wstring& runtimeDir);
int runDeviceInfo(bool debugLayer);

int runFullProbe(const ProbeArgs& args)
{
    const std::string runIdNarrow = narrow(args.runId.empty() ? std::wstring(L"<generated>") : args.runId);

    // --- Resolve and validate the absolute runtime directory.
    wchar_t resolved[MAX_PATH * 2]{};
    if (GetFullPathNameW(args.runtimeDir.c_str(), static_cast<DWORD>(std::size(resolved)), resolved, nullptr) == 0) {
        veyra::log::error("probe", "full: GetFullPathNameW failed for runtime dir");
        return 2;
    }
    if (_wcsnicmp(resolved, args.runtimeDir.c_str(), args.runtimeDir.size()) != 0) {
        veyra::log::error("probe", std::format("full: runtime dir must be absolute/canonical given={} resolved={}",
            narrow(args.runtimeDir), narrow(resolved)));
        return 2;
    }
    const std::wstring runtimeDir(resolved);
    const std::wstring dllPath = runtimeDir + L"\\nvngx_dlssnr.dll";
    veyra::log::info("probe", std::format("full: runtimeDir={} dll={}", narrow(runtimeDir), narrow(dllPath)));

    ProbeSummary summary{};
    summary.runId = runIdNarrow;
    summary.osBuild = osBuildString();

    // --- Probe exe self-hash (stale-artifact guard).
    const std::wstring exe = ownExePath();
    if (exe.empty()) {
        veyra::log::error("probe", "full: GetModuleFileNameW failed");
        return 2;
    }
    veyra::FileIdentity exeIdentity{};
    veyra::IdentityError exeError = veyra::IdentityError::None;
    if (!veyra::computeFileIdentity(exe, exeIdentity, exeError)) {
        veyra::log::error("probe", std::format("full: exe identity failed error={}", veyra::identityErrorString(exeError)));
        return 3;
    }
    summary.exeSha256 = exeIdentity.sha256Upper;
    veyra::log::info("probe", std::format("full: exe sha256={} size={}", summary.exeSha256, exeIdentity.sizeBytes));

    // --- Staged runtime identity (pinned contract).
    veyra::FileIdentity dllIdentity{};
    veyra::IdentityError dllError = veyra::IdentityError::None;
    if (!veyra::computeFileIdentity(dllPath, dllIdentity, dllError)) {
        veyra::log::error("probe", std::format("full: runtime identity failed error={}", veyra::identityErrorString(dllError)));
        return 3;
    }
    if (dllIdentity.sizeBytes != kExpectedDllSize ||
        dllIdentity.sha256Upper != kExpectedDllSha256 ||
        !dllIdentity.signatureValid ||
        !dllIdentity.signerIsNvidia ||
        dllIdentity.fileVersion != kExpectedDllFileVersion) {
        veyra::log::error("probe", std::format("full: runtime identity mismatch size={} sha256={} valid={} nvidia={} version={}",
            dllIdentity.sizeBytes, dllIdentity.sha256Upper, dllIdentity.signatureValid, dllIdentity.signerIsNvidia,
            narrow(dllIdentity.fileVersion)));
        return 3;
    }
    summary.runtimePath = narrow(dllPath);
    summary.runtimeSizeBytes = dllIdentity.sizeBytes;
    summary.runtimeSha256 = dllIdentity.sha256Upper;
    summary.runtimeFileVersion = narrow(dllIdentity.fileVersion);
    summary.runtimeSignature = dllIdentity.signatureValid ? "Valid" : "Invalid";
    summary.runtimeSigner = narrow(dllIdentity.signerSubject);
    veyra::log::info("probe", "full: staged runtime identity matches the pinned contract");

    // --- System32 nvofapi64 presence/version (no load).
    const std::wstring nvofPath = nvofapi64Path();
    summary.nvofapiPath = narrow(nvofPath);
    summary.nvofapiFileVersion = narrow(fileVersionOf(nvofPath, summary.nvofapiPresent));
    veyra::log::info("probe", std::format("full: nvofapi64 present={} version={} path={}",
        summary.nvofapiPresent, summary.nvofapiFileVersion, summary.nvofapiPath));

    // --- Restricted load of the staged DLSSNR runtime + required exports.
    HMODULE runtimeModule = LoadLibraryExW(dllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (runtimeModule == nullptr) {
        const DWORD lastError = GetLastError();
        veyra::log::error("probe", std::format("full: LoadLibraryExW failed lastError={} path={}", lastError, narrow(dllPath)));
        return 4;
    }
    veyra::log::info("probe", "full: LoadLibraryExW ok flags=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32");

    size_t exportsPresent = 0;
    for (size_t i = 0; i < kRequiredExportCount; ++i) {
        const FARPROC address = GetProcAddress(runtimeModule, kRequiredExports[i]);
        summary.exports[i] = address != nullptr;
        if (address != nullptr) {
            ++exportsPresent;
        }
        veyra::log::info("probe", std::format("full: export {} {} address=0x{:016X}",
            kRequiredExports[i], address != nullptr ? "present" : "MISSING",
            reinterpret_cast<uintptr_t>(address)));
    }
    if (exportsPresent != kRequiredExportCount) {
        FreeLibrary(runtimeModule);
        return 5;
    }
    summary.experimentalDlssnr = true;
    veyra::log::info("probe", std::format("full: required exports {}/{}", exportsPresent, kRequiredExportCount));

    // --- D3D12 device context (debug layer follows the build configuration).
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
        veyra::log::error("probe", std::format("full: device context failed status={}", veyra::statusString(status)));
        FreeLibrary(runtimeModule);
        return 6;
    }
    const veyra::gfx::AdapterInfo& adapter = context.adapter();
    summary.adapterName = narrow(adapter.description);
    summary.adapterVendorId = adapter.vendorIdHex;
    summary.adapterLuid = adapter.luidString;
    summary.adapterDedicatedVideoMemoryMiB = adapter.dedicatedVideoMemoryBytes / (1024 * 1024);
    summary.adapterIsNvidia = adapter.isNvidia;
    summary.driverVersion = narrow(adapter.driverVersion);
    summary.featureLevel = context.featureLevelString();
    summary.commandSlots = context.commandSlotCount();

    // --- Fixed-size Win32 window + flip-model swapchain on the direct queue.
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = probeWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        veyra::log::error("probe", std::format("full: RegisterClassExW failed lastError={}", GetLastError()));
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }
    constexpr LONG kWindowWidth = 1280;
    constexpr LONG kWindowHeight = 720;
    RECT windowRect{ 0, 0, kWindowWidth, kWindowHeight };
    if (!AdjustWindowRect(&windowRect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE)) {
        veyra::log::error("probe", std::format("full: AdjustWindowRect failed lastError={}", GetLastError()));
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }
    HWND window = CreateWindowExW(0, kWindowClassName, L"Veyra runtime probe (Phase 0)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        veyra::log::error("probe", std::format("full: CreateWindowExW failed lastError={}", GetLastError()));
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }

    veyra::gfx::ComPtr<IDXGIFactory2> swapFactory;
    HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&swapFactory));
    if (FAILED(result)) {
        veyra::log::error("probe", std::format("full: swapchain CreateDXGIFactory2 failed hr={}", veyra::hresultString(result)));
        DestroyWindow(window);
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = kWindowWidth;
    swapDesc.Height = kWindowHeight;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.Stereo = FALSE;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 3;
    swapDesc.Scaling = DXGI_SCALING_NONE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    veyra::gfx::ComPtr<IDXGISwapChain3> swapchain;
    veyra::gfx::ComPtr<IDXGISwapChain1> swapchain1;
    result = swapFactory->CreateSwapChainForHwnd(context.directQueue(), window, &swapDesc, nullptr, nullptr, &swapchain1);
    if (SUCCEEDED(result)) {
        const HRESULT qiResult = swapchain1.As(&swapchain);
        if (FAILED(qiResult)) {
            veyra::log::error("probe", std::format("full: swapchain QI to IDXGISwapChain3 failed hr={}", veyra::hresultString(qiResult)));
            result = qiResult;
        }
    }
    if (FAILED(result)) {
        veyra::log::error("probe", std::format("full: CreateSwapChainForHwnd failed hr={}", veyra::hresultString(result)));
        DestroyWindow(window);
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }
    (void)swapFactory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    constexpr UINT kBufferCount = 3;
    veyra::gfx::ComPtr<ID3D12Resource> backBuffers[kBufferCount];
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kBufferCount;
    veyra::gfx::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    result = context.device()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(result)) {
        veyra::log::error("probe", std::format("full: CreateDescriptorHeap failed hr={}", veyra::hresultString(result)));
        DestroyWindow(window);
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }
    const UINT rtvHandleSize = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvBase = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBufferCount; ++i) {
        result = swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
        if (FAILED(result)) {
            veyra::log::error("probe", std::format("full: GetBuffer {} failed hr={}", i, veyra::hresultString(result)));
            DestroyWindow(window);
            context.shutdown();
            FreeLibrary(runtimeModule);
            return 7;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvBase;
        rtv.ptr += static_cast<SIZE_T>(rtvHandleSize) * i;
        context.device()->CreateRenderTargetView(backBuffers[i].Get(), nullptr, rtv);
    }

    veyra::gfx::CommandSlotRing ring;
    status = veyra::Status::Ok;
    if (!ring.initialize(context.device(), context.directQueue(), context.fence(), context.fenceEvent(), 4, status)) {
        veyra::log::error("probe", std::format("full: slot ring init failed status={}", veyra::statusString(status)));
        DestroyWindow(window);
        context.shutdown();
        FreeLibrary(runtimeModule);
        return 7;
    }

    // --- Window loop: per-frame clear via the slot ring + vsync present.
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    gCloseRequested = false;

    LARGE_INTEGER qpcFrequency{};
    QueryPerformanceFrequency(&qpcFrequency);
    LARGE_INTEGER qpcStart{};
    QueryPerformanceCounter(&qpcStart);

    const float clearColors[2][4] = {
        { 0.043f, 0.043f, 0.086f, 1.0f },
        { 0.545f, 0.267f, 0.071f, 1.0f },
    };
    uint64_t frameIndex = 0;
    uint64_t framesPresented = 0;
    bool deviceRemoved = false;
    uint32_t deviceRemovedReason = 0;
    double elapsedSeconds = 0.0;

    while (!gCloseRequested) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        LARGE_INTEGER qpcNow{};
        QueryPerformanceCounter(&qpcNow);
        elapsedSeconds = static_cast<double>(qpcNow.QuadPart - qpcStart.QuadPart) / static_cast<double>(qpcFrequency.QuadPart);
        if (elapsedSeconds >= static_cast<double>(args.windowSeconds)) {
            break;
        }

        const UINT backBufferIndex = swapchain->GetCurrentBackBufferIndex();
        const uint32_t slot = static_cast<uint32_t>(frameIndex % 4);
        ID3D12GraphicsCommandList* list = ring.acquire(slot, status);
        if (list == nullptr) {
            veyra::log::error("probe", std::format("full: slot acquire failed frame={} status={}", frameIndex, veyra::statusString(status)));
            break;
        }

        D3D12_RESOURCE_BARRIER barriers[2]{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = backBuffers[backBufferIndex].Get();
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barriers[0]);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvBase;
        rtv.ptr += static_cast<SIZE_T>(rtvHandleSize) * backBufferIndex;
        list->ClearRenderTargetView(rtv, clearColors[frameIndex & 1], 0, nullptr);

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = backBuffers[backBufferIndex].Get();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barriers[1]);

        if (!ring.submitAndSignal(slot)) {
            veyra::log::error("probe", std::format("full: slot submit failed frame={}", frameIndex));
            break;
        }

        const HRESULT presentResult = swapchain->Present(1, 0);
        if (SUCCEEDED(presentResult)) {
            ++framesPresented;
        }
        else {
            veyra::log::error("probe", std::format("full: Present failed hr={} frame={}", veyra::hresultString(presentResult), frameIndex));
            deviceRemovedReason = static_cast<uint32_t>(presentResult);
            deviceRemoved = true;
            break;
        }

        if ((frameIndex % 128) == 0) {
            uint32_t reason = 0;
            if (!context.checkDeviceAlive(reason)) {
                deviceRemoved = true;
                deviceRemovedReason = reason;
                break;
            }
        }
        ++frameIndex;
    }

    LARGE_INTEGER qpcEnd{};
    QueryPerformanceCounter(&qpcEnd);
    const double totalSeconds = static_cast<double>(qpcEnd.QuadPart - qpcStart.QuadPart) / static_cast<double>(qpcFrequency.QuadPart);
    summary.windowDurationSeconds = static_cast<uint32_t>(totalSeconds);
    summary.windowFramesPresented = framesPresented;
    summary.windowDeviceRemoved = deviceRemoved;
    summary.windowDeviceRemovedReason = deviceRemovedReason;
    veyra::log::info("probe", std::format("full: window loop finished seconds={:.3} requested={} frames={} deviceRemoved={} reason=0x{:08X} closeRequested={}",
        totalSeconds, args.windowSeconds, framesPresented, deviceRemoved, deviceRemovedReason, gCloseRequested));

    // --- Reverse-order cleanup.
    (void)ring.waitIdle();
    ring.shutdown();
    for (UINT i = 0; i < kBufferCount; ++i) {
        backBuffers[i].Reset();
    }
    rtvHeap.Reset();
    swapchain.Reset();
    swapFactory.Reset();
    DestroyWindow(window);
    (void)UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    context.shutdown();
    FreeLibrary(runtimeModule);

    if (deviceRemoved) {
        veyra::log::error("probe", std::format("full: device removed reason={}", veyra::hresultString(static_cast<long>(deviceRemovedReason))));
        return 8;
    }
    if (gCloseRequested && totalSeconds < static_cast<double>(args.windowSeconds)) {
        veyra::log::error("probe", "full: window closed before the requested duration");
        return 9;
    }

    if (!args.jsonFile.empty()) {
        if (!writeTextFile(args.jsonFile, buildJson(summary))) {
            veyra::log::error("probe", std::format("full: json write failed path={}", narrow(args.jsonFile)));
            return 10;
        }
        veyra::log::info("probe", std::format("full: json written path={}", narrow(args.jsonFile)));
    }
    veyra::log::info("probe", "full: PASS");
    return 0;
}

// ---------------------------------------------------------------------------
// --self-test: veyra_base primitive verification
// ---------------------------------------------------------------------------
int runSelfTest(const std::wstring& runtimeDir)
{
    const std::wstring exe = ownExePath();
    if (exe.empty()) {
        veyra::log::error("probe", "self-test: GetModuleFileNameW failed");
        return 2;
    }

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
            dllIdentity.fileVersion != kExpectedDllFileVersion) {
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

// ---------------------------------------------------------------------------
// --device-info: adapter/device/slot-ring report
// ---------------------------------------------------------------------------
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
    veyra::log::info("probe", std::format("device-info: adapter={} vendor={} nvidia={} luid={} vramMiB={} driver={}",
        narrow(adapter.description), adapter.vendorIdHex, adapter.isNvidia, adapter.luidString,
        adapter.dedicatedVideoMemoryBytes / (1024 * 1024),
        narrow(adapter.driverVersion.empty() ? std::wstring(L"<none>") : adapter.driverVersion)));
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
    ProbeArgs args{};
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
            args.runtimeDir = argv[++i];
        }
        else if (arg == L"--window-seconds" && i + 1 < argc) {
            args.windowSeconds = static_cast<uint32_t>(wcstoul(argv[++i], nullptr, 10));
        }
        else if (arg == L"--run-id" && i + 1 < argc) {
            args.runId = argv[++i];
        }
        else if (arg == L"--log-file" && i + 1 < argc) {
            args.logFile = argv[++i];
        }
        else if (arg == L"--json-file" && i + 1 < argc) {
            args.jsonFile = argv[++i];
        }
    }

    if (selfTest) {
        return runSelfTest(args.runtimeDir);
    }
    if (deviceInfo) {
        return runDeviceInfo(debugLayer);
    }
    if (!args.runtimeDir.empty()) {
        if (!args.logFile.empty()) {
            (void)veyra::Logger::instance().openFile(args.logFile);
        }
        if (args.runId.empty()) {
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            args.runId = std::format(L"veyra-{:016X}-{:08X}",
                static_cast<uint64_t>(qpc.QuadPart), GetCurrentProcessId());
        }
        const int exitCode = runFullProbe(args);
        veyra::Logger::instance().closeFile();
        return exitCode;
    }

    veyra::log::info("probe", "no mode selected; use --self-test, --device-info, or --runtime-dir <abs> (full probe)");
    return 0;
}
