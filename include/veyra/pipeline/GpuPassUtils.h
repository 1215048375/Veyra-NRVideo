#pragma once

// GpuPassUtils - GPU helper facilities shared by the EnhanceGraph product
// library and the probes (R3.2). Moved verbatim from tools/player_probe
// main.cpp; the DescriptorStager's staging-heap SRV path encodes a proven
// driver workaround (RTX 5070 / 616.56: direct CreateShaderResourceView into
// a shader-visible heap corrupts flip-model Present; staged copies are safe).
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include "veyra/Log.h"

#ifndef VEYRA_SHADER_DIR
#error "VEYRA_SHADER_DIR must be defined by the build system"
#endif

namespace veyra::pipeline {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

inline ComPtr<ID3D12Resource> makeTexture(ID3D12Device* device, uint32_t w, uint32_t h,
                                          DXGI_FORMAT fmt, bool uav)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    td.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> r;
    const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) {
        veyra::log::error("gfx-util", std::format("texture alloc failed {}x{} hr=0x{:X}",
            w, h, static_cast<unsigned>(hr)));
        return nullptr;
    }
    return r;
}

inline ComPtr<ID3D12Resource> makeUploadBuffer(ID3D12Device* device, uint64_t size)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = size; bd.Height = 1; bd.DepthOrArraySize = 1;
    bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) {
        veyra::log::error("gfx-util", std::format("upload buffer alloc failed size={} hr=0x{:X}",
            static_cast<uint64_t>(size), static_cast<unsigned>(hr)));
        return nullptr;
    }
    return r;
}

// Tracks the current D3D12 state of graph-owned resources and emits only the
// transitions that are actually needed.
class StateTracker {
public:
    void set(ID3D12Resource* r, D3D12_RESOURCE_STATES s) { states_[r] = s; }
    D3D12_RESOURCE_STATES get(ID3D12Resource* r) const {
        const auto it = states_.find(r);
        return it != states_.end() ? it->second : D3D12_RESOURCE_STATE_COMMON;
    }
    void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                    D3D12_RESOURCE_STATES to) {
        const D3D12_RESOURCE_STATES from = get(r);
        if (from == to) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        set(r, to);
    }
    void uavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = r;
        list->ResourceBarrier(1, &b);
    }

private:
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states_;
};

// Compute pass helper (8 constants + SRV table + UAV table; single heap).
struct ComputePass {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT increment = 0;

    bool loadShader(const char* name, std::vector<uint8_t>& bytes) const
    {
        const std::string path = std::string(VEYRA_SHADER_DIR "/") + name;
        HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            veyra::log::error("gfx-util", std::format("shader missing: {}", path));
            return false;
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(f, &sz);
        bytes.resize(static_cast<size_t>(sz.QuadPart));
        DWORD read = 0;
        ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
        CloseHandle(f);
        return !bytes.empty();
    }

    bool create(ID3D12Device* device, const std::vector<uint8_t>& cs, UINT heapSlots,
                UINT srvCount = 3, UINT uavCount = 1)
    {
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = srvCount;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = uavCount;
        uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_ROOT_PARAMETER1 rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[0].Constants.ShaderRegister = 0;
        rp[0].Constants.Num32BitValues = 8;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[2].DescriptorTable.NumDescriptorRanges = 1;
        rp[2].DescriptorTable.pDescriptorRanges = &uavRange;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd{};
        rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rd.Desc_1_1.NumParameters = 3;
        rd.Desc_1_1.pParameters = rp;
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rd, &sig, &err))) return false;
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig.Get();
        pd.CS.pShaderBytecode = cs.data();
        pd.CS.BytecodeLength = cs.size();
        if (FAILED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = heapSlots;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    void bind(ID3D12GraphicsCommandList* list, const float constants[8],
              uint64_t srvGpu, uint64_t uavGpu) const
    {
        ID3D12DescriptorHeap* heaps[] = { heap.Get() };
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(rootSig.Get());
        list->SetPipelineState(pso.Get());
        list->SetComputeRoot32BitConstants(0, 8, constants, 0);
        const D3D12_GPU_DESCRIPTOR_HANDLE srv{ srvGpu };
        const D3D12_GPU_DESCRIPTOR_HANDLE uav{ uavGpu };
        list->SetComputeRootDescriptorTable(1, srv);
        list->SetComputeRootDescriptorTable(2, uav);
    }
};

inline bool loadShaderBytes(const char* name, std::vector<uint8_t>& bytes)
{
    const std::string path = std::string(VEYRA_SHADER_DIR "/") + name;
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        veyra::log::error("gfx-util", std::format("shader missing: {}", path));
        return false;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    bytes.resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(f);
    return !bytes.empty();
}

inline void makeSrv(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT fmt,
                    const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = fmt;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(resource, &srv, handle);
}

inline void makeUav(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT fmt,
                    const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = fmt;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
}

// Driver workaround (evidenced by bare-present stages 5-9, 2026-09-04):
// on this driver (RTX 5070, 616.56), CreateShaderResourceView writing
// directly into a SHADER-VISIBLE CBV_SRV_UAV heap corrupts the flip-model
// Present path (fabricated DXGI_ERROR_DEVICE_REMOVED, removedReason
// DXGI_ERROR_INVALID_CALL, no DRED). Creating the SRV in a NON-shader-
// visible staging heap and CopyDescriptorsSimple into the visible heap is
// proven safe (stage 9: 600/600 presents). UAVs/CBVs direct into visible
// heaps are safe (stages 7/8) and stay direct.
class DescriptorStager {
public:
    bool initialize(ID3D12Device* device, UINT slots)
    {
        device_ = device;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = slots;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)))) return false;
        increment_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        capacity_ = slots;
        return true;
    }

    // Creates an SRV for `resource` via the staging heap and copies it into
    // `targetSlot` of `visibleHeap`. srvDesc may be null for defaults.
    void stageSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc,
                  ID3D12DescriptorHeap* visibleHeap, UINT targetSlot)
    {
        const UINT stagingSlot = next_ % capacity_;
        next_ = (next_ + 1) % capacity_;
        const D3D12_CPU_DESCRIPTOR_HANDLE staging{
            heap_->GetCPUDescriptorHandleForHeapStart().ptr + stagingSlot * increment_ };
        device_->CreateShaderResourceView(resource, srvDesc, staging);
        const D3D12_CPU_DESCRIPTOR_HANDLE dst{
            visibleHeap->GetCPUDescriptorHandleForHeapStart().ptr + targetSlot * increment_ };
        device_->CopyDescriptorsSimple(1, dst, staging, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    UINT increment() const { return increment_; }

private:
    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> heap_;
    UINT increment_ = 0;
    UINT capacity_ = 0;
    UINT next_ = 0;
};

// Graphics present pass: fullscreen triangle blit onto a flip back buffer
// (flip buffers may only transition PRESENT <-> RENDER_TARGET).
struct GraphicsPass {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT increment = 0;

    bool create(ID3D12Device* device, const std::vector<uint8_t>& vs,
                const std::vector<uint8_t>& ps, UINT heapSlots)
    {
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER1 rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[0].Constants.ShaderRegister = 0;
        rp[0].Constants.Num32BitValues = 8;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srvRange;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd{};
        rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rd.Desc_1_1.NumParameters = 2;
        rd.Desc_1_1.pParameters = rp;
        rd.Desc_1_1.NumStaticSamplers = 1;
        rd.Desc_1_1.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rd, &sig, &err))) return false;
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig.Get();
        pd.VS.pShaderBytecode = vs.data();
        pd.VS.BytecodeLength = vs.size();
        pd.PS.pShaderBytecode = ps.data();
        pd.PS.BytecodeLength = ps.size();
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.SampleDesc.Count = 1;
        if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = heapSlots;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }
};

inline D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleOf(const ComputePass& p, UINT slot)
{
    return D3D12_CPU_DESCRIPTOR_HANDLE{ p.heap->GetCPUDescriptorHandleForHeapStart().ptr + slot * p.increment };
}

inline D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleOf(const ComputePass& p, UINT slot)
{
    return D3D12_GPU_DESCRIPTOR_HANDLE{ p.heap->GetGPUDescriptorHandleForHeapStart().ptr + slot * p.increment };
}

} // namespace veyra::pipeline
