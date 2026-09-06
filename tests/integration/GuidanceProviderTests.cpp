// Guidance provider integration tests - real D3D12 device (RTX), diagnostic
// readback allowed in tests only (the normal path never reads back).
// Verifies ZeroGuidanceProvider: textures exist at the working extent, GPU
// contents are all zero, metadata (provenance/sequence/epoch/reset flag) is
// honest, and the reset boundary flag behaves.
#include <cstdio>
#include <cstring>
#include <vector>

#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/guidance/ZeroGuidanceProvider.h"

using namespace veyra;
using namespace veyra::guidance;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char* name, bool ok) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  [FAIL] %s\n", name); }
    else { std::printf("  [PASS] %s\n", name); }
}

// Diagnostic readback of a texture's first row region through a staging
// texture (test-only path).
bool readbackFirstPixels(gfx::D3D12DeviceContext& ctx, ID3D12Resource* tex,
    DXGI_FORMAT format, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
    const unsigned bpp = (format == DXGI_FORMAT_R16G16_FLOAT) ? 8
        : (format == DXGI_FORMAT_R32_FLOAT) ? 4 : 1;
    const uint64_t rowBytes = static_cast<uint64_t>(w) * bpp;
    const uint64_t rowPitch = (rowBytes + 255) & ~255ull;

    D3D12_HEAP_PROPERTIES rp{};
    rp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = rowPitch * h;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    gfx::ComPtr<ID3D12Resource> staging;
    if (FAILED(ctx.device()->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)))) {
        return false;
    }
    gfx::ComPtr<ID3D12CommandAllocator> allocator;
    if (FAILED(ctx.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)))) { return false; }
    gfx::ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(ctx.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)))) { return false; }

    D3D12_RESOURCE_BARRIER toSrc{};
    toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrc.Transition.pResource = tex;
    toSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &toSrc);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = tex;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = staging.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = format;
    dst.PlacedFootprint.Footprint.Width = w;
    dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
    // Copy only the first rows via a box: the staging buffer is sized for
    // this region, not the whole texture.
    D3D12_BOX box{0, 0, 0, w, h, 1};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    D3D12_RESOURCE_BARRIER back{};
    back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    back.Transition.pResource = tex;
    back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    back.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &back);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    ctx.directQueue()->ExecuteCommandLists(1, lists);
    const uint64_t v = ctx.fenceCompletedValue() + 1;
    ctx.directQueue()->Signal(ctx.fence(), v);
    if (!ctx.waitForFenceValue(v)) { return false; }

    out.resize(rowBytes);
    D3D12_RANGE wr{0, rowBytes};
    uint8_t* mapped = nullptr;
    if (FAILED(staging->Map(0, &wr, reinterpret_cast<void**>(&mapped)))) { return false; }
    std::memcpy(out.data(), mapped, rowBytes);
    staging->Unmap(0, nullptr);
    return true;
}

} // namespace

int main() {
    std::printf("guidance-provider: device bootstrap\n");
    gfx::D3D12DeviceContext ctx;
    gfx::DeviceContextDesc desc;
    desc.enableDebugLayer = false;
    Status status;
    if (!ctx.initialize(desc, status)) {
        std::printf("guidance-provider: device init FAILED (no GPU run)\n");
        return 1;
    }

    std::printf("guidance-provider: zero provider initialize\n");
    GuidanceConfig cfg;
    cfg.workingWidth = 1920;
    cfg.workingHeight = 1080;
    ZeroGuidanceProvider zero(ctx);
    check("zero:initialize", zero.initialize(cfg));
    check("zero:extent", zero.workingWidth() == 1920 && zero.workingHeight() == 1080);

    std::printf("guidance-provider: produce + metadata\n");
    pipeline::FramePacket cur;
    cur.sequence = 5;
    cur.pts = pipeline::Rational{5005, 60000};
    cur.sourceKind = pipeline::SourceKind::TestPattern;
    cur.sourceEpoch = 3;
    cur.color.resource = reinterpret_cast<ID3D12Resource*>(0x1);
    cur.color.width = 1920; cur.color.height = 1080;
    cur.color.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipeline::FrameWindow window;
    window.current = &cur;

    pipeline::GuidanceFrame g;
    check("zero:produce", zero.produce(window, g));
    check("zero:provenance-honest", g.provenance == pipeline::GuidanceProvenance::Zero);
    check("zero:metadata", g.sourceSequence == 5 && g.sourceEpoch == 3);
    check("zero:extents-match", g.extentsMatch(1920, 1080));
    check("zero:valid", g.valid());

    std::printf("guidance-provider: reset boundary flag\n");
    pipeline::FramePacket cur2 = cur;
    cur2.sequence = 6;
    cur2.sourceEpoch = 4; // epoch advanced between frames
    pipeline::FrameWindow window2;
    window2.current = &cur2;
    pipeline::GuidanceFrame g2;
    check("zero:produce-after-epoch-change", zero.produce(window2, g2));
    check("zero:requires-reset-flag", g2.requiresReset);
    pipeline::GuidanceFrame g3;
    check("zero:produce-same-epoch-no-reset", zero.produce(window2, g3) && !g3.requiresReset);

    std::printf("guidance-provider: gpu zero verification (diagnostic readback)\n");
    std::vector<uint8_t> bytes;
    bool allZero = true;
    if (readbackFirstPixels(ctx, g.motion.resource, DXGI_FORMAT_R16G16_FLOAT, 1920, 8, bytes)) {
        for (uint8_t b : bytes) { if (b != 0) { allZero = false; break; } }
        check("zero:motion-gpu-all-zero", allZero);
    } else {
        check("zero:motion-gpu-all-zero", false);
    }
    allZero = true;
    if (readbackFirstPixels(ctx, g.depth.resource, DXGI_FORMAT_R32_FLOAT, 1920, 8, bytes)) {
        for (uint8_t b : bytes) { if (b != 0) { allZero = false; break; } }
        check("zero:depth-gpu-all-zero", allZero);
    } else {
        check("zero:depth-gpu-all-zero", false);
    }
    allZero = true;
    if (readbackFirstPixels(ctx, g.confidence.resource, DXGI_FORMAT_R8_UNORM, 1920, 8, bytes)) {
        for (uint8_t b : bytes) { if (b != 0) { allZero = false; break; } }
        check("zero:confidence-gpu-all-zero", allZero);
    } else {
        check("zero:confidence-gpu-all-zero", false);
    }

    ctx.shutdown();
    std::printf("guidance-provider: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
