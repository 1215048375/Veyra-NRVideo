// veyra_scene_analyzer — standalone scene/cadence analysis tool.
// Processes a test video clip and reports scene cuts, flashes, duplicates,
// and cadence breaks. Used by the phase5 gate.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "veyra/core/SceneCadenceAnalyzer.h"
#include "veyra/Log.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
}

namespace {

std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        default: r += c;
        if (static_cast<unsigned char>(c) > 127) r.back() = '?';
        }
    }
    return r;
}

// Compute a 256-bin luma histogram from an RGBA frame.
std::vector<double> computeHistogram(const uint8_t* data, int stride, int w, int h) {
    std::vector<double> hist(256, 0.0);
    for (int y = 0; y < h; y += 4) { // Sample every 4th row for speed
        const uint8_t* row = data + y * stride;
        for (int x = 0; x < w; x += 4) {
            uint8_t r = row[x*4], g = row[x*4+1], b = row[x*4+2];
            uint8_t luma = static_cast<uint8_t>(0.299*r + 0.587*g + 0.114*b);
            hist[luma] += 1.0;
        }
    }
    double total = 0;
    for (double& v : hist) total += v;
    if (total > 0) for (double& v : hist) v /= total;
    return hist;
}

// Compute normalized SAD between two RGBA frames.
double computeSAD(const uint8_t* a, int sa, const uint8_t* b, int sb, int w, int h) {
    double sad = 0;
    int count = 0;
    for (int y = 0; y < h; y += 4) {
        const uint8_t* ra = a + y * sa;
        const uint8_t* rb = b + y * sb;
        for (int x = 0; x < w; x += 4) {
            sad += std::abs(static_cast<int>(ra[x*4]) - static_cast<int>(rb[x*4]));
            sad += std::abs(static_cast<int>(ra[x*4+1]) - static_cast<int>(rb[x*4+1]));
            sad += std::abs(static_cast<int>(ra[x*4+2]) - static_cast<int>(rb[x*4+2]));
            count += 3;
        }
    }
    return count > 0 ? (sad / count) / 255.0 : 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string input = "validation/fixed_clips/test_h264_1080p.mp4";
    std::string jsonFile;
    std::string runId = "scene-analyzer";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--input") == 0 && i+1 < argc) input = argv[++i];
        else if (std::strcmp(argv[i], "--json-file") == 0 && i+1 < argc) jsonFile = argv[++i];
        else if (std::strcmp(argv[i], "--run-id") == 0 && i+1 < argc) runId = argv[++i];
    }

    veyra::log::info("scene", std::format("analyzing input={}", input));

    // Open video
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, input.c_str(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "scene: cannot open %s\n", input.c_str());
        return 1;
    }
    avformat_find_stream_info(fmt, nullptr);
    int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vstream < 0) { avformat_close_input(&fmt); return 1; }

    AVCodecContext* codec = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(codec, fmt->streams[vstream]->codecpar);
    codec->thread_count = 1;
    const AVCodec* dec = avcodec_find_decoder(codec->codec_id);
    if (!dec || avcodec_open2(codec, dec, nullptr) < 0) {
        avcodec_free_context(&codec); avformat_close_input(&fmt); return 1;
    }

    SwsContext* sws = sws_getContext(codec->width, codec->height, codec->pix_fmt,
        codec->width, codec->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);

    veyra::core::SceneCadenceAnalyzer analyzer;

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgba = av_frame_alloc();
    rgba->format = AV_PIX_FMT_RGBA; rgba->width = codec->width; rgba->height = codec->height;
    av_frame_get_buffer(rgba, 32);

    std::vector<uint8_t> prevFrame;
    uint64_t frameCount = 0;
    uint64_t prevPts = 0;
    std::vector<std::pair<uint64_t, std::string>> events;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != vstream) { av_packet_unref(pkt); continue; }
        avcodec_send_packet(codec, pkt);
        while (avcodec_receive_frame(codec, frame) == 0) {
            sws_scale(sws, frame->data, frame->linesize, 0, codec->height,
                      rgba->data, rgba->linesize);
            auto hist = computeHistogram(rgba->data[0], rgba->linesize[0], codec->width, codec->height);
            double sad = 0;
            if (!prevFrame.empty()) {
                sad = computeSAD(prevFrame.data(), codec->width*4,
                                 rgba->data[0], rgba->linesize[0], codec->width, codec->height);
            }
            uint64_t pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->pkt_dts;
            auto result = analyzer.analyze(frameCount, hist, sad, pts);
            if (result.isSceneCut) events.push_back({frameCount, "cut"});
            if (result.isFlash) events.push_back({frameCount, "flash"});
            if (result.isDuplicate) events.push_back({frameCount, "duplicate"});
            if (result.isCadenceBreak) events.push_back({frameCount, "cadence-break"});
            // Save current frame for SAD
            size_t sz = static_cast<size_t>(rgba->linesize[0]) * codec->height;
            prevFrame.assign(rgba->data[0], rgba->data[0] + sz);
            prevPts = pts;
            ++frameCount;
        }
        av_packet_unref(pkt);
    }
    // Flush
    avcodec_send_packet(codec, nullptr);
    while (avcodec_receive_frame(codec, frame) == 0) { ++frameCount; }

    // Cleanup
    av_frame_free(&rgba); av_frame_free(&frame); av_packet_free(&pkt);
    sws_freeContext(sws); avcodec_free_context(&codec); avformat_close_input(&fmt);

    veyra::log::info("scene", std::format("analyzed {} frames, cuts={} flashes={} dups={} events={}",
        frameCount, analyzer.sceneCutCount(), analyzer.flashCount(), analyzer.duplicateCount(), events.size()));

    // Write JSON
    if (!jsonFile.empty()) {
        std::string json = "{\n";
        json += "  \"probe\": \"veyra_scene_analyzer\",\n";
        json += std::format("  \"runId\": \"{}\",\n", jsonEscape(runId));
        json += std::format("  \"framesAnalyzed\": {},\n", frameCount);
        json += std::format("  \"sceneCuts\": {},\n", analyzer.sceneCutCount());
        json += std::format("  \"flashes\": {},\n", analyzer.flashCount());
        json += std::format("  \"duplicates\": {},\n", analyzer.duplicateCount());
        json += "  \"events\": [";
        for (size_t i = 0; i < events.size(); ++i) {
            if (i > 0) json += ",";
            json += std::format("{{\"frame\": {}, \"type\": \"{}\"}}", events[i].first, events[i].second);
        }
        json += "]\n}\n";
        HANDLE f = CreateFileA(jsonFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(f, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
            CloseHandle(f);
        }
    }

    bool ok = frameCount > 0;
    std::printf("%s\n", ok ? "scene: PASS" : "scene: FAIL");
    return ok ? 0 : 1;
}
