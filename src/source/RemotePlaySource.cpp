#include "veyra/source/RemotePlaySource.h"

#include "veyra/Log.h"
#include "veyra/pipeline/ColorMetadata.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <format>
#include <limits>
#include <span>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
}

namespace veyra::source {
namespace {

std::shared_ptr<AVFrame> cloneFrame(const AVFrame* frame)
{
    AVFrame* clone = av_frame_clone(frame);
    if (clone == nullptr) {
        return {};
    }
    return std::shared_ptr<AVFrame>(clone, [](AVFrame* value) {
        av_frame_free(&value);
    });
}

pipeline::SourcePixelFormat pixelFormatFromFrame(const AVFrame& frame)
{
    switch (frame.format) {
    case AV_PIX_FMT_NV12: return pipeline::SourcePixelFormat::NV12;
    case AV_PIX_FMT_P010: return pipeline::SourcePixelFormat::P010;
    case AV_PIX_FMT_YUV420P: return pipeline::SourcePixelFormat::Yuv420P;
    case AV_PIX_FMT_YUYV422: return pipeline::SourcePixelFormat::Yuy2;
    case AV_PIX_FMT_BGRA: return pipeline::SourcePixelFormat::Bgra8;
    default: return pipeline::SourcePixelFormat::Unknown;
    }
}

} // namespace

RemotePlaySource::RemotePlaySource()
    : inbox_(std::make_unique<remoteplay::SessionInbox>())
{
}

RemotePlaySource::~RemotePlaySource()
{
    close();
}

bool RemotePlaySource::open(const SourceOpenDesc&)
{
    veyra::log::error("remoteplay", "RemotePlaySource requires connect() with PS5 credentials");
    return false;
}

bool RemotePlaySource::connect(const RemotePlayConnectDesc& desc)
{
    close();
    if (desc.request.video.validate() || !remoteplay::validHost(desc.request.host)) {
        veyra::log::error("remoteplay", "connect rejected: invalid host or video profile");
        return false;
    }
    request_ = remoteplay::NativeConnectRequest{};
    request_.host = desc.request.host;
    request_.video = desc.request.video;
    request_.credentials.accountId = desc.request.credentials.accountId;
    request_.credentials.registrationKey = desc.request.credentials.registrationKey;
    request_.credentials.sessionKey = desc.request.credentials.sessionKey;

    origin100ns_ = static_cast<std::uint64_t>(remoteplay::monotonic100ns());
    try {
        inbox_ = std::make_unique<remoteplay::SessionInbox>(desc.queueLimits);
    } catch (...) {
        veyra::log::error("remoteplay", "invalid queue limits");
        return false;
    }
    auto token = inbox_->begin(static_cast<remoteplay::HostTime>(origin100ns_));
    token_ = token; // Keep a second weak token; backend owns the moved copy.
    const auto started = backend_.start(request_, token);
    if (!started.ok) {
        token_.reset();
        inbox_->invalidate();
        try { inbox_->finishStop(); } catch (...) {}
        veyra::log::error("remoteplay", std::format("{} failed code={}", started.operation, started.code));
        return false;
    }
    info_ = SourceInfo{};
    info_.opened = true;
    info_.kind = pipeline::SourceKind::RemotePlay;
    info_.width = request_.video.width;
    info_.height = request_.video.height;
    info_.averageFps = static_cast<double>(request_.video.fps);
    info_.nominalRateNum = static_cast<int>(request_.video.fps);
    info_.nominalRateDen = 1;
    info_.timestampQuantum = 1.0 / info_.averageFps;
    info_.duration = pipeline::Rational::unknown();
    info_.hardwareDecodeActive = false;
    info_.color.pixelFormat = pipeline::SourcePixelFormat::Yuv420P;
    info_.color.range = pipeline::ColorRange::Limited;
    info_.color.rangeAssumed = true;
    info_.color.matrix = request_.video.width > 1280 ? pipeline::YuvMatrix::BT709 : pipeline::YuvMatrix::BT601;
    info_.color.matrixAssumed = true;
    info_.color.transfer = pipeline::TransferFunction::BT709;
    info_.color.transferAssumed = true;
    info_.color.primaries = pipeline::ColorPrimaries::BT709;
    info_.color.primariesAssumed = true;
    clock_.reset(static_cast<remoteplay::HostTime>(origin100ns_), request_.video.fps);
    sequence_ = 0;
    fallbackSourceIndex_ = 0;
    decoderEpoch_ = 1;
    connected_ = true;
    decoderReady_ = false;
    waitingForFirstFrame_ = true;
    pendingOpenFlag_ = true;
    veyra::log::info("remoteplay", std::format("session started host={} profile={}x{}@{} codec={}",
        request_.host, request_.video.width, request_.video.height, request_.video.fps,
        request_.video.codec == remoteplay::Codec::H264 ? "H264" : "H265"));
    return true;
}

remoteplay::SessionInbox::Snapshot RemotePlaySource::sessionSnapshot() const
{
    return inbox_->snapshot();
}

bool RemotePlaySource::openDecoder(remoteplay::Codec codec, std::uint32_t width, std::uint32_t height)
{
    if (codecContext_ != nullptr) {
        if (codecContext_->codec_id == (codec == remoteplay::Codec::H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC)
            && static_cast<std::uint32_t>(codecContext_->width) == width
            && static_cast<std::uint32_t>(codecContext_->height) == height) {
            return true;
        }
        flushDecoder();
        avcodec_free_context(&codecContext_);
        decoderReady_ = false;
    }
    const AVCodecID codecId = codec == remoteplay::Codec::H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
    const AVCodec* decoder = avcodec_find_decoder(codecId);
    if (decoder == nullptr) {
        veyra::log::error("remoteplay", "FFmpeg decoder unavailable");
        return false;
    }
    codecContext_ = avcodec_alloc_context3(decoder);
    if (codecContext_ == nullptr) {
        return false;
    }
    codecContext_->codec_id = codecId;
    codecContext_->width = static_cast<int>(width);
    codecContext_->height = static_cast<int>(height);
    codecContext_->time_base = AVRational{1, static_cast<int>(request_.video.fps)};
    codecContext_->thread_count = 1;
    codecContext_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(codecContext_, decoder, nullptr) < 0) {
        veyra::log::error("remoteplay", "FFmpeg remote decoder open failed");
        avcodec_free_context(&codecContext_);
        return false;
    }
    decoderFrame_ = av_frame_alloc();
    if (decoderFrame_ == nullptr) {
        avcodec_free_context(&codecContext_);
        return false;
    }
    decoderReady_ = true;
    info_.width = width;
    info_.height = height;
    veyra::log::info("remoteplay", std::format("software decoder opened codec={} {}x{}", decoder->name, width, height));
    return true;
}

pipeline::FramePacket RemotePlaySource::makePacket(const remoteplay::VideoSample& sample,
    std::uint64_t sourceIndex, std::uint64_t epoch, bool reset)
{
    pipeline::FramePacket packet{};
    packet.sequence = 0;
    packet.sourceKind = pipeline::SourceKind::RemotePlay;
    packet.sourceEpoch = epoch;
    packet.arrivalHost100ns = sample.arrival100ns;
    const auto stamp = clock_.video(sourceIndex, sample.arrival100ns);
    if (stamp) {
        packet.pts = pipeline::Rational{stamp->pts100ns, 10000000};
        packet.duration = pipeline::Rational{stamp->duration100ns, 10000000};
        if (stamp->discontinuity) packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
    } else {
        packet.pts = pipeline::Rational::unknown();
        packet.duration = pipeline::Rational::unknown();
    }
    if (pendingOpenFlag_) {
        packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Open);
        pendingOpenFlag_ = false;
    }
    if (reset) packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
    packet.colorInfo = info_.color;
    return packet;
}

bool RemotePlaySource::drainDecoder(std::uint64_t sourceIndex, const pipeline::FramePacket& sourcePacket)
{
    for (;;) {
        const int result = avcodec_receive_frame(codecContext_, decoderFrame_);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return true;
        }
        if (result < 0) {
            veyra::log::error("remoteplay", std::format("avcodec_receive_frame failed code={}", result));
            return false;
        }
        auto clone = cloneFrame(decoderFrame_);
        if (!clone) return false;
        pipeline::FramePacket packet = sourcePacket;
        packet.sequence = ++sequence_;
        packet.colorInfo.pixelFormat = pixelFormatFromFrame(*decoderFrame_);
        packet.color.resource = nullptr;
        // A packet can legally produce more than one decoded frame. Keep each
        // frame in order and give additional frames the source cadence PTS.
        packet.pts = sourcePacket.pts;
        (void)sourceIndex;
        ready_.push_back({std::move(clone), packet});
    }
}

bool RemotePlaySource::submitPacket(std::span<const std::uint8_t> bytes,
    std::uint64_t sourceIndex, const pipeline::FramePacket& sourcePacket)
{
    if (!decoderReady_ || bytes.empty()) return false;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr || av_new_packet(packet, static_cast<int>(bytes.size())) < 0) {
        av_packet_free(&packet);
        return false;
    }
    std::memcpy(packet->data, bytes.data(), bytes.size());
    packet->pts = static_cast<int64_t>(sourceIndex);
    packet->dts = packet->pts;
    bool ok = true;
    for (;;) {
        const int send = avcodec_send_packet(codecContext_, packet);
        if (send == 0) {
            ok = drainDecoder(sourceIndex, sourcePacket);
            break;
        }
        if (send == AVERROR(EAGAIN)) {
            if (!drainDecoder(sourceIndex, sourcePacket)) { ok = false; break; }
            continue;
        }
        veyra::log::error("remoteplay", std::format("avcodec_send_packet failed code={}", send));
        ok = false;
        break;
    }
    av_packet_free(&packet);
    return ok;
}

SourceReadStatus RemotePlaySource::read(pipeline::FramePacket& out, const AVFrame** decodedFrame)
{
    if (decodedFrame) *decodedFrame = nullptr;
    if (!connected_) return SourceReadStatus::Error;
    if (!ready_.empty()) {
        auto item = std::move(ready_.front());
        ready_.pop_front();
        out = item.packet;
        lastFrame_ = std::move(item.frame);
        if (decodedFrame) *decodedFrame = lastFrame_.get();
        return SourceReadStatus::Frame;
    }
    const auto now = remoteplay::monotonic100ns();
    auto queued = inbox_->tryVideo(now);
    if (!queued) {
        const auto state = inbox_->snapshot().state;
        if (state == remoteplay::SessionState::Failed || state == remoteplay::SessionState::Idle) return SourceReadStatus::Error;
        return SourceReadStatus::Waiting;
    }
    const auto& sample = queued->sample;
    if (!decoderReady_ && !openDecoder(sample.codec, sample.width, sample.height)) {
        inbox_->decodeFailed(sample.generation);
        return SourceReadStatus::Error;
    }
    if (queued->resetDecoder) {
        flushDecoder();
        ++decoderEpoch_;
        clock_.reset(static_cast<remoteplay::HostTime>(origin100ns_), request_.video.fps);
        pendingOpenFlag_ = true;
    }
    const std::uint64_t sourceIndex = sample.wireFrameIndex
        ? *sample.wireFrameIndex
        : ++fallbackSourceIndex_;
    const pipeline::FramePacket sourcePacket = makePacket(sample, sourceIndex, decoderEpoch_, queued->resetDecoder);
    if (queued->configBefore && !submitPacket(queued->configBefore->payload.bytes(), sourceIndex, sourcePacket)) {
        inbox_->decodeFailed(sample.generation);
        return SourceReadStatus::Error;
    }
    if (!submitPacket(sample.payload.bytes(), sourceIndex, sourcePacket)) {
        inbox_->decodeFailed(sample.generation);
        return SourceReadStatus::Error;
    }
    if (ready_.empty()) return SourceReadStatus::Waiting;
    auto item = std::move(ready_.front());
    ready_.pop_front();
    out = item.packet;
    lastFrame_ = std::move(item.frame);
    if (decodedFrame) *decodedFrame = lastFrame_.get();
    waitingForFirstFrame_ = false;
    return SourceReadStatus::Frame;
}

void RemotePlaySource::flushDecoder() noexcept
{
    ready_.clear();
    if (codecContext_) avcodec_flush_buffers(codecContext_);
}

std::size_t RemotePlaySource::pullAudio(float* stereo, std::size_t frames, double* firstPtsMs)
{
    if (!stereo || frames == 0) return 0;
    std::size_t written = 0;
    while (written < frames) {
        auto block = inbox_->tryAudio();
        if (!block) break;
        if (block->channels == 0 || block->rate == 0) continue;
        if (firstPtsMs && written == 0) {
            const auto pts = inbox_->snapshot().commonOrigin100ns +
                static_cast<std::int64_t>(block->firstSample) * 10000000ll / block->rate;
            *firstPtsMs = static_cast<double>(pts) / 10000.0;
        }
        const auto copyFrames = std::min(frames - written, block->frames());
        for (std::size_t i = 0; i < copyFrames; ++i) {
            const auto sourceChannels = block->channels;
            const auto sourceIndex = i * sourceChannels;
            stereo[(written + i) * 2] = static_cast<float>(block->samples[sourceIndex]) / 32768.0f;
            stereo[(written + i) * 2 + 1] = sourceChannels > 1
                ? static_cast<float>(block->samples[sourceIndex + 1]) / 32768.0f
                : stereo[(written + i) * 2];
        }
        written += copyFrames;
        if (copyFrames < block->frames()) break;
    }
    return written;
}

void RemotePlaySource::close() noexcept
{
    connected_ = false;
    ready_.clear();
    lastFrame_.reset();
    if (token_) {
        inbox_->invalidate();
        (void)backend_.stop();
        try { inbox_->finishStop(); } catch (...) {}
        token_.reset();
    }
    flushDecoder();
    av_frame_free(&decoderFrame_);
    avcodec_free_context(&codecContext_);
    decoderReady_ = false;
    info_ = SourceInfo{};
}

std::size_t RemotePlayAudioSource::pull(float* stereo, std::size_t frames, double* firstPtsMs)
{
    const auto count = source_.pullAudio(stereo, frames, firstPtsMs);
    if (count && firstPtsMs && std::isfinite(*firstPtsMs)) {
        lastEndPtsMs_ = *firstPtsMs + 1000.0 * static_cast<double>(count) / 48000.0;
    }
    return count;
}

} // namespace veyra::source
