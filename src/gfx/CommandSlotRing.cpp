#include "veyra/gfx/CommandSlotRing.h"

#include <format>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"

namespace veyra::gfx {

CommandSlotRing::~CommandSlotRing()
{
    shutdown();
}

bool CommandSlotRing::initialize(ID3D12Device* device,
                                 ID3D12CommandQueue* queue,
                                 ID3D12Fence* fence,
                                 HANDLE fenceEvent,
                                 uint32_t slotCount,
                                 Status& status)
{
    if (initialized_) {
        shutdown();
    }
    device_ = device;
    queue_ = queue;
    fence_ = fence;
    fenceEvent_ = fenceEvent;
    slotCount_ = slotCount;

    if (device_ == nullptr || queue_ == nullptr || fence_ == nullptr || fenceEvent_ == nullptr || slotCount_ == 0) {
        status = Status::InvalidArgument;
        veyra::log::error("gfx", "slot-ring: invalid initialize arguments");
        return false;
    }

    D3D12_QUERY_HEAP_DESC timestampHeapDesc{};
    timestampHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    timestampHeapDesc.Count = slotCount_ * 2; // begin+end pair per slot
    timestampHeapDesc.NodeMask = 0;
    const HRESULT heapResult = device_->CreateQueryHeap(&timestampHeapDesc, IID_PPV_ARGS(&timestampHeap_));
    if (FAILED(heapResult)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("slot-ring: CreateQueryHeap failed hr={}", veyra::hresultString(heapResult)));
        return false;
    }
    UINT64 frequency = 0;
    const HRESULT freqResult = queue_->GetTimestampFrequency(&frequency);
    veyra::log::info("gfx", std::format("slot-ring: timestamp heap created slots={} pairs={} frequencyHz={} freqQuery={}",
        slotCount_, timestampHeapDesc.Count, frequency, veyra::hresultString(freqResult)));

    slots_.resize(slotCount_);
    for (uint32_t i = 0; i < slotCount_; ++i) {
        HRESULT result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slots_[i].allocator));
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("slot-ring: CreateCommandAllocator slot={} failed hr={}", i, veyra::hresultString(result)));
            return false;
        }
        result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slots_[i].allocator.Get(), nullptr, IID_PPV_ARGS(&slots_[i].list));
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("slot-ring: CreateCommandList slot={} failed hr={}", i, veyra::hresultString(result)));
            return false;
        }
        // Command lists are created in the recording state; close them so
        // acquire() can reset them deterministically.
        result = slots_[i].list->Close();
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("slot-ring: initial Close slot={} failed hr={}", i, veyra::hresultString(result)));
            return false;
        }
        slots_[i].fenceValue = 0;
    }

    initialized_ = true;
    veyra::log::info("gfx", std::format("slot-ring: initialized slots={}", slotCount_));
    return true;
}

void CommandSlotRing::shutdown()
{
    if (!initialized_) {
        return;
    }
    (void)waitIdle();
    slots_.clear();
    timestampHeap_.Reset();
    initialized_ = false;
    veyra::log::info("gfx", "slot-ring: shutdown complete");
}

ID3D12GraphicsCommandList* CommandSlotRing::acquire(uint32_t slot, Status& status)
{
    if (!initialized_ || slot >= slotCount_) {
        status = Status::InvalidArgument;
        veyra::log::error("gfx", std::format("slot-ring: acquire slot={} out of range", slot));
        return nullptr;
    }
    Slot& target = slots_[slot];

    if (target.fenceValue != 0) {
        const uint64_t completed = fence_->GetCompletedValue();
        if (completed < target.fenceValue) {
            const HRESULT waitResult = fence_->SetEventOnCompletion(target.fenceValue, fenceEvent_);
            if (FAILED(waitResult)) {
                status = Status::DeviceFailure;
                veyra::log::error("gfx", std::format("slot-ring: SetEventOnCompletion slot={} failed hr={}", slot, veyra::hresultString(waitResult)));
                return nullptr;
            }
            const DWORD wait = WaitForSingleObject(fenceEvent_, 10000);
            if (wait != WAIT_OBJECT_0) {
                status = Status::DeviceFailure;
                veyra::log::error("gfx", std::format("slot-ring: fence wait slot={} waitResult={} fenceValue={}", slot, wait, target.fenceValue));
                return nullptr;
            }
        }
    }

    HRESULT result = target.allocator->Reset();
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("slot-ring: allocator reset slot={} failed hr={}", slot, veyra::hresultString(result)));
        return nullptr;
    }
    result = target.list->Reset(target.allocator.Get(), nullptr);
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("slot-ring: list reset slot={} failed hr={}", slot, veyra::hresultString(result)));
        return nullptr;
    }
    return target.list.Get();
}

bool CommandSlotRing::submitAndSignal(uint32_t slot)
{
    if (!initialized_ || slot >= slotCount_) {
        veyra::log::error("gfx", std::format("slot-ring: submit slot={} out of range", slot));
        return false;
    }
    Slot& target = slots_[slot];

    const HRESULT closeResult = target.list->Close();
    if (FAILED(closeResult)) {
        veyra::log::error("gfx", std::format("slot-ring: close slot={} failed hr={}", slot, veyra::hresultString(closeResult)));
        return false;
    }
    ID3D12CommandList* lists[] = { target.list.Get() };
    queue_->ExecuteCommandLists(1, lists);

    const uint64_t value = nextFenceValue_++;
    const HRESULT signalResult = queue_->Signal(fence_, value);
    if (FAILED(signalResult)) {
        veyra::log::error("gfx", std::format("slot-ring: signal slot={} failed hr={}", slot, veyra::hresultString(signalResult)));
        return false;
    }
    target.fenceValue = value;
    return true;
}

bool CommandSlotRing::waitIdle()
{
    if (!initialized_) {
        return true;
    }
    uint64_t highest = 0;
    for (const Slot& slot : slots_) {
        if (slot.fenceValue > highest) {
            highest = slot.fenceValue;
        }
    }
    if (highest == 0) {
        return true;
    }
    if (fence_->GetCompletedValue() >= highest) {
        return true;
    }
    const HRESULT setResult = fence_->SetEventOnCompletion(highest, fenceEvent_);
    if (FAILED(setResult)) {
        veyra::log::error("gfx", std::format("slot-ring: waitIdle SetEventOnCompletion failed hr={}", veyra::hresultString(setResult)));
        return false;
    }
    const DWORD wait = WaitForSingleObject(fenceEvent_, 30000);
    if (wait != WAIT_OBJECT_0) {
        veyra::log::error("gfx", std::format("slot-ring: waitIdle wait={} fenceValue={}", wait, highest));
        return false;
    }
    return true;
}

uint32_t CommandSlotRing::timestampQueryIndex(uint32_t slot, bool end) const
{
    return slot * 2 + (end ? 1u : 0u);
}

bool CommandSlotRing::exerciseAll()
{
    if (!initialized_) {
        veyra::log::error("gfx", "slot-ring: exerciseAll before initialize");
        return false;
    }
    for (uint32_t slot = 0; slot < slotCount_; ++slot) {
        Status status = Status::Ok;
        ID3D12GraphicsCommandList* list = acquire(slot, status);
        if (list == nullptr) {
            return false;
        }
        list->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryIndex(slot, false));
        list->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryIndex(slot, true));
        if (!submitAndSignal(slot)) {
            return false;
        }
        veyra::log::info("gfx", std::format("slot-ring: slot={} recorded timestamp pair [{}..{}] and signaled fenceValue={}",
            slot, timestampQueryIndex(slot, false), timestampQueryIndex(slot, true), slots_[slot].fenceValue));
    }
    if (!waitIdle()) {
        return false;
    }
    veyra::log::info("gfx", std::format("slot-ring: exercised {} slots acquire->timestamps->submit->signal->waitIdle ok", slotCount_));
    return true;
}

} // namespace veyra::gfx
