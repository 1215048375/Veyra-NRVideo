// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Types.h"
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
namespace veyra::remoteplay {
// Only for DECODED reference-counted frames. Never feed compressed packets here.
// T should own an av_frame_clone/ref (custom deleter); a raw AVFrame* is not ownership.
template<class T> class LatestMailbox {
public:
    struct Entry {Generation generation;std::uint64_t sequence;T value;};
    void begin(Generation generation) {
        if(!generation)throw std::invalid_argument("Generation zero is invalid");
        std::optional<Entry> retired;
        {std::lock_guard lock(mutex_);retired.swap(entry_);generation_=generation;closed_=false;overwritten_=0;lastSequence_=0;}
    }
    bool publish(Entry incoming) {
        std::optional<Entry> retired;
        {std::lock_guard lock(mutex_);
         if(closed_||incoming.generation!=generation_||incoming.sequence<=lastSequence_)return false;
         lastSequence_=incoming.sequence;if(entry_){++overwritten_;retired.swap(entry_);}entry_.emplace(std::move(incoming));}
        return true; // Retired frame released outside the mailbox mutex.
    }
    std::optional<Entry> take() {
        std::lock_guard lock(mutex_);auto out=std::move(entry_);entry_.reset();return out;
    }
    void close() {
        std::optional<Entry> retired;
        {std::lock_guard lock(mutex_);closed_=true;retired.swap(entry_);}
    }
    std::uint64_t overwritten() const {std::lock_guard lock(mutex_);return overwritten_;}
private:
    mutable std::mutex mutex_;std::optional<Entry> entry_;
    Generation generation_=0;std::uint64_t lastSequence_=0,overwritten_=0;bool closed_=true;
};
} // namespace veyra::remoteplay
