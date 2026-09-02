#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "veyra/Result.h"

namespace veyra::gfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Rotating command-slot ring (Playbook section 6.2). Each slot owns its
// command allocator, recycled command list, fence value and two timestamp
// query indices. A frame only ever waits on the fence of the slot it is about
// to reuse, never on the frame it just submitted.
class CommandSlotRing {
public:
    CommandSlotRing() = default;
    ~CommandSlotRing();

    CommandSlotRing(const CommandSlotRing&) = delete;
    CommandSlotRing& operator=(const CommandSlotRing&) = delete;

    // `fenceEvent` is shared with the owning device context; waits use
    // SetEventOnCompletion + WaitForSingleObject on that event.
    bool initialize(ID3D12Device* device,
                    ID3D12CommandQueue* queue,
                    ID3D12Fence* fence,
                    HANDLE fenceEvent,
                    uint32_t slotCount,
                    Status& status);
    void shutdown();

    uint32_t slotCount() const { return slotCount_; }
    bool initialized() const { return initialized_; }

    // Waits for this slot's outstanding fence value (if any), then resets the
    // allocator and command list. Returns the recycled list ready for record.
    ID3D12GraphicsCommandList* acquire(uint32_t slot, Status& status);

    // Closes, submits and signals the slot; advances the fence timeline.
    bool submitAndSignal(uint32_t slot);

    // Blocks until every slot's work completed.
    bool waitIdle();

    // Phase 0 skeleton proof: for each slot, acquire (wait+reset), record a
    // real begin/end timestamp query pair, submit, signal, then wait idle.
    bool exerciseAll();

    // Two timestamp query indices per slot (begin at 2*slot, end at 2*slot+1).
    uint32_t timestampQueryIndex(uint32_t slot, bool end) const;

private:
    struct Slot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        uint64_t fenceValue = 0; // 0 means "never submitted"
    };

    bool initialized_ = false;
    uint32_t slotCount_ = 0;
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE fenceEvent_ = nullptr;
    uint64_t nextFenceValue_ = 1;
    ComPtr<ID3D12QueryHeap> timestampHeap_;
    std::vector<Slot> slots_;
};

} // namespace veyra::gfx
