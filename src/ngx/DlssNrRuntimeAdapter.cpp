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

} // namespace

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
    veyra::log::info("ngx", "nr-adapter: snippet loaded with restricted search flags (LOCAL EXPERIMENTAL ONLY)");

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
    // Implemented in P1.3 together with its boundary test battery.
    (void)status;
    veyra::log::error("ngx", "nr-adapter: caller compatibility shim not implemented yet (P1.3)");
    return false;
}

void DlssNrRuntimeAdapter::restoreCallerCompatibility()
{
    // Nothing to restore until P1.3 installs the shim.
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
