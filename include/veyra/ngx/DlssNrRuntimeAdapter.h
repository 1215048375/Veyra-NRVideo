#pragma once

#include <d3d12.h>
#include <nvsdk_ngx.h>

#include <cstdint>
#include <string>

#include "veyra/Result.h"

namespace veyra::ngx {

// Local-only experimental adapter for the signed DLSSNR snippet
// (Playbook section 8). This is the ONLY translation unit in the project that
// knows: Feature ID 18, the signed snippet application ID, the DLL export
// signatures, the caller-name compatibility shim, and the local runtime path
// convention. It is compiled only when VEYRA_ENABLE_EXPERIMENTAL_DLSSNR is ON
// and is local experimental only.
class DlssNrRuntimeAdapter {
public:
    using InitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(
        unsigned long long,
        const wchar_t*,
        ID3D12Device*,
        NVSDK_NGX_Version,
        const NVSDK_NGX_Parameter*);
    using CreateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
        ID3D12GraphicsCommandList*,
        NVSDK_NGX_Feature,
        NVSDK_NGX_Parameter*,
        NVSDK_NGX_Handle**);
    using EvaluateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
        ID3D12GraphicsCommandList*,
        const NVSDK_NGX_Handle*,
        const NVSDK_NGX_Parameter*,
        PFN_NVSDK_NGX_ProgressCallback);
    using ReleaseFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
    using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);

    struct Exports {
        InitExtFn initExt = nullptr;
        CreateFeatureFn createFeature = nullptr;
        EvaluateFeatureFn evaluateFeature = nullptr;
        ReleaseFeatureFn releaseFeature = nullptr;
        ShutdownFn shutdown1 = nullptr;
    };

    // Feature ID and signed-snippet application ID from the verified call
    // contract; the App ID is NOT the Veyra NGX Project ID.
    static constexpr NVSDK_NGX_Feature kFeatureId = static_cast<NVSDK_NGX_Feature>(18);
    static constexpr unsigned long long kSignedSnippetAppId = 0x0876232Cull;

    DlssNrRuntimeAdapter() = default;
    ~DlssNrRuntimeAdapter();

    DlssNrRuntimeAdapter(const DlssNrRuntimeAdapter&) = delete;
    DlssNrRuntimeAdapter& operator=(const DlssNrRuntimeAdapter&) = delete;

    // Canonicalizes `runtimeDir`, loads nvngx_dlssnr.dll from it with
    // restricted search flags and resolves all five exports (fail-closed on
    // any missing one). The caller must have verified the file identity
    // beforehand (veyra_base FileIdentity).
    bool load(const std::wstring& runtimeDir, Status& status);

    // Caller-name compatibility shim on the snippet's GetModuleFileNameW IAT
    // slot (Playbook 8.3). Exactly one adapter session may own the slot.
    // P1.3 implements install/restore; load() alone never touches the IAT.
    bool installCallerCompatibility(Status& status);
    void restoreCallerCompatibility();
    bool callerCompatibilityInstalled() const { return shimInstalled_; }

    // Reverse-order teardown: restores the IAT shim if installed, then frees
    // the snippet module. Safe to call after partial initialization.
    void unload();

    bool loaded() const { return module_ != nullptr; }
    const Exports& exports() const { return exports_; }
    HMODULE module() const { return module_; }
    const std::wstring& dllPath() const { return dllPath_; }

    // Test hooks for the --shim-test battery (P1.3). Callers must install the
    // shim first; the hooks invoke the installed shim function directly.
    static DWORD WINAPI testShimGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size);
    static HMODULE testCallerModule();

private:
    HMODULE module_ = nullptr;
    Exports exports_{};
    std::wstring dllPath_;

    // Shim state (owned by this adapter session only).
    bool shimInstalled_ = false;
    void** iatSlot_ = nullptr;
    void* originalGetModuleFileNameW_ = nullptr;
};

} // namespace veyra::ngx
