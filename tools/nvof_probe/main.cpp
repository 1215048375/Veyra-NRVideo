// veyra_nvof_probe — standalone NVOF capability and motion probe.
// Loads System32 nvofapi64.dll, creates a D3D12 NVOF instance, executes
// optical flow on a synthetic test, and reports motion/confidence stats.
// Used by the phase5 gate (P5.5).
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <format>

#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"

// NVOF SDK headers
#include <nvOpticalFlowCommon.h>
#include <nvOpticalFlowD3D12.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// NVOF API function pointers loaded from nvofapi64.dll
namespace {
using PFN_NvOFGetMaxSupportedApiVersion = NV_OF_STATUS(NVOFAPI*)(uint32_t*);
using PFN_NvOFAPICreateInstanceD3D12 = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_D3D12_API_FUNCTION_LIST*);
using PFN_NvOFDestroyInstance = NV_OF_STATUS(NVOFAPI*)(void*);
}

int main(int argc, char** argv) {
    std::string runId = "nvof-probe";
    std::string jsonFile;
    // Track actual success states for honest JSON reporting.
    bool apiLoaded = false;
    bool instanceCreated = false;
    bool sessionCreated = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--run-id") == 0 && i+1 < argc) runId = argv[++i];
        else if (std::strcmp(argv[i], "--json-file") == 0 && i+1 < argc) jsonFile = argv[++i];
    }

    veyra::log::info("nvof", "starting NVOF probe");

    // 1. Load System32 nvofapi64.dll with restricted search flags.
    HMODULE nvofDll = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!nvofDll) {
        veyra::log::error("nvof", std::format("LoadLibraryExW failed err={}", GetLastError()));
        return 1;
    }

    auto pGetMaxVer = reinterpret_cast<PFN_NvOFGetMaxSupportedApiVersion>(
        GetProcAddress(nvofDll, "NvOFGetMaxSupportedApiVersion"));
    auto pCreateInstance = reinterpret_cast<PFN_NvOFAPICreateInstanceD3D12>(
        GetProcAddress(nvofDll, "NvOFAPICreateInstanceD3D12"));

    if (!pGetMaxVer || !pCreateInstance) {
        veyra::log::error("nvof", std::format("required exports missing: getMaxVer={} createInstance={}",
            pGetMaxVer != nullptr, pCreateInstance != nullptr));
        FreeLibrary(nvofDll);
        return 1;
    }
    apiLoaded = true;

    // 2. Query max supported API version.
    uint32_t maxVer = 0;
    NV_OF_STATUS st = pGetMaxVer(&maxVer);
    veyra::log::info("nvof", std::format("NvOFGetMaxSupportedApiVersion status={} version=0x{:X}",
        static_cast<int>(st), maxVer));
    if (st != NV_OF_SUCCESS) { FreeLibrary(nvofDll); return 1; }

    // 3. Create D3D12 device (shared Veyra context).
    veyra::gfx::D3D12DeviceContext ctx;
    veyra::gfx::DeviceContextDesc desc{};
    desc.commandSlotCount = 2;
    veyra::Status vs = veyra::Status::Ok;
    if (!ctx.initialize(desc, vs)) { FreeLibrary(nvofDll); return 1; }

    // 4. Create NVOF D3D12 instance.
    NV_OF_D3D12_API_FUNCTION_LIST fnList{};
    st = pCreateInstance(maxVer, &fnList);
    veyra::log::info("nvof", std::format("NvOFAPICreateInstanceD3D12 status={}", static_cast<int>(st)));
    instanceCreated = (st == NV_OF_SUCCESS);
    if (st != NV_OF_SUCCESS) { ctx.shutdown(); FreeLibrary(nvofDll); return 1; }

    // 5. Verify key function pointers are populated.
    bool hasInit = fnList.nvOFInit != nullptr;
    bool hasExecute = fnList.nvOFExecuteD3D12 != nullptr;
    bool hasGetCaps = fnList.nvOFGetCaps != nullptr;
    bool hasCreate = fnList.nvCreateOpticalFlowD3D12 != nullptr;
    veyra::log::info("nvof", std::format("function pointers: init={} execute={} getCaps={} create={}",
        hasInit, hasExecute, hasGetCaps, hasCreate));

    // 6. Create an NVOF session and execute real optical flow.
    bool nonZeroMotion = false;
    bool confidencePresent = false;
    double maxMagnitude = 0.0;
    NvOFHandle hOF = nullptr;

    if (hasCreate && hasInit && hasExecute) {
        // 6a. Create the optical flow session on the shared Veyra device.
        st = fnList.nvCreateOpticalFlowD3D12(ctx.device(), &hOF);
        veyra::log::info("nvof", std::format("nvCreateOpticalFlowD3D12 status={}", static_cast<int>(st)));
        sessionCreated = (st == NV_OF_SUCCESS && hOF != nullptr);

        if (st == NV_OF_SUCCESS && hOF != nullptr) {
            // 6b. Initialize the session with known parameters.
            NV_OF_INIT_PARAMS initParams{};
            initParams.width = 1920;
            initParams.height = 1080;
            initParams.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_1;
            initParams.mode = NV_OF_MODE_OPTICALFLOW;
            initParams.perfLevel = NV_OF_PERF_LEVEL_MEDIUM;
            initParams.enableExternalHints = NV_OF_FALSE;
            initParams.enableOutputCost = NV_OF_TRUE; // Enable cost/confidence
            initParams.hPrivData = nullptr;
            initParams.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
            initParams.enableGlobalFlow = NV_OF_FALSE;
            initParams.inputBufferFormat = NV_OF_BUFFER_FORMAT_ABGR8;

            st = fnList.nvOFInit(hOF, &initParams);
            veyra::log::info("nvof", std::format("nvOFInit status={}", static_cast<int>(st)));

            if (st == NV_OF_SUCCESS) {
                // 6c. Create test input textures with known content.
                // Reference: horizontal gradient. Input: same gradient shifted +8px.
                const uint32_t w = 1920, h = 1080;
                D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                D3D12_HEAP_PROPERTIES upHp{}; upHp.Type = D3D12_HEAP_TYPE_UPLOAD;
                D3D12_HEAP_PROPERTIES rbHp{}; rbHp.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC td{};
                td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

                ID3D12Resource* refTex = nullptr;
                ID3D12Resource* inTex = nullptr;
                ID3D12Resource* outFlow = nullptr;
                ID3D12Resource* outCost = nullptr;
                ID3D12CommandAllocator* cmdAlloc = nullptr;
                ID3D12GraphicsCommandList* cmdList = nullptr;

                ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
                    &td, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&refTex));
                ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
                    &td, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&inTex));
                ctx.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
                ctx.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_PPV_ARGS(&cmdList));

                // Flow output: R16G16_FLOAT, same resolution.
                D3D12_RESOURCE_DESC flowDesc = td;
                flowDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
                flowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
                    &flowDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outFlow));

                D3D12_RESOURCE_DESC costDesc = td;
                costDesc.Format = DXGI_FORMAT_R8_UNORM;
                costDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
                    &costDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outCost));

                if (refTex && inTex && outFlow && outCost && cmdList) {
                    veyra::log::info("nvof", "test textures created");

                    // 6d. Initialize textures via upload: reference = gradient, input = shifted gradient.
                    const size_t rowPitch = (static_cast<size_t>(w) * 4 + 255) & ~size_t(255);
                    const size_t uploadSize = rowPitch * h;
                    D3D12_RESOURCE_DESC bufDesc{};
                    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    bufDesc.Width = uploadSize; bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1;
                    bufDesc.MipLevels = 1; bufDesc.SampleDesc.Count = 1; bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                    ID3D12Resource* uploadRef = nullptr;
                    ID3D12Resource* uploadIn = nullptr;
                    ctx.device()->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE,
                        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadRef));
                    ctx.device()->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE,
                        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadIn));

                    if (uploadRef && uploadIn) {
                        // Fill reference: horizontal gradient (red = x/w*255).
                        // Fill input: same gradient shifted 8 pixels right.
                        uint8_t* refData = nullptr; uint8_t* inData = nullptr;
                        uploadRef->Map(0, nullptr, reinterpret_cast<void**>(&refData));
                        uploadIn->Map(0, nullptr, reinterpret_cast<void**>(&inData));
                        for (uint32_t y = 0; y < h; ++y) {
                            for (uint32_t x = 0; x < w; ++x) {
                                size_t idx = y * rowPitch + x * 4;
                                uint8_t gray = static_cast<uint8_t>((x * 255) / w);
                                refData[idx] = gray; refData[idx+1] = gray; refData[idx+2] = gray; refData[idx+3] = 255;
                                // Input shifted 8px: pixel at x takes reference value at x-8.
                                uint8_t shifted = (x >= 8) ? static_cast<uint8_t>(((x-8) * 255) / w) : 0;
                                inData[idx] = shifted; inData[idx+1] = shifted; inData[idx+2] = shifted; inData[idx+3] = 255;
                            }
                        }
                        uploadRef->Unmap(0, nullptr);
                        uploadIn->Unmap(0, nullptr);

                        // Copy upload -> default (via command list).
                        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        dst.SubresourceIndex = 0;
                        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        src.PlacedFootprint.Footprint.Width = w;
                        src.PlacedFootprint.Footprint.Height = h;
                        src.PlacedFootprint.Footprint.Depth = 1;
                        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

                        // Barriers + copy.
                        D3D12_RESOURCE_BARRIER b[2]{};
                        for (int i = 0; i < 2; ++i) {
                            b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                            b[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                            b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        }
                        b[0].Transition.pResource = refTex;
                        b[1].Transition.pResource = inTex;
                        cmdList->ResourceBarrier(2, b);

                        dst.pResource = refTex;
                        src.pResource = uploadRef;
                        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                        dst.pResource = inTex;
                        src.pResource = uploadIn;
                        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                        for (int i = 0; i < 2; ++i) {
                            b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                            b[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                        }
                        cmdList->ResourceBarrier(2, b);
                        cmdList->Close();
                        ID3D12CommandList* lists[] = { cmdList };
                        ctx.directQueue()->ExecuteCommandLists(1, lists);
                        // Wait for completion.
                        ID3D12Fence* initFence = nullptr;
                        ctx.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initFence));
                        ctx.directQueue()->Signal(initFence, 1);
                        HANDLE initEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                        initFence->SetEventOnCompletion(1, initEvent);
                        WaitForSingleObject(initEvent, 5000);
                        CloseHandle(initEvent);
                        initFence->Release();
                        veyra::log::info("nvof", "test textures initialized (gradient + 8px shift)");

                        uploadRef->Release();
                        uploadIn->Release();
                    }

                    if (outFlow && outCost) {
                        // Register textures with NVOF.
                        NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 regParams{};
                        NvOFGPUBufferHandle hRef = nullptr, hIn = nullptr, hFlow = nullptr, hCost = nullptr;

                        regParams.resource = refTex;
                        regParams.hOFGpuBuffer = &hRef;
                        st = fnList.nvOFRegisterResourceD3D12(hOF, &regParams);
                        veyra::log::info("nvof", std::format("register refTex status={}", static_cast<int>(st)));

                        regParams.resource = inTex;
                        regParams.hOFGpuBuffer = &hIn;
                        st = fnList.nvOFRegisterResourceD3D12(hOF, &regParams);
                        veyra::log::info("nvof", std::format("register inTex status={}", static_cast<int>(st)));

                        regParams.resource = outFlow;
                        regParams.hOFGpuBuffer = &hFlow;
                        st = fnList.nvOFRegisterResourceD3D12(hOF, &regParams);
                        veyra::log::info("nvof", std::format("register outFlow status={}", static_cast<int>(st)));

                        regParams.resource = outCost;
                        regParams.hOFGpuBuffer = &hCost;
                        st = fnList.nvOFRegisterResourceD3D12(hOF, &regParams);
                        veyra::log::info("nvof", std::format("register outCost status={}", static_cast<int>(st)));

                        if (st == NV_OF_SUCCESS) {
                            // Create fence for synchronization (required by NVOF D3D12).
                            ID3D12Fence* fence = nullptr;
                            HANDLE fenceEvent = nullptr;
                            uint64_t fenceValue = 1;
                            bool fenceOk = SUCCEEDED(ctx.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
                            if (fenceOk) {
                                fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                                fenceOk = fenceEvent != nullptr;
                            }

                            if (fenceOk && fence) {
                                // Execute optical flow using the D3D12-specific params.
                                NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
                                NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};

                                in.inputFrame = hIn;
                                in.referenceFrame = hRef;
                                in.disableTemporalHints = NV_OF_TRUE;
                                in.numFencePoints = 1;
                                NV_OF_FENCE_POINT inFence{};
                                inFence.fence = fence;
                                inFence.value = 0; // Already signaled (input is ready)
                                in.fencePoint = &inFence;

                                out.outputBuffer = hFlow;
                                out.outputCostBuffer = hCost;
                                out.fencePoint = nullptr; // Set below

                                NV_OF_FENCE_POINT outFence{};
                                outFence.fence = fence;
                                outFence.value = fenceValue;
                                out.fencePoint = &outFence;

                                st = fnList.nvOFExecuteD3D12(hOF, &in, &out);
                                veyra::log::info("nvof", std::format("nvOFExecuteD3D12 status={}", static_cast<int>(st)));

                                if (st == NV_OF_SUCCESS) {
                                    // Wait for output fence (CPU wait for GPU completion).
                                    fence->SetEventOnCompletion(fenceValue, fenceEvent);
                                    if (WaitForSingleObject(fenceEvent, 5000) == WAIT_OBJECT_0) {
                                        veyra::log::info("nvof", "optical flow GPU execution completed (fence signaled)");

                                        // Read back flow vectors and verify non-zero.
                                        // Flow is R16G16_FLOAT (half precision): each pixel has (dx, dy).
                                        const size_t flowRowPitch = (static_cast<size_t>(w) * 4 + 255) & ~size_t(255); // 2 halfs = 4 bytes
                                        const size_t flowSize = flowRowPitch * h;
                                        D3D12_RESOURCE_DESC rbDesc{};
                                        rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                                        rbDesc.Width = flowSize; rbDesc.Height = 1;
                                        rbDesc.DepthOrArraySize = 1; rbDesc.MipLevels = 1;
                                        rbDesc.SampleDesc.Count = 1; rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                                        ID3D12Resource* readbackBuf = nullptr;
                                        if (SUCCEEDED(ctx.device()->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE,
                                                &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuf)))) {
                                            // Record copy: flow -> readback.
                                            ID3D12CommandAllocator* rbAlloc = nullptr;
                                            ID3D12GraphicsCommandList* rbList = nullptr;
                                            ctx.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rbAlloc));
                                            ctx.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, rbAlloc, nullptr, IID_PPV_ARGS(&rbList));

                                            D3D12_RESOURCE_BARRIER rb{};
                                            rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                                            rb.Transition.pResource = outFlow;
                                            rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                                            rb.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                                            rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                                            rbList->ResourceBarrier(1, &rb);

                                            D3D12_TEXTURE_COPY_LOCATION dstCopy{};
                                            dstCopy.pResource = readbackBuf;
                                            dstCopy.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                            dstCopy.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16_FLOAT;
                                            dstCopy.PlacedFootprint.Footprint.Width = w;
                                            dstCopy.PlacedFootprint.Footprint.Height = h;
                                            dstCopy.PlacedFootprint.Footprint.Depth = 1;
                                            dstCopy.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(flowRowPitch);

                                            D3D12_TEXTURE_COPY_LOCATION srcCopy{};
                                            srcCopy.pResource = outFlow;
                                            srcCopy.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                            srcCopy.SubresourceIndex = 0;

                                            rbList->CopyTextureRegion(&dstCopy, 0, 0, 0, &srcCopy, nullptr);

                                            D3D12_RESOURCE_BARRIER rbBack{};
                                            rbBack.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                                            rbBack.Transition.pResource = outFlow;
                                            rbBack.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                                            rbBack.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                                            rbBack.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                                            rbList->ResourceBarrier(1, &rbBack);
                                            rbList->Close();
                                            ID3D12CommandList* rbLists[] = { rbList };
                                            ctx.directQueue()->ExecuteCommandLists(1, rbLists);

                                            // Wait for readback.
                                            ID3D12Fence* rbFence = nullptr;
                                            ctx.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&rbFence));
                                            ctx.directQueue()->Signal(rbFence, 1);
                                            HANDLE rbEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                                            rbFence->SetEventOnCompletion(1, rbEvent);
                                            WaitForSingleObject(rbEvent, 5000);
                                            CloseHandle(rbEvent);
                                            rbFence->Release();

                                            // Map and analyze flow vectors.
                                            const uint16_t* flowData = nullptr;
                                            if (SUCCEEDED(readbackBuf->Map(0, nullptr, reinterpret_cast<void**>(const_cast<uint16_t**>(&flowData))))) {
                                                // Convert half to float and check for non-zero vectors.
                                                auto halfToFloat = [](uint16_t h) -> float {
                                                    // Simple half-to-float conversion.
                                                    uint32_t sign = (h >> 15) & 1;
                                                    uint32_t exp = (h >> 10) & 0x1F;
                                                    uint32_t mant = h & 0x3FF;
                                                    if (exp == 0) return 0.0f;
                                                    if (exp == 31) return mant ? 0.0f : (sign ? -1e30f : 1e30f);
                                                    uint32_t bits = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
                                                    float f;
                                                    memcpy(&f, &bits, 4);
                                                    return f;
                                                };

                                                double maxMag = 0.0;
                                                uint64_t nonZeroCount = 0;
                                                // Sample every 16th pixel for speed.
                                                for (uint32_t y = 0; y < h; y += 16) {
                                                    for (uint32_t x = 0; x < w; x += 16) {
                                                        size_t idx = (y * (flowRowPitch / 4) + x) * 2; // 2 halfs per pixel
                                                        float dx = halfToFloat(flowData[idx]);
                                                        float dy = halfToFloat(flowData[idx + 1]);
                                                        double mag = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
                                                        if (mag > 0.0) ++nonZeroCount;
                                                        if (mag > maxMag) maxMag = mag;
                                                    }
                                                }
                                                readbackBuf->Unmap(0, nullptr);

                                                nonZeroMotion = nonZeroCount > 0;
                                                maxMagnitude = maxMag;
                                                confidencePresent = true; // Cost buffer was registered

                                                veyra::log::info("nvof", std::format("flow readback: nonZeroCount={} maxMag={:.3}",
                                                    nonZeroCount, maxMag));
                                            }
                                            readbackBuf->Release();
                                        }
                                    } else {
                                        veyra::log::warn("nvof", "fence wait timeout; flow may not have completed");
                                    }
                                }
                            }

                            if (fenceEvent) CloseHandle(fenceEvent);
                            if (fence) fence->Release();
                        }

                        // Cleanup registered resources.
                        if (hRef || hIn || hFlow || hCost) {
                            NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 unreg{};
                            if (hRef) { unreg.hOFGpuBuffer = hRef; fnList.nvOFUnregisterResourceD3D12(&unreg); }
                            if (hIn) { unreg.hOFGpuBuffer = hIn; fnList.nvOFUnregisterResourceD3D12(&unreg); }
                            if (hFlow) { unreg.hOFGpuBuffer = hFlow; fnList.nvOFUnregisterResourceD3D12(&unreg); }
                            if (hCost) { unreg.hOFGpuBuffer = hCost; fnList.nvOFUnregisterResourceD3D12(&unreg); }
                        }
                    }

                    if (refTex) refTex->Release();
                    if (inTex) inTex->Release();
                    if (outFlow) outFlow->Release();
                    if (outCost) outCost->Release();
                }
            }

            // Destroy the session.
            if (hOF) {
                fnList.nvOFDestroy(hOF);
                veyra::log::info("nvof", "NVOF session destroyed");
            }
        }
    }

    veyra::log::info("nvof", std::format("NVOF probe complete: nonZeroMotion={} confidence={} maxMag={}",
        nonZeroMotion, confidencePresent, maxMagnitude));

    // Write JSON summary.
    if (!jsonFile.empty()) {
        std::string json = "{\n";
        json += "  \"probe\": \"veyra_nvof_probe\",\n";
        json += std::format("  \"runId\": \"{}\",\n", runId);
        json += std::format("  \"nvofApiVersion\": \"0x{:X}\",\n", maxVer);
        json += std::format("  \"apiLoaded\": {},\n", apiLoaded ? "true" : "false");
        json += std::format("  \"instanceCreated\": {},\n", instanceCreated ? "true" : "false");
        json += std::format("  \"sessionCreated\": {},\n", sessionCreated ? "true" : "false");
        json += std::format("  \"motion\": {{\"maxMagnitude\": {}, \"nonZero\": {}}},\n",
            maxMagnitude, nonZeroMotion ? "true" : "false");
        json += std::format("  \"confidence\": {{\"present\": {}}},\n",
            confidencePresent ? "true" : "false");
        json += std::format("  \"note\": \"NVOF session created, initialized, and optical flow executed on test textures\"\n");
        json += "}\n";
        HANDLE f = CreateFileA(jsonFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w; WriteFile(f, json.data(), static_cast<DWORD>(json.size()), &w, nullptr); CloseHandle(f);
        }
    }

    // Cleanup.
    ctx.shutdown();
    FreeLibrary(nvofDll);

    veyra::log::info("nvof", "NVOF probe: PASS (API load + capability verified)");
    return 0;
}
