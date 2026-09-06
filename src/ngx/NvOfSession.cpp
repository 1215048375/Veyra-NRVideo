#include "veyra/ngx/NvOfSession.h"

#include <windows.h>
#include <d3d12.h>

// NVOF SDK headers
#include <nvOpticalFlowCommon.h>
#include <nvOpticalFlowD3D12.h>

#include <format>

#include "veyra/Log.h"

namespace veyra::ngx {

using NvOfHandleT = NvOFHandle;
using NvOfBufferT = NvOFGPUBufferHandle;
using PFN_NvOFAPICreateInstanceD3D12Local = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_D3D12_API_FUNCTION_LIST*);

struct NvOfSession::FnList {
    NV_OF_D3D12_API_FUNCTION_LIST list{};
    PFN_NvOFAPICreateInstanceD3D12Local createInstance = nullptr;
};

namespace {
using PFN_NvOFGetMaxSupportedApiVersion = NV_OF_STATUS(NVOFAPI*)(uint32_t*);
} // namespace

NvOfSession::~NvOfSession()
{
    shutdown();
}

bool NvOfSession::initialize(ID3D12Device* device,
                             ID3D12Resource* inputA,
                             ID3D12Resource* inputB,
                             ID3D12Resource* flowOut,
                             ID3D12Resource* costOut,
                             const Desc& desc,
                             Status& status)
{
    // Entry contract (s9-A + s10-IV): ALL required pointers checked BEFORE
    // ANY side effect - including the shutdown() of a previously-valid
    // session. A null-argument call on a live session must be rejected
    // WITHOUT destroying or altering that session.
    if (device == nullptr || inputA == nullptr || inputB == nullptr ||
        flowOut == nullptr || costOut == nullptr ||
        desc.inFence == nullptr || desc.outFence == nullptr) {
        log::error("nvof-session", std::format(
            "initialize rejected: device={} inputA={} inputB={} flowOut={} costOut={} inFence={} outFence={}",
            device ? 1 : 0, inputA ? 1 : 0, inputB ? 1 : 0, flowOut ? 1 : 0,
            costOut ? 1 : 0, desc.inFence ? 1 : 0, desc.outFence ? 1 : 0));
        status = Status::InvalidArgument;
        return false;
    }
    if (initialized_) {
        shutdown();
    }
    inFence_ = desc.inFence;
    outFence_ = desc.outFence;

    // Absolute System32 load with restricted search flags (Playbook 5.4).
    dll_ = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dll_ == nullptr) {
        log::error("nvof-session", std::format("LoadLibraryExW nvofapi64.dll failed err={}", GetLastError()));
        status = Status::LoadLibraryFailure;
        return false;
    }
    auto getMaxVer = reinterpret_cast<PFN_NvOFGetMaxSupportedApiVersion>(
        GetProcAddress(static_cast<HMODULE>(dll_), "NvOFGetMaxSupportedApiVersion"));
    if (getMaxVer == nullptr) {
        log::error("nvof-session", "NvOFGetMaxSupportedApiVersion export missing");
        status = Status::MissingExport;
        return false;
    }
    uint32_t maxVer = 0;
    const NV_OF_STATUS vst = getMaxVer(&maxVer);
    log::info("nvof-session", std::format("maxApiVersion status={} version=0x{:X}",
        static_cast<int>(vst), maxVer));
    if (vst != NV_OF_SUCCESS) {
        status = Status::DeviceFailure;
        return false;
    }

    fn_ = new FnList();
    fn_->createInstance = reinterpret_cast<PFN_NvOFAPICreateInstanceD3D12Local>(
        GetProcAddress(static_cast<HMODULE>(dll_), "NvOFAPICreateInstanceD3D12"));
    if (fn_->createInstance == nullptr) {
        log::error("nvof-session", "NvOFAPICreateInstanceD3D12 export missing");
        status = Status::MissingExport;
        return false;
    }
    const NV_OF_STATUS ist = fn_->createInstance(maxVer, &fn_->list);
    log::info("nvof-session", std::format("CreateInstanceD3D12 status={}", static_cast<int>(ist)));
    if (ist != NV_OF_SUCCESS || fn_->list.nvCreateOpticalFlowD3D12 == nullptr ||
        fn_->list.nvOFInit == nullptr || fn_->list.nvOFExecuteD3D12 == nullptr ||
        fn_->list.nvOFRegisterResourceD3D12 == nullptr || fn_->list.nvOFDestroy == nullptr) {
        status = Status::DeviceFailure;
        return false;
    }

    NvOfHandleT ofHandle = reinterpret_cast<NvOfHandleT>(ofHandle_);
    const NV_OF_STATUS cst = fn_->list.nvCreateOpticalFlowD3D12(device, &ofHandle);
    ofHandle_ = ofHandle;
    log::info("nvof-session", std::format("CreateOpticalFlowD3D12 status={} handle={}",
        static_cast<int>(cst), ofHandle != nullptr ? "non-null" : "null"));
    if (cst != NV_OF_SUCCESS || ofHandle == nullptr) {
        status = Status::DeviceFailure;
        return false;
    }

    // Capability query (contract, s7): two-call protocol. First call with
    // nullptr returns the ELEMENT COUNT; then allocate exactly that many
    // elements and read. Scalar caps use element count 1. Never divide by
    // sizeof(uint32_t). Query failure, an empty list, or an unsupported
    // requested grid all fail closed - no silent fallback.
    if (fn_->list.nvOFGetCaps == nullptr) {
        log::error("nvof-session", "nvOFGetCaps missing from function table (fail closed)");
        status = Status::MissingExport;
        return false;
    }
    {
        uint32_t elemCount = 0;
        NV_OF_STATUS gSt = fn_->list.nvOFGetCaps(ofHandle,
            NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, nullptr, &elemCount);
        if (gSt != NV_OF_SUCCESS || elemCount == 0 || elemCount > 64) {
            log::error("nvof-session", std::format(
                "grids caps first call failed st={} elemCount={} (fail closed)", (int)gSt, elemCount));
            status = Status::DeviceFailure;
            return false;
        }
        caps_.supportedGrids.assign(elemCount, 0);
        uint32_t filled = elemCount;
        gSt = fn_->list.nvOFGetCaps(ofHandle,
            NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, caps_.supportedGrids.data(), &filled);
        if (gSt != NV_OF_SUCCESS || filled == 0) {
            log::error("nvof-session", std::format(
                "grids caps second call failed st={} filled={} (fail closed)", (int)gSt, filled));
            status = Status::DeviceFailure;
            return false;
        }
        caps_.supportedGrids.resize(filled);
        // Scalar caps: element count reset to 1 before EVERY query; any
        // failure or absurd value fails closed (logged with actual state).
        auto scalarCap = [&](NV_OF_CAPS cap, const char* name, uint32_t& out) -> bool {
            uint32_t v = 0;
            uint32_t one = 1;   // reset per query
            const NV_OF_STATUS sc = fn_->list.nvOFGetCaps(ofHandle, cap, &v, &one);
            if (sc != NV_OF_SUCCESS) {
                log::error("nvof-session", std::format(
                    "scalar cap {} query failed st={} (fail closed)", name, (int)sc));
                return false;
            }
            out = v;
            return true;
        };
        if (!scalarCap(NV_OF_CAPS_WIDTH_MIN, "WIDTH_MIN", caps_.minWidth) ||
            !scalarCap(NV_OF_CAPS_WIDTH_MAX, "WIDTH_MAX", caps_.maxWidth) ||
            !scalarCap(NV_OF_CAPS_HEIGHT_MIN, "HEIGHT_MIN", caps_.minHeight) ||
            !scalarCap(NV_OF_CAPS_HEIGHT_MAX, "HEIGHT_MAX", caps_.maxHeight)) {
            status = Status::DeviceFailure;
            return false;
        }
        caps_.queried = true;
        std::string list;
        for (uint32_t g : caps_.supportedGrids) list += std::to_string(g) + " ";
        const bool widthOk = caps_.minWidth <= desc.width && desc.width <= caps_.maxWidth;
        const bool heightOk = caps_.minHeight <= desc.height && desc.height <= caps_.maxHeight;
        log::info("nvof-session", std::format(
            "caps: elemCount={} grids=[{}] width=[{},{}] height=[{},{}] | requested {}x{} widthOk={} heightOk={}",
            elemCount, list, caps_.minWidth, caps_.maxWidth, caps_.minHeight, caps_.maxHeight,
            desc.width, desc.height, widthOk, heightOk));
        if (!widthOk || !heightOk) {
            log::error("nvof-session", std::format(
                "requested extent {}x{} outside capability range (fail closed)", desc.width, desc.height));
            status = Status::InvalidArgument;
            return false;
        }
    }
    const uint32_t grid = desc.gridSize;
    bool gridSupported = false;
    for (uint32_t g : caps_.supportedGrids) {
        if (g == grid) { gridSupported = true; break; }
    }
    if (!gridSupported) {
        std::string list;
        for (uint32_t g : caps_.supportedGrids) list += std::to_string(g) + " ";
        log::error("nvof-session", std::format(
            "requested grid {} NOT in capability list [{}] (fail closed)", grid, list));
        status = Status::InvalidArgument;
        return false;
    }
    gridW_ = (desc.width + grid - 1) / grid;
    gridH_ = (desc.height + grid - 1) / grid;
    caps_.selectedGrid = grid;

    // ---- 3. GetDesc validation of every registered resource -------------
    {
        const D3D12_RESOURCE_DESC da = inputA->GetDesc();
        const D3D12_RESOURCE_DESC db = inputB->GetDesc();
        const D3D12_RESOURCE_DESC df = flowOut->GetDesc();
        const D3D12_RESOURCE_DESC dc = costOut->GetDesc();
        const bool inputsOk = da.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
                              db.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
                              da.Width == desc.width && da.Height == desc.height &&
                              db.Width == desc.width && db.Height == desc.height;
        const bool flowOk = df.Format == DXGI_FORMAT_R16G16_SINT &&
                            df.Width == gridW_ && df.Height == gridH_;
        const bool costOk = dc.Format == DXGI_FORMAT_R8_UINT &&
                            dc.Width == gridW_ && dc.Height == gridH_;
        log::info("nvof-session", std::format(
            "contract-check: inputs A=0x{:X}/{}x{} B=0x{:X}/{}x{} (want B8G8R8A8/{}x{}) | flow 0x{:X}/{}x{} (want 0x26=R16G16_SINT/{:d}x{:d}) | cost 0x{:X}/{}x{} (want 0x3E=R8_UINT/{:d}x{:d}) -> inputsOk={} flowOk={} costOk={}",
            (unsigned)da.Format, (uint32_t)da.Width, (uint32_t)da.Height,
            (unsigned)db.Format, (uint32_t)db.Width, (uint32_t)db.Height,
            desc.width, desc.height,
            (unsigned)df.Format, (uint32_t)df.Width, (uint32_t)df.Height, gridW_, gridH_,
            (unsigned)dc.Format, (uint32_t)dc.Width, (uint32_t)dc.Height, gridW_, gridH_,
            inputsOk, flowOk, costOk));
        if (!inputsOk || !flowOk || !costOk) {
            log::error("nvof-session", "resource contract violated (fail closed before registration)");
            status = Status::InvalidArgument;
            return false;
        }
    }

    NV_OF_INIT_PARAMS init{};
    init.width = desc.width;
    init.height = desc.height;
    init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(grid);
    init.mode = NV_OF_MODE_OPTICALFLOW;
    init.perfLevel = NV_OF_PERF_LEVEL_MEDIUM;
    init.enableExternalHints = NV_OF_FALSE;
    init.enableOutputCost = NV_OF_TRUE;  // cost is mandatory (explicit costOut param)
    init.hPrivData = nullptr;
    init.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
    init.enableGlobalFlow = NV_OF_FALSE;
    init.inputBufferFormat = NV_OF_BUFFER_FORMAT_ABGR8;
    // Output contract: SHORT2 (S10.5) at grid extent; the caller registers
    // an R16G16_SINT grid-extent resource as flowOut (never half-float).
    const NV_OF_STATUS initSt = fn_->list.nvOFInit(ofHandle, &init);
    log::info("nvof-session", std::format("nvOFInit status={} ({}x{} grid{} fwd ABGR8 flowExtent={}x{})",
        static_cast<int>(initSt), desc.width, desc.height, grid, gridW_, gridH_));
    if (initSt != NV_OF_SUCCESS) {
        status = Status::DeviceFailure;
        return false;
    }

    // Unified registration with rollback: whichever of inputA/inputB/flowOut/
    // costOut fails, every already-registered resource is unregistered in
    // REVERSE order (logged) before returning false. No reliance on later
    // destructor behavior.
    auto unregisterOne = [&](NvOfOpaqueHandle& h, const char* name) {
        if (h == nullptr) return;
        NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 up{};
        up.hOFGpuBuffer = reinterpret_cast<NvOfBufferT>(h);
        const NV_OF_STATUS us = fn_->list.nvOFUnregisterResourceD3D12(&up);
        log::info("nvof-session", std::format("rollback unregister {} status={}", name, (int)us));
        if (us == NV_OF_SUCCESS) h = nullptr;
    };
    auto rollbackAll = [&]() {
        unregisterOne(hCost_, "costOut");
        unregisterOne(hFlow_, "flowOut");
        unregisterOne(hInputB_, "inputB");
        unregisterOne(hInputA_, "inputA");
    };
    auto registerResource = [&](ID3D12Resource* resource, NvOfOpaqueHandle& handleOpaque,
                                const char* name) -> bool {
        NvOfBufferT handle = nullptr;
        NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 reg{};
        reg.resource = resource;
        reg.hOFGpuBuffer = &handle;
        const NV_OF_STATUS rst = fn_->list.nvOFRegisterResourceD3D12(ofHandle, &reg);
        handleOpaque = handle;
        log::info("nvof-session", std::format("register {} status={}", name, static_cast<int>(rst)));
        if (rst != NV_OF_SUCCESS) {
            log::error("nvof-session", std::format(
                "register {} failed st={}; rolling back all registered resources", name, (int)rst));
            rollbackAll();
        }
        return rst == NV_OF_SUCCESS;
    };
    if (!registerResource(inputA, hInputA_, "inputA") ||
        !registerResource(inputB, hInputB_, "inputB") ||
        !registerResource(flowOut, hFlow_, "flowOut") ||
        !registerResource(costOut, hCost_, "costOut")) {
        status = Status::DeviceFailure;
        return false;
    }

    initialized_ = true;
    status = Status::Ok;
    return true;
}

bool NvOfSession::execute(uint64_t inValue, Status& status)
{
    if (!initialized_) {
        status = Status::InvalidArgument;
        return false;
    }
    // Out-fence point: flow complete at the pre-published next value. The
    // caller makes its queue wait on outFence_ == nextOutValue_ afterwards.
    NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
    NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};
    NV_OF_FENCE_POINT inFencePoint{};
    NV_OF_FENCE_POINT outFencePoint{};
    const NvOfHandleT ofHandle = reinterpret_cast<NvOfHandleT>(ofHandle_);

    in.inputFrame = reinterpret_cast<NvOfBufferT>(hInputB_);    // current
    in.referenceFrame = reinterpret_cast<NvOfBufferT>(hInputA_); // previous
    // Direction (P0.4 item 10): input = current (B), reference = previous
    // (A) -> SDK vectors point current -> previous. The densify pass has an
    // explicit negate flag; the +8px test pins the sign before DLSSG.
    in.disableTemporalHints = NV_OF_TRUE; // matches the verified probe path
    in.numFencePoints = 1;
    inFencePoint.fence = inFence_;
    inFencePoint.value = inValue;
    in.fencePoint = &inFencePoint;

    out.outputBuffer = reinterpret_cast<NvOfBufferT>(hFlow_);
    out.outputCostBuffer = reinterpret_cast<NvOfBufferT>(hCost_);
    outFencePoint.fence = outFence_;
    outFencePoint.value = nextOutValue_;
    out.fencePoint = &outFencePoint;

    const NV_OF_STATUS st = fn_->list.nvOFExecuteD3D12(ofHandle, &in, &out);
    if (st != NV_OF_SUCCESS) {
        log::error("nvof-session", std::format("nvOFExecuteD3D12 status={} execute#{}",
            static_cast<int>(st), executeCount_));
        status = Status::DeviceFailure;
        return false;
    }
    ++executeCount_;
    ++nextOutValue_;
    status = Status::Ok;
    return true;
}

bool NvOfSession::unregisterAll(Status& status)
{
    // Reverse-order unregister: cost -> flow -> inputB -> inputA. The caller
    // MUST keep the registered D3D12 resources alive until this returns and
    // MUST Release them BEFORE shutdown() runs: observed 2026-09-04 (probe
    // t10-L0-r2), releasing an NVOF-registered ID3D12Resource after
    // nvOFDestroy + FreeLibrary(nvofapi64.dll) segfaults inside the resource
    // destruction path. Correct ownership order is
    //   unregisterAll() -> release textures -> shutdown().
    if (!initialized_ || ofHandle_ == nullptr || fn_ == nullptr ||
        fn_->list.nvOFUnregisterResourceD3D12 == nullptr) {
        status = Status::InvalidArgument;
        return false;
    }
    bool allOk = true;
    auto unregisterOne = [&](NvOfOpaqueHandle& h, const char* name) {
        if (h == nullptr) return;
        NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 p{};
        p.hOFGpuBuffer = reinterpret_cast<NvOfBufferT>(h);
        const NV_OF_STATUS st = fn_->list.nvOFUnregisterResourceD3D12(&p);
        log::info("nvof-session", std::format("unregister {} status={}", name, static_cast<int>(st)));
        if (st == NV_OF_SUCCESS) h = nullptr;
        else allOk = false;
    };
    unregisterOne(hCost_, "costOut");
    unregisterOne(hFlow_, "flowOut");
    unregisterOne(hInputB_, "inputB");
    unregisterOne(hInputA_, "inputA");
    status = allOk ? Status::Ok : Status::DeviceFailure;
    return allOk;
}

void NvOfSession::shutdown()
{
    // Reverse-order teardown (user directive s6 item 6): unregister
    // cost -> flow -> inputB -> inputA, destroy session, free the function
    // table, then the DLL. Every status is logged. If unregisterAll() was
    // already called, the handles are null and the unregister steps no-op.
    auto unregister = [&](NvOfOpaqueHandle& h, const char* name) {
        if (h == nullptr || fn_ == nullptr || fn_->list.nvOFUnregisterResourceD3D12 == nullptr || ofHandle_ == nullptr) return;
        NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 p{};
        p.hOFGpuBuffer = reinterpret_cast<NvOfBufferT>(h);
        const NV_OF_STATUS st = fn_->list.nvOFUnregisterResourceD3D12(&p);
        log::info("nvof-session", std::format("unregister {} status={}", name, static_cast<int>(st)));
        if (st == NV_OF_SUCCESS) h = nullptr;
    };
    unregister(hCost_, "costOut");
    unregister(hFlow_, "flowOut");
    unregister(hInputB_, "inputB");
    unregister(hInputA_, "inputA");
    if (ofHandle_ != nullptr && fn_ != nullptr && fn_->list.nvOFDestroy != nullptr) {
        const NV_OF_STATUS st = fn_->list.nvOFDestroy(reinterpret_cast<NvOfHandleT>(ofHandle_));
        log::info("nvof-session", std::format("nvOFDestroy status={} executes={}",
            static_cast<int>(st), executeCount_));
        ofHandle_ = nullptr;
    }
    hInputA_ = hInputB_ = hFlow_ = nullptr;
    hCost_ = nullptr;
    delete fn_;
    fn_ = nullptr;
    if (dll_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(dll_));
        dll_ = nullptr;
    }
    initialized_ = false;
    inFence_ = outFence_ = nullptr;
    nextOutValue_ = 1;
}

} // namespace veyra::ngx
