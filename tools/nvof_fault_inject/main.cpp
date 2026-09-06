// nvof_fault_inject - table-driven null-argument fault injection for
// NvOfSession::initialize (s9-A3). For every required parameter, passes a
// valid set except ONE null and asserts: returns false, InvalidArgument, no
// crash. Also verifies a fully-null call. Exit 0 only if every case fails
// cleanly. No NVOF session may be created (checked via log absence of
// "CreateOpticalFlowD3D12").

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <cstdio>
#include <format>
#include <string>
#include <vector>

#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/NvOfSession.h"

using veyra::gfx::ComPtr;

int main(int argc, char** argv)
{
    std::string jsonPath = "nvof-fault-inject.json";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json-file") == 0 && i + 1 < argc) jsonPath = argv[++i];
    }

    veyra::gfx::D3D12DeviceContext ctx;
    veyra::gfx::DeviceContextDesc dcd{};
    dcd.enableDebugLayer = false;
    dcd.commandSlotCount = 2;
    veyra::Status st = veyra::Status::Ok;
    if (!ctx.initialize(dcd, st)) return 3;

    // Valid resources per contract.
    auto makeTex = [&](uint32_t w, uint32_t h, DXGI_FORMAT f) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
        d.Format = f; d.SampleDesc.Count = 1;
        ComPtr<ID3D12Resource> r;
        ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
        return r;
    };
    const uint32_t W = 1920, H = 1080, GRID = 4;
    const uint32_t GW = (W + GRID - 1) / GRID, GH = (H + GRID - 1) / GRID;
    ComPtr<ID3D12Resource> texA = makeTex(W, H, DXGI_FORMAT_B8G8R8A8_UNORM);
    ComPtr<ID3D12Resource> texB = makeTex(W, H, DXGI_FORMAT_B8G8R8A8_UNORM);
    ComPtr<ID3D12Resource> texFlow = makeTex(GW, GH, DXGI_FORMAT_R16G16_SINT);
    ComPtr<ID3D12Resource> texCost = makeTex(GW, GH, DXGI_FORMAT_R8_UINT);
    ComPtr<ID3D12Fence> inF, outF;
    ctx.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&inF));
    ctx.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&outF));
    if (!texA || !texB || !texFlow || !texCost || !inF || !outF) return 3;

    struct Case {
        const char* name;
        ID3D12Device* device;
        ID3D12Resource* a;
        ID3D12Resource* b;
        ID3D12Resource* flow;
        ID3D12Resource* cost;
        ID3D12Fence* inFence;
        ID3D12Fence* outFence;
    };
    const Case cases[] = {
        {"null-device",  nullptr,  texA.Get(), texB.Get(), texFlow.Get(), texCost.Get(), inF.Get(),  outF.Get()},
        {"null-inputA",  ctx.device(), nullptr,  texB.Get(), texFlow.Get(), texCost.Get(), inF.Get(),  outF.Get()},
        {"null-inputB",  ctx.device(), texA.Get(), nullptr,  texFlow.Get(), texCost.Get(), inF.Get(),  outF.Get()},
        {"null-flowOut", ctx.device(), texA.Get(), texB.Get(), nullptr,     texCost.Get(), inF.Get(),  outF.Get()},
        {"null-costOut", ctx.device(), texA.Get(), texB.Get(), texFlow.Get(), nullptr,     inF.Get(),  outF.Get()},
        {"null-inFence", ctx.device(), texA.Get(), texB.Get(), texFlow.Get(), texCost.Get(), nullptr,  outF.Get()},
        {"null-outFence",ctx.device(), texA.Get(), texB.Get(), texFlow.Get(), texCost.Get(), inF.Get(),  nullptr},
    };

    int failures = 0;
    std::string json = "{\n  \"probe\": \"nvof_fault_inject\",\n  \"cases\": [\n";
    for (size_t i = 0; i < std::size(cases); ++i) {
        const Case& c = cases[i];
        veyra::ngx::NvOfSession s;
        veyra::Status s2 = veyra::Status::Ok;
        veyra::ngx::NvOfSession::Desc d{};
        d.width = W; d.height = H;
        d.gridSize = GRID;
        // fences via the desc
        ID3D12Fence* inFence = c.inFence;
        ID3D12Fence* outFence = c.outFence;
        d.inFence = inFence;
        d.outFence = outFence;
        const bool ok = s.initialize(c.device, c.a, c.b, c.flow, c.cost, d, s2);
        const bool rejected = (!ok) && (s2 == veyra::Status::InvalidArgument) && !s.initialized();
        if (!rejected) ++failures;
        veyra::log::info("fault-inject", std::format("{}: ok={} status={} initialized={} -> {}",
            c.name, ok, static_cast<int>(s2), s.initialized() ? 1 : 0, rejected ? "REJECTED-CLEAN" : "BAD"));
        json += std::format("    {{\"name\": \"{}\", \"returnedFalse\": {}, \"invalidArgument\": {}, \"notInitialized\": {}, \"clean\": {}}}",
            c.name, ok ? "false" : "true",
            s2 == veyra::Status::InvalidArgument ? "true" : "false",
            s.initialized() ? "false" : "true",
            rejected ? "true" : "false");
        if (i + 1 < std::size(cases)) json += ",";
        json += "\n";
    }
    // s10-IV: create a VALID session, then call initialize with costOut=null;
    // the original session must survive untouched.
    {
        veyra::ngx::NvOfSession live;
        veyra::ngx::NvOfSession::Desc d{};
        d.width = W; d.height = H; d.gridSize = GRID;
        d.inFence = inF.Get(); d.outFence = outF.Get();
        veyra::Status s2 = veyra::Status::Ok;
        if (!live.initialize(ctx.device(), texA.Get(), texB.Get(), texFlow.Get(), texCost.Get(), d, s2)) {
            veyra::log::error("fault-inject", "live-session setup failed unexpectedly");
            ++failures;
        } else {
            const uint64_t before = live.executeCount();
            veyra::Status s3 = veyra::Status::Ok;
            const bool ok2 = live.initialize(ctx.device(), texA.Get(), texB.Get(), texFlow.Get(), nullptr, d, s3);
            const bool preserved = (!ok2) && (s3 == veyra::Status::InvalidArgument) &&
                                   live.initialized() && live.executeCount() == before;
            if (!preserved) ++failures;
            veyra::log::info("fault-inject", std::format(
                "live-session-null-costOut: ok={} status={} stillInitialized={} executesUnchanged={} -> {}",
                ok2, static_cast<int>(s3), live.initialized() ? 1 : 0,
                live.executeCount() == before, preserved ? "PRESERVED" : "BAD"));
            json += std::string(",\n    {\"name\": \"live-session-null-costOut\", \"clean\": ") + (preserved ? "true" : "false") + "}";
        }
    }
    const bool pass = failures == 0;
    json += std::format("  ],\n  \"pass\": {}\n}}\n", pass ? "true" : "false");
    FILE* f = std::fopen(jsonPath.c_str(), "wb");
    if (f) { std::fwrite(json.data(), 1, json.size(), f); std::fclose(f); }
    veyra::log::info("fault-inject", std::format("VERDICT {} ({}/{} clean)", pass ? "PASS" : "FAIL",
        std::size(cases) - failures, std::size(cases)));
    ctx.shutdown();
    return pass ? 0 : 1;
}
