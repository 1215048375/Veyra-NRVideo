// veyra_clip_gen - deterministic H.264 corpus clip generator (tool-only build).
// Links the openh264-enabled FFmpeg from the local toolchain and veyra_base
// (SHA-256). The player never links these libraries.
//
// Scenarios (Playbook section 20; every renderer is a pure function of
// (scenario, x, y, frameIndex, extent) so runs are reproducible):
//   translation          high-contrast checkerboard moving +8 px/frame, wrap
//   occlusion            slow diagonal texture behind a sweeping solid rect
//   cut-flash-duplicate  4 hard-cut segments + 1-frame white flash + duplicate
//                        frame run + PTS-stable cadence break signal
//   particles            dark field with translucent bright blobs (alpha)
//   ui-text              fixed text-like bar block over a scrolling gradient
//
// --make-corpus writes <dir>/corpus-manifest.json plus <dir>/corpus/*.mp4 for
// 5 scenarios x {1920x1080, 3840x2160} @ 60 fps. Manifest clip paths are
// relative to the manifest file's own directory ("pathBase": "manifest-dir").
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <veyra/FileIdentity.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

namespace {

enum class Scenario {
    Translation,
    Occlusion,
    CutFlashDuplicate,
    Particles,
    UiText,
};

bool parseScenario(const std::string& name, Scenario& out)
{
    if (name == "translation") { out = Scenario::Translation; return true; }
    if (name == "occlusion") { out = Scenario::Occlusion; return true; }
    if (name == "cut-flash-duplicate") { out = Scenario::CutFlashDuplicate; return true; }
    if (name == "particles") { out = Scenario::Particles; return true; }
    if (name == "ui-text") { out = Scenario::UiText; return true; }
    return false;
}

const char* scenarioName(Scenario s)
{
    switch (s) {
    case Scenario::Translation: return "translation";
    case Scenario::Occlusion: return "occlusion";
    case Scenario::CutFlashDuplicate: return "cut-flash-duplicate";
    case Scenario::Particles: return "particles";
    case Scenario::UiText: return "ui-text";
    }
    return "unknown";
}

// Deterministic 0..1 hash for particle placement (integer-only chains).
double detHash01(uint32_t a, uint32_t b)
{
    uint32_t h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<double>(h & 0xFFFFFFu) / 16777215.0;
}

// ---------------------------------------------------------------------------
// Per-pixel scenario renderers (RGB full-range working values).
// ---------------------------------------------------------------------------

void renderTranslation(int x, int y, int w, int h, int64_t frame, uint8_t out[3])
{
    const int cell = h >= 2160 ? 128 : 64;
    const int64_t offset = frame * 8;
    const int cx = static_cast<int>((x + offset) / cell);
    const int cy = y / cell;
    const bool parity = ((cx + cy) & 1) != 0;
    // Two high-contrast, chroma-distinct colors so luma AND chroma motion exist.
    if (parity) { out[0] = 205; out[1] = 70; out[2] = 60; }
    else { out[0] = 55; out[1] = 55; out[2] = 200; }
    // Soft vertical gradient keeps every region non-flat.
    const double g = static_cast<double>(y) / h;
    const int lift = static_cast<int>(g * 24.0);
    out[0] = static_cast<uint8_t>(out[0] + lift > 255 ? 255 : out[0] + lift);
    out[1] = static_cast<uint8_t>(out[1] + lift > 255 ? 255 : out[1] + lift);
    out[2] = static_cast<uint8_t>(out[2] + lift > 255 ? 255 : out[2] + lift);
}

void renderOcclusionBackground(int x, int y, int64_t frame, uint8_t out[3])
{
    const int period = 48;
    const int64_t offset = frame * -2;
    const int phase = static_cast<int>((x + offset) + y) % period;
    const int band = phase < period / 2 ? 0 : 1;
    const int base = band == 0 ? 96 : 168;
    const int fine = ((x / 4 + y / 4) & 1) != 0 ? 10 : -10;
    out[0] = static_cast<uint8_t>(base + fine);
    out[1] = static_cast<uint8_t>(base + fine + 8);
    out[2] = static_cast<uint8_t>(base + fine + 16);
}

void renderOcclusion(int x, int y, int w, int h, int64_t totalFrames, int64_t frame, uint8_t out[3])
{
    // Foreground rect sweeps left->right->left across the clip (triangle wave).
    const int rectW = w * 35 / 100;
    const int rectH = h * 50 / 100;
    const int rectY0 = (h - rectH) / 2;
    const double t = static_cast<double>(frame) / static_cast<double>(totalFrames - 1);
    double tri = t * 2.0;
    if (tri > 1.0) { tri = 2.0 - tri; }
    const int rectX0 = static_cast<int>(tri * static_cast<double>(w - rectW));
    if (y >= rectY0 && y < rectY0 + rectH && x >= rectX0 && x < rectX0 + rectW) {
        const int border = (h >= 2160) ? 8 : 4;
        const bool edge = x < rectX0 + border || x >= rectX0 + rectW - border
            || y < rectY0 + border || y >= rectY0 + rectH - border;
        if (edge) { out[0] = 225; out[1] = 225; out[2] = 235; }
        else { out[0] = 70; out[1] = 125; out[2] = 180; }
        return;
    }
    renderOcclusionBackground(x, y, frame, out);
}

void renderCutSegment(int seg, int x, int y, int w, int h, int64_t frame, uint8_t out[3])
{
    switch (seg) {
    case 0: {
        // Fine checker, distinct colors from the translation scenario.
        const int cell = h >= 2160 ? 48 : 24;
        const bool parity = (((x / cell) + (y / cell)) & 1) != 0;
        out[0] = parity ? 190 : 70; out[1] = parity ? 150 : 95; out[2] = parity ? 60 : 175;
        return;
    }
    case 1: {
        // Large slow vertical color blocks.
        const int cell = h >= 2160 ? 384 : 192;
        const int block = static_cast<int>((y / cell + frame / 30) % 4);
        const uint8_t v = static_cast<uint8_t>(60 + block * 45);
        out[0] = v; out[1] = static_cast<uint8_t>(220 - block * 30); out[2] = static_cast<uint8_t>(90 + block * 25);
        return;
    }
    case 2: {
        // Fast horizontal stripes (+16 px/frame equivalent by phase).
        const int period = h >= 2160 ? 160 : 80;
        const int phase = static_cast<int>((x + frame * 16) % period);
        const bool on = phase < period / 2;
        const uint8_t v = on ? 210 : 55;
        out[0] = v; out[1] = static_cast<uint8_t>(v * 3 / 4); out[2] = static_cast<uint8_t>(v * 2 / 3);
        return;
    }
    default: {
        // Expanding rings from center.
        const double dx = x - w * 0.5, dy = y - h * 0.5;
        const double r = std::sqrt(dx * dx + dy * dy);
        const double period = h >= 2160 ? 240.0 : 120.0;
        const double phase = std::fmod(r - static_cast<double>(frame) * 6.0, period);
        const bool on = phase >= 0 && phase < period * 0.5;
        const uint8_t v = on ? 200 : 80;
        out[0] = v; out[1] = v; out[2] = static_cast<uint8_t>(on ? 120 : 190);
        return;
    }
    }
}

void renderCutFlashDuplicate(int x, int y, int w, int h, int64_t totalFrames, int64_t frame, uint8_t out[3])
{
    const int64_t segFrames = totalFrames / 4;
    // Duplicate run: frames [60%, 60%+3) reuse the content of frame 60%-1.
    const int64_t dupStart = totalFrames * 6 / 10;
    int64_t eff = frame;
    if (frame >= dupStart && frame < dupStart + 3) { eff = dupStart - 1; }
    // Single-frame white flash right after the middle hard cut.
    const int64_t flashFrame = segFrames * 2 + 1;
    if (frame == flashFrame) { out[0] = 242; out[1] = 242; out[2] = 242; return; }
    const int seg = static_cast<int>(eff / segFrames);
    renderCutSegment(seg, x, y, w, h, eff, out);
}

void renderUiTextBackground(int x, int y, int w, int h, int64_t frame, uint8_t out[3])
{
    const double u = static_cast<double>(x) / w;
    const double v = static_cast<double>(y) / h;
    const double scroll = std::fmod(u + static_cast<double>(frame) * 6.0 / w, 1.0);
    out[0] = static_cast<uint8_t>(40 + scroll * 150);
    out[1] = static_cast<uint8_t>(60 + v * 90);
    out[2] = static_cast<uint8_t>(180 - scroll * 120);
}

void renderUiText(int x, int y, int w, int h, int64_t frame, uint8_t out[3])
{
    // Fixed text-like block: rows of white bars with deterministic widths.
    const int x0 = w * 10 / 100, x1 = w * 55 / 100;
    const int y0 = h * 15 / 100, y1 = h * 85 / 100;
    if (x >= x0 && x < x1 && y >= y0 && y < y1) {
        const int lineH = h / 26;
        const int gap = h / 78;
        const int localY = y - y0;
        const int inLine = localY % (lineH + gap);
        if (inLine < lineH) {
            const int line = localY / (lineH + gap);
            const double frac = 0.35 + 0.6 * detHash01(static_cast<uint32_t>(line), 7u);
            const int barW = static_cast<int>(frac * (x1 - x0));
            if (x - x0 < barW || (line < 3 && x - x0 < (x1 - x0))) {
                out[0] = 245; out[1] = 245; out[2] = 245;
                return;
            }
        }
    }
    renderUiTextBackground(x, y, w, h, frame, out);
}

void renderScenarioPixel(Scenario s, int x, int y, int w, int h, int64_t totalFrames, int64_t frame, uint8_t out[3])
{
    switch (s) {
    case Scenario::Translation: renderTranslation(x, y, w, h, frame, out); return;
    case Scenario::Occlusion: renderOcclusion(x, y, w, h, totalFrames, frame, out); return;
    case Scenario::CutFlashDuplicate: renderCutFlashDuplicate(x, y, w, h, totalFrames, frame, out); return;
    case Scenario::UiText: renderUiText(x, y, w, h, frame, out); return;
    case Scenario::Particles: renderUiTextBackground(x, y, w, h, frame, out); return; // bg only; blobs drawn after
    }
}

// ---------------------------------------------------------------------------
// Full-frame RGB rendering (particles drawn as overlay pass).
// ---------------------------------------------------------------------------

void renderFrameRgb(Scenario s, int64_t frame, int64_t totalFrames, int w, int h, std::vector<uint8_t>& rgb)
{
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t px[3];
            renderScenarioPixel(s, x, y, w, h, totalFrames, frame, px);
            uint8_t* d = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            d[0] = px[0]; d[1] = px[1]; d[2] = px[2];
        }
    }
    if (s != Scenario::Particles) { return; }

    // Translucent bright blobs over the dark background (alpha blend 0.55).
    const double scale = static_cast<double>(h) / 1080.0;
    const int count = 48;
    for (int i = 0; i < count; ++i) {
        const double bx = detHash01(static_cast<uint32_t>(i), 1u) * w;
        const double by = detHash01(static_cast<uint32_t>(i), 2u) * h;
        const double dir = detHash01(static_cast<uint32_t>(i), 3u) > 0.5 ? 1.0 : -1.0;
        const double speed = (2.0 + 5.0 * detHash01(static_cast<uint32_t>(i), 4u)) * scale;
        const double cxp = std::fmod(bx + dir * speed * static_cast<double>(frame) + w, w);
        const double cyp = std::fmod(by + std::sin(static_cast<double>(i) + frame * 0.01) * 40.0 * scale + h, h);
        const double radius = (24.0 + detHash01(static_cast<uint32_t>(i), 5u) * 72.0) * scale;
        const uint8_t cr = static_cast<uint8_t>(120 + detHash01(static_cast<uint32_t>(i), 6u) * 130);
        const uint8_t cg = static_cast<uint8_t>(120 + detHash01(static_cast<uint32_t>(i), 7u) * 130);
        const uint8_t cb = static_cast<uint8_t>(120 + detHash01(static_cast<uint32_t>(i), 8u) * 130);
        const int x0 = std::max(0, static_cast<int>(cxp - radius));
        const int x1 = std::min(w - 1, static_cast<int>(cxp + radius));
        const int y0 = std::max(0, static_cast<int>(cyp - radius));
        const int y1 = std::min(h - 1, static_cast<int>(cyp + radius));
        const double r2 = radius * radius;
        for (int yy = y0; yy <= y1; ++yy) {
            for (int xx = x0; xx <= x1; ++xx) {
                const double dx = xx - cxp, dy = yy - cyp;
                const double d2 = dx * dx + dy * dy;
                if (d2 >= r2) { continue; }
                const double fall = 1.0 - std::sqrt(d2) / radius; // 1 center -> 0 edge
                const double a = 0.55 * fall;
                uint8_t* p = &rgb[(static_cast<size_t>(yy) * w + xx) * 3];
                p[0] = static_cast<uint8_t>(p[0] * (1.0 - a) + cr * a);
                p[1] = static_cast<uint8_t>(p[1] * (1.0 - a) + cg * a);
                p[2] = static_cast<uint8_t>(p[2] * (1.0 - a) + cb * a);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RGB -> YUV420P (BT.601 limited), 2x2 chroma averaged. No swscale dependency.
// ---------------------------------------------------------------------------

void fillYuv420pFromRgb(AVFrame* frame, const std::vector<uint8_t>& rgb, int w, int h)
{
    for (int y = 0; y < h; ++y) {
        uint8_t* yRow = frame->data[0] + y * frame->linesize[0];
        const uint8_t* src = &rgb[static_cast<size_t>(y) * w * 3];
        for (int x = 0; x < w; ++x) {
            const double r = src[x * 3 + 0] / 255.0;
            const double g = src[x * 3 + 1] / 255.0;
            const double b = src[x * 3 + 2] / 255.0;
            const double y_ = 0.299 * r + 0.587 * g + 0.114 * b;
            yRow[x] = static_cast<uint8_t>(16 + y_ * 219);
        }
    }
    const int cw = w / 2, ch = h / 2;
    for (int cy = 0; cy < ch; ++cy) {
        uint8_t* uRow = frame->data[1] + cy * frame->linesize[1];
        uint8_t* vRow = frame->data[2] + cy * frame->linesize[2];
        for (int cx = 0; cx < cw; ++cx) {
            double rr = 0, gg = 0, bb = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t* p = &rgb[(static_cast<size_t>(cy * 2 + dy) * w + cx * 2 + dx) * 3];
                    rr += p[0] / 255.0; gg += p[1] / 255.0; bb += p[2] / 255.0;
                }
            }
            rr /= 4.0; gg /= 4.0; bb /= 4.0;
            const double u_ = -0.169 * rr - 0.331 * gg + 0.500 * bb;
            const double v_ = 0.500 * rr - 0.419 * gg - 0.081 * bb;
            uRow[cx] = static_cast<uint8_t>(128 + u_ * 224);
            vRow[cx] = static_cast<uint8_t>(128 + v_ * 224);
        }
    }
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

int encodeClip(const std::string& output, Scenario scenario, int width, int height, int fps, int seconds, int64_t& outFrames)
{
    AVFormatContext* formatContext = nullptr;
    avformat_alloc_output_context2(&formatContext, nullptr, nullptr, output.c_str());
    if (formatContext == nullptr) {
        std::fprintf(stderr, "clip-gen: avformat_alloc_output_context2 failed for %s\n", output.c_str());
        return 1;
    }
    const AVCodec* codec = avcodec_find_encoder_by_name("libopenh264");
    if (codec == nullptr) {
        std::fprintf(stderr, "clip-gen: libopenh264 encoder not found\n");
        return 2;
    }
    AVStream* stream = avformat_new_stream(formatContext, nullptr);
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (stream == nullptr || codecContext == nullptr) { return 3; }
    codecContext->width = width;
    codecContext->height = height;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext->time_base = { 1, fps };
    codecContext->framerate = { fps, 1 };
    codecContext->gop_size = fps;
    codecContext->max_b_frames = 0;
    codecContext->bit_rate = height >= 2160 ? 24'000'000 : 8'000'000;
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
            std::fprintf(stderr, "clip-gen: avio_open failed for %s\n", output.c_str());
            return 5;
        }
    }
    if (avformat_write_header(formatContext, nullptr) < 0) { return 5; }

    AVFrame* yuvFrame = av_frame_alloc();
    yuvFrame->format = AV_PIX_FMT_YUV420P;
    yuvFrame->width = width;
    yuvFrame->height = height;
    av_frame_get_buffer(yuvFrame, 32);
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    AVPacket* packet = av_packet_alloc();
    const int64_t totalFrames = static_cast<int64_t>(seconds) * fps;
    for (int64_t frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
        renderFrameRgb(scenario, frameIndex, totalFrames, width, height, rgb);
        fillYuv420pFromRgb(yuvFrame, rgb, width, height);
        yuvFrame->pts = frameIndex;
        if (avcodec_send_frame(codecContext, yuvFrame) < 0) { return 6; }
        for (;;) {
            const int result = avcodec_receive_packet(codecContext, packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) { break; }
            if (result < 0) { return 6; }
            av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (av_interleaved_write_frame(formatContext, packet) < 0) { return 7; }
        }
    }
    (void)avcodec_send_frame(codecContext, nullptr);
    for (;;) {
        const int result = avcodec_receive_packet(codecContext, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) { break; }
        if (result < 0) { return 6; }
        av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;
        av_interleaved_write_frame(formatContext, packet);
    }
    av_write_trailer(formatContext);
    outFrames = totalFrames;
    std::fprintf(stderr, "clip-gen: wrote %s (%lld frames, %ds@%d, %dx%d, %s)\n",
        output.c_str(), static_cast<long long>(totalFrames), seconds, fps, width, height, scenarioName(scenario));

    av_packet_free(&packet);
    av_frame_free(&yuvFrame);
    avcodec_free_context(&codecContext);
    if (!(formatContext->oformat->flags & AVFMT_NOFILE)) { avio_closep(&formatContext->pb); }
    avformat_free_context(formatContext);
    return 0;
}

std::string toForwardSlashes(std::string s)
{
    for (char& c : s) { if (c == '\\') { c = '/'; } }
    return s;
}

int makeCorpus(const std::string& dir)
{
    CreateDirectoryA(dir.c_str(), nullptr);
    const std::string corpusDir = dir + "/corpus";
    CreateDirectoryA(corpusDir.c_str(), nullptr);

    const struct { Scenario s; const char* name; } scenarios[] = {
        { Scenario::Translation, "translation" },
        { Scenario::Occlusion, "occlusion" },
        { Scenario::CutFlashDuplicate, "cut-flash-duplicate" },
        { Scenario::Particles, "particles" },
        { Scenario::UiText, "ui-text" },
    };
    const struct { int w, h; const char* suffix; } extents[] = {
        { 1920, 1080, "1080p60" },
        { 3840, 2160, "4k60" },
    };
    const int fps = 60;
    const int seconds = 10;

    std::string manifest;
    manifest += "{\n  \"generator\": \"veyra_clip_gen\",\n  \"generatorVersion\": 2,\n";
    manifest += "  \"pathBase\": \"manifest-dir\",\n  \"clips\": [\n";
    bool first = true;
    for (const auto& sc : scenarios) {
        for (const auto& ext : extents) {
            const std::string file = std::string(sc.name) + "_" + ext.suffix + ".mp4";
            const std::string path = corpusDir + "/" + file;
            int64_t frames = 0;
            const int rc = encodeClip(path, sc.s, ext.w, ext.h, fps, seconds, frames);
            if (rc != 0) {
                std::fprintf(stderr, "clip-gen: corpus generation failed rc=%d for %s\n", rc, path.c_str());
                return rc;
            }
            veyra::FileIdentity ident;
            veyra::IdentityError err = veyra::IdentityError::None;
            std::wstring wide(path.begin(), path.end());
            if (!veyra::computeFileIdentity(wide, ident, err) || ident.sha256Upper.empty()) {
                std::fprintf(stderr, "clip-gen: identity/hash failed for %s err=%d\n", path.c_str(), static_cast<int>(err));
                return 10;
            }
            if (!first) { manifest += ",\n"; }
            first = false;
            manifest += std::format("    {{\"scenario\": \"{}\", \"width\": {}, \"height\": {}, \"fps\": {}, "
                "\"durationSeconds\": {}, \"frames\": {}, \"path\": \"corpus/{}\", \"sizeBytes\": {}, "
                "\"sha256\": \"{}\", \"command\": \"veyra_clip_gen --scenario {} --width {} --height {} "
                "--fps {} --seconds {} --output {}\"}}",
                sc.name, ext.w, ext.h, fps, seconds, static_cast<long long>(frames), file,
                static_cast<unsigned long long>(ident.sizeBytes), ident.sha256Upper,
                sc.name, ext.w, ext.h, fps, seconds, toForwardSlashes(path));
        }
    }
    manifest += "\n  ]\n}\n";

    const std::string manifestPath = dir + "/corpus-manifest.json";
    FILE* f = nullptr;
    if (fopen_s(&f, manifestPath.c_str(), "wb") != 0 || f == nullptr) {
        std::fprintf(stderr, "clip-gen: cannot write %s\n", manifestPath.c_str());
        return 11;
    }
    std::fwrite(manifest.data(), 1, manifest.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "clip-gen: corpus manifest written: %s\n", manifestPath.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string output;
    std::string scenarioArg;
    std::string corpusDir;
    int seconds = 10;
    int fps = 60;
    int width = 1920;
    int height = 1080;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) output = argv[++i];
        else if (arg == "--scenario" && i + 1 < argc) scenarioArg = argv[++i];
        else if (arg == "--make-corpus" && i + 1 < argc) corpusDir = argv[++i];
        else if (arg == "--seconds" && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (arg == "--fps" && i + 1 < argc) fps = std::atoi(argv[++i]);
        else if (arg == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (arg == "--height" && i + 1 < argc) height = std::atoi(argv[++i]);
    }
    if (!corpusDir.empty()) { return makeCorpus(corpusDir); }
    Scenario scenario = Scenario::Translation;
    if (scenarioArg.empty() || !parseScenario(scenarioArg, scenario)) {
        std::fprintf(stderr, "clip-gen: --scenario required (translation|occlusion|cut-flash-duplicate|particles|ui-text) or --make-corpus <dir>\n");
        return 1;
    }
    if (output.empty()) {
        std::fprintf(stderr, "clip-gen: --output required\n");
        return 1;
    }
    int64_t frames = 0;
    return encodeClip(output, scenario, width, height, fps, seconds, frames);
}
