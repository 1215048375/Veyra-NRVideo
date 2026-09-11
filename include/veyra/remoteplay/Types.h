// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::remoteplay {
using HostTime = std::int64_t; // Local steady-clock, 100 ns. NEVER PS5 render time.
using Generation = std::uint64_t;
HostTime monotonic100ns() noexcept;

enum class Codec : std::uint8_t { H264, H265 };
enum class TimestampProvenance : std::uint8_t { Unknown, SourceProvided, LocalEstimated };
enum class SessionState : std::uint8_t { Idle, Connecting, WaitingFirstFrame, Streaming, LoginPinRequired, Failed, Stopping };
enum class SampleKind : std::uint8_t { CodecConfig, AccessUnit };
struct VideoProfile {
    std::uint32_t width = 1920, height = 1080, fps = 60, bitrateKbps = 15000;
    Codec codec = Codec::H264;
    std::optional<std::string> validate() const;
};
using AccountId = std::array<std::uint8_t, 8>;
std::optional<AccountId> accountIdFromBase64(std::string_view);
std::optional<AccountId> accountIdFromDecimal(std::string_view);
std::string accountIdToBase64(const AccountId&);
std::optional<std::uint32_t> parsePairingPin(std::string_view);
bool validHost(std::string_view) noexcept; // IPv4 or DNS hostname; no URL/userinfo/path/port.
bool validProfileId(std::string_view) noexcept;

// 64 is the required FFmpeg input padding at the audited API. Consumers using
// another ABI must static_assert against AV_INPUT_BUFFER_PADDING_SIZE.
class PaddedBytes {
public:
    static constexpr std::size_t Padding = 64;
    PaddedBytes() = default;
    static PaddedBytes copy(std::span<const std::uint8_t>);
    std::span<const std::uint8_t> bytes() const noexcept { return {storage_.data(), size_}; }
    const std::uint8_t* data() const noexcept { return storage_.data(); }
    std::size_t size() const noexcept { return size_; }
    bool paddingIsZero() const noexcept;
private:
    std::vector<std::uint8_t> storage_;
    std::size_t size_ = 0;
};
struct VideoSample {
    Generation generation = 0;
    SampleKind kind = SampleKind::AccessUnit;
    Codec codec = Codec::H264;
    PaddedBytes payload;
    // Present only when supplied by the metadata extension, never invented.
    std::optional<std::uint16_t> wireFrameIndex;
    std::uint32_t width = 0, height = 0;
    std::int32_t framesLost = 0;
    bool referenceRecovered = false; // NOT a FEC success or a keyframe indicator.
    HostTime arrival100ns = 0;
};
struct NalInfo {
    bool valid = false, hasPicture = false, hasConfig = false, idr = false;
};
// Annex-B only. H.265 CRA is intentionally NOT considered a clean IDR.
NalInfo inspectAnnexB(std::span<const std::uint8_t>, Codec) noexcept;

struct PcmBlock {
    Generation generation = 0;
    std::uint32_t channels = 0, rate = 0;
    std::uint64_t firstSample = 0; // Per channel, not interleaved element count.
    HostTime arrival100ns = 0;
    bool discontinuity = false;
    std::vector<std::int16_t> samples;
    std::size_t frames() const noexcept { return channels ? samples.size()/channels : 0; }
    bool valid() const noexcept;
};
struct ControllerState {
    // Semantic buttons; translated explicitly to Chiaki in the native bridge.
    enum Button : std::uint32_t { Cross=1u<<0, Circle=1u<<1, Square=1u<<2, Triangle=1u<<3,
        Left=1u<<4, Right=1u<<5, Up=1u<<6, Down=1u<<7, L1=1u<<8, R1=1u<<9,
        L3=1u<<10, R3=1u<<11, Options=1u<<12, Share=1u<<13, Touchpad=1u<<14, PS=1u<<15 };
    std::uint32_t buttons = 0;
    std::int16_t leftX=0, leftY=0, rightX=0, rightY=0;
    std::uint8_t l2=0, r2=0;
    bool operator==(const ControllerState&) const = default;
};
} // namespace veyra::remoteplay
