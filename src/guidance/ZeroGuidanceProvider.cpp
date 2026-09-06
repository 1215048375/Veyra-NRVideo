#include "veyra/guidance/ZeroGuidanceProvider.h"

#include <cstring>
#include <format>

#include "veyra/Log.h"

namespace veyra::guidance {

using gfx::ComPtr;

ZeroGuidanceProvider::ZeroGuidanceProvider(gfx::D3D12DeviceContext& context)
    : context_(context)
{
}

ZeroGuidanceProvider::~ZeroGuidanceProvider()
{
    // ComPtr release order is declaration order within this class; the
    // textures do not depend on each other, only on the device, which
    // outlives the provider (owned by the graph's render-thread context).
    ready_ = false;
}

bool ZeroGuidanceProvider::createZeroedTexture(unsigned bytesPerPixel, DXGI_FORMAT format,
    ComPtr<ID3D12Resource>& out)
{
    ID3D12Device* device = context_.device();
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = width_;
    td.Height = height_;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_NONE; // SRV-consumed; no UAV needed
    ComPtr<ID3D12Resource> tex;
    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
    if (FAILED(hr)) {
        veyra::log::error("guidance-zero", std::format(
            "texture alloc failed {}x{} fmt=0x{:X} hr=0x{:X}",
            width_, height_, static_cast<unsigned>(format), static_cast<unsigned>(hr)));
        return false;
    }

    // Upload-copy zero init (Phase 1 lesson: ClearUnorderedAccessViewFloat
    // segfaulted on this machine; the copy path is the proven equivalent).
    const uint64_t rowBytes = static_cast<uint64_t>(width_) * bytesPerPixel;
    const uint64_t rowPitch = (rowBytes + 255) & ~255ull; // 256-aligned rows
    const uint64_t uploadSize = rowPitch * height_;
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    hr = upload_->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr) || mapped == nullptr) {
        veyra::log::error("guidance-zero", std::format("upload map failed hr=0x{:X}",
            static_cast<unsigned>(hr)));
        return false;
    }
    for (uint32_t y = 0; y < height_; ++y) {
        std::memset(mapped + y * rowPitch, 0, rowBytes);
    }
    upload_->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = format;
    src.PlacedFootprint.Footprint.Width = width_;
    src.PlacedFootprint.Footprint.Height = height_;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = tex.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    ID3D12CommandAllocator* allocator = nullptr;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&allocator));
    ComPtr<ID3D12CommandAllocator> allocatorGuard(allocator);
    if (FAILED(hr)) { return false; }
    ID3D12GraphicsCommandList* list = nullptr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
        IID_PPV_ARGS(&list));
    ComPtr<ID3D12GraphicsCommandList> listGuard(list);
    if (FAILED(hr)) { return false; }
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = tex.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
    list->Close();
    ID3D12CommandList* lists[] = {list};
    context_.directQueue()->ExecuteCommandLists(1, lists);

    // One-time initialization wait (Playbook: create/resize may wait once).
    const uint64_t waitValue = context_.fenceCompletedValue() + 1;
    context_.directQueue()->Signal(context_.fence(), waitValue);
    if (!context_.waitForFenceValue(waitValue)) {
        veyra::log::error("guidance-zero", "init fence wait failed");
        return false;
    }
    out = tex;
    return true;
}

bool ZeroGuidanceProvider::initialize(const GuidanceConfig& config)
{
    if (!context_.initialized()) {
        veyra::log::error("guidance-zero", "device context not initialized");
        return false;
    }
    if (config.workingWidth == 0 || config.workingHeight == 0) {
        veyra::log::error("guidance-zero", "working extent must be nonzero");
        return false;
    }
    config_ = config;
    width_ = config.workingWidth;
    height_ = config.workingHeight;

    // One upload buffer sized for the largest plane (confidence R8 = 1 B/px;
    // motion RG16F = 8 B/px row pitch dominates after alignment).
    const uint64_t rowBytes = static_cast<uint64_t>(width_) * 8;
    const uint64_t rowPitch = (rowBytes + 255) & ~255ull;
    D3D12_HEAP_PROPERTIES up{};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = rowPitch * height_;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = context_.device()->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_));
    if (FAILED(hr)) {
        veyra::log::error("guidance-zero", std::format("upload alloc failed hr=0x{:X}",
            static_cast<unsigned>(hr)));
        return false;
    }

    motion_.Reset(); depth_.Reset(); confidence_.Reset();
    if (!createZeroedTexture(8, DXGI_FORMAT_R16G16_FLOAT, motion_)) return false;
    if (!createZeroedTexture(4, DXGI_FORMAT_R32_FLOAT, depth_)) return false;
    if (!createZeroedTexture(1, DXGI_FORMAT_R8_UNORM, confidence_)) return false;

    ready_ = true;
    veyra::log::info("guidance-zero", std::format(
        "initialized {}x{} (RG16F motion / R32F depth / R8 confidence, all zero)",
        width_, height_));
    return true;
}

bool ZeroGuidanceProvider::produce(const pipeline::FrameWindow& window, pipeline::GuidanceFrame& out)
{
    if (!ready_) {
        veyra::log::error("guidance-zero", "produce before initialize");
        return false;
    }
    if (window.current == nullptr) {
        veyra::log::error("guidance-zero", "produce without current frame");
        return false;
    }

    out = pipeline::GuidanceFrame{};
    out.motion.resource = motion_.Get();
    out.motion.expectedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    out.motion.ownerSlot = 0;
    out.motion.readyFenceValue = 0; // static content, ready immediately
    out.motion.width = width_;
    out.motion.height = height_;
    out.motion.format = DXGI_FORMAT_R16G16_FLOAT;

    out.depth.resource = depth_.Get();
    out.depth.expectedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    out.depth.width = width_;
    out.depth.height = height_;
    out.depth.format = DXGI_FORMAT_R32_FLOAT;

    out.confidence.resource = confidence_.Get();
    out.confidence.expectedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    out.confidence.width = width_;
    out.confidence.height = height_;
    out.confidence.format = DXGI_FORMAT_R8_UNORM;

    out.provenance = pipeline::GuidanceProvenance::Zero;
    out.sourceSequence = window.current->sequence;
    out.sourceEpoch = window.current->sourceEpoch;
    out.depthAgeFrames = 0;
    // Zero motion after an epoch change is not a fallback event - it is the
    // only legal content for this provider. The graph fills resetReason from
    // the ResetCoordinator; the provider only reports the boundary flag.
    out.requiresReset = window.current->sourceEpoch != epoch_;
    out.resetReason = pipeline::ResetReason::None;
    epoch_ = window.current->sourceEpoch;
    ++producedFrames_;
    return true;
}

void ZeroGuidanceProvider::reset(uint64_t epoch, pipeline::ResetReason reason) noexcept
{
    // Textures are static zeros; resetting only drops the provider's epoch
    // association so the next produce() reports requiresReset truthfully.
    epoch_ = epoch ? epoch : 1;
    (void)reason;
}

} // namespace veyra::guidance
