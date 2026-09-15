#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace veyra::engine {
enum class PlaylistMode { Sequential, Shuffle, RepeatOne };

// UI-thread queue. Playback stays in EngineController; entries never own a decoder.
class Playlist {
public:
    static constexpr size_t limit = 4096;
    std::vector<std::wstring> entries;
    std::optional<size_t> current;
    PlaylistMode mode = PlaylistMode::Sequential;
    bool repeatAll = false;

    static bool isVideo(const std::wstring& path) {
        auto ext = std::filesystem::path(path).extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
        return ext == L".mp4" || ext == L".mkv" || ext == L".mov" || ext == L".avi" || ext == L".ts"
            || ext == L".m4v" || ext == L".mts" || ext == L".m2ts" || ext == L".webm";
    }
    std::optional<size_t> add(const std::wstring& path) {
        if (!isVideo(path)) return {};
        const auto normalized = std::filesystem::path(path).lexically_normal().wstring();
        auto found = std::find(entries.begin(), entries.end(), normalized);
        if (found != entries.end()) return size_t(found - entries.begin());
        if (entries.size() >= limit) return {};
        entries.push_back(normalized);
        return entries.size() - 1;
    }
    void remove(size_t index) {
        if (index >= entries.size()) return;
        entries.erase(entries.begin() + index);
        if (current && *current == index) detach();
        else if (current && *current > index) --*current;
    }
    void move(size_t from, size_t to) {
        if (from >= entries.size() || to >= entries.size()) return;
        std::swap(entries[from], entries[to]);
        if (current && *current == from) current = to;
        else if (current && *current == to) current = from;
    }
    void clear() { entries.clear(); detach(); }
    void detach() { current.reset(); session_ = 0; }
    void suspend() { session_ = 0; }
    void bind(size_t index, uint64_t session) { current = index; session_ = session; consumed_ = false; }
    std::optional<size_t> adjacent(int direction) const {
        if (entries.empty()) return {};
        if (mode == PlaylistMode::Shuffle) {
            if (shuffleSource_ != entries || shuffled_.empty()) {
                shuffleSource_ = entries;shuffled_ = entries;
                std::shuffle(shuffled_.begin(), shuffled_.end(), random_);
                if (current && *current < entries.size()) {
                    auto found = std::find(shuffled_.begin(), shuffled_.end(), entries[*current]);
                    std::iter_swap(shuffled_.begin(), found);
                }
            }
            auto found = current && *current < entries.size() ? std::find(shuffled_.begin(), shuffled_.end(), entries[*current]) : shuffled_.end();
            int next = found == shuffled_.end() ? (direction < 0 ? int(shuffled_.size())-1 : 0) : int(found-shuffled_.begin()) + (direction < 0 ? -1 : 1);
            if (next < 0 || next >= int(shuffled_.size())) {
                if (!repeatAll) return {};
                next = next < 0 ? int(shuffled_.size())-1 : 0;
            }
            return size_t(std::find(entries.begin(), entries.end(), shuffled_[next])-entries.begin());
        }
        if (!current) return direction < 0 ? entries.size() - 1 : 0;
        if (direction < 0 && *current > 0) return *current - 1;
        if (direction > 0 && *current + 1 < entries.size()) return *current + 1;
        if (repeatAll) return direction < 0 ? entries.size() - 1 : 0;
        return {};
    }
    // Consume each EOF once. Stale sessions, failures, stop, images and live sources cannot advance.
    std::optional<size_t> ended(uint64_t session) {
        if (!current || !session_ || session != session_ || consumed_) return {};
        consumed_ = true;
        return mode == PlaylistMode::RepeatOne ? current : adjacent(1);
    }
private:
    uint64_t session_ = 0;
    bool consumed_ = false;
    mutable std::vector<std::wstring> shuffleSource_, shuffled_;
    mutable std::mt19937 random_{std::random_device{}()};
};
}
