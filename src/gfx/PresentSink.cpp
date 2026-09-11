#include "veyra/gfx/PresentSink.h"

#include <windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include <format>

#include "veyra/Log.h"

namespace veyra::gfx {

namespace {

const wchar_t* kWindowClassName = L"VeyraPresentSink";


} // namespace

PresentSink::~PresentSink()
{
    shutdown();
}

LRESULT CALLBACK PresentSink::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // Buffer recreation is driven by the engine loop (resize()).
        return DefWindowProcW(hwnd, msg, wp, lp);
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool PresentSink::initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                             const Desc& desc, Status& status)
{
    device_ = device;
    shutdownCalled_ = false;
    desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;

    // Register a window class once per process.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &PresentSink::wndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClassRegistered_ = RegisterClassExW(&wc) != 0;
    if (!windowClassRegistered_) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log::error("present", std::format("RegisterClassExW failed err={}", err));
            status = Status::WindowFailure;
            return false;
        }
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    AdjustWindowRect(&rect, style, FALSE);
    hwnd_ = desc.targetWindow ? desc.targetWindow : CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, kWindowClassName, desc_.title.c_str(),
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        log::error("present", std::format("CreateWindowExW failed err={}", GetLastError()));
        status = Status::WindowFailure;
        return false;
    }
    if (!desc.targetWindow) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    // Factory with tearing awareness.
    UINT factoryFlags = 0;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)))) {
        log::error("present", "CreateDXGIFactory2 failed");
        status = Status::WindowFailure;
        return false;
    }
    factory_ = factory;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory_.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing)))) {
            tearingSupported_ = allowTearing != FALSE;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width_;
    scd.Height = height_;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 3;
    // Opt-in capture experiment; actual capture support depends on the
    // external capture API. Neither mode changes the pixel format.
    scd.SwapEffect = desc.captureCompatible ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    if(desc.xess){
        xess_=std::make_unique<XessPresenter>();
        if(!xess_->initialize(device,queue,factory_.Get(),hwnd_,scd,swapChain_.GetAddressOf())){
            // XeSS is an optional experimental presenter. A missing or
            // incompatible local runtime must not prevent basic playback.
            log::warn("present", "XeSS FG initialization failed; falling back to native presentation");
            swapChain_.Reset();
            xess_.reset();
        }
    }
    if (!swapChain_) {
    if (FAILED(factory_->CreateSwapChainForHwnd(queue, hwnd_, &scd, nullptr, nullptr, &swapChain1))) {
        log::error("present", "CreateSwapChainForHwnd failed");
        status = Status::WindowFailure;
        return false;
    }
    if (FAILED(swapChain1.As(&swapChain_))) {
        log::error("present", "QI IDXGISwapChain3 failed");
        status = Status::WindowFailure;
        return false;
    }
    }
    // Block ALT+ENTER; the engine owns mode changes.
    (void)factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    queue_ = queue;

    if (!refetchBackBuffers()) {
        status = Status::WindowFailure;
        return false;
    }
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    scBufferWidth_ = width_;
    scBufferHeight_ = height_;
    bufferExtentW_ = width_;
    bufferExtentH_ = height_;

    DXGI_SWAP_CHAIN_DESC1 actual{};
    const HRESULT descResult=swapChain_->GetDesc1(&actual);
    if(FAILED(descResult)||actual.SwapEffect!=scd.SwapEffect){
        log::error("present",std::format("swapchain mode rejected hr=0x{:X} requested={} actual={}",unsigned(descResult),unsigned(scd.SwapEffect),unsigned(actual.SwapEffect)));
        status=Status::WindowFailure;return false;
    }
    log::info("present", std::format("present-sink: window {}x{} swapEffect={} buffers=3 vsync={} tearing={} captureCompatible={} (capture not verified)",
        width_, height_, desc.captureCompatible?"flip-sequential":"flip-discard", desc_.vsync ? 1 : 0, tearingSupported_ ? 1 : 0, desc.captureCompatible));
    return true;
}

bool PresentSink::refetchBackBuffers()
{
    for (auto& b : backBuffers_) b.Reset();
    for (UINT i = 0; i < 3; ++i) {
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])))) {
            log::error("present", std::format("GetBuffer({}) failed", i));
            return false;
        }
    }
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    return true;
}

void PresentSink::recreateSwapChain(UINT flags)
{
    (void)flags;
    // Not used anymore: ResizeBuffers failure is a hard error surfaced to
    // the engine (P0.6). Kept as a no-op stub for link compatibility.
}

bool PresentSink::processMessages(bool& windowClosed)
{
    windowClosed = false;
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            windowClosed = true;
            closed_ = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

ID3D12Resource* PresentSink::currentBackBuffer()
{
    return backBuffers_[backBufferIndex_].Get();
}

bool PresentSink::present(Status& status)
{
    const UINT syncInterval = desc_.vsync ? 1 : 0;
    const UINT flags = (!desc_.vsync && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ++attemptedPresentCount_;
    if(xess_&&!xess_->beforePresent()){status=Status::WindowFailure;return false;}
    const HRESULT hr = swapChain_->Present(syncInterval, flags);
    if (SUCCEEDED(hr)) {
        ++presentCount_;
        backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
        if(xess_&&!xess_->afterPresent()){status=Status::WindowFailure;return false;}
        return true;
    }
    ++failedPresentCount_;
    const HRESULT removed = device_ != nullptr ? device_->GetDeviceRemovedReason() : S_OK;
    log::error("present", std::format(
        "Present FAILED hr=0x{:X} sync={} flags=0x{:X} attempted={} failed={} removedReason=0x{:X}",
        static_cast<unsigned>(hr), syncInterval, flags,
        static_cast<unsigned long long>(attemptedPresentCount_),
        static_cast<unsigned long long>(failedPresentCount_),
        static_cast<unsigned>(removed)));
    // Device-lost class failures: dump DRED evidence (breadcrumbs + page
    // fault) before reporting the failure to the engine.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
        removed != S_OK) {
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (device_ != nullptr && SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dred)))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&crumbs))) {
                UINT count = 0;
                for (const D3D12_AUTO_BREADCRUMB_NODE* n = crumbs.pHeadAutoBreadcrumbNode;
                     n != nullptr && count < 8; n = n->pNext, ++count) {
                    log::error("present", std::format(
                        "DRED breadcrumb #{}: lastOp={} of {} on cmdList={} cmdQueue={}",
                        count, n->pLastBreadcrumbValue ? static_cast<int>(*n->pLastBreadcrumbValue) : -1,
                        n->BreadcrumbCount, n->pCommandList != nullptr ? "ptr" : "null",
                        n->pCommandQueue != nullptr ? "ptr" : "null"));
                }
                if (crumbs.pHeadAutoBreadcrumbNode == nullptr) {
                    log::error("present", "DRED breadcrumbs: none (CPU-side removal)");
                }
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
            if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault))) {
                log::error("present", std::format("DRED pageFaultVA=0x{:X}",
                    static_cast<unsigned long long>(fault.PageFaultVA)));
            }
        }
        status = Status::DeviceFailure;
        return false;
    }
    status = Status::WindowFailure;
    return false;
}

bool PresentSink::waitForQueueIdle()
{
    if (queue_ == nullptr || device_ == nullptr) return true;
    ComPtr<ID3D12Fence> idleFence;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence)))) return false;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (ev == nullptr) return false;
    bool completed=false;
    const HRESULT signal = queue_->Signal(idleFence.Get(), 1);
    const HRESULT wait = SUCCEEDED(signal) ? idleFence->SetEventOnCompletion(1, ev) : signal;
    if (SUCCEEDED(wait)) {
        const DWORD result = WaitForSingleObject(ev, 5000);
        completed=result==WAIT_OBJECT_0;
        if (result != WAIT_OBJECT_0) log::warn("present", std::format("queue idle wait result={}", result));
    } else {
        log::warn("present", std::format("queue idle setup failed hr=0x{:X}", static_cast<unsigned>(wait)));
    }
    CloseHandle(ev);
    return completed;
}

void PresentSink::resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    if (width == width_ && height == height_) return;
    const UINT flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    // DXGI spec compliance before ResizeBuffers: wait for outstanding GPU
    // work on the presenting queue, then release ALL back-buffer references.
    if (!waitForQueueIdle()) return;
    for (auto& b : backBuffers_) b.Reset();
    HRESULT hr = swapChain_->ResizeBuffers(3, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, flags);
    if (FAILED(hr)) {
        log::error("present", std::format("ResizeBuffers FAILED hr=0x{:X} (queue idle, buffers released)",
            static_cast<unsigned>(hr)));
        refetchBackBuffers();
        return;
    }
    if (!refetchBackBuffers()) return;
    width_ = width;
    height_ = height;
    scBufferWidth_ = width_;
    scBufferHeight_ = height_;
    bufferExtentW_ = width_;
    bufferExtentH_ = height_;
    pendingResize_ = true;
    log::info("present", std::format("present-sink: resized to {}x{}", width_, height_));
}

void PresentSink::shutdown()
{
    // Dependency order (s10, proven by the 0x87D matrix): backbuffers ->
    // swapchain -> window -> factory. The window must OUTLIVE the swapchain:
    // with the D3D12 debug layer active, releasing a flip swapchain whose
    // target HWND is already destroyed raises exception 0x87D in
    // KERNELBASE (WER event 1000, observed t10-L1 2026-09-04). The swapchain
    // must also be released BEFORE the D3D12 queue it presents on.
    if (shutdownCalled_) return; // second call (destructor) has nothing left
    shutdownCalled_ = true;
    // XeSS owns a proxy swapchain and may have copied tagged inputs on the
    // presenting queue. Its shutdown contract requires that work to finish.
    waitForQueueIdle();
    log::info("present", "sink-shutdown: sub-step backbuffers-release");
    for (auto& b : backBuffers_) b.Reset();
    log::info("present", "sink-shutdown: sub-step swapchain-release");
    if (swapChain_ != nullptr) {
        swapChain_->AddRef();
        const ULONG rc = swapChain_->Release();
        log::info("present", std::format("sink-shutdown: swapchain pre-release refcount={} (2 = only our ComPtr + probe)", rc));
    }
    swapChain_.Reset();
    xess_.reset();
    log::info("present", "sink-shutdown: sub-step window-destroy-last");
    if (hwnd_ != nullptr && !desc_.targetWindow) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
        }
    }
    log::info("present", "sink-shutdown: sub-step factory-release");
    factory_.Reset();

    device_ = nullptr;
    log::info("present", "sink-shutdown: complete");
}

} // namespace veyra::gfx
