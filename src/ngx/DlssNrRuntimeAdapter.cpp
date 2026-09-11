#include "veyra/ngx/DlssNrRuntimeAdapter.h"

#include <windows.h>

#include <format>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"

namespace veyra::ngx {

namespace {

constexpr const char* kExportNames[] = {
    "NVSDK_NGX_D3D12_Init_Ext",
    "NVSDK_NGX_D3D12_CreateFeature",
    "NVSDK_NGX_D3D12_EvaluateFeature",
    "NVSDK_NGX_D3D12_ReleaseFeature",
    "NVSDK_NGX_D3D12_Shutdown1",
};

// ---------------------------------------------------------------------------
// Caller-name compatibility shim (Playbook 8.3). The signed snippet queries
// the calling module's file name via GetModuleFileNameW; when asked about the
// Veyra caller module it must observe the literal L"nvngx.dll". The shim is
// installed on the snippet's own IAT slot only - never a global Kernel32
// hook - and only one adapter session may own the slot.
// ---------------------------------------------------------------------------
using GetModuleFileNameWFn = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

GetModuleFileNameWFn g_realGetModuleFileNameW = nullptr;
HMODULE g_callerModule = nullptr;
bool g_shimOwnerActive = false;

// 9 characters + NUL, matching what the snippet expects to observe.
constexpr wchar_t kShimModuleName[] = L"nvngx.dll";
constexpr DWORD kShimModuleNameChars = 9;

__declspec(noinline) DWORD WINAPI SnippetGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size)
{
    if (g_callerModule == nullptr) {
        // Resolve the Veyra caller module from this shim function's own
        // address; never guess by executable file name.
        HMODULE caller = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SnippetGetModuleFileNameW), &caller) != FALSE) {
            g_callerModule = caller;
        }
    }

    if (module == g_callerModule) {
        if (size == 0) {
            // No buffer access, no last-error change, return 0.
            return 0;
        }
        if (size <= kShimModuleNameChars) {
            // Too small: copy size-1 characters, NUL at [size-1], return size
            // and set ERROR_INSUFFICIENT_BUFFER (real API semantics).
            for (DWORD i = 0; i + 1 < size; ++i) {
                filename[i] = kShimModuleName[i];
            }
            filename[size - 1] = L'\0';
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return size;
        }
        for (DWORD i = 0; i <= kShimModuleNameChars; ++i) {
            filename[i] = kShimModuleName[i];
        }
        return kShimModuleNameChars;
    }

    // Everything else (nullptr, other modules) forwards to the saved real
    // function pointer with untouched semantics.
    if (g_realGetModuleFileNameW == nullptr) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return 0;
    }
    return g_realGetModuleFileNameW(module, filename, size);
}

// Walks the in-memory PE import table of `module` and returns the IAT slot
// (address of the thunk function pointer) importing `functionName` from a
// KERNEL32 or API-set DLL. Returns nullptr when no such import exists.
void** FindImportedFunctionSlot(HMODULE module, const char* functionName)
{
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) {
        return nullptr;
    }

    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char* dllName = reinterpret_cast<const char*>(base + descriptor->Name);
        const bool isKernel32 = _stricmp(dllName, "kernel32.dll") == 0;
        const bool isApiSet = _strnicmp(dllName, "api-ms-", 7) == 0 || _strnicmp(dllName, "ext-ms-", 7) == 0;
        if (!isKernel32 && !isApiSet) {
            continue;
        }

        const ULONGLONG lookupRva = descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
        if (lookupRva == 0) {
            continue;
        }
        const auto* lookup = reinterpret_cast<const ULONGLONG*>(base + lookupRva);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (size_t i = 0; lookup[i] != 0; ++i) {
            if (IMAGE_SNAP_BY_ORDINAL64(lookup[i])) {
                continue;
            }
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + static_cast<size_t>(lookup[i]));
            if (strcmp(reinterpret_cast<const char*>(byName->Name), functionName) == 0) {
                return reinterpret_cast<void**>(&iat[i].u1.Function);
            }
        }
    }
    return nullptr;
}

} // namespace

DWORD WINAPI DlssNrRuntimeAdapter::testShimGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size)
{
    return SnippetGetModuleFileNameW(module, filename, size);
}

HMODULE DlssNrRuntimeAdapter::testCallerModule()
{
    if (g_callerModule == nullptr) {
        (void)SnippetGetModuleFileNameW(nullptr, nullptr, 0); // force lazy resolution
    }
    return g_callerModule;
}

// ---------------------------------------------------------------------------
// SEH-guarded snippet calls. Each outer method is noexcept-friendly: no C++
// objects with destructors inside the __try frame.
// ---------------------------------------------------------------------------
namespace {

__declspec(noinline) NVSDK_NGX_Result CallSnippetInitExt(
    DlssNrRuntimeAdapter::InitExtFn fn, unsigned long long appId, const wchar_t* runtimeDir,
    ID3D12Device* device, NVSDK_NGX_Version version, const NVSDK_NGX_Parameter* parameters,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = fn(appId, runtimeDir, device, version, parameters);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallSnippetCreateFeature(
    DlssNrRuntimeAdapter::CreateFeatureFn fn, ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Feature feature, NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = fn(cmdList, feature, parameters, handle);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallSnippetEvaluateFeature(
    DlssNrRuntimeAdapter::EvaluateFeatureFn fn, ID3D12GraphicsCommandList* cmdList,
    const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = fn(cmdList, handle, parameters, callback);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallSnippetReleaseFeature(
    DlssNrRuntimeAdapter::ReleaseFeatureFn fn, NVSDK_NGX_Handle* handle, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = fn(handle);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallSnippetShutdown1(
    DlssNrRuntimeAdapter::ShutdownFn fn, ID3D12Device* device, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = fn(device);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

} // namespace

bool DlssNrRuntimeAdapter::snippetInitExt(ID3D12Device* device, const std::wstring& runtimeDir,
    uint64_t& result, uint32_t& sehCode)
{
    result = 0;
    sehCode = 0;
    if (module_ == nullptr || exports_.initExt == nullptr) {
        return false;
    }
    result = static_cast<uint64_t>(CallSnippetInitExt(exports_.initExt, kSignedSnippetAppId,
        runtimeDir.c_str(), device, NVSDK_NGX_Version_API, nullptr, sehCode));
    veyra::log::info("ngx", std::format("snippet Init_Ext appId=0x{:X} result={} seh={}",
        kSignedSnippetAppId, veyra::ngxResultString(result), sehCode));
    return sehCode == 0;
}

bool DlssNrRuntimeAdapter::snippetCreateFeature(ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle, uint64_t& result, uint32_t& sehCode)
{
    result = 0;
    sehCode = 0;
    *handle = nullptr;
    if (module_ == nullptr || exports_.createFeature == nullptr) {
        return false;
    }
    result = static_cast<uint64_t>(CallSnippetCreateFeature(exports_.createFeature, cmdList,
        kFeatureId, parameters, handle, sehCode));
    veyra::log::info("ngx", std::format("snippet CreateFeature id={} result={} handle={} seh={}",
        static_cast<uint32_t>(kFeatureId), veyra::ngxResultString(result),
        *handle != nullptr ? "non-null" : "null", sehCode));
    return sehCode == 0;
}

bool DlssNrRuntimeAdapter::snippetEvaluateFeature(ID3D12GraphicsCommandList* cmdList,
    const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    uint64_t& result, uint32_t& sehCode)
{
    result = 0;
    sehCode = 0;
    if (module_ == nullptr || exports_.evaluateFeature == nullptr) {
        return false;
    }
    result = static_cast<uint64_t>(CallSnippetEvaluateFeature(exports_.evaluateFeature, cmdList,
        handle, parameters, nullptr, sehCode));
    return sehCode == 0;
}

bool DlssNrRuntimeAdapter::snippetReleaseFeature(NVSDK_NGX_Handle* handle, uint64_t& result, uint32_t& sehCode)
{
    result = 0;
    sehCode = 0;
    if (module_ == nullptr || exports_.releaseFeature == nullptr) {
        return false;
    }
    result = static_cast<uint64_t>(CallSnippetReleaseFeature(exports_.releaseFeature, handle, sehCode));
    veyra::log::info("ngx", std::format("snippet ReleaseFeature result={} seh={}",
        veyra::ngxResultString(result), sehCode));
    return sehCode == 0;
}

bool DlssNrRuntimeAdapter::snippetShutdown1(ID3D12Device* device, uint64_t& result, uint32_t& sehCode)
{
    result = 0;
    sehCode = 0;
    if (module_ == nullptr || exports_.shutdown1 == nullptr) {
        return false;
    }
    result = static_cast<uint64_t>(CallSnippetShutdown1(exports_.shutdown1, device, sehCode));
    veyra::log::info("ngx", std::format("snippet Shutdown1 result={} seh={}",
        veyra::ngxResultString(result), sehCode));
    return sehCode == 0;
}

NVSDK_NGX_Result NVSDK_CONV DlssNrRuntimeAdapter::scalingRatioCallback(NVSDK_NGX_Parameter* parameters)
{
    if (parameters != nullptr) {
        parameters->Set("DLSSNR.ScalingRatio", 1.0f);
    }
    return NVSDK_NGX_Result_Success;
}

DlssNrRuntimeAdapter::~DlssNrRuntimeAdapter()
{
    unload();
}

bool DlssNrRuntimeAdapter::load(const std::wstring& runtimeDir, Status& status)
{
    if (module_ != nullptr) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "nr-adapter: load called twice");
        return false;
    }

    wchar_t resolved[MAX_PATH * 2]{};
    if (GetFullPathNameW(runtimeDir.c_str(), static_cast<DWORD>(std::size(resolved)), resolved, nullptr) == 0) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "nr-adapter: GetFullPathNameW failed");
        return false;
    }
    dllPath_ = std::wstring(resolved) + L"\\nvngx_dlssnr.dll";

    // Development staging and the explicitly named experimental Release pack.
    const auto allowed=[&](const wchar_t* suffix){const size_t n=wcslen(suffix);return dllPath_.size()>n&&_wcsicmp(dllPath_.c_str()+dllPath_.size()-n,suffix)==0;};
    if (!allowed(L"\\runtime_local\\nvidia\\nvngx_dlssnr.dll")&&!allowed(L"\\runtime\\experimental\\nvngx_dlssnr.dll")) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "nr-adapter: resolved runtime path is outside approved staging/experimental directory; refusing to load");
        return false;
    }

    const DWORD attributes = GetFileAttributesW(dllPath_.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        status = Status::FileNotFound;
        veyra::log::error("ngx", "nr-adapter: staged DLL missing");
        return false;
    }

    module_ = LoadLibraryExW(dllPath_.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module_ == nullptr) {
        const DWORD lastError = GetLastError();
        status = Status::LoadLibraryFailure;
        veyra::log::error("ngx", std::format("nr-adapter: LoadLibraryExW failed lastError={}", lastError));
        return false;
    }
    veyra::log::info("ngx", "EXPERIMENTAL LOCAL-ONLY DLSSNR ADAPTER ENABLED");

    void* addresses[5] = {};
    for (size_t i = 0; i < 5; ++i) {
        addresses[i] = reinterpret_cast<void*>(GetProcAddress(module_, kExportNames[i]));
        veyra::log::info("ngx", std::format("nr-adapter: export {} {} address=0x{:016X}",
            kExportNames[i], addresses[i] != nullptr ? "present" : "MISSING",
            reinterpret_cast<uintptr_t>(addresses[i])));
    }
    if (addresses[0] == nullptr || addresses[1] == nullptr || addresses[2] == nullptr ||
        addresses[3] == nullptr || addresses[4] == nullptr) {
        status = Status::MissingExport;
        FreeLibrary(module_);
        module_ = nullptr;
        return false;
    }

    exports_.initExt = reinterpret_cast<InitExtFn>(addresses[0]);
    exports_.createFeature = reinterpret_cast<CreateFeatureFn>(addresses[1]);
    exports_.evaluateFeature = reinterpret_cast<EvaluateFeatureFn>(addresses[2]);
    exports_.releaseFeature = reinterpret_cast<ReleaseFeatureFn>(addresses[3]);
    exports_.shutdown1 = reinterpret_cast<ShutdownFn>(addresses[4]);
    return true;
}

bool DlssNrRuntimeAdapter::installCallerCompatibility(Status& status)
{
    if (module_ == nullptr) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "caller-compat: snippet not loaded");
        return false;
    }
    if (shimInstalled_) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "caller-compat: shim already installed by this session");
        return false;
    }
    if (g_shimOwnerActive) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "caller-compat: another adapter session already owns the IAT slot");
        return false;
    }

    void** slot = FindImportedFunctionSlot(module_, "GetModuleFileNameW");
    if (slot == nullptr) {
        status = Status::MissingExport;
        veyra::log::error("ngx", "caller-compat: no GetModuleFileNameW import slot found in snippet IAT; refusing wider hooks");
        return false;
    }

    originalGetModuleFileNameW_ = *slot;
    if (originalGetModuleFileNameW_ == nullptr) {
        status = Status::MissingExport;
        veyra::log::error("ngx", "caller-compat: IAT slot is null before install");
        return false;
    }

    g_realGetModuleFileNameW = reinterpret_cast<GetModuleFileNameWFn>(originalGetModuleFileNameW_);
    g_callerModule = nullptr;
    g_shimOwnerActive = true;
    iatSlot_ = slot;

    DWORD oldProtect = 0;
    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect) == FALSE) {
        status = Status::DeviceFailure;
        veyra::log::error("ngx", std::format("caller-compat: VirtualProtect failed lastError={}", GetLastError()));
        g_shimOwnerActive = false;
        iatSlot_ = nullptr;
        return false;
    }

    InterlockedExchangePointer(slot, reinterpret_cast<void*>(&SnippetGetModuleFileNameW));

    DWORD restoredProtect = 0;
    if (VirtualProtect(slot, sizeof(void*), oldProtect, &restoredProtect) == FALSE) {
        status = Status::DeviceFailure;
        veyra::log::error("ngx", std::format("caller-compat: VirtualProtect restore failed lastError={}", GetLastError()));
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

    shimInstalled_ = true;
    veyra::log::info("ngx", std::format("caller-compat: shim installed slot=0x{:016X} original=0x{:016X} (LOCAL EXPERIMENTAL ONLY)",
        reinterpret_cast<uintptr_t>(iatSlot_), reinterpret_cast<uintptr_t>(originalGetModuleFileNameW_)));
    return true;
}

void DlssNrRuntimeAdapter::restoreCallerCompatibility()
{
    if (!shimInstalled_ || iatSlot_ == nullptr) {
        return;
    }

    DWORD oldProtect = 0;
    if (VirtualProtect(iatSlot_, sizeof(void*), PAGE_READWRITE, &oldProtect) != FALSE) {
        InterlockedExchangePointer(iatSlot_, originalGetModuleFileNameW_);
        DWORD restoredProtect = 0;
        (void)VirtualProtect(iatSlot_, sizeof(void*), oldProtect, &restoredProtect);
        FlushInstructionCache(GetCurrentProcess(), iatSlot_, sizeof(void*));
    }

    const bool restoredCorrectly = *iatSlot_ == originalGetModuleFileNameW_;
    if (!restoredCorrectly) {
        veyra::log::error("ngx", std::format("caller-compat: IAT slot not restored to original actual=0x{:016X} expected=0x{:016X}",
            reinterpret_cast<uintptr_t>(*iatSlot_), reinterpret_cast<uintptr_t>(originalGetModuleFileNameW_)));
    }
    else {
        veyra::log::info("ngx", "caller-compat: IAT slot restored to original function");
    }

    g_shimOwnerActive = false;
    g_callerModule = nullptr;
    g_realGetModuleFileNameW = nullptr;
    iatSlot_ = nullptr;
    originalGetModuleFileNameW_ = nullptr;
    shimInstalled_ = false;
}

void DlssNrRuntimeAdapter::unload()
{
    if (shimInstalled_) {
        restoreCallerCompatibility();
    }
    if (module_ != nullptr) {
        // Reverse order: IAT restored above, then free the snippet module.
        FreeLibrary(module_);
        module_ = nullptr;
        veyra::log::info("ngx", "nr-adapter: snippet module freed");
    }
    exports_ = Exports{};
}

} // namespace veyra::ngx
