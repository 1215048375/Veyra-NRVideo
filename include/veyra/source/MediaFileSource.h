#pragma once

// MediaFileSource - local video file input (H.264/HEVC in MP4/MKV/MOV)
// composing the product veyra_media libraries. No second decode
// implementation exists here: FFmpegDemuxer + FFmpegVideoDecoder do the
// work; this class owns contract translation (PTS/duration/color/flags/
// epoch) and the seek protocol.
#include <cstdint>

#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/source/IFrameSource.h"

namespace veyra::source {

class MediaFileSource final : public IFrameSource {
public:
    MediaFileSource() = default;
    ~MediaFileSource() override;

    MediaFileSource(const MediaFileSource&) = delete;
    MediaFileSource& operator=(const MediaFileSource&) = delete;

    bool open(const SourceOpenDesc& desc) override;
    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket& out, const AVFrame** decodedFrame) override;
    bool seek(const pipeline::Rational& targetSeconds) override;
    void close() noexcept override;

    uint64_t framesRead() const { return framesRead_; }
    uint64_t seekCount() const { return seekCount_; }
    uint64_t epoch() const { return epoch_; }
    const std::wstring& errorMessage() const { return errorMessage_; }

private:
    pipeline::ColorDescription parseColor(const AVCodecParameters* params) const;

    media::FFmpegDemuxer demuxer_;
    media::FFmpegVideoDecoder decoder_;
    SourceInfo info_{};
    uint64_t sequence_ = 0;
    uint64_t epoch_ = 1;
    uint64_t framesRead_ = 0;
    uint64_t seekCount_ = 0;
    bool pendingSeekFlag_ = false;
    bool draining_ = false;
    bool eofSignalled_ = false;
    std::wstring errorMessage_;
    int64_t lastPtsUs_ = INT64_MIN;
};

} // namespace veyra::source
