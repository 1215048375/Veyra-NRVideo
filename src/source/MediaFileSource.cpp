#include "veyra/source/MediaFileSource.h"

#include <climits>
#include <cmath>
#include <format>

#include "veyra/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace veyra::source {

namespace {

int64_t rationalToUs(const pipeline::Rational& r)
{
    if (!r.known || r.den == 0) { return 0; }
    return static_cast<int64_t>(static_cast<long double>(r.num) * 1000000.0L
        / static_cast<long double>(r.den) + (r.num >= 0 ? 0.5L : -0.5L));
}

} // namespace

MediaFileSource::~MediaFileSource()
{
    close();
}

pipeline::ColorDescription MediaFileSource::parseColor(const AVCodecParameters* params) const
{
    pipeline::ColorDescription cd;
    switch (params->format) {
    case AV_PIX_FMT_NV12: cd.pixelFormat = pipeline::SourcePixelFormat::NV12; break;
    case AV_PIX_FMT_P010: cd.pixelFormat = pipeline::SourcePixelFormat::P010; break;
    case AV_PIX_FMT_YUV420P: cd.pixelFormat = pipeline::SourcePixelFormat::Yuv420P; break;
    case AV_PIX_FMT_YUYV422: cd.pixelFormat = pipeline::SourcePixelFormat::Yuy2; break;
    case AV_PIX_FMT_BGRA: cd.pixelFormat = pipeline::SourcePixelFormat::Bgra8; break;
    default: cd.pixelFormat = pipeline::SourcePixelFormat::Unknown; break;
    }
    switch (params->color_range) {
    case AVCOL_RANGE_MPEG: cd.range = pipeline::ColorRange::Limited; break;
    case AVCOL_RANGE_JPEG: cd.range = pipeline::ColorRange::Full; break;
    default: cd.range = pipeline::ColorRange::Unknown; break;
    }
    switch (params->color_space) {
    case AVCOL_SPC_BT470BG: cd.matrix = pipeline::YuvMatrix::BT601; break;
    case AVCOL_SPC_SMPTE170M: cd.matrix = pipeline::YuvMatrix::BT601; break;
    case AVCOL_SPC_BT709: cd.matrix = pipeline::YuvMatrix::BT709; break;
    case AVCOL_SPC_BT2020_NCL: cd.matrix = pipeline::YuvMatrix::BT2020NCL; break;
    case AVCOL_SPC_BT2020_CL: cd.matrix = pipeline::YuvMatrix::BT2020CL; break;
    default: cd.matrix = pipeline::YuvMatrix::Unknown; break;
    }
    switch (params->color_trc) {
    case AVCOL_TRC_BT709: cd.transfer = pipeline::TransferFunction::BT709; break;
    case AVCOL_TRC_IEC61966_2_1: cd.transfer = pipeline::TransferFunction::SRGB; break;
    case AVCOL_TRC_BT2020_10: cd.transfer = pipeline::TransferFunction::BT2020_10; break;
    case AVCOL_TRC_LINEAR: cd.transfer = pipeline::TransferFunction::Linear; break;
    default: cd.transfer = pipeline::TransferFunction::Unknown; break;
    }
    switch (params->color_primaries) {
    case AVCOL_PRI_BT470M:
    case AVCOL_PRI_BT470BG: cd.primaries = pipeline::ColorPrimaries::BT601_625; break;
    case AVCOL_PRI_SMPTE170M: cd.primaries = pipeline::ColorPrimaries::BT601_525; break;
    case AVCOL_PRI_BT709: cd.primaries = pipeline::ColorPrimaries::BT709; break;
    case AVCOL_PRI_BT2020: cd.primaries = pipeline::ColorPrimaries::BT2020; break;
    default: cd.primaries = pipeline::ColorPrimaries::Unknown; break;
    }

    // Documented, logged defaults when the container is silent (Playbook 9):
    // SD -> BT.601, HD SDR -> BT.709; flagged assumed so the UI can show it.
    if (cd.matrix == pipeline::YuvMatrix::Unknown) {
        const bool hd = info_.height >= 700;
        cd.matrix = hd ? pipeline::YuvMatrix::BT709 : pipeline::YuvMatrix::BT601;
        cd.matrixAssumed = true;
    }
    if (cd.range == pipeline::ColorRange::Unknown) {
        cd.range = pipeline::ColorRange::Limited;
        cd.rangeAssumed = true;
    }
    if (cd.transfer == pipeline::TransferFunction::Unknown) {
        cd.transfer = pipeline::TransferFunction::BT709;
        cd.transferAssumed = true;
    }
    if (cd.primaries == pipeline::ColorPrimaries::Unknown) {
        cd.primaries = pipeline::ColorPrimaries::BT709;
        cd.primariesAssumed = true;
    }
    return cd;
}

bool MediaFileSource::open(const SourceOpenDesc& desc)
{
    close();
    if (!demuxer_.open(desc.path)) {
        veyra::log::error("source-file", "demuxer open failed");
        return false;
    }
    const AVCodecParameters* params = demuxer_.videoCodecParameters();
    if (params == nullptr) {
        veyra::log::error("source-file", "no video codec parameters");
        return false;
    }

    bool decoderOpen = false;
    if (desc.preferHardwareDecode && desc.d3d12Device != nullptr && desc.d3d12Queue != nullptr) {
        decoderOpen = decoder_.openD3D12VA(params, demuxer_.videoTimeBaseNum(),
            demuxer_.videoTimeBaseDen(),
            static_cast<ID3D12Device*>(desc.d3d12Device),
            static_cast<ID3D12CommandQueue*>(desc.d3d12Queue));
        if (decoderOpen) {
            veyra::log::info("source-file", "D3D12VA decode active (shared device)");
        } else {
            veyra::log::warn("source-file", "D3D12VA open failed; falling back to software decode (explicit)");
        }
    }
    if (!decoderOpen) {
        decoderOpen = decoder_.openSoftware(params, demuxer_.videoTimeBaseNum(),
            demuxer_.videoTimeBaseDen());
    }
    if (!decoderOpen) {
        veyra::log::error("source-file", "decoder open failed");
        close();
        return false;
    }

    info_ = SourceInfo{};
    info_.opened = true;
    info_.kind = pipeline::SourceKind::File;
    info_.width = static_cast<uint32_t>(decoder_.width());
    info_.height = static_cast<uint32_t>(decoder_.height());
    const int64_t durUs = demuxer_.durationUs();
    if (durUs > 0) {
        info_.duration = pipeline::Rational{durUs, 1000000};
    } else {
        info_.duration = pipeline::Rational::unknown();
    }
    info_.averageFps = demuxer_.averageFps();
    info_.nominalRateNum = demuxer_.nominalRateNum();
    info_.nominalRateDen = demuxer_.nominalRateDen();
    info_.timestampQuantum = demuxer_.videoTimeBaseDen() > 0 ? double(demuxer_.videoTimeBaseNum()) / demuxer_.videoTimeBaseDen() : 0;
    info_.hardwareDecodeActive = decoder_.usingD3D12Frames();
    info_.color = parseColor(params);

    sequence_ = 0;
    framesRead_ = 0;
    seekCount_ = 0;
    draining_ = false;
    eofSignalled_ = false;
    pendingSeekFlag_ = false;
    lastPtsUs_ = INT64_MIN;
    epoch_ = 1;
    ++epoch_; // fresh open = new epoch

    veyra::log::info("source-file", std::format(
        "opened {}x{} dur={}s avgFps={:.3f} hw={} matrix={}{} range={}{} transfer={}{}",
        info_.width, info_.height,
        info_.duration.isUnknown() ? -1.0 : info_.duration.toDouble(),
        info_.averageFps, info_.hardwareDecodeActive,
        static_cast<int>(info_.color.matrix), info_.color.matrixAssumed ? "(assumed)" : "",
        static_cast<int>(info_.color.range), info_.color.rangeAssumed ? "(assumed)" : "",
        static_cast<int>(info_.color.transfer), info_.color.transferAssumed ? "(assumed)" : ""));
    return true;
}

SourceReadStatus MediaFileSource::read(pipeline::FramePacket& out, const AVFrame** decodedFrame)
{
    if (!info_.opened) { return SourceReadStatus::Error; }
    if (decodedFrame != nullptr) { *decodedFrame = nullptr; }

    const AVFrame* frame = nullptr;
    for (;;) {
        frame = decoder_.receiveFrame();
        if (frame != nullptr) { break; }
        if (draining_) {
            // Decoder fully drained after EOF.
            return SourceReadStatus::Eos;
        }
        bool eof = false;
        if (!demuxer_.readVideoPacket(eof)) {
            if (!eof) { return SourceReadStatus::Error; }
            draining_ = true;
            decoder_.sendPacket(nullptr); // enter drain mode
            continue;
        }
        if (!decoder_.sendPacket(demuxer_.currentPacket())) {
            veyra::log::warn("source-file", "sendPacket rejected; retrying next packet");
        }
    }

    out = pipeline::FramePacket{};
    ++sequence_;
    out.sequence = sequence_;
    const int tbNum = decoder_.frameTimeBaseNum();
    const int tbDen = decoder_.frameTimeBaseDen();
    if (frame->pts != AV_NOPTS_VALUE && tbDen > 0) {
        out.pts = pipeline::Rational{frame->pts * static_cast<int64_t>(tbNum), tbDen};
    } else {
        out.pts = pipeline::Rational::unknown(); // never fabricate a timestamp
    }
    if (frame->duration > 0 && tbDen > 0) {
        out.duration = pipeline::Rational{frame->duration * static_cast<int64_t>(tbNum), tbDen};
    } else if (info_.averageFps > 0.0) {
        out.duration = pipeline::Rational{1000000, static_cast<int32_t>(1000000.0 * info_.averageFps + 0.5)};
    } else {
        out.duration = pipeline::Rational::unknown();
    }
    out.sourceKind = pipeline::SourceKind::File;
    out.colorInfo = info_.color;
    out.sourceEpoch = epoch_;

    uint32_t flags = 0;
    if (framesRead_ == 0) { flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Open); }
    if (pendingSeekFlag_) {
        flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Seek);
        pendingSeekFlag_ = false;
    }
    const int64_t ptsUs = rationalToUs(out.pts);
    if (lastPtsUs_ != INT64_MIN && !out.pts.isUnknown() && ptsUs < lastPtsUs_) {
        flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
    }
    if (!out.pts.isUnknown()) { lastPtsUs_ = ptsUs; }
    out.flags = flags;

    // The canonical linear working texture is produced by the graph's
    // ingress conversion; the source hands over the decoded frame view.
    out.color.resource = nullptr;

    ++framesRead_;
    if (decodedFrame != nullptr) { *decodedFrame = frame; }
    return SourceReadStatus::Frame;
}

bool MediaFileSource::seek(const pipeline::Rational& targetSeconds)
{
    if (!info_.opened) { return false; }
    if (targetSeconds.isUnknown() || targetSeconds.den == 0 || targetSeconds.isNegative()) {
        veyra::log::error("source-file", "seek target must be a known non-negative rational");
        return false;
    }
    const int64_t targetUs = rationalToUs(targetSeconds);
    if (!demuxer_.seekToUs(targetUs)) { return false; }
    decoder_.flushBuffers();
    draining_ = false;
    eofSignalled_ = false;
    pendingSeekFlag_ = true;
    lastPtsUs_ = INT64_MIN;
    ++epoch_;
    ++seekCount_;
    veyra::log::info("source-file", std::format("seek targetUs={} epoch={}", targetUs, epoch_));
    return true;
}

void MediaFileSource::close() noexcept
{
    decoder_.close();
    demuxer_.close();
    info_ = SourceInfo{};
}

} // namespace veyra::source
