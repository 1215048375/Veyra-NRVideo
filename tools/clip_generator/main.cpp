// veyra_clip_gen - deterministic H.264 test clip generator (tool-only build).
// Links the openh264-enabled FFmpeg from C:\veyra-deps\tools-installed; the
// player itself never links these libraries. Output: MP4/H.264 1080p with a
// deterministic moving pattern (bars + frame counter + gradient) so seek and
// parity checks have non-uniform, reproducible content.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <format>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

namespace {

// Deterministic RGB value at a pixel (same pattern the harness family uses).
void patternRgb(int x, int y, int width, int height, int64_t frameIndex, uint8_t out[3])
{
    const int bars = 8;
    const int barWidth = width / bars;
    const double u = static_cast<double>(x) / width;
    const double v = static_cast<double>(y) / height;
    if (y < height / 2) {
        const int bar = x / barWidth;
        const uint8_t lit = ((bar + frameIndex) % bars == 0) ? 235 : 40;
        out[0] = bar == 0 || bar == 3 || bar == 6 ? lit : 40;
        out[1] = bar == 1 || bar == 3 || bar == 5 ? lit : 40;
        out[2] = bar == 2 || bar == 5 || bar == 6 ? lit : 40;
    }
    else {
        out[0] = static_cast<uint8_t>(u * 255);
        out[1] = static_cast<uint8_t>(v * 255);
        out[2] = static_cast<uint8_t>(((u + v) * 0.5) * 255);
    }
    if (y < 12) {
        const uint8_t bit = (frameIndex >> (x * 8 / width)) & 1 ? 235 : 16;
        out[0] = out[1] = out[2] = bit;
    }
}

// Fill a YUV420P frame directly (BT.601 limited range) without swscale.
void fillYuv420p(AVFrame* frame, int width, int height, int64_t frameIndex)
{
    for (int y = 0; y < height; ++y) {
        uint8_t* yRow = frame->data[0] + y * frame->linesize[0];
        for (int x = 0; x < width; ++x) {
            uint8_t rgb[3];
            patternRgb(x, y, width, height, frameIndex, rgb);
            const double r = rgb[0] / 255.0;
            const double g = rgb[1] / 255.0;
            const double b = rgb[2] / 255.0;
            const double y_ = 0.299 * r + 0.587 * g + 0.114 * b;
            yRow[x] = static_cast<uint8_t>(16 + y_ * 219);
        }
    }
    const int chromaWidth = width / 2;
    const int chromaHeight = height / 2;
    for (int cy = 0; cy < chromaHeight; ++cy) {
        uint8_t* uRow = frame->data[1] + cy * frame->linesize[1];
        uint8_t* vRow = frame->data[2] + cy * frame->linesize[2];
        for (int cx = 0; cx < chromaWidth; ++cx) {
            // Average the 2x2 RGB block, then convert chroma.
            double rr = 0, gg = 0, bb = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    uint8_t rgb[3];
                    patternRgb(cx * 2 + dx, cy * 2 + dy, width, height, frameIndex, rgb);
                    rr += rgb[0] / 255.0;
                    gg += rgb[1] / 255.0;
                    bb += rgb[2] / 255.0;
                }
            }
            rr /= 4.0;
            gg /= 4.0;
            bb /= 4.0;
            const double y_ = 0.299 * rr + 0.587 * gg + 0.114 * bb;
            const double u_ = -0.169 * rr - 0.331 * gg + 0.500 * bb;
            const double v_ = 0.500 * rr - 0.419 * gg - 0.081 * bb;
            uRow[cx] = static_cast<uint8_t>(128 + u_ * 224);
            vRow[cx] = static_cast<uint8_t>(128 + v_ * 224);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string output = "test_h264_1080p.mp4";
    int seconds = 30;
    int fps = 30;
    int width = 1920;
    int height = 1080;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) output = argv[++i];
        else if (arg == "--seconds" && i + 1 < argc) seconds = atoi(argv[++i]);
        else if (arg == "--fps" && i + 1 < argc) fps = atoi(argv[++i]);
        else if (arg == "--width" && i + 1 < argc) width = atoi(argv[++i]);
        else if (arg == "--height" && i + 1 < argc) height = atoi(argv[++i]);
    }

    AVFormatContext* formatContext = nullptr;
    avformat_alloc_output_context2(&formatContext, nullptr, nullptr, output.c_str());
    if (formatContext == nullptr) {
        std::fprintf(stderr, "clip-gen: avformat_alloc_output_context2 failed\n");
        return 1;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libopenh264");
    if (codec == nullptr) {
        std::fprintf(stderr, "clip-gen: libopenh264 encoder not found\n");
        return 2;
    }

    AVStream* stream = avformat_new_stream(formatContext, nullptr);
    if (stream == nullptr) {
        return 3;
    }
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (codecContext == nullptr) {
        return 3;
    }
    codecContext->width = width;
    codecContext->height = height;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext->time_base = { 1, fps };
    codecContext->framerate = { fps, 1 };
    codecContext->gop_size = fps;
    codecContext->max_b_frames = 0;
    codecContext->bit_rate = 6'000'000;
    if (formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::fprintf(stderr, "clip-gen: avcodec_open2 failed\n");
        return 4;
    }
    avcodec_parameters_from_context(stream->codecpar, codecContext);
    stream->time_base = codecContext->time_base;

    if (!(formatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&formatContext->pb, output.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::fprintf(stderr, "clip-gen: avio_open failed\n");
            return 5;
        }
    }
    if (avformat_write_header(formatContext, nullptr) < 0) {
        return 5;
    }

    AVFrame* yuvFrame = av_frame_alloc();
    yuvFrame->format = AV_PIX_FMT_YUV420P;
    yuvFrame->width = width;
    yuvFrame->height = height;
    av_frame_get_buffer(yuvFrame, 32);

    AVPacket* packet = av_packet_alloc();
    const int64_t totalFrames = static_cast<int64_t>(seconds) * fps;
    for (int64_t frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
        fillYuv420p(yuvFrame, width, height, frameIndex);
        yuvFrame->pts = frameIndex;
        if (avcodec_send_frame(codecContext, yuvFrame) < 0) {
            return 6;
        }
        for (;;) {
            const int result = avcodec_receive_packet(codecContext, packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                break;
            }
            if (result < 0) {
                return 6;
            }
            av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (av_interleaved_write_frame(formatContext, packet) < 0) {
                return 7;
            }
        }
    }
    (void)avcodec_send_frame(codecContext, nullptr);
    for (;;) {
        const int result = avcodec_receive_packet(codecContext, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            break;
        }
        if (result < 0) {
            return 6;
        }
        av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;
        av_interleaved_write_frame(formatContext, packet);
    }
    av_write_trailer(formatContext);

    std::fprintf(stderr, "clip-gen: wrote %s (%lld frames, %ds@%d)\n",
        output.c_str(), static_cast<long long>(totalFrames), seconds, fps);

    av_packet_free(&packet);
    av_frame_free(&yuvFrame);
    avcodec_free_context(&codecContext);
    if (!(formatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&formatContext->pb);
    }
    avformat_free_context(formatContext);
    return 0;
}
