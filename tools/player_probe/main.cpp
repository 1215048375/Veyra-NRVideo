// veyra_player_probe — Phase 6 realtime engine probe (Playbook section 22).
// Full chain on one D3D12 device: FFmpeg demux/decode (software) -> NV12
// upload -> YuvToLinearRgb -> DLSS SR (bypass at 1:1) -> ParityEncode ->
// Feature 18 NR -> ParityDecode -> ScaleBlit to the SDR RGBA8 working frame
// -> NVOF per-pixel forward flow -> DLSSG 2X -> PresentSink flip-discard
// present of [generated, real]. Audio is a WASAPI shared event-mode renderer
// and the master clock. Scenario mode exercises play/pause, 10 seeks,
// window resize, NR/FG toggles and loop; endurance mode runs 4K30 and 4K60
// pacing passes. The normal path performs ZERO GPU->CPU readbacks.
//
// SYSTEM CONSTRAINTS (diagnosed 2026-09-04 on this machine; an injected
// D3D12 layer - consistent with third-party screen-capture software - makes
// CreateCommittedResource, Buffer::Map AND command-list Close fail after
// descriptor views exist, while the device itself keeps working):
//   1. Allocate ALL committed resources (and Map upload buffers) BEFORE the
//      first CreateShaderResourceView.
//   2. After views exist, never record CopyTextureRegion/CopyResource (they
//      poison Close). All per-frame data movement goes through compute
//      shaders (Nv12Upload, ScaleBlit).
//   3. The local experimental NR snippet cannot Evaluate on a command list
//      with a bound compute PSO/descriptor heaps; the NR evaluate gets its
//      own freshly-reset list.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/hwcontext_d3d12va.h>
}

#pragma warning(push, 0)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssg.h>
#pragma warning(pop)

#include "../nr_harness/harness_util.h"
#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/ngx/DlssFgBackend.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/DlssSrBackend.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/NvOfSession.h"

namespace {

using veyra::gfx::ComPtr;
namespace utilns = veyra::harness::util;
using utilns::jsonEscape;
using utilns::writeTextFileUtf8;

struct EngineMetrics {
    uint64_t presentCount = 0;
    uint64_t fgGeneratedFrames = 0;
    uint64_t realFramesPresented = 0;
    uint64_t normalPathReadbackCount = 0;
    uint64_t nrEvaluateCount = 0;
    uint64_t srEvaluateCount = 0;
    uint64_t nvofExecuteCount = 0;
    double maxAvDriftMs = 0.0;
    int64_t maxInFlight = 0;
};

ComPtr<ID3D12Resource> makeTexture(ID3D12Device* device, uint32_t w, uint32_t h,
                                   DXGI_FORMAT fmt, bool uav)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    td.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> r;
    const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) {
        veyra::log::error("player", std::format("texture alloc failed {}x{} hr=0x{:X}",
            w, h, static_cast<unsigned>(hr)));
        return nullptr;
    }
    return r;
}

ComPtr<ID3D12Resource> makeUploadBuffer(ID3D12Device* device, uint64_t size)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = size; bd.Height = 1; bd.DepthOrArraySize = 1;
    bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) {
        veyra::log::error("player", std::format("upload buffer alloc failed size={} hr=0x{:X}",
            static_cast<uint64_t>(size), static_cast<unsigned>(hr)));
        return nullptr;
    }
    return r;
}

class StateTracker {
public:
    void set(ID3D12Resource* r, D3D12_RESOURCE_STATES s) { states_[r] = s; }
    D3D12_RESOURCE_STATES get(ID3D12Resource* r) const {
        const auto it = states_.find(r);
        return it != states_.end() ? it->second : D3D12_RESOURCE_STATE_COMMON;
    }
    void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                    D3D12_RESOURCE_STATES to) {
        const D3D12_RESOURCE_STATES from = get(r);
        if (from == to) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        set(r, to);
    }
    void uavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = r;
        list->ResourceBarrier(1, &b);
    }

private:
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states_;
};

// ---------------------------------------------------------------------------
// Audio: watermarked producer thread + event-driven WASAPI renderer with a
// PTS-mapped master clock (P0.1, user directive 2026-09-04 s4).
//
// decodeUntil previously compared ringMs() (buffer length, max ~2000ms)
// against an ABSOLUTE media-time target, so it decoded until ring overflow
// on every call (16974 overruns observed). The rewrite:
//   - one dedicated audio thread does BOTH decode production and the
//     event-driven WASAPI pump;
//   - production uses relative watermarks in buffered-duration units
//     (low 250ms / prefill 500ms / high 1000ms); the ring is bounded and
//     player mode NEVER drops decoded audio to "solve" overflow;
//   - after open/seek the thread prefills to the prefill watermark BEFORE
//     the renderer is started;
//   - seek is atomic: stop+reset WASAPI, flush demuxer/decoder/resampler,
//     clear ring, seek demuxer, drop decoded samples with PTS < target,
//     prefill, re-anchor the clock to the real PTS of the next sample,
//     start;
//   - IAudioClock device position is mapped onto real audio PTS via the
//     anchor (firstPtsAtStart + consumed duration), not wall-clock zero.
// ---------------------------------------------------------------------------

class AudioRenderer;  // forward: pipeline thread needs the renderer

constexpr double kAudioLowWatermarkMs = 250.0;
constexpr double kAudioPrefillMs = 500.0;
constexpr double kAudioHighWatermarkMs = 1000.0;
constexpr uint32_t kAudioRate = 48000;

class AudioPipeline {
public:
    ~AudioPipeline() { closeAll(); stopThread(); }

    bool open(const std::wstring& path)
    {
        std::string narrow;
        narrow.assign(path.begin(), path.end());
        if (avformat_open_input(&fmt_, narrow.c_str(), nullptr, nullptr) != 0) return false;
        if (avformat_find_stream_info(fmt_, nullptr) < 0) return false;
        const AVCodec* codec = nullptr;
        const int si = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (si < 0 || codec == nullptr) {
            veyra::log::info("audio", "no audio stream in source");
            return false;
        }
        streamIndex_ = si;
        codecCtx_ = avcodec_alloc_context3(codec);
        if (avcodec_parameters_to_context(codecCtx_, fmt_->streams[si]->codecpar) < 0) return false;
        if (avcodec_open2(codecCtx_, codec, nullptr) < 0) return false;
        stream_ = fmt_->streams[si];
        veyra::log::info("audio", std::format("audio stream idx={} codec={} rate={}",
            si, codec->name, codecCtx_->sample_rate));
        return true;
    }

    double bufferedMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return 1000.0 * static_cast<double>(ringFrames_) / kAudioRate;
    }

    // PTS (ms) of the first unconsumed buffered sample; < 0 when empty.
    double headPtsMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (segments_.empty()) return -1.0;
        return segments_.front().startPtsMs;
    }

    // Decoded-ahead PTS (ms) of the last buffered sample end; -1 if empty.
    double tailPtsMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (segments_.empty()) return -1.0;
        const Segment& t = segments_.back();
        return t.startPtsMs + 1000.0 * static_cast<double>(t.frames) / kAudioRate;
    }

    uint64_t underruns() const { return underruns_.load(); }
    uint64_t overruns() const { return overruns_.load(); }  // must stay 0
    uint64_t seekCount() const { return seekCount_.load(); }
    double lastPrefillMs() const { return lastPrefillMs_.load(); }
    double firstPtsAfterLastSeek() const { return firstPtsAfterSeek_.load(); }

    // Pull up to maxFrames stereo frames; sets the PTS of the first pulled
    // frame. Returns frames pulled (0 legal; caller writes silence and it is
    // counted as an underrun by the pump only when the endpoint had space).
    size_t pull(float* dst, size_t maxFrames, double* firstPtsMs)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (firstPtsMs) *firstPtsMs = segments_.empty() ? -1.0 : segments_.front().startPtsMs;
        const size_t take = std::min(maxFrames, ringFrames_);
        size_t copied = 0;
        while (copied < take && !segments_.empty()) {
            Segment& seg = segments_.front();
            const size_t n = std::min(take - copied, seg.frames);
            // Ring is a deque of float pairs: copy n frames.
            for (size_t i = 0; i < n * 2; ++i) {
                dst[copied * 2 + i] = ring_.front();
                ring_.pop_front();
            }
            copied += n;
            seg.frames -= n;
            seg.startPtsMs += 1000.0 * static_cast<double>(n) / kAudioRate;
            if (seg.frames == 0) segments_.pop_front();
        }
        ringFrames_ -= take;
        return take;
    }

    void stopThread()
    {
        stopFlag_ = true;
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    // ---- Seek protocol (engine thread calls; audio thread executes) ------
    // Blocks until the audio thread finished the atomic re-sequence and
    // prefilled. Returns the PTS of the first buffered sample after seek.
    double requestSeek(double targetMs)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        seekTargetMs_ = targetMs;
        seekDone_ = false;
        seekRequested_ = true;
        wake_.notify_all();
        // The audio thread signals when the ring is prefilled past target.
        if (!seekDoneCv_.wait_for(lock, std::chrono::milliseconds(3000), [this] { return seekDone_; })) {
            veyra::log::error("audio", "seek prefill timed out");
        }
        return segments_.empty() ? targetMs : segments_.front().startPtsMs;
    }

private:
    struct Segment {
        double startPtsMs;
        size_t frames;
    };

    // ---- Producer side (audio thread only; mutex for ring sharing) -------
    void pushDecoded(const AVFrame* frame)
    {
        // Convert to 48k stereo float in converted_.
        if (swr_ == nullptr) {
            AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
            AVChannelLayout inLayout = frame->ch_layout.nb_channels > 0
                ? frame->ch_layout : outLayout;
            swr_alloc_set_opts2(&swr_,
                &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate,
                &inLayout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
                0, nullptr);
            if (swr_ == nullptr || swr_init(swr_) < 0) return;
        }
        uint8_t* planes[1] = { reinterpret_cast<uint8_t*>(converted_.data()) };
        const int outSamples = swr_convert(swr_, planes,
            static_cast<int>(converted_.size() / 2),
            const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (outSamples <= 0) return;

        std::unique_lock<std::mutex> lock(mutex_);
        const double segStart = nextPtsMs_;
        const size_t addFrames = static_cast<size_t>(outSamples);
        if (ringFrames_ + addFrames > kMaxRingFrames) {
            // Player mode: never drop. This is a hard error (bounded ring
            // should never overflow because production is watermarked).
            overruns_.fetch_add(1);
            veyra::log::error("audio", "ring overflow despite watermarks (bug)");
            return;
        }
        ring_.insert(ring_.end(), converted_.data(), converted_.data() + addFrames * 2);
        ringFrames_ += addFrames;
        if (!segments_.empty()) {
            Segment& last = segments_.back();
            const double lastEnd = last.startPtsMs + 1000.0 * static_cast<double>(last.frames) / kAudioRate;
            if (std::fabs(lastEnd - nextPtsMs_) < 1.0) {
                last.frames += addFrames;      // contiguous: extend
            } else {
                segments_.push_back({ nextPtsMs_, addFrames });
            }
        } else {
            segments_.push_back({ nextPtsMs_, addFrames });
        }
        nextPtsMs_ += 1000.0 * static_cast<double>(addFrames) / kAudioRate;
        (void)segStart;
    }

    void decodeBlock()
    {
        // Decode until high watermark or EOF; called only when below high.
        while (bufferedMsLocked() < kAudioHighWatermarkMs && !stopFlag_ && !seekRequested_) {
            if (!havePacket_) {
                if (av_read_frame(fmt_, packet_) < 0) {
                    demuxEof_ = true;
                    break;
                }
                havePacket_ = true;
                if (packet_->stream_index != streamIndex_) {
                    av_packet_unref(packet_);
                    havePacket_ = false;
                    continue;
                }
            }
            if (avcodec_send_packet(codecCtx_, havePacket_ ? packet_ : nullptr) == 0 && havePacket_) {
                av_packet_unref(packet_);
                havePacket_ = false;
            }
            AVFrame* frame = av_frame_alloc();
            bool enough = false;
            while (avcodec_receive_frame(codecCtx_, frame) == 0) {
                const double ptsMs = 1000.0 * frame->pts * stream_->time_base.num / stream_->time_base.den;
                nextPtsMs_ = ptsMs + 1000.0 * frame->nb_samples / kAudioRate;
                if (discardUntilPtsMs_ < 0.0 || ptsMs >= discardUntilPtsMs_) {
                    pushDecoded(frame);
                }
                av_frame_unref(frame);
                if (bufferedMsLocked() >= kAudioHighWatermarkMs) { enough = true; break; }
            }
            av_frame_free(&frame);
            if (enough || demuxEof_) break;
        }
    }

    double bufferedMsLocked() const
    {
        return 1000.0 * static_cast<double>(ringFrames_) / kAudioRate;
    }

    void closeAll()
    {
        if (swr_ != nullptr) swr_free(&swr_);
        if (packet_ != nullptr) av_packet_free(&packet_);
        if (codecCtx_ != nullptr) avcodec_free_context(&codecCtx_);
        if (fmt_ != nullptr) avformat_close_input(&fmt_);
    }

    friend class AudioThread;

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = av_packet_alloc();
    SwrContext* swr_ = nullptr;
    int streamIndex_ = -1;
    bool havePacket_ = false;
    bool demuxEof_ = false;
    double nextPtsMs_ = 0.0;           // PTS of the NEXT decoded sample
    double discardUntilPtsMs_ = -1.0;  // seek pruning
    static constexpr size_t kMaxRingFrames = kAudioRate * 2; // 2s hard bound

    mutable std::mutex mutex_;
    std::deque<float> ring_{};         // stereo interleaved
    size_t ringFrames_ = 0;
    std::deque<Segment> segments_{};   // PTS bookkeeping of ring contents

    std::condition_variable wake_;
    std::condition_variable seekDoneCv_;
    std::atomic<bool> stopFlag_{false};
    bool seekRequested_ = false;
    bool seekDone_ = true;
    double seekTargetMs_ = 0.0;

    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<uint64_t> seekCount_{0};
    std::atomic<double> lastPrefillMs_{0.0};
    std::atomic<double> firstPtsAfterSeek_{-1.0};
    std::vector<float> converted_{std::vector<float>(kAudioRate)};

public:
    void runOnAudioThread(AudioRenderer* renderer);  // defined after renderer
private:
    std::thread thread_;
public:
    void startThread(AudioRenderer* renderer)
    {
        thread_ = std::thread(&AudioPipeline::runOnAudioThread, this, renderer);
    }
};

// ---------------------------------------------------------------------------
// WASAPI renderer: event-driven shared mode; device clock mapped onto media
// PTS through an explicit anchor set at Start and after every seek restart.
// ---------------------------------------------------------------------------
class AudioRenderer {
public:
    bool start()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInited_ = SUCCEEDED(hr);
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enum_));
        if (FAILED(hr)) return false;
        hr = enum_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (FAILED(hr)) return false;
        hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&client_));
        if (FAILED(hr)) return false;
        WAVEFORMATEX* mix = nullptr;
        if (FAILED(client_->GetMixFormat(&mix))) return false;
        sampleRate_ = mix->nSamplesPerSec;
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            10 * 10000, 0, mix, nullptr);
        const bool initOk = SUCCEEDED(hr);
        CoTaskMemFree(mix);
        if (!initOk) return false;
        if (FAILED(client_->GetBufferSize(&bufferFrames_))) return false;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event_ == nullptr || FAILED(client_->SetEventHandle(event_))) return false;
        if (FAILED(client_->GetService(__uuidof(IAudioRenderClient),
                reinterpret_cast<void**>(&render_)))) return false;
        if (FAILED(client_->GetService(__uuidof(IAudioClock),
                reinterpret_cast<void**>(&clock_)))) return false;
        UINT64 freq = 0;
        if (SUCCEEDED(clock_->GetFrequency(&freq))) clockFrequency_ = freq;
        running_ = true;
        veyra::log::info("audio", std::format("renderer opened {}Hz event-mode buffer={} frames (not started; prefill first)",
            sampleRate_, bufferFrames_));
        return true;
    }

    // Called by the audio thread after prefill: begins playback anchored at
    // the PTS of the sample that will be written first.
    bool startAnchored(double firstBufferPtsMs)
    {
        // Prefill the whole endpoint buffer with REAL samples happens on the
        // first pump() call; anchor NOW at the device position with the PTS
        // of the first sample we are about to submit.
        UINT64 pos = 0, qpc = 0;
        (void)clock_->GetPosition(&pos, &qpc);
        anchorPos_ = pos;
        anchorPtsMs_.store(firstBufferPtsMs);
        if (FAILED(client_->Start())) return false;
        started_ = true;
        veyra::log::info("audio", std::format("renderer STARTED anchored ptsMs={:.1f} devicePos={} freq={}",
            firstBufferPtsMs, pos, clockFrequency_));
        return true;
    }

    // Event-driven pump for ONE event cycle. Writes real data when the ring
    // has it, silence otherwise (underrun counted). Returns false on hard
    // failure.
    bool pumpOnce(AudioPipeline& pipeline, double* firstWrittenPtsMs)
    {
        if (!running_ || !started_) return true;
        if (WaitForSingleObject(event_, 50) != WAIT_OBJECT_0) return true;
        UINT32 padding = 0;
        if (FAILED(client_->GetCurrentPadding(&padding))) return false;
        const UINT32 avail = bufferFrames_ - padding;
        if (avail == 0) return true;
        double firstPts = -1.0;
        const size_t got = pipeline.pull(chunk_.data(), avail, &firstPts);
        if (firstWrittenPtsMs) *firstWrittenPtsMs = firstPts;
        BYTE* dest = nullptr;
        if (FAILED(render_->GetBuffer(avail, &dest))) return false;
        if (got > 0) {
            std::memcpy(dest, chunk_.data(), got * 8);
            if (got < avail) {
                std::memset(dest + got * 8, 0, (avail - got) * 8);
                underruns_.fetch_add(1);
            }
        } else {
            std::memset(dest, 0, static_cast<size_t>(avail) * 8);
            underruns_.fetch_add(1);
        }
        (void)render_->ReleaseBuffer(avail, 0);
        framesWritten_ += avail;
        return true;
    }

    // Master clock: real media PTS of the sample currently being played.
    double mediaTimeMs() const
    {
        if (!running_ || !started_) return anchorPtsMs_.load();
        UINT64 pos = 0, qpc = 0;
        if (FAILED(clock_->GetPosition(&pos, &qpc)) || clockFrequency_ == 0) {
            return anchorPtsMs_.load();
        }
        const double consumedMs = 1000.0 * static_cast<double>(pos - anchorPos_) /
            static_cast<double>(clockFrequency_);
        return anchorPtsMs_.load() + consumedMs;
    }

    // Atomic seek support: stop + reset the endpoint; the clock is invalid
    // until the next startAnchored.
    void stopAndReset()
    {
        if (!running_) return;
        (void)client_->Stop();
        (void)client_->Reset();
        started_ = false;
        framesWritten_ = 0;
    }

    bool started() const { return started_; }
    uint64_t underruns() const { return underruns_.load(); }
    uint64_t framesWritten() const { return framesWritten_; }

    void shutdown()
    {
        if (!running_) return;
        (void)client_->Stop();
        (void)client_->Reset();
        #define REL(x) if (x) { x->Release(); x = nullptr; }
        REL(clock_); REL(render_); REL(client_); REL(device_); REL(enum_);
        #undef REL
        if (event_ != nullptr) { CloseHandle(event_); event_ = nullptr; }
        running_ = false;
        if (comInited_) CoUninitialize();
    }

private:
    IMMDeviceEnumerator* enum_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    IAudioClock* clock_ = nullptr;
    HANDLE event_ = nullptr;
    UINT32 bufferFrames_ = 0;
    uint32_t sampleRate_ = kAudioRate;
    UINT64 clockFrequency_ = 0;
    UINT64 anchorPos_ = 0;
    std::atomic<double> anchorPtsMs_{0.0};
    bool started_ = false;
    bool running_ = false;
    bool comInited_ = false;
    std::atomic<uint64_t> underruns_{0};
    uint64_t framesWritten_ = 0;
    std::vector<float> chunk_{std::vector<float>(8192 * 2)};
};

// The audio thread: prefill -> start -> { event pump + watermark decode }.
inline void AudioPipeline::runOnAudioThread(AudioRenderer* renderer)
{
    const auto t0 = std::chrono::steady_clock::now();
    // Initial prefill (open case).
    {
        std::unique_lock<std::mutex> lock(mutex_);
        seekRequested_ = false;
        discardUntilPtsMs_ = -1.0;
    }
    decodeBlock();
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (segments_.empty()) {
            veyra::log::warn("audio", "no audio decoded at startup");
        }
    }
    const double firstPts = headPtsMs();
    if (renderer != nullptr && firstPts >= 0.0) {
        renderer->startAnchored(firstPts);
    }
    lastPrefillMs_.store(std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count() * 1000.0);
    veyra::log::info("audio", std::format("startup prefill done in {:.0f}ms firstPtsMs={:.1f} bufferedMs={:.0f}",
        lastPrefillMs_.load(), firstPts, bufferedMs()));

    while (!stopFlag_.load()) {
        // Seek request? Atomic re-sequence.
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (seekRequested_) {
                seekRequested_ = false;
                const double target = seekTargetMs_;
                seekCount_.fetch_add(1);
                lock.unlock();
                if (renderer != nullptr) renderer->stopAndReset();
                {
                    std::lock_guard<std::mutex> l2(mutex_);
                    ring_.clear();
                    ringFrames_ = 0;
                    segments_.clear();
                    if (havePacket_) { av_packet_unref(packet_); havePacket_ = false; }
                    avcodec_flush_buffers(codecCtx_);
                    const int64_t tbTarget = static_cast<int64_t>(
                        target * stream_->time_base.den / 1000 / stream_->time_base.num);
                    avformat_seek_file(fmt_, streamIndex_, INT64_MIN, tbTarget, INT64_MAX, 0);
                    demuxEof_ = false;
                    discardUntilPtsMs_ = target;
                }
                decodeBlock();  // drops pts < target, fills to high watermark
                double startPts = headPtsMs();
                if (startPts < 0.0) startPts = target;
                firstPtsAfterSeek_.store(startPts);
                if (renderer != nullptr) renderer->startAnchored(startPts);
                {
                    std::lock_guard<std::mutex> l2(mutex_);
                    discardUntilPtsMs_ = -1.0;
                    seekDone_ = true;
                }
                seekDoneCv_.notify_all();
                veyra::log::info("audio", std::format("seek done targetMs={:.0f} startPtsMs={:.1f} bufferedMs={:.0f}",
                    target, startPts, bufferedMs()));
                continue;
            }
        }
        // Regular cycle: pump the endpoint, then top up below high watermark.
        if (renderer != nullptr) {
            double firstPts = -1.0;
            if (!renderer->pumpOnce(*this, &firstPts)) {
                veyra::log::error("audio", "pumpOnce failed");
                break;
            }
        }
        if (bufferedMs() < kAudioHighWatermarkMs) {
            decodeBlock();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (demuxEof_ && bufferedMs() < 1.0) {
            // End of media: park (engine loops the clip via its own seek).
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// ---------------------------------------------------------------------------
// Compute pass helper (8 constants + SRV table + UAV table; single heap).
// ---------------------------------------------------------------------------
struct ComputePass {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT increment = 0;

    bool loadShader(const char* name, std::vector<uint8_t>& bytes) const
    {
        const std::string path = std::string(VEYRA_SHADER_DIR "/") + name;
        HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            veyra::log::error("player", std::format("shader missing: {}", path));
            return false;
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(f, &sz);
        bytes.resize(static_cast<size_t>(sz.QuadPart));
        DWORD read = 0;
        ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
        CloseHandle(f);
        return !bytes.empty();
    }

    bool create(ID3D12Device* device, const std::vector<uint8_t>& cs, UINT heapSlots,
                UINT srvCount = 3, UINT uavCount = 1)
    {
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = srvCount;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = uavCount;
        uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_ROOT_PARAMETER1 rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[0].Constants.ShaderRegister = 0;
        rp[0].Constants.Num32BitValues = 8;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[2].DescriptorTable.NumDescriptorRanges = 1;
        rp[2].DescriptorTable.pDescriptorRanges = &uavRange;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd{};
        rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rd.Desc_1_1.NumParameters = 3;
        rd.Desc_1_1.pParameters = rp;
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rd, &sig, &err))) return false;
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig.Get();
        pd.CS.pShaderBytecode = cs.data();
        pd.CS.BytecodeLength = cs.size();
        if (FAILED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = heapSlots;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    void bind(ID3D12GraphicsCommandList* list, const float constants[8],
              uint64_t srvGpu, uint64_t uavGpu) const
    {
        ID3D12DescriptorHeap* heaps[] = { heap.Get() };
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(rootSig.Get());
        list->SetPipelineState(pso.Get());
        list->SetComputeRoot32BitConstants(0, 8, constants, 0);
        const D3D12_GPU_DESCRIPTOR_HANDLE srv{ srvGpu };
        const D3D12_GPU_DESCRIPTOR_HANDLE uav{ uavGpu };
        list->SetComputeRootDescriptorTable(1, srv);
        list->SetComputeRootDescriptorTable(2, uav);
    }
};

bool loadShaderBytes(const char* name, std::vector<uint8_t>& bytes)
{
    const std::string path = std::string(VEYRA_SHADER_DIR "/") + name;
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        veyra::log::error("player", std::format("shader missing: {}", path));
        return false;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    bytes.resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(f);
    return !bytes.empty();
}

void makeSrv(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT fmt,
             const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = fmt;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(resource, &srv, handle);
}

void makeUav(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT fmt,
             const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = fmt;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
}

// Driver workaround (evidenced by bare-present stages 5-9, 2026-09-04):
// on this driver (RTX 5070, 616.56), CreateShaderResourceView writing
// directly into a SHADER-VISIBLE CBV_SRV_UAV heap corrupts the flip-model
// Present path (fabricated DXGI_ERROR_DEVICE_REMOVED, removedReason
// DXGI_ERROR_INVALID_CALL, no DRED). Creating the SRV in a NON-shader-
// visible staging heap and CopyDescriptorsSimple into the visible heap is
// proven safe (stage 9: 600/600 presents). UAVs/CBVs direct into visible
// heaps are safe (stages 7/8) and stay direct.
class DescriptorStager {
public:
    bool initialize(ID3D12Device* device, UINT slots)
    {
        device_ = device;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = slots;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)))) return false;
        increment_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        capacity_ = slots;
        return true;
    }

    // Creates an SRV for `resource` via the staging heap and copies it into
    // `targetSlot` of `visibleHeap`. srvDesc may be null for defaults.
    void stageSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc,
                  ID3D12DescriptorHeap* visibleHeap, UINT targetSlot)
    {
        const UINT stagingSlot = next_ % capacity_;
        next_ = (next_ + 1) % capacity_;
        const D3D12_CPU_DESCRIPTOR_HANDLE staging{
            heap_->GetCPUDescriptorHandleForHeapStart().ptr + stagingSlot * increment_ };
        device_->CreateShaderResourceView(resource, srvDesc, staging);
        const D3D12_CPU_DESCRIPTOR_HANDLE dst{
            visibleHeap->GetCPUDescriptorHandleForHeapStart().ptr + targetSlot * increment_ };
        device_->CopyDescriptorsSimple(1, dst, staging, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    UINT increment() const { return increment_; }

private:
    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> heap_;
    UINT increment_ = 0;
    UINT capacity_ = 0;
    UINT next_ = 0;
};

// Graphics present pass: fullscreen triangle blit onto a flip back buffer
// (flip buffers may only transition PRESENT <-> RENDER_TARGET).
struct GraphicsPass {
    veyra::gfx::ComPtr<ID3D12RootSignature> rootSig;
    veyra::gfx::ComPtr<ID3D12PipelineState> pso;
    veyra::gfx::ComPtr<ID3D12DescriptorHeap> heap;
    UINT increment = 0;

    bool create(ID3D12Device* device, const std::vector<uint8_t>& vs,
                const std::vector<uint8_t>& ps, UINT heapSlots)
    {
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER1 rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[0].Constants.ShaderRegister = 0;
        rp[0].Constants.Num32BitValues = 8;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srvRange;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd{};
        rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rd.Desc_1_1.NumParameters = 2;
        rd.Desc_1_1.pParameters = rp;
        rd.Desc_1_1.NumStaticSamplers = 1;
        rd.Desc_1_1.pStaticSamplers = &sampler;
        veyra::gfx::ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rd, &sig, &err))) return false;
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig.Get();
        pd.VS.pShaderBytecode = vs.data();
        pd.VS.BytecodeLength = vs.size();
        pd.PS.pShaderBytecode = ps.data();
        pd.PS.BytecodeLength = ps.size();
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.SampleDesc.Count = 1;
        if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = heapSlots;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }
};

} // namespace

namespace {
const char chrQ = '"';
const wchar_t chrWHelper() { return L'W'; } // unused placeholder
const std::wstring& absRuntimeBare()
{
    static std::wstring cached = [] {
        wchar_t buf[MAX_PATH * 2]{};
        GetFullPathNameW(L"runtime_local\\nvidia", MAX_PATH * 2, buf, nullptr);
        return std::wstring(buf);
    }();
    return cached;
}
} // namespace

namespace {
// s10 diagnostic: the 0x87D teardown exception under the D3D12 debug layer
// has no local dump (cdb/WER-LocalDumps unavailable without admin). This SEH
// wrapper converts it into logged evidence: exception code, faulting address
// and owning module. The run STILL FAILS when it fires - nothing is excused.
DWORD g_sehCode = 0;
void* g_sehAddr = nullptr;
char g_sehMod[MAX_PATH] = {};
int sehFilter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord) {
        g_sehCode = ep->ExceptionRecord->ExceptionCode;
        g_sehAddr = ep->ExceptionRecord->ExceptionAddress;
        HMODULE m = nullptr;
        if (g_sehAddr &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(g_sehAddr), &m) && m) {
            wchar_t wpath[MAX_PATH]{};
            GetModuleFileNameW(m, wpath, MAX_PATH);
            size_t k = 0;
            for (; wpath[k] && k < MAX_PATH - 1; ++k) g_sehMod[k] = static_cast<char>(wpath[k]);
            g_sehMod[k] = 0;
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool shutdownSinkSeh(veyra::gfx::PresentSink& sink)
{
    __try {
        sink.shutdown();
        return true;
    }
    __except (sehFilter(GetExceptionInformation())) {
        return false;
    }
}
} // namespace

int main(int argc, char** argv)
{
    std::wstring input, runtimeDir = L"runtime_local\\nvidia", logFile, jsonFile;
    std::string runId = "player-probe";
    bool endurance = false;
    int durationSeconds = 300;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        auto wvalue = [&]() -> std::wstring { const std::string v = value(); return std::wstring(v.begin(), v.end()); };
        if (arg == "--input") input = wvalue();
        else if (arg == "--runtime-dir") runtimeDir = wvalue();
        else if (arg == "--log-file") logFile = wvalue();
        else if (arg == "--json-file") jsonFile = wvalue();
        else if (arg == "--run-id") runId = value();
        else if (arg == "--endurance") endurance = true;
        else if (arg == "--duration-seconds") durationSeconds = std::atoi(value().c_str());
        else { std::fprintf(stderr, "unknown arg %s\n", arg.c_str()); return 2; }
    }
    if (input.empty()) { std::fprintf(stderr, "--input required\n"); return 2; }
    if (!logFile.empty()) (void)veyra::Logger::instance().openFile(logFile);

    bool playPauseWorks = false, seekWorks = false, resizeWorks = false;
    bool fgToggleWorks = false, nrToggleWorks = false, srToggleWorks = false;
    int seekCount = 0;
    EngineMetrics metrics;
    uint64_t audioUnderruns = 0, audioOverruns = 0;
    double g_audioEndBufferedMs = 0.0, g_audioEndHeadPtsMs = -1.0, g_audioEndClockPtsMs = -1.0;
    double driftMinMs = 0.0, driftP50Ms = 0.0, driftP95Ms = 1e9, driftP99Ms = 0.0, driftMaxMs = 0.0;
    uint64_t latenessSampleCount = 0;
    uint64_t g_droppedSourceFrames = 0, g_droppedGeneratedFrames = 0;
    std::string mvecSource = "nvof";
    uint64_t droppedLatePresents = 0;
    bool g_d3dDiagEnabled = false;
    ID3D12InfoQueue* g_d3dDiagQueue = nullptr;
    uint64_t g_d3dDiagErrors = 0, g_d3dDiagCorruption = 0;
    uint64_t g_d3dDiagWarnings = 0, g_d3dDiagInfo = 0;
    bool g_diagRetrievalComplete = true;
    UINT64 g_diagStoredRuntime = 0, g_diagRetrievedRuntime = 0, g_diagRetrievalFailures = 0;
    UINT64 g_diagQueueCapacity = 0;
    bool g_diagQueueSaturated = false;
    UINT64 g_diagStartupStored = 0, g_diagTeardownStored = 0, g_diagTeardownRetrieved = 0;
    uint64_t g_diagTeardownErrors = 0, g_diagTeardownCorruption = 0;
    double g_audioLastPrefillMs = 0.0, g_audioFirstPtsAfterSeekMs = -1.0;
    uint64_t g_audioSeekCount = 0;
    uint64_t nvofFrameFailures = 0;
    uint64_t lastNvofSignal = 0;  // set inside the run scope; JSON uses it after
    uint64_t invalidDriftSamples = 0;  // legacy field (kept for schema compat)
    double end4k30Duration = 0, end4k60Duration = 0;
    uint64_t end4k60Fg = 0, end4k30Fg = 0;
    double end4k60Hz = 0, end4k30Hz = 0;
    double workingSetGrowthMB = 0;
    bool overall = false;

    PROCESS_MEMORY_COUNTERS memStart{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memStart, sizeof(memStart));

    veyra::media::FFmpegDemuxer demuxer;
    veyra::media::FFmpegVideoDecoder decoder;
    AudioPipeline audioPipe;
    AudioRenderer audio;
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::CommandSlotRing ring;
    veyra::gfx::PresentSink sink;
    veyra::ngx::NgxCoreHost coreHost;
    veyra::ngx::DlssSrBackend srBackend;
    veyra::ngx::DlssFgBackend fgBackend;
    veyra::ngx::DlssNrRuntimeAdapter nrAdapter;
    veyra::ngx::NvOfSession nvof;
    ComPtr<ID3D12Fence> nvofOutFence;
    NVSDK_NGX_Parameter* ngxParams = nullptr;
    NVSDK_NGX_Handle* nrHandle = nullptr;
    SwsContext* nv12Ctx = nullptr;
    HANDLE nvofOutEvent = nullptr;

    // s8 item 5 (hoisted to main scope for s10): parsed BEFORE the run
    // scope so device creation and the post-scope teardown scans both see it.
    // s8 item 5: explicit diagnostic switch. VEYRA_D3D_DIAG=1 enables the
    // D3D12 debug layer + GPU-Based Validation + synchronized queue
    // validation + DRED BEFORE device creation, and an InfoQueue scan at
    // engine end that fails the run on any ERROR/CORRUPTION.
    int d3dDiagLevel = 0;
    {
        char lv[8]{};
        GetEnvironmentVariableA("VEYRA_D3D_DIAG", lv, sizeof(lv));
        if (lv[0]) d3dDiagLevel = std::atoi(lv);
    }
    const bool d3dDiag = d3dDiagLevel >= 1;
    // s10-II item 1: ONE owning InfoQueue ComPtr at OUTER scope; it must
    // outlive the resource scope because the teardown/final scans run
    // after ring/swapchain shutdown, right before context.shutdown.
    ComPtr<ID3D12InfoQueue> d3dDiagQueue;
    std::unordered_map<std::string, uint64_t> diagHistogram;
    std::vector<std::string> diagErrorSamples, diagCorruptionSamples, diagWarningSamples;

    // ---- s10-II: reliable InfoQueue retrieval (one function, all phases)
    auto scanInfoQueue = [&](const char* phase,
                             uint64_t& outErr, uint64_t& outCorr,
                             uint64_t& outWarn, uint64_t& outInfo,
                             UINT64& outStored, UINT64& outRetrieved, UINT64& outFailures,
                             std::unordered_map<std::string, uint64_t>& hist,
                             std::vector<std::string>& errSamples,
                             std::vector<std::string>& corrSamples) -> bool {
        outErr = outCorr = outWarn = outInfo = 0;
        outStored = outRetrieved = outFailures = 0;
        hist.clear(); errSamples.clear(); corrSamples.clear();
        if (d3dDiagQueue.Get() == nullptr) return false;
        outStored = d3dDiagQueue->GetNumStoredMessages();
        std::vector<char> buf(4096);
        for (UINT64 k = 0; k < outStored; ++k) {
            SIZE_T len = buf.size();
            D3D12_MESSAGE* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (d3dDiagQueue->GetMessageW(k, m, &len) != S_OK) { ++outFailures; continue; }
            ++outRetrieved;
            switch (m->Severity) {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                ++outCorr;
                if (corrSamples.size() < 4) corrSamples.push_back(std::format("id={} {}",
                    static_cast<unsigned>(m->ID), m->pDescription ? m->pDescription : ""));
                break;
            case D3D12_MESSAGE_SEVERITY_ERROR:
                ++outErr;
                if (errSamples.size() < 4) errSamples.push_back(std::format("id={} {}",
                    static_cast<unsigned>(m->ID), m->pDescription ? m->pDescription : ""));
                break;
            case D3D12_MESSAGE_SEVERITY_WARNING: ++outWarn; break;
            case D3D12_MESSAGE_SEVERITY_INFO: ++outInfo; break;
            default: break;
            }
            hist[std::to_string(static_cast<unsigned>(m->ID))]++;
        }
        veyra::log::info("player", std::format(
            "diag({}): stored={} retrieved={} failures={} err={} corr={} warn={} info={}",
            phase, outStored, outRetrieved, outFailures, outErr, outCorr, outWarn, outInfo));
        return outFailures == 0 && outRetrieved == outStored;
    };

    auto stage = [](const char* phase) {
        veyra::log::info("teardown", std::string("before ") + phase);
        veyra::Logger::instance().flush();
    };
    auto stageDone = [](const char* phase) {
        veyra::log::info("teardown", std::string("after ") + phase);
        veyra::Logger::instance().flush();
    };

    do { // one scope; break = early exit with teardown at the end
        veyra::Status st = veyra::Status::Ok;
        if (!demuxer.open(input)) { veyra::log::error("player", "demuxer open failed"); break; }
        const int64_t durationUs = demuxer.durationUs();

        const bool hasAudio = audioPipe.open(input);
        if (!hasAudio) veyra::log::info("player", "no audio; explicit fallback clock required");

        // --- GPU context ----------------------------------------------------
        if (d3dDiag) {
            ComPtr<ID3D12Debug1> dbg1;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg1)))) {
                dbg1->EnableDebugLayer();
                if (d3dDiagLevel >= 2) {
                    dbg1->SetEnableGPUBasedValidation(TRUE);
                    dbg1->SetEnableSynchronizedCommandQueueValidation(TRUE);
                    veyra::log::info("player", "diag level 2: layer + GBV + sync queue validation ON");
                } else {
                    veyra::log::info("player", "diag level 1: layer + DRED ON (GBV off; see iso matrix note)");
                }
            }
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredS;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredS)))) {
                dredS->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dredS->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }
        veyra::gfx::DeviceContextDesc ddesc{};
        ddesc.enableDebugLayer = d3dDiag;
        ddesc.commandSlotCount = 4;
        if (!context.initialize(ddesc, st)) break;
        if (!ring.initialize(context.device(), context.directQueue(), context.fence(),
                context.fenceEvent(), 4, st)) break;

        // BARE-PRESENT isolation mode: no NGX, no views, no decode. Clears
        // the back buffer and presents; counts SUCCEEDED vs FAILED presents
        // (step-8 evidence gathering for the device-removed investigation).
        static const int bareStage = [] {
            char b[8]{};
            GetEnvironmentVariableA("VEYRA_BARE_STAGE", b, sizeof(b));
            return b[0] ? std::atoi(b) : 0;
        }();
        if (bareStage >= 1 || GetEnvironmentVariableW(L"VEYRA_BARE_PRESENT", nullptr, 0) != 0) {
            veyra::gfx::PresentSink bareSink;
            veyra::gfx::PresentSink::Desc bd{};
            bd.width = 1280; bd.height = 720;
            bd.vsync = GetEnvironmentVariableW(L"VEYRA_BARE_VSYNC", nullptr, 0) != 0;
            bd.title = L"Veyra Bare Present";
            if (!bareSink.initialize(context.device(), context.directQueue(), bd, st)) break;
            ComPtr<ID3D12DescriptorHeap> bareRtv;
            D3D12_DESCRIPTOR_HEAP_DESC rh{};
            rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rh.NumDescriptors = 3;
            if (FAILED(context.device()->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&bareRtv)))) break;
            const UINT inc = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            for (UINT i = 0; i < 3; ++i) {
                ComPtr<ID3D12Resource> bb;
                if (SUCCEEDED(bareSink.swapChain()->GetBuffer(i, IID_PPV_ARGS(&bb)))) {
                    context.device()->CreateRenderTargetView(bb.Get(), nullptr,
                        { bareRtv->GetCPUDescriptorHandleForHeapStart().ptr + i * inc });
                }
            }
            NVSDK_NGX_Parameter* bareParams = nullptr;
            veyra::ngx::DlssSrBackend bareSr;
            veyra::ngx::DlssFgBackend bareFg;
            veyra::ngx::DlssNrRuntimeAdapter bareNr;
            NVSDK_NGX_Handle* bareNrHandle = nullptr;
            if (bareStage >= 1) {
                wchar_t absRt[MAX_PATH * 2]{};
                GetFullPathNameW(runtimeDir.c_str(), MAX_PATH * 2, absRt, nullptr);
                std::ifstream ids2(std::wstring(absRt) + L"\\..\\config\\ngx-local.json", std::ios::binary);
                std::string idt((std::istreambuf_iterator<char>(ids2)), std::istreambuf_iterator<char>());
                std::string pid2, ev2;
                auto scanJson = [&idt](const char* key) -> std::string {
                    const char q = 0x22;
                    std::string nd;
                    nd += q; nd += key; nd += q;
                    size_t p = idt.find(nd);
                    if (p == std::string::npos) return std::string();
                    p = idt.find(q, idt.find(':', p + nd.size()));
                    if (p == std::string::npos) return std::string();
                    const size_t s = p + 1;
                    const size_t e = idt.find(q, s);
                    if (e == std::string::npos) return std::string();
                    return idt.substr(s, e - s);
                };
                pid2 = scanJson("ngxProjectId");
                ev2 = scanJson("engineVersion");
                if (pid2.empty() || !coreHost.initialize(context.device(), absRt,
                        pid2.c_str(), ev2.c_str(), st)) {
                    veyra::log::error("player", "bare stage1: core init failed");
                    break;
                }
                veyra::log::info("player", "bare stage1: NGX core initialized");
            }
            if (bareStage >= 2) {
                uint64_t r2 = 0; uint32_t s2 = 0;
                if (!bareNr.load(absRuntimeBare(), st) ||
                    !bareNr.installCallerCompatibility(st) ||
                    !bareNr.snippetInitExt(context.device(), absRuntimeBare(), r2, s2) ||
                    r2 != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                    veyra::log::error("player", std::format("bare stage2: snippet init failed 0x{:X}", r2));
                    break;
                }
                veyra::log::info("player", "bare stage2: NR snippet initialized");
            }
            if (bareStage >= 3) {
                veyra::ngx::DlssFgBackend::Capability capB{};
                const bool avail = bareFg.queryCapability(coreHost, capB, st);
                veyra::log::info("player", std::format("bare stage3: capability available={}", avail));
            }
            if (bareStage >= 4) {
                bareParams = coreHost.allocateParameters(st);
                ID3D12GraphicsCommandList* cl = ring.acquire(0, st);
                veyra::ngx::DlssFgBackend::CreateDesc fd2{};
                fd2.width = 1280; fd2.height = 720;
                fd2.renderWidth = 1280; fd2.renderHeight = 720;
                fd2.backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
                const bool fgOk = bareFg.create(coreHost, cl, bareParams, fd2, st);
                (void)ring.submitAndSignal(0);
                (void)ring.waitIdle();
                veyra::log::info("player", std::format("bare stage4: FG create ok={} result=0x{:X}",
                    fgOk, bareFg.createResult()));
            }
            if (bareStage == 5) {
                // Module evidence: list loaded modules NOT from known-safe
                // roots (Windows, our exe dir, veyra-deps, runtime_local).
                {
                    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
                    if (snap != INVALID_HANDLE_VALUE) {
                        MODULEENTRY32W me{};
                        me.dwSize = sizeof(me);
                        std::wstring selfDir;
                        wchar_t exeP[MAX_PATH * 2]{};
                        GetModuleFileNameW(nullptr, exeP, MAX_PATH * 2);
                        const std::wstring exeW(exeP);
                        const size_t slash = exeW.find_last_of(0x5C); // backslash
                        if (slash != std::wstring::npos) selfDir = exeW.substr(0, slash);
                        int foreign = 0;
                        if (Module32FirstW(snap, &me)) {
                            do {
                                const std::wstring p(me.szExePath);
                                const bool safe = (p.find(L"Windows\\") != std::wstring::npos)
                                    || (p.find(L"Windows") != std::wstring::npos && p.find(L"System32") != std::wstring::npos)
                                    || p.find(L"veyra-deps") != std::wstring::npos
                                    || p.find(L"runtime_local") != std::wstring::npos
                                    || (!selfDir.empty() && p.rfind(selfDir, 0) == 0);
                                if (!safe) {
                                    ++foreign;
                                    if (foreign <= 12) {
                                        veyra::log::warn("player", std::string("foreign-module: ")
                                            + std::string(me.szExePath, me.szExePath + wcslen(me.szExePath)));
                                    }
                                }
                            } while (Module32NextW(snap, &me));
                        }
                        CloseHandle(snap);
                        veyra::log::info("player", std::format("module-scan: foreign={} (listed up to 12)", foreign));
                    }
                }
                // A minimal descriptor view (heap + texture + SRV).
                D3D12_DESCRIPTOR_HEAP_DESC hh{};
                hh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                hh.NumDescriptors = 2;
                hh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ComPtr<ID3D12DescriptorHeap> heap5;
                ComPtr<ID3D12Resource> tex5 = makeTexture(context.device(), 64, 64,
                    DXGI_FORMAT_R8G8B8A8_UNORM, false);
                if (FAILED(context.device()->CreateDescriptorHeap(&hh, IID_PPV_ARGS(&heap5))) ||
                    tex5 == nullptr) break;
                makeSrv(context.device(), tex5.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { heap5->GetCPUDescriptorHandleForHeapStart() });
                veyra::log::info("player", "bare stage5: descriptor view created");

            if (bareStage == 6) {
                // SRV in a NON-shader-visible heap.
                D3D12_DESCRIPTOR_HEAP_DESC h6{};
                h6.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                h6.NumDescriptors = 1;
                h6.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                ComPtr<ID3D12DescriptorHeap> heap6;
                ComPtr<ID3D12Resource> tex6 = makeTexture(context.device(), 64, 64,
                    DXGI_FORMAT_R8G8B8A8_UNORM, false);
                if (FAILED(context.device()->CreateDescriptorHeap(&h6, IID_PPV_ARGS(&heap6))) ||
                    tex6 == nullptr) break;
                makeSrv(context.device(), tex6.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { heap6->GetCPUDescriptorHandleForHeapStart() });
                veyra::log::info("player", "bare stage6: non-shader-visible SRV created");
            }
            if (bareStage == 7) {
                // UAV (texture with ALLOW_UNORDERED_ACCESS) + view.
                D3D12_DESCRIPTOR_HEAP_DESC h7{};
                h7.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                h7.NumDescriptors = 1;
                h7.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ComPtr<ID3D12DescriptorHeap> heap7;
                ComPtr<ID3D12Resource> tex7 = makeTexture(context.device(), 64, 64,
                    DXGI_FORMAT_R8G8B8A8_UNORM, true);
                if (FAILED(context.device()->CreateDescriptorHeap(&h7, IID_PPV_ARGS(&heap7))) ||
                    tex7 == nullptr) break;
                makeUav(context.device(), tex7.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { heap7->GetCPUDescriptorHandleForHeapStart() });
                veyra::log::info("player", "bare stage7: shader-visible UAV created");
            }
            if (bareStage == 8) {
                // CBV over a small upload buffer.
                D3D12_DESCRIPTOR_HEAP_DESC h8{};
                h8.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                h8.NumDescriptors = 1;
                h8.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ComPtr<ID3D12DescriptorHeap> heap8;
                ComPtr<ID3D12Resource> buf8 = makeUploadBuffer(context.device(), 256);
                if (FAILED(context.device()->CreateDescriptorHeap(&h8, IID_PPV_ARGS(&heap8))) ||
                    buf8 == nullptr) break;
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
                cbv.BufferLocation = buf8->GetGPUVirtualAddress();
                cbv.SizeInBytes = 256;
                context.device()->CreateConstantBufferView(&cbv,
                    { heap8->GetCPUDescriptorHandleForHeapStart() });
                veyra::log::info("player", "bare stage8: shader-visible CBV created");

            if (bareStage == 9) {
                // WORKAROUND TEST: create the SRV in a NON-shader-visible
                // staging heap, then CopyDescriptors into a shader-visible
                // heap. If Present survives, this is the engine fix.
                D3D12_DESCRIPTOR_HEAP_DESC h9a{};
                h9a.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                h9a.NumDescriptors = 2;
                h9a.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                ComPtr<ID3D12DescriptorHeap> stage9Staging;
                ComPtr<ID3D12DescriptorHeap> stage9Visible;
                D3D12_DESCRIPTOR_HEAP_DESC h9b{};
                h9b.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                h9b.NumDescriptors = 2;
                h9b.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ComPtr<ID3D12Resource> tex9 = makeTexture(context.device(), 64, 64,
                    DXGI_FORMAT_R8G8B8A8_UNORM, false);
                if (FAILED(context.device()->CreateDescriptorHeap(&h9a, IID_PPV_ARGS(&stage9Staging))) ||
                    FAILED(context.device()->CreateDescriptorHeap(&h9b, IID_PPV_ARGS(&stage9Visible))) ||
                    tex9 == nullptr) break;
                makeSrv(context.device(), tex9.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { stage9Staging->GetCPUDescriptorHandleForHeapStart() });
                context.device()->CopyDescriptorsSimple(1,
                    { stage9Visible->GetCPUDescriptorHandleForHeapStart() },
                    { stage9Staging->GetCPUDescriptorHandleForHeapStart() },
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                veyra::log::info("player", "bare stage9: SRV staged then copied into shader-visible heap");

            }
            if (bareStage == 10 || bareStage == 11 || bareStage == 12) {
                veyra::log::info("player", "bare 10-12 block entered");
                // Common resources for the engine-step tests.
                ComPtr<ID3D12Resource> tex10 = makeTexture(context.device(), 1280, 720,
                    DXGI_FORMAT_R16G16B16A16_FLOAT, true);
                ComPtr<ID3D12Resource> out10 = makeTexture(context.device(), 1280, 720,
                    DXGI_FORMAT_R16G16B16A16_FLOAT, true);
                ComPtr<ID3D12Resource> m10 = makeTexture(context.device(), 1280, 720,
                    DXGI_FORMAT_R16G16B16A16_FLOAT, true); // motion (fmt irrelevant)
                ComPtr<ID3D12Resource> d10 = makeTexture(context.device(), 1280, 720,
                    DXGI_FORMAT_R32_FLOAT, false);
                if (!tex10 || !out10 || !m10 || !d10) break;
                NVSDK_NGX_Parameter* p10 = coreHost.allocateParameters(st);
                if (p10 == nullptr) break;

                // One DLSS SR evaluate (in: tex10 -> out10).
                {
                    veyra::ngx::DlssSrBackend sr10;
                    veyra::ngx::DlssSrBackend::CreateDesc cd{};
                    cd.inputWidth = 1280; cd.inputHeight = 720;
                    cd.outputWidth = 1280; cd.outputHeight = 720; // 1:1 bypass ok? need upscale
                    cd.outputWidth = 2560; cd.outputHeight = 1440;
                    cd.perfQuality = 1;
                    ComPtr<ID3D12Resource> outBig = makeTexture(context.device(), 2560, 1440,
                        DXGI_FORMAT_R16G16B16A16_FLOAT, true);
                    if (!outBig) break;
                    ID3D12GraphicsCommandList* l10 = ring.acquire(0, st);
                    if (l10 == nullptr) break;
                    const bool created10 = sr10.create(coreHost, l10, p10, cd, st);
                    (void)ring.submitAndSignal(0);
                    (void)ring.waitIdle();
                    veyra::log::info("player", std::format("bare stage10: SR create ok={} result=0x{:X}",
                        created10, sr10.createResult()));
                    if (created10) {
                        ID3D12GraphicsCommandList* l10b = ring.acquire(0, st);
                        D3D12_RESOURCE_BARRIER b10[2]{};
                        for (int k = 0; k < 2; ++k) {
                            b10[k].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            b10[k].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                        }
                        b10[0].Transition.pResource = tex10.Get();
                        b10[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                        b10[1].Transition.pResource = outBig.Get();
                        b10[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                        l10b->ResourceBarrier(2, b10);
                        veyra::ngx::DlssSrBackend::EvalDesc ed10{};
                        ed10.color = tex10.Get();
                        ed10.output = outBig.Get();
                        ed10.depth = d10.Get();
                        ed10.motionVectors = m10.Get();
                        ed10.reset = true;
                        const bool eval10 = sr10.evaluate(l10b, p10, ed10, st);
                        D3D12_RESOURCE_BARRIER back10[2]{};
                        for (int k = 0; k < 2; ++k) {
                            back10[k].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        }
                        back10[0].Transition.pResource = tex10.Get();
                        back10[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                        back10[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                        back10[1].Transition.pResource = outBig.Get();
                        back10[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                        back10[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                        l10b->ResourceBarrier(2, back10);
                        (void)ring.submitAndSignal(0);
                        (void)ring.waitIdle();
                        veyra::log::info("player", std::format("bare stage10: SR evaluate ok={}", eval10));
                    }
                }

                if (bareStage >= 11) {
                    // One NR snippet evaluate on a fresh list.
                    veyra::ngx::DlssNrRuntimeAdapter nr10;
                    uint64_t r10 = 0; uint32_t s10 = 0;
                    if (!nr10.load(absRuntimeBare(), st) ||
                        !nr10.installCallerCompatibility(st) ||
                        !nr10.snippetInitExt(context.device(), absRuntimeBare(), r10, s10) ||
                        r10 != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                        veyra::log::error("player", std::format("bare stage11: snippet init failed 0x{:X}", r10));
                        break;
                    }
                    ComPtr<ID3D12Resource> proxy10 = makeTexture(context.device(), 1280, 720,
                        DXGI_FORMAT_R8G8B8A8_UNORM, true);
                    ComPtr<ID3D12Resource> neural10 = makeTexture(context.device(), 1280, 720,
                        DXGI_FORMAT_R8G8B8A8_UNORM, true);
                    if (!proxy10 || !neural10) break;
                    ID3D12GraphicsCommandList* l11 = ring.acquire(0, st);
                    D3D12_RESOURCE_BARRIER b11[2]{};
                    for (int k = 0; k < 2; ++k) {
                        b11[k].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        b11[k].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    }
                    b11[0].Transition.pResource = proxy10.Get();
                    b11[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    b11[1].Transition.pResource = neural10.Get();
                    b11[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    l11->ResourceBarrier(2, b11);
                    NVSDK_NGX_Handle* h11 = nullptr;
                    {
                        namespace p = veyra::ngx::dlssnr;
                        veyra::ngx::ParameterBlock pb(p10);
                        pb.setU32(p::kWidth, 1280); pb.setU32(p::kHeight, 720);
                        pb.setU32(p::kInputWidth, 1280); pb.setU32(p::kInputHeight, 720);
                        pb.setU32(p::kOutputWidth, 1280); pb.setU32(p::kOutputHeight, 720);
                        pb.setU32(p::kOutputDotWidth, 1280); pb.setU32(p::kOutputDotHeight, 720);
                        pb.setU32(p::kUpscaling, 0);
                        pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
                        pb.setVoid(p::kComputeScalingRatioCallback,
                            reinterpret_cast<void*>(&veyra::ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
                        pb.setU32(p::kStdWidth, 1280); pb.setU32(p::kStdHeight, 720);
                        pb.setI32(p::kPerfQualityValue, 1);
                        pb.setU32(p::kCreationNodeMask, 1); pb.setU32(p::kVisibilityNodeMask, 1);
                        ID3D12GraphicsCommandList* lc = ring.acquire(1, st);
                        if (!nr10.snippetCreateFeature(lc, p10, &h11, r10, s10) ||
                            r10 != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                            veyra::log::error("player", std::format("bare stage11: NR create failed 0x{:X}", r10));
                            break;
                        }
                        (void)ring.submitAndSignal(1);
                        (void)ring.waitIdle();
                    }
                    namespace p = veyra::ngx::dlssnr;
                    veyra::ngx::ParameterBlock pb(p10);
                    pb.setD3D12Resource(p::kColor, proxy10.Get());
                    pb.setD3D12Resource(p::kOutput, neural10.Get());
                    pb.setD3D12Resource(p::kMVec, m10.Get());
                    pb.setD3D12Resource(p::kDepth, d10.Get());
                    pb.setU32(p::kColorSubrectWidth, 1280); pb.setU32(p::kColorSubrectHeight, 720);
                    pb.setU32(p::kOutputSubrectWidth, 1280); pb.setU32(p::kOutputSubrectHeight, 720);
                    pb.setU32(p::kMVecSubrectWidth, 1280); pb.setU32(p::kMVecSubrectHeight, 720);
                    pb.setU32(p::kDepthSubrectWidth, 1280); pb.setU32(p::kDepthSubrectHeight, 720);
                    pb.setI32(p::kEnabled, 1);
                    pb.setI32(p::kReset, 1);
                    uint64_t er11 = 0; uint32_t es11 = 0;
                    const bool ok11 = nr10.snippetEvaluateFeature(l11, h11, p10, er11, es11);
                    (void)ring.submitAndSignal(0);
                    (void)ring.waitIdle();
                    veyra::log::info("player", std::format("bare stage11: NR evaluate ok={} result=0x{:X} seh={}",
                        ok11, er11, es11));
                    if (h11 != nullptr) {
                        uint64_t rr = 0; uint32_t rs = 0;
                        (void)nr10.snippetReleaseFeature(h11, rr, rs);
                    }
                    nr10.restoreCallerCompatibility();
                    nr10.unload();
                }

                if (bareStage >= 12) {
                    // A real compute dispatch with a shader-visible heap
                    // bound (ScaleBlit 1:1 from tex-swap into out10).
                    ComputePass pass12;
                    std::vector<uint8_t> cs12;
                    if (!pass12.loadShader("ScaleBlit.dxil", cs12) ||
                        !pass12.create(context.device(), cs12, 4)) break;
                    DescriptorStager stager12;
                    if (!stager12.initialize(context.device(), 8)) break;
                    D3D12_SHADER_RESOURCE_VIEW_DESC s12{};
                    s12.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    s12.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    s12.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    stager12.stageSrv(tex10.Get(), &s12, pass12.heap.Get(), 0);
                    {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC u12{};
                        u12.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                        u12.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        context.device()->CreateUnorderedAccessView(out10.Get(), nullptr, &u12,
                            { pass12.heap->GetCPUDescriptorHandleForHeapStart().ptr + pass12.increment });
                    }
                    ID3D12GraphicsCommandList* l12 = ring.acquire(0, st);
                    D3D12_RESOURCE_BARRIER b12[2]{};
                    for (int k = 0; k < 2; ++k) {
                        b12[k].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        b12[k].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    }
                    b12[0].Transition.pResource = tex10.Get();
                    b12[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    b12[1].Transition.pResource = out10.Get();
                    b12[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    l12->ResourceBarrier(2, b12);
                    const float c12[8] = { 1280, 720, 1280, 720, 0, 0, 0, 0 };
                    pass12.bind(l12, c12, pass12.heap->GetGPUDescriptorHandleForHeapStart().ptr,
                        pass12.heap->GetGPUDescriptorHandleForHeapStart().ptr + pass12.increment);
                    l12->Dispatch(80, 45, 1);
                    D3D12_RESOURCE_BARRIER back12[2]{};
                    for (int k = 0; k < 2; ++k) {
                        back12[k].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    }
                    back12[0].Transition.pResource = tex10.Get();
                    back12[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    back12[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    back12[1].Transition.pResource = out10.Get();
                    back12[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    back12[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    l12->ResourceBarrier(2, back12);
                    (void)ring.submitAndSignal(0);
                    (void)ring.waitIdle();
                    veyra::log::info("player", "bare stage12: compute dispatch with visible heap done");
                }
            }
            }
            }
            if (bareStage == 13) {
                char m13[8]{};
                GetEnvironmentVariableA("VEYRA_BARE13", m13, sizeof(m13));
                const int mask = m13[0] ? std::atoi(m13) : 0;
                ComPtr<ID3D12Resource> texA;
                if (mask & 1) {
                    texA = makeTexture(context.device(), 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, true);
                    if (!texA) break;
                    veyra::log::info("player", "bare13: textures on");
                }
                if ((mask & 2) && texA) {
                    // committed upload + CopyTextureRegion, engine-style.
                    ComPtr<ID3D12Resource> up = makeUploadBuffer(context.device(), 2048 * 4 * 64);
                    if (!up) break;
                    ID3D12GraphicsCommandList* lc = ring.acquire(0, st);
                    D3D12_RESOURCE_BARRIER b{};
                    b.Transition.pResource = texA.Get();
                    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    lc->ResourceBarrier(1, &b);
                    D3D12_TEXTURE_COPY_LOCATION d{}, sc{};
                    d.pResource = texA.Get();
                    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    sc.pResource = up.Get();
                    sc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    sc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    sc.PlacedFootprint.Footprint.Width = 1920;
                    sc.PlacedFootprint.Footprint.Height = 64;
                    sc.PlacedFootprint.Footprint.Depth = 1;
                    sc.PlacedFootprint.Footprint.RowPitch = 2048 * 4;
                    lc->CopyTextureRegion(&d, 0, 0, 0, &sc, nullptr);
                    D3D12_RESOURCE_BARRIER bb{};
                    bb.Transition.pResource = texA.Get();
                    bb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                    bb.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    lc->ResourceBarrier(1, &bb);
                    lc->Close();
                    ID3D12CommandList* ls[]{ lc };
                    context.directQueue()->ExecuteCommandLists(1, ls);
                    ComPtr<ID3D12Fence> f13;
                    context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f13));
                    context.directQueue()->Signal(f13.Get(), 1);
                    f13->SetEventOnCompletion(1, nvofOutEvent);
                    WaitForSingleObject(nvofOutEvent, 5000);
                    veyra::log::info("player", "bare13: depth-style upload copy on");
                }
                if (mask & 8) {
                    // NGX core init AFTER the sink exists (engine order).
                    wchar_t absRt[MAX_PATH * 2]{};
                    GetFullPathNameW(runtimeDir.c_str(), MAX_PATH * 2, absRt, nullptr);
                    std::ifstream ids13(std::wstring(absRt) + L"\\..\\config\\ngx-local.json", std::ios::binary);
                    std::string idt13((std::istreambuf_iterator<char>(ids13)), std::istreambuf_iterator<char>());
                    auto scan13 = [&idt13](const char* key) -> std::string {
                        const char q = 0x22;
                        std::string nd; nd += q; nd += key; nd += q;
                        size_t p = idt13.find(nd);
                        if (p == std::string::npos) return std::string();
                        p = idt13.find(q, idt13.find(':', p + nd.size()));
                        if (p == std::string::npos) return std::string();
                        const size_t st2 = p + 1;
                        const size_t e = idt13.find(q, st2);
                        if (e == std::string::npos) return std::string();
                        return idt13.substr(st2, e - st2);
                    };
                    const std::string pid13 = scan13("ngxProjectId");
                    const std::string ev13 = scan13("engineVersion");
                    if (pid13.empty() || !coreHost.initialize(context.device(), absRt,
                            pid13.c_str(), ev13.c_str(), st)) break;
                    veyra::log::info("player", "bare13: NGX core AFTER sink");
                }
                if (mask & 16) {
                    uint64_t r13 = 0; uint32_t s13 = 0;
                    if (!bareNr.load(absRuntimeBare(), st) ||
                        !bareNr.installCallerCompatibility(st) ||
                        !bareNr.snippetInitExt(context.device(), absRuntimeBare(), r13, s13) ||
                        r13 != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) break;
                    veyra::log::info("player", "bare13: snippet init on");
                }
                if (mask & 32) {
                    veyra::ngx::DlssFgBackend::Capability c13{};
                    (void)bareFg.queryCapability(coreHost, c13, st);
                    veyra::log::info("player", "bare13: capability query on");
                }
                if (mask & 64) {
                    if (coreHost.initialized()) {
                        NVSDK_NGX_Parameter* p13 = coreHost.allocateParameters(st);
                        ID3D12GraphicsCommandList* l13 = ring.acquire(0, st);
                        veyra::ngx::DlssFgBackend::CreateDesc fd13{};
                        fd13.width = 1280; fd13.height = 720;
                        fd13.renderWidth = 1280; fd13.renderHeight = 720;
                        fd13.backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
                        (void)bareFg.create(coreHost, l13, p13, fd13, st);
                        (void)ring.submitAndSignal(0);
                        (void)ring.waitIdle();
                    }
                    veyra::log::info("player", "bare13: FG create on");
                }
                // The poison probe: ONE staged SRV into a fresh visible heap.
                {
                    ComPtr<ID3D12Resource> tex13 = makeTexture(context.device(), 64, 64,
                        DXGI_FORMAT_R8G8B8A8_UNORM, false);
                    D3D12_DESCRIPTOR_HEAP_DESC hs13{};
                    hs13.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    hs13.NumDescriptors = 2;
                    hs13.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                    ComPtr<ID3D12DescriptorHeap> st13;
                    if (FAILED(context.device()->CreateDescriptorHeap(&hs13, IID_PPV_ARGS(&st13))) ||
                        !tex13) break;
                    D3D12_DESCRIPTOR_HEAP_DESC hk{};
                    hk.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    hk.NumDescriptors = 2;
                    hk.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    ComPtr<ID3D12DescriptorHeap> heap13;
                    if (FAILED(context.device()->CreateDescriptorHeap(&hk, IID_PPV_ARGS(&heap13)))) break;
                    makeSrv(context.device(), tex13.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                        { st13->GetCPUDescriptorHandleForHeapStart() });
                    context.device()->CopyDescriptorsSimple(1,
                        { heap13->GetCPUDescriptorHandleForHeapStart() },
                        { st13->GetCPUDescriptorHandleForHeapStart() },
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    veyra::log::info("player", std::format("bare13: staged SRV probe (mask={})", mask));
                }
            }
            const float teal[4] = { 0.1f, 0.4f, 0.4f, 1.0f };
            uint64_t ok = 0, fail = 0;
            for (int i = 0; i < 600; ++i) {  // ~10 s at 60/s pacing
                bool closed = false;
                (void)bareSink.processMessages(closed);
                if (closed) break;
                ID3D12GraphicsCommandList* list = ring.acquire(i % 4, st);
                if (list == nullptr) break;
                ID3D12Resource* back = bareSink.currentBackBuffer();
                D3D12_RESOURCE_BARRIER b{};
                b.Transition.pResource = back;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &b);
                const UINT idx = bareSink.swapChain()->GetCurrentBackBufferIndex();
                const D3D12_CPU_DESCRIPTOR_HANDLE rtv{ bareRtv->GetCPUDescriptorHandleForHeapStart().ptr + idx * inc };
                list->ClearRenderTargetView(rtv, teal, 0, nullptr);
                D3D12_RESOURCE_BARRIER back2{};
                back2.Transition.pResource = back;
                back2.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                back2.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                back2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &back2);
                if (!ring.submitAndSignal(i % 4)) break;
                veyra::Status pst = veyra::Status::Ok;
                if (bareSink.present(pst)) ++ok; else ++fail;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            veyra::log::info("player", std::format("bare-present[stage={}]: ok={} failed={} attempted={}",
                bareStage, ok, fail, bareSink.attemptedPresentCount()));
            bareSink.shutdown();
            ring.shutdown();
            context.shutdown();
            if (!logFile.empty()) {
                std::string bareJson = "{\n  \"probe\": \"bare_present\",\n  \"ok\": {},\n  \"failed\": {}\n}";
                (void)utilns::writeTextFileUtf8(jsonFile.empty() ? L"logs/phase6-manual/bare-present.json" : jsonFile, bareJson);
            }
            overall = (fail == 0 && ok >= 500);
            break; // bare mode exits after the loop
        }

        nvofOutEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (FAILED(context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&nvofOutFence)))) break;

        // Software decode is the working path: D3D12VA decodes are slowed to
        // ~8fps by the injected layer once descriptor views exist (pool
        // warm-up fixes allocation, not the per-decode recording). The sw
        // decoder leaks ~0.5-1.2% of frame bytes per frame in this vcpkg
        // build (proportional to resolution), so the decoder context is
        // recycled periodically to release it; the demuxer stays open.
        bool hwDecode = false;
        if (GetEnvironmentVariableW(L"VEYRA_HW_DECODE", nullptr, 0) != 0) {
            hwDecode = decoder.openD3D12VA(demuxer.videoCodecParameters(),
                demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen(),
                context.device(), context.directQueue());
        }
        if (!hwDecode) {
            if (!decoder.openSoftware(demuxer.videoCodecParameters(),
                    demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen())) {
                veyra::log::error("player", "decoder open failed");
                break;
            }
        }
        const uint32_t srcW = static_cast<uint32_t>(decoder.width());
        const uint32_t srcH = static_cast<uint32_t>(decoder.height());
        uint32_t decodeRecycleFrames = 1200;
        {
            char buf[16]{};
            GetEnvironmentVariableA("VEYRA_DECODE_RECYCLE", buf, sizeof(buf));
            if (buf[0]) decodeRecycleFrames = static_cast<uint32_t>(std::atoi(buf));
        }
        uint64_t framesSinceRecycle = 0;
        veyra::log::info("player", std::format("source {}x{} durationMs={} hwDecode={} recycleEvery={}",
            srcW, srcH, durationUs / 1000, hwDecode ? 1 : 0, decodeRecycleFrames));

        if (hwDecode) {
            int warmed = 0;
            for (int i = 0; i < 8; ++i) {
                bool eof = false;
                if (!demuxer.readVideoPacket(eof)) break;
                if (!decoder.sendPacket(demuxer.currentPacket())) continue;
                if (decoder.receiveFrame() != nullptr) ++warmed;
            }
            (void)ring.waitIdle();
            demuxer.seekToUs(0);
            decoder.flushBuffers();
            veyra::log::info("player", std::format("D3D12VA pool warmed ({} frames) and rewound", warmed));
        }

        const uint32_t workW = 3840, workH = 2160;
        const bool srNeeded = (srcW != workW) || (srcH != workH);

        // --- NGX (core, capability, NR snippet, parameter block) -----------
        wchar_t absRuntime[MAX_PATH * 2]{};
        GetFullPathNameW(runtimeDir.c_str(), MAX_PATH * 2, absRuntime, nullptr);
        std::ifstream ids(std::wstring(absRuntime) + L"\\..\\config\\ngx-local.json", std::ios::binary);
        std::string idText((std::istreambuf_iterator<char>(ids)), std::istreambuf_iterator<char>());
        std::string projectId, engineVersion;
        {
            auto scan = [&idText](const char* key) {
                const std::string needle = std::string("\"") + key + "\"";
                size_t p = idText.find(needle);
                if (p == std::string::npos) return std::string();
                p = idText.find('"', idText.find(':', p + needle.size()));
                if (p == std::string::npos) return std::string();
                return idText.substr(p + 1, idText.find('"', p + 1) - p - 1);
            };
            projectId = scan("ngxProjectId");
            engineVersion = scan("engineVersion");
        }
        if (projectId.empty()) { veyra::log::error("player", "ngx-local.json missing"); break; }
        const bool noNgx = GetEnvironmentVariableW(L"VEYRA_NO_NGX", nullptr, 0) != 0;
        if (noNgx) {
            veyra::log::warn("player", "BISECT: NGX core+features skipped; NVOF only");
        } else if (!coreHost.initialize(context.device(), absRuntime,
                projectId.c_str(), engineVersion.c_str(), st)) break;

        veyra::ngx::DlssFgBackend::Capability fgCaps{};
        const bool fgAvailable = noNgx ? true : fgBackend.queryCapability(coreHost, fgCaps, st);
        if (!noNgx && !fgAvailable) { veyra::log::error("player", "FG unavailable; fail closed"); break; }
        veyra::log::info("player", std::format("FG capability available={} multiFrameMax={}",
            fgCaps.available, fgCaps.multiFrameCountMax));
        if (!fgAvailable) { veyra::log::error("player", "FG unavailable; fail closed"); break; }

        uint64_t nrResult = 0; uint32_t nrSeh = 0;
        if (noNgx) { /* skip NR snippet */ }
        else if (!nrAdapter.load(absRuntime, st) || !nrAdapter.installCallerCompatibility(st) ||
            !nrAdapter.snippetInitExt(context.device(), absRuntime, nrResult, nrSeh) ||
            nrResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
            veyra::log::error("player", std::format("NR snippet init failed 0x{:X}", nrResult));
            break;
        }
        if (!noNgx) {
            ngxParams = coreHost.allocateParameters(st);
            if (ngxParams == nullptr) break;
        }

        // --- ALL committed resources FIRST (system constraint, see header) --
        const size_t lumaPitch = (static_cast<size_t>(srcW) + 255) & ~size_t(255);
        const size_t chromaPitch = lumaPitch;
        const size_t lumaSize = lumaPitch * srcH;
        const size_t chromaSize = chromaPitch * (srcH / 2);
        const size_t dPitch = (static_cast<size_t>(workW) * 4 + 255) & ~size_t(255);
        const size_t dSize = dPitch * workH;

        ComPtr<ID3D12Resource> upLuma[2] = {
            makeUploadBuffer(context.device(), lumaSize),
            makeUploadBuffer(context.device(), lumaSize)
        };
        ComPtr<ID3D12Resource> upChroma[2] = {
            makeUploadBuffer(context.device(), chromaSize),
            makeUploadBuffer(context.device(), chromaSize)
        };
        ComPtr<ID3D12Resource> upDepth = makeUploadBuffer(context.device(), dSize);
        ComPtr<ID3D12Resource> upZeroDepth = makeUploadBuffer(context.device(), dSize);
        ComPtr<ID3D12Resource> upZeroMotion = makeUploadBuffer(context.device(), dSize);
        ComPtr<ID3D12Resource> lumaTex = makeTexture(context.device(), srcW, srcH, DXGI_FORMAT_R8_UNORM, true);
        ComPtr<ID3D12Resource> chromaTex = makeTexture(context.device(), srcW, srcH / 2, DXGI_FORMAT_R8G8_UNORM, true);
        ComPtr<ID3D12Resource> srcRgba = makeTexture(context.device(), srcW, srcH, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        ComPtr<ID3D12Resource> workRgba = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        ComPtr<ID3D12Resource> proxyTex = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        ComPtr<ID3D12Resource> neuralTex = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        ComPtr<ID3D12Resource> finalRgba = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        ComPtr<ID3D12Resource> videoFrame[2] = {
            makeTexture(context.device(), workW, workH, DXGI_FORMAT_R8G8B8A8_UNORM, true),
            makeTexture(context.device(), workW, workH, DXGI_FORMAT_R8G8B8A8_UNORM, true)
        };
        const uint32_t nvofW = workW, nvofH = workH;
        // Densified flow (FG input) + confidence at working extent; the raw
        // SHORT2/cost textures live above at grid extent (nvofRawTex/nvofCostTex).
        ComPtr<ID3D12Resource> confTex = makeTexture(context.device(), nvofW, nvofH, DXGI_FORMAT_R8_UNORM, true);
        ComPtr<ID3D12Resource> flowTex = makeTexture(context.device(), nvofW, nvofH, DXGI_FORMAT_R16G16_FLOAT, true);
        if (!confTex || !flowTex) {
            veyra::log::error("player", "densify output allocation failed (fail closed)");
            break;
        }
        ComPtr<ID3D12Resource> depthTex = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R32_FLOAT, false);
        ComPtr<ID3D12Resource> genFrame[2] = {
            makeTexture(context.device(), workW, workH, DXGI_FORMAT_R8G8B8A8_UNORM, true),
            makeTexture(context.device(), workW, workH, DXGI_FORMAT_R8G8B8A8_UNORM, true)
        };  // P0.3: generated-frame slots so consecutive FG outputs never
            // overwrite an unconsumed present.
        ComPtr<ID3D12Resource> nrZeroMotion = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R16G16_FLOAT, false);
        ComPtr<ID3D12Resource> nrZeroDepth = makeTexture(context.device(), workW, workH, DXGI_FORMAT_R32_FLOAT, false);
        if (!upLuma[0] || !upLuma[1] || !upChroma[0] || !upChroma[1] ||
            !upDepth || !upZeroDepth || !upZeroMotion ||
            !lumaTex || !chromaTex || !srcRgba || !workRgba || !proxyTex ||
            !neuralTex || !finalRgba || !videoFrame[0] || !videoFrame[1] ||
            !flowTex || !depthTex || !genFrame[0] || !genFrame[1] ||
            !nrZeroMotion || !nrZeroDepth) {
            veyra::log::error("player", "resource allocation failed");
            break;
        }

        // Persistent mapping of the NV12 upload ring (before any views).
        uint8_t* mappedLuma[2] = {};
        uint8_t* mappedChroma[2] = {};
        bool uploadsMapped = true;
        for (int i = 0; i < 2; ++i) {
            if (FAILED(upLuma[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedLuma[i]))) ||
                FAILED(upChroma[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedChroma[i])))) {
                uploadsMapped = false;
            }
        }
        if (!uploadsMapped) {
            veyra::log::error("player", "NV12 upload persistent map failed");
            break;
        }
        veyra::log::info("player", "NV12 upload ring persistently mapped (2 buffers)");

        // Depth constants uploaded once (copies are safe before views exist).
        {
            uint8_t* d = nullptr; uint8_t* zd = nullptr; uint8_t* zm = nullptr;
            upDepth->Map(0, nullptr, reinterpret_cast<void**>(&d));
            upZeroDepth->Map(0, nullptr, reinterpret_cast<void**>(&zd));
            upZeroMotion->Map(0, nullptr, reinterpret_cast<void**>(&zm));
            for (uint32_t y = 0; y < workH; ++y) {
                float* dRow = reinterpret_cast<float*>(d + y * dPitch);
                float* zdRow = reinterpret_cast<float*>(zd + y * dPitch);
                uint16_t* zmRow = reinterpret_cast<uint16_t*>(zm + y * dPitch);
                for (uint32_t x = 0; x < workW; ++x) {
                    dRow[x] = 0.9f;  // explicit constant far depth (video content)
                    zdRow[x] = 0.5f; // NR zero-depth explicit fallback
                    zmRow[x * 2] = 0; zmRow[x * 2 + 1] = 0;
                }
            }
            upDepth->Unmap(0, nullptr);
            upZeroDepth->Unmap(0, nullptr);
            upZeroMotion->Unmap(0, nullptr);

            ID3D12GraphicsCommandList* list = ring.acquire(0, st);
            if (list == nullptr) break;
            auto uploadTex = [&](ID3D12Resource* tex, const ComPtr<ID3D12Resource>& up, DXGI_FORMAT fmt) {
                D3D12_RESOURCE_BARRIER b{};
                b.Transition.pResource = tex;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &b);
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                dst.pResource = tex;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.pResource = up.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Footprint.Format = fmt;
                src.PlacedFootprint.Footprint.Width = workW;
                src.PlacedFootprint.Footprint.Height = workH;
                src.PlacedFootprint.Footprint.Depth = 1;
                src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(dPitch);
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                D3D12_RESOURCE_BARRIER back{};
                back.Transition.pResource = tex;
                back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &back);
            };
            uploadTex(depthTex.Get(), upDepth, DXGI_FORMAT_R32_FLOAT);
            uploadTex(nrZeroDepth.Get(), upZeroDepth, DXGI_FORMAT_R32_FLOAT);
            uploadTex(nrZeroMotion.Get(), upZeroMotion, DXGI_FORMAT_R16G16_FLOAT);
            list->Close();
            ID3D12CommandList* lists[] = { list };
            context.directQueue()->ExecuteCommandLists(1, lists);
            ComPtr<ID3D12Fence> initFence;
            context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initFence));
            context.directQueue()->Signal(initFence.Get(), 1);
            initFence->SetEventOnCompletion(1, nvofOutEvent);
            WaitForSingleObject(nvofOutEvent, 5000);
            veyra::log::info("player", "depth/zero guidance textures initialized");
        }

        const bool noFeatures = GetEnvironmentVariableW(L"VEYRA_NO_FEATURES", nullptr, 0) != 0;

        // --- NVOF + NGX features (before any descriptor view) ---------------
        veyra::ngx::NvOfSession::Desc nd{};
        nd.width = nvofW; nd.height = nvofH;
        nd.inFence = context.fence();
        nd.outFence = nvofOutFence.Get();
        nd.gridSize = 4;
        // P0.4/s6 contract: B8G8R8A8 inputs, raw R16G16_SINT + R8_UINT cost
        // at grid extent, passed explicitly; allocation failure = fail closed.
        const uint32_t nvofGrid = 4;
        const uint32_t rawW = (nvofW + nvofGrid - 1) / nvofGrid;
        const uint32_t rawH = (nvofH + nvofGrid - 1) / nvofGrid;
        ComPtr<ID3D12Resource> nvofRawTex = makeTexture(context.device(), rawW, rawH, DXGI_FORMAT_R16G16_SINT, false);
        ComPtr<ID3D12Resource> nvofCostTex = makeTexture(context.device(), rawW, rawH, DXGI_FORMAT_R8_UINT, false);
        if (!nvofRawTex || !nvofCostTex) {
            veyra::log::error("player", "NVOF raw/cost allocation failed (fail closed)");
            break;
        }
        // NVOF inputs must be B8G8R8A8 (ABGR8 in SDK terms).
        ComPtr<ID3D12Resource> nvofInA = makeTexture(context.device(), nvofW, nvofH, DXGI_FORMAT_B8G8R8A8_UNORM, true);
        ComPtr<ID3D12Resource> nvofInB = makeTexture(context.device(), nvofW, nvofH, DXGI_FORMAT_B8G8R8A8_UNORM, true);
        if (!nvofInA || !nvofInB) {
            veyra::log::error("player", "NVOF B8G8R8A8 input allocation failed (fail closed)");
            break;
        }
        if (!noFeatures && !nvof.initialize(context.device(), nvofInA.Get(), nvofInB.Get(),
                nvofRawTex.Get(), nvofCostTex.Get(), nd, st)) {
            veyra::log::error("player", "NVOF init failed");
            break;
        }
        const uint32_t selectedGrid = nvof.caps().selectedGrid;
        // The historical 20x warm-up on UNINITIALIZED A/B was removed (user
        // directive s7 item 5): it was a workaround from the invalid-SRV era,
        // not a contract. If a pre-allocation need ever re-emerges, it must
        // be a documented duplicate-frame warm-up on deterministic content.

        if (!noFeatures && !noNgx) {
            namespace p = veyra::ngx::dlssnr;
            veyra::ngx::ParameterBlock pb(ngxParams);
            pb.setU32(p::kWidth, workW); pb.setU32(p::kHeight, workH);
            pb.setU32(p::kInputWidth, workW); pb.setU32(p::kInputHeight, workH);
            pb.setU32(p::kOutputWidth, workW); pb.setU32(p::kOutputHeight, workH);
            pb.setU32(p::kOutputDotWidth, workW); pb.setU32(p::kOutputDotHeight, workH);
            pb.setU32(p::kUpscaling, 0);
            pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
            pb.setVoid(p::kComputeScalingRatioCallback,
                reinterpret_cast<void*>(&veyra::ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
            pb.setI32(p::kHintRenderPreset, 0);
            pb.setU32(p::kStdWidth, workW); pb.setU32(p::kStdHeight, workH);
            pb.setI32(p::kPerfQualityValue, 1);
            pb.setU32(p::kCreationNodeMask, 1); pb.setU32(p::kVisibilityNodeMask, 1);
            ID3D12GraphicsCommandList* list = ring.acquire(0, st);
            if (list == nullptr) break;
            if (!nrAdapter.snippetCreateFeature(list, ngxParams, &nrHandle, nrResult, nrSeh) ||
                nrResult != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                veyra::log::error("player", std::format("NR create failed 0x{:X}", nrResult));
                break;
            }
            (void)ring.submitAndSignal(0);
            (void)ring.waitIdle();
        }

        const bool srEnabled = srNeeded; // 1:1 bypass otherwise
        bool nrEnabled = GetEnvironmentVariableW(L"VEYRA_NR_OFF", nullptr, 0) == 0 &&
                         (noFeatures || noNgx ? false : true);  // no NR handle exists -> off
        bool fgEnabled = GetEnvironmentVariableW(L"VEYRA_FG_OFF", nullptr, 0) == 0 &&
                         !noNgx;  // no feature handle exists under NO_NGX

        if (srEnabled && !noFeatures && !noNgx) {
            veyra::ngx::DlssSrBackend::CreateDesc sd{};
            sd.inputWidth = srcW; sd.inputHeight = srcH;
            sd.outputWidth = workW; sd.outputHeight = workH;
            sd.perfQuality = 1; sd.enableOutputSubrects = false;
            ID3D12GraphicsCommandList* list = ring.acquire(0, st);
            if (list == nullptr) break;
            if (!srBackend.create(coreHost, list, ngxParams, sd, st) || !srBackend.created()) {
                veyra::log::error("player", "SR create failed");
                break;
            }
            (void)ring.submitAndSignal(0);
            (void)ring.waitIdle();
        }

        if (!noFeatures && !noNgx) {
            veyra::ngx::DlssFgBackend::CreateDesc fd{};
            fd.width = workW; fd.height = workH;
            fd.renderWidth = workW; fd.renderHeight = workH;
            fd.backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            ID3D12GraphicsCommandList* list = ring.acquire(0, st);
            if (list == nullptr) break;
            if (!fgBackend.create(coreHost, list, ngxParams, fd, st) || !fgBackend.created()) {
                veyra::log::error("player", std::format("FG create failed 0x{:X}", fgBackend.createResult()));
                break;
            }
            (void)ring.submitAndSignal(0);
            (void)ring.waitIdle();

            // FG warm-up evaluate before descriptor views (same injected-layer
            // constraint as NVOF: the runtime allocates internals at the first
            // evaluate and would fail after views exist).
            {
                ID3D12GraphicsCommandList* wlist = ring.acquire(0, st);
                if (wlist == nullptr) break;
                D3D12_RESOURCE_BARRIER b[4]{};
                for (int i = 0; i < 4; ++i) {
                    b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                }
                b[0].Transition.pResource = nvofInB.Get();
                b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                b[1].Transition.pResource = nrZeroMotion.Get();
                b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                b[2].Transition.pResource = depthTex.Get();
                b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                b[3].Transition.pResource = genFrame[0].Get();
                b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                wlist->ResourceBarrier(4, b);
                veyra::ngx::DlssFgBackend::EvalDesc fe{};
                fe.backbuffer = nvofInB.Get();
                fe.depth = depthTex.Get();
                fe.mvecs = nrZeroMotion.Get();
                fe.outputInterpolated = genFrame[0].Get();
                fe.reset = true;
                fe.frameId = 0;
                fe.mvecScaleX = 1.0f;
                fe.mvecScaleY = 1.0f;
                bool warmOk = fgBackend.evaluate(wlist, ngxParams, fe, st);
                (void)warmOk;
                for (int i = 0; i < 4; ++i) b[i].Transition.StateBefore = b[i].Transition.StateAfter;
                b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                wlist->ResourceBarrier(4, b);
                if (!ring.submitAndSignal(0) || !ring.waitIdle()) break;
                veyra::log::info("player", std::format("FG warm-up evaluate done ok={} result=0x{:X}",
                    warmOk ? 1 : 0, fgBackend.createResult()));
            }
        }

        // --- PresentSink (swapchain allocs also precede views) ---------------
        veyra::gfx::PresentSink::Desc sinkDesc{};
        sinkDesc.width = std::min<uint32_t>(workW, 1920);
        sinkDesc.height = std::min<uint32_t>(workH, 1080);
        {
            char bw[8]{}, bh[8]{};
            GetEnvironmentVariableA("VEYRA_SINK_W", bw, sizeof(bw));
            GetEnvironmentVariableA("VEYRA_SINK_H", bh, sizeof(bh));
            if (bw[0]) sinkDesc.width = static_cast<uint32_t>(std::atoi(bw));
            if (bh[0]) sinkDesc.height = static_cast<uint32_t>(std::atoi(bh));
        }
        sinkDesc.vsync = false;
        sinkDesc.title = L"Veyra Player Probe";
        if (!sink.initialize(context.device(), context.directQueue(), sinkDesc, st)) break;

        // --- Compute passes (PSOs + heaps; still no views) -------------------
        ComputePass yuvPass, encPass, decPass, blitPass, uploadPass;
        const bool skipPasses = GetEnvironmentVariableW(L"VEYRA_SKIP_PASSES", nullptr, 0) != 0;
        const bool skipViews = GetEnvironmentVariableW(L"VEYRA_SKIP_VIEWS", nullptr, 0) != 0;
        const bool viewsTex  = GetEnvironmentVariableW(L"VEYRA_VIEWS_TEX", nullptr, 0) != 0;
        const bool viewsRaw  = GetEnvironmentVariableW(L"VEYRA_VIEWS_RAW", nullptr, 0) != 0;
        const bool viewsUav  = GetEnvironmentVariableW(L"VEYRA_VIEWS_UAV", nullptr, 0) != 0;
        if (!skipPasses) {
            std::vector<uint8_t> cs;
            if (!yuvPass.loadShader("YuvToLinearRgb.dxil", cs) || !yuvPass.create(context.device(), cs, 8)) break;
            if (!encPass.loadShader("ParityEncode.dxil", cs) || !encPass.create(context.device(), cs, 8)) break;
            if (!decPass.loadShader("ParityDecode.dxil", cs) || !decPass.create(context.device(), cs, 8)) break;
            if (!blitPass.loadShader("ScaleBlit.dxil", cs) || !blitPass.create(context.device(), cs, 16)) break;
            if (!uploadPass.loadShader("Nv12Upload.dxil", cs) || !uploadPass.create(context.device(), cs, 4, 1, 2)) break;
        }
        ComputePass densifyPass;
        {
            std::vector<uint8_t> cs;
            if (!densifyPass.loadShader("NvofDensify.dxil", cs) ||
                !densifyPass.create(context.device(), cs, 6, 2, 2)) break;
        }
        DescriptorStager stager;
        if (!stager.initialize(context.device(), 64)) {
            veyra::log::error("player", "descriptor stager init failed");
            break;
        }
        auto cpuHandle = [](const ComputePass& p, UINT slot) {
            return D3D12_CPU_DESCRIPTOR_HANDLE{ p.heap->GetCPUDescriptorHandleForHeapStart().ptr + slot * p.increment };
        };
        // stagedSrv: SRV creation must go through the staging heap (driver
        // workaround; see DescriptorStager).
        auto stagedSrv = [&](ID3D12Resource* resource, DXGI_FORMAT fmt,
                             ComputePass& pass, UINT slot) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = fmt;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MostDetailedMip = 0;
            srv.Texture2D.MipLevels = 1;
            srv.Texture2D.PlaneSlice = 0;
            srv.Texture2D.ResourceMinLODClamp = 0.0f;
            makeSrv(context.device(), resource, fmt, cpuHandle(pass, slot));
        };
        auto gpuHandle = [](const ComputePass& p, UINT slot) {
            return D3D12_GPU_DESCRIPTOR_HANDLE{ p.heap->GetGPUDescriptorHandleForHeapStart().ptr + slot * p.increment };
        };

        GraphicsPass presentPass;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        UINT rtvIncrement = 0;
        {
            if (!skipPasses) {
                std::vector<uint8_t> vs, ps;
                if (!loadShaderBytes("PresentBlit_vs.dxil", vs) ||
                    !loadShaderBytes("PresentBlit_ps.dxil", ps) ||
                    !presentPass.create(context.device(), vs, ps, 8)) {
                    veyra::log::error("player", "present graphics pass creation failed");
                    break;
                }
            }
            D3D12_DESCRIPTOR_HEAP_DESC rh{};
            rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rh.NumDescriptors = 3;
            if (FAILED(context.device()->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap)))) {
                veyra::log::error("player", "RTV heap creation failed");
                break;
            }
            rtvIncrement = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        auto refreshBackbufferRtvs = [&]() {
            for (UINT bb = 0; bb < 3; ++bb) {
                ComPtr<ID3D12Resource> backBuffer;
                if (SUCCEEDED(sink.swapChain()->GetBuffer(bb, IID_PPV_ARGS(&backBuffer)))) {
                    context.device()->CreateRenderTargetView(backBuffer.Get(), nullptr,
                        { rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bb * rtvIncrement });
                }
            }
        };


        // --- Audio start (after GPU init) ------------------------------------
        if (hasAudio) {
            if (!audio.start()) {
                veyra::log::error("player", "audio renderer open failed");
                break;
            }
            audioPipe.startThread(&audio);  // prefill + anchored Start on thread
        }

        if (!skipPasses && !skipViews) {
            // --- STATIC DESCRIPTOR VIEWS (LAST: system constraint) ----------------
            if (viewsTex) stagedSrv(lumaTex.Get(), DXGI_FORMAT_R8_UNORM, yuvPass, 0);
            if (viewsTex) stagedSrv(chromaTex.Get(), DXGI_FORMAT_R8G8_UNORM, yuvPass, 1);
            if (viewsUav) makeUav(context.device(), srcRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandle(yuvPass, 2));
            if (viewsTex) stagedSrv(workRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, encPass, 0);
            if (viewsUav) makeUav(context.device(), proxyTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpuHandle(encPass, 1));
            if (viewsTex) stagedSrv(workRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, decPass, 0);
            if (viewsTex) stagedSrv(proxyTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass, 1);
            if (viewsTex) stagedSrv(neuralTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass, 2);
            if (viewsUav) makeUav(context.device(), finalRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandle(decPass, 3));
            // Upload pass: raw buffer SRVs per parity (slots 0/1), plane UAVs (2/3).
            for (int i = 0; i < 2; ++i) {
                D3D12_SHADER_RESOURCE_VIEW_DESC raw{};
                raw.Format = DXGI_FORMAT_R32_TYPELESS;
                raw.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                raw.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                raw.Buffer.FirstElement = 0;
                raw.Buffer.NumElements = static_cast<UINT>(lumaSize / 4);
                raw.Buffer.StructureByteStride = 0; // RAW view
            
                if (viewsRaw) context.device()->CreateShaderResourceView(upLuma[i].Get(), &raw, cpuHandle(uploadPass, i));
            }
            if (viewsUav) makeUav(context.device(), lumaTex.Get(), DXGI_FORMAT_R8_UNORM, cpuHandle(uploadPass, 2));
            if (viewsUav) makeUav(context.device(), chromaTex.Get(), DXGI_FORMAT_R8G8_UNORM, cpuHandle(uploadPass, 3));
            // Blit pass layout (all static; per-use offsets chosen at bind time):
            //  0: srcRgba SRV        1: workRgba UAV      (SR bypass / NR-off blit)
            //  2: finalRgba SRV      3/4: videoFrame UAV  (section 5)
            //  5: nvofInB SRV        6: nvofInA UAV       (NVOF A:=B)
            //  7/8: videoFrame SRV   9: nvofInB UAV       (NVOF B:=video)
            // 10: genTex SRV        11/12: videoFrame SRV (presents)
            // 13/14/15: swapchain backbuffer UAVs         (presents)
            if (viewsTex) stagedSrv(srcRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass, 0);
            if (viewsUav) makeUav(context.device(), workRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandle(blitPass, 1));
            if (!skipViews) if (viewsTex) stagedSrv(finalRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass, 2);
            if (viewsUav) makeUav(context.device(), videoFrame[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpuHandle(blitPass, 3));
            if (viewsUav) makeUav(context.device(), videoFrame[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpuHandle(blitPass, 4));
            if (viewsTex) stagedSrv(nvofInB.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, blitPass, 5);
            if (viewsUav) makeUav(context.device(), nvofInA.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpuHandle(blitPass, 6));
            if (viewsTex) stagedSrv(videoFrame[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass, 7);
            if (viewsTex) stagedSrv(videoFrame[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass, 8);
            if (viewsUav) makeUav(context.device(), nvofInB.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpuHandle(blitPass, 9));
            if (viewsTex) stagedSrv(genFrame[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass, 10);
        if (viewsTex) stagedSrv(genFrame[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass, 13);
            if (viewsTex) stagedSrv(videoFrame[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass, 11);
            if (viewsTex) stagedSrv(videoFrame[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, blitPass, 12);
            for (UINT bb = 0; bb < 3; ++bb) {
                ComPtr<ID3D12Resource> backBuffer;
                if (SUCCEEDED(sink.swapChain()->GetBuffer(bb, IID_PPV_ARGS(&backBuffer)))) {
                    context.device()->CreateRenderTargetView(backBuffer.Get(), nullptr,
                        { rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bb * rtvIncrement });
                }
            }

            // Densify pass views (P0.4): 0=rawFlow SRV(int2) 1=cost SRV(uint)
        // 2=flowOut UAV(float2) 3=confOut UAV(float).
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC rf{};
            rf.Format = DXGI_FORMAT_R16G16_SINT;
            rf.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            rf.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            rf.Texture2D.MostDetailedMip = 0;
            rf.Texture2D.MipLevels = 1;
            rf.Texture2D.PlaneSlice = 0;
            rf.Texture2D.ResourceMinLODClamp = 0.0f;
            context.device()->CreateShaderResourceView(nvofRawTex.Get(), &rf, cpuHandle(densifyPass, 0));
            D3D12_SHADER_RESOURCE_VIEW_DESC rc{};
            rc.Format = DXGI_FORMAT_R8_UINT;
            rc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            rc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            rc.Texture2D.MostDetailedMip = 0;
            rc.Texture2D.MipLevels = 1;
            rc.Texture2D.PlaneSlice = 0;
            rc.Texture2D.ResourceMinLODClamp = 0.0f;
            context.device()->CreateShaderResourceView(nvofCostTex.Get(), &rc, cpuHandle(densifyPass, 1));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uf{};
            uf.Format = DXGI_FORMAT_R16G16_FLOAT;
            uf.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            context.device()->CreateUnorderedAccessView(flowTex.Get(), nullptr, &uf, cpuHandle(densifyPass, 2));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uc{};
            uc.Format = DXGI_FORMAT_R8_UNORM;
            uc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            context.device()->CreateUnorderedAccessView(confTex.Get(), nullptr, &uc, cpuHandle(densifyPass, 3));
        }

        // Present pass SRVs: 0/1=genFrame[0/1], 2/3=videoFrame[0/1].
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MostDetailedMip = 0;
                srv.Texture2D.MipLevels = 1;
                srv.Texture2D.PlaneSlice = 0;
                srv.Texture2D.ResourceMinLODClamp = 0.0f;
                if (viewsTex) makeSrv(context.device(), genFrame[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr });
                if (viewsTex) makeSrv(context.device(), genFrame[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr + presentPass.increment });
                if (viewsTex) makeSrv(context.device(), videoFrame[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr + 2ull * presentPass.increment });
                if (viewsTex) makeSrv(context.device(), videoFrame[1].Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                    { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr + 3ull * presentPass.increment });
            }


        }

        // k-incremental staged-SRV experiment (present-path isolation).
        {
            char texN[8]{};
            GetEnvironmentVariableA("VEYRA_TEX_N", texN, sizeof(texN));
            const int k = texN[0] ? std::atoi(texN) : 0;
            if (k > 0) {
                D3D12_DESCRIPTOR_HEAP_DESC hk{};
                hk.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                hk.NumDescriptors = 64;
                hk.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ComPtr<ID3D12DescriptorHeap> heapK;
                if (FAILED(context.device()->CreateDescriptorHeap(&hk, IID_PPV_ARGS(&heapK)))) break;
                ComPtr<ID3D12Resource> texK = makeTexture(context.device(), 64, 64,
                    DXGI_FORMAT_R8G8B8A8_UNORM, false);
                if (!texK) break;
                D3D12_SHADER_RESOURCE_VIEW_DESC sk{};
                sk.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sk.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sk.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                for (int i = 0; i < k; ++i) {
                    stager.stageSrv(texK.Get(), &sk, heapK.Get(), static_cast<UINT>(i));
                }
                veyra::log::info("player", std::format("k-experiment: staged {} SRVs into a fresh visible heap", k));
            }
            if (GetEnvironmentVariableW(L"VEYRA_TEX_ENGINE", nullptr, 0) != 0 && !skipPasses && !skipViews) {
                D3D12_SHADER_RESOURCE_VIEW_DESC sk2{};
                sk2.Format = DXGI_FORMAT_R8_UNORM;
                sk2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sk2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                stager.stageSrv(lumaTex.Get(), &sk2, yuvPass.heap.Get(), 3);
                veyra::log::info("player", "k-experiment: staged ONE lumaTex SRV into yuvPass heap slot 3");
            }
        }

        // --- State tracker ----------------------------------------------------
        uint64_t d3dDiagErrors = 0, d3dDiagCorruption = 0;
        uint64_t d3dDiagWarnings = 0, d3dDiagInfo = 0;
        UINT64 diagStartupStored = 0, diagStartupRetrieved = 0;
        bool diagRetrievalComplete = true;
        UINT64 diagStoredRuntime = 0, diagRetrievedRuntime = 0, diagRetrievalFailures = 0;
        UINT64 diagQueueCapacity = 0;
        bool diagQueueSaturated = false;
        bool diagActive = false;
        if (d3dDiag) {
            if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&d3dDiagQueue))) && d3dDiagQueue.Get()) {
                D3D12_INFO_QUEUE_FILTER noFilter{};
                d3dDiagQueue->PushStorageFilter(&noFilter);  // record all
                // Default storage is 1024 messages; a 4K run generates more
                // INFO and the queue DROPS messages when full (and
                // GetMessageW can then fail wholesale - observed failures=
                // 1024 with cap=1024 saturated=1). Unlimited (-1) storage
                // removes the cap; the runtime phase is cleared after
                // startup so counts stay meaningful.
                d3dDiagQueue->SetMessageCountLimit(static_cast<UINT64>(-1));
                diagActive = true;
                g_d3dDiagQueue = d3dDiagQueue.Get();
                // s9-B phase 1 (startup): count creation-time messages, then
                // clear so the measured-runtime phase starts empty.
                diagStartupStored = d3dDiagQueue->GetNumStoredMessages();
                d3dDiagQueue->ClearStoredMessages();
                veyra::log::info("player", std::format("diag(startup): stored-cleared={}", diagStartupStored));
            } else {
                veyra::log::error("player", "diag: requested but InfoQueue attach FAILED (fail closed)");
                diagRetrievalComplete = false;
            }
        }
        StateTracker tracker;
        for (ID3D12Resource* r : { lumaTex.Get(), chromaTex.Get(), workRgba.Get(),
             proxyTex.Get(), neuralTex.Get(), finalRgba.Get(), videoFrame[0].Get(),
             videoFrame[1].Get(), nvofInA.Get(), nvofInB.Get(), flowTex.Get(),
             depthTex.Get(), genFrame[0].Get(), genFrame[1].Get(), nrZeroMotion.Get(), nrZeroDepth.Get(),
             srcRgba.Get() }) {
            tracker.set(r, D3D12_RESOURCE_STATE_COMMON);
        }
        for (int i = 0; i < 2; ++i) {
            tracker.set(upLuma[i].Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
            tracker.set(upChroma[i].Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
        }

        std::vector<uint8_t> nv12Buf(lumaSize + chromaSize);
        // P0.3: bounded 6-slot display pool with explicit slot ownership.
        // genFrame[2] joins videoFrame[2] as dedicated slots (real frames use
        // slots 0..1, generated frames own gen slots); a 6-entry queue cap
        // provides backpressure - frames are never dropped for sync.
        uint64_t resetEpoch = 1;                 // bumped on seek/loop/reset
        std::vector<double> latenessSamples;     // P0.2 signed, pre-decision
        bool slotFree[2] = { true, true };       // P0.3 real-slot liveness
        uint64_t droppedSourceFrames = 0;
        uint64_t droppedGeneratedFrames = 0;
        // P0.3: every queue item owns its exact texture slot; the present
        // path may only display the item's own resource. Bounded 6-slot
        // pool gives backpressure instead of frame dropping.
        struct PresentItem {
            double dueMs;            // target present time (media clock)
            int kind;                // 0 = real, 1 = generated
            uint64_t ptsMs;          // media PTS of the content
            uint64_t frameSeq;       // producer frame sequence
            uint32_t textureSlot;    // real: videoFrame[slot]; gen: genFrame[slot]
            uint64_t epoch;          // reset epoch (seek/loop); older epoch invalidates
            uint64_t fenceValue;     // producer fence: item displayable once completed
        };
        std::deque<PresentItem> presentQueue;
        uint64_t realFrameIndex = 0;
        int lastParity = 0;
        bool prevValid = false;
        double prevPtsMs = 0.0;

        const auto qpcNowMs = [] {
            return 1000.0 * static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1e9;
        };
        double wallEpochMs = qpcNowMs();
        const auto mediaTimeMs = [&]() -> double {
            return hasAudio ? audio.mediaTimeMs() : (qpcNowMs() - wallEpochMs);
        };

        // --- P0.5: per-pass GPU timing (diagnostic, serialized) --------------
        const bool gpuTs = GetEnvironmentVariableW(L"VEYRA_GPU_TS", nullptr, 0) != 0;
        std::vector<std::pair<const char*, double>> passTimings; // name, ms
        auto gpuMark = [&](const char* name) {
            if (!gpuTs) return;
            // Signal a private fence and wait: the QPC delta at completion
            // approximates this pass's GPU cost (serialized mode).
            static ComPtr<ID3D12Fence> tsFence;
            static HANDLE tsEvent = nullptr;
            static uint64_t tsVal = 0;
            static bool tsInit = false;
            if (!tsInit) {
                context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tsFence));
                tsEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                tsInit = true;
            }
            const auto t0 = std::chrono::steady_clock::now();
            ++tsVal;
            context.directQueue()->Signal(tsFence.Get(), tsVal);
            tsFence->SetEventOnCompletion(tsVal, tsEvent);
            WaitForSingleObject(tsEvent, 2000);
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            passTimings.emplace_back(name, ms);
        };
        auto gpuReport = [&]() {
            if (!gpuTs) return;
            // Aggregate per pass name.
            std::unordered_map<std::string, std::vector<double>> agg;
            for (auto& [n, ms] : passTimings) agg[n].push_back(ms);
            std::string line = "gpu-pass-ms:";
            for (auto& [n, v] : agg) {
                std::sort(v.begin(), v.end());
                const double p50 = v[v.size() / 2];
                const double p95 = v[std::min(v.size() - 1, static_cast<size_t>(0.95 * (v.size() - 1)))];
                line += std::format(" {} n={} p50={:.2f} p95={:.2f};", n, v.size(), p50, p95);
            }
            veyra::log::info("player-ts", line);
            passTimings.clear();
        };

        // --- Per-frame graph ------------------------------------------------
        auto processOneFrame = [&](const AVFrame* frame) -> bool {
            const int parity = static_cast<int>(realFrameIndex % 2);
            static const bool graphOff = GetEnvironmentVariableW(L"VEYRA_GRAPH_OFF", nullptr, 0) != 0;
            if (frame->pts == AV_NOPTS_VALUE || frame->pts < 0) {
                return true; // no timestamp: skip this frame entirely
            }
            const double ptsMs = 1000.0 * frame->pts * demuxer.videoTimeBaseNum() /
                demuxer.videoTimeBaseDen();
            if (graphOff) {
                presentQueue.push_back({ ptsMs, 0, static_cast<uint64_t>(ptsMs),
                realFrameIndex, static_cast<uint32_t>(parity), resetEpoch,
                ring.lastSignaledValue() });
                prevPtsMs = ptsMs;
                prevValid = true;
                ++realFrameIndex;
                return true;
            }
            const uint32_t slot = static_cast<uint32_t>(realFrameIndex % 4);
            const bool reset = !prevValid;

            // 1. Source NV12: D3D12VA texture directly (GPU) or CPU upload.
            ID3D12Resource* nv12Texture = nullptr;
            ID3D12GraphicsCommandList* list = ring.acquire(slot, st);
            if (list == nullptr) { veyra::log::error("player", "F4 ring acquire"); return false; }

            if (frame->format == AV_PIX_FMT_D3D12) {
                auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);
                if (d3dFrame == nullptr || d3dFrame->texture == nullptr) {
                    veyra::log::error("player", "F0 null d3d12va frame");
                    return false;
                }
                nv12Texture = d3dFrame->texture;
                const UINT nv12Slice = static_cast<UINT>(d3dFrame->subresource_index);
                if (d3dFrame->sync_ctx.fence != nullptr) {
                    if (FAILED(context.directQueue()->Wait(
                            d3dFrame->sync_ctx.fence, d3dFrame->sync_ctx.fence_value))) {
                        veyra::log::error("player", "F0b nv12 fence wait");
                        return false;
                    }
                }
                const bool isArray = nv12Texture->GetDesc().DepthOrArraySize > 1;
                D3D12_CPU_DESCRIPTOR_HANDLE base = yuvPass.heap->GetCPUDescriptorHandleForHeapStart();
                D3D12_SHADER_RESOURCE_VIEW_DESC lumaSrv{};
                lumaSrv.Format = DXGI_FORMAT_R8_UNORM;
                lumaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                lumaSrv.Texture2D.MostDetailedMip = 0;
                lumaSrv.Texture2D.MipLevels = 1;
                lumaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
                if (isArray) {
                    lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    lumaSrv.Texture2DArray.MostDetailedMip = 0;
                    lumaSrv.Texture2DArray.MipLevels = 1;
                    lumaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
                    lumaSrv.Texture2DArray.ArraySize = 1;
                    lumaSrv.Texture2DArray.PlaneSlice = 0;
                } else {
                    lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    lumaSrv.Texture2D.MostDetailedMip = 0;
                    lumaSrv.Texture2D.MipLevels = 1;
                    lumaSrv.Texture2D.PlaneSlice = 0;
                }
                context.device()->CreateShaderResourceView(nv12Texture, &lumaSrv, cpuHandle(yuvPass, 0));
                D3D12_SHADER_RESOURCE_VIEW_DESC chromaSrv{};
                chromaSrv.Format = DXGI_FORMAT_R8G8_UNORM;
                chromaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                chromaSrv.Texture2D.MostDetailedMip = 0;
                chromaSrv.Texture2D.MipLevels = 1;
                chromaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
                if (isArray) {
                    chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    chromaSrv.Texture2DArray.MostDetailedMip = 0;
                    chromaSrv.Texture2DArray.MipLevels = 1;
                    chromaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
                    chromaSrv.Texture2DArray.ArraySize = 1;
                    chromaSrv.Texture2DArray.PlaneSlice = 1;
                } else {
                    chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    chromaSrv.Texture2D.MostDetailedMip = 0;
                    chromaSrv.Texture2D.MipLevels = 1;
                    chromaSrv.Texture2D.PlaneSlice = 1;
                }
                context.device()->CreateShaderResourceView(nv12Texture, &chromaSrv, cpuHandle(yuvPass, 1));
                tracker.transition(list, nv12Texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            } else {
                nv12Ctx = sws_getCachedContext(nv12Ctx, frame->width, frame->height,
                    static_cast<AVPixelFormat>(frame->format),
                    frame->width, frame->height, AV_PIX_FMT_NV12, SWS_POINT,
                    nullptr, nullptr, nullptr);
                if (nv12Ctx == nullptr) { veyra::log::error("player", "F1 sws"); return false; }
                uint8_t* planes[2] = { nv12Buf.data(), nv12Buf.data() + lumaSize };
                const int strides[2] = { static_cast<int>(lumaPitch), static_cast<int>(chromaPitch) };
                sws_scale(nv12Ctx, frame->data, frame->linesize, 0, frame->height, planes, strides);
                for (uint32_t y = 0; y < srcH; ++y)
                    std::memcpy(mappedLuma[parity] + y * lumaPitch, planes[0] + y * lumaPitch, srcW);
                for (uint32_t y = 0; y < srcH / 2; ++y)
                    std::memcpy(mappedChroma[parity] + y * chromaPitch, planes[1] + y * chromaPitch, srcW);
                tracker.transition(list, lumaTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker.transition(list, chromaTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker.transition(list, upLuma[parity].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                const float constants[8] = {
                    static_cast<float>(srcW), static_cast<float>(srcH),
                    static_cast<float>(lumaPitch), static_cast<float>(chromaPitch), 0, 0, 0, 0 };
                uploadPass.bind(list, constants,
                    gpuHandle(uploadPass, parity).ptr, gpuHandle(uploadPass, 2).ptr);
                list->Dispatch((srcW + 31) / 32 * 2, (srcH + 31) / 32 * 2, 1);
                tracker.uavBarrier(list, lumaTex.Get());
                tracker.uavBarrier(list, chromaTex.Get());
                tracker.transition(list, upLuma[parity].Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
                tracker.transition(list, lumaTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker.transition(list, chromaTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            gpuMark("upload");
            // 2. YUV -> RGBA16F.
            tracker.transition(list, srcRgba.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            {
                const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
                    static_cast<float>(srcW), static_cast<float>(srcH), 0.0f, 0.0f };
                yuvPass.bind(list, constants, gpuHandle(yuvPass, 0).ptr, gpuHandle(yuvPass, 2).ptr);
                list->Dispatch((srcW + 15) / 16, (srcH + 15) / 16, 1);
            }
            tracker.uavBarrier(list, srcRgba.Get());
            tracker.transition(list, srcRgba.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (nv12Texture != nullptr) {
                tracker.transition(list, nv12Texture, D3D12_RESOURCE_STATE_COMMON);
            }

            gpuMark("yuv");
            // 3. SR into workRgba (or 1:1 blit bypass).
            if (srEnabled) {
                tracker.transition(list, workRgba.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                veyra::ngx::DlssSrBackend::EvalDesc ed{};
                ed.color = srcRgba.Get();
                ed.output = workRgba.Get();
                ed.depth = nrZeroDepth.Get();
                ed.motionVectors = nrZeroMotion.Get();
                ed.reset = reset;
                if (!srBackend.evaluate(list, ngxParams, ed, st)) { veyra::log::error("player", "F5 sr"); return false; }
                ++metrics.srEvaluateCount;
                tracker.uavBarrier(list, workRgba.Get());
                tracker.transition(list, workRgba.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            } else {
                tracker.transition(list, workRgba.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                const float constants[8] = {
                    static_cast<float>(srcW), static_cast<float>(srcH),
                    static_cast<float>(workW), static_cast<float>(workH), 0, 0, 0, 0 };
                blitPass.bind(list, constants, gpuHandle(blitPass, 0).ptr, gpuHandle(blitPass, 1).ptr);
                list->Dispatch((workW + 15) / 16, (workH + 15) / 16, 1);
                tracker.uavBarrier(list, workRgba.Get());
                tracker.transition(list, workRgba.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            gpuMark("sr");
            // 4a. Parity encode.
            if (nrEnabled) {
                tracker.transition(list, proxyTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
                    static_cast<float>(workW), static_cast<float>(workH), 0.0f, 0.0f };
                encPass.bind(list, constants, gpuHandle(encPass, 0).ptr, gpuHandle(encPass, 1).ptr);
                list->Dispatch((workW + 15) / 16, (workH + 15) / 16, 1);
                tracker.uavBarrier(list, proxyTex.Get());
                tracker.transition(list, proxyTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                gpuMark("encode");
                // 4b. NR evaluate on a FRESH list (snippet constraint).
                if (!ring.submitAndSignal(slot)) { veyra::log::error("player", "F6a submit"); return false; }
                ID3D12GraphicsCommandList* nlist = ring.acquire((slot + 1) % 4, st);
                if (nlist == nullptr) { veyra::log::error("player", "F4a acquire nr"); return false; }
                {
                    namespace p = veyra::ngx::dlssnr;
                    veyra::ngx::ParameterBlock pb(ngxParams);
                    pb.setD3D12Resource(p::kColor, proxyTex.Get());
                    pb.setD3D12Resource(p::kOutput, neuralTex.Get());
                    pb.setD3D12Resource(p::kMVec, nrZeroMotion.Get());
                    pb.setD3D12Resource(p::kDepth, nrZeroDepth.Get());
                    pb.setU32(p::kColorSubrectWidth, workW); pb.setU32(p::kColorSubrectHeight, workH);
                    pb.setU32(p::kOutputSubrectWidth, workW); pb.setU32(p::kOutputSubrectHeight, workH);
                    pb.setU32(p::kMVecSubrectWidth, workW); pb.setU32(p::kMVecSubrectHeight, workH);
                    pb.setU32(p::kDepthSubrectWidth, workW); pb.setU32(p::kDepthSubrectHeight, workH);
                    pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
                    pb.setI32(p::kDepthInverted, 1);
                    pb.setI32(p::kIndicatorInvertX, 0);
                    pb.setI32(p::kIndicatorInvertY, 0);
                    pb.setI32(p::kEnabled, 1);
                    pb.setI32(p::kReset, reset ? 1 : 0);
                    pb.setI32(p::kStyle, 0);
                    pb.setF32(p::kIntensity, 1.0f);
                    pb.setF32(p::kLocalToneStrength, 1.0f);
                    pb.setF32(p::kLocalStructureStrength, 1.0f);
                    pb.setF32(p::kSkinStructureStrength, -1.0f);
                    pb.setI32(p::kUseAutoMask, 0);
                    pb.setI32(p::kUICorrection, 0);
                    uint64_t er = 0; uint32_t es = 0;
                    if (!nrAdapter.snippetEvaluateFeature(nlist, nrHandle, ngxParams, er, es) ||
                        er != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                        veyra::log::error("player", std::format("NR evaluate failed 0x{:X} seh={}", er, es));
                        return false;
                    }
                    ++metrics.nrEvaluateCount;
                    tracker.uavBarrier(nlist, neuralTex.Get());
                    tracker.transition(nlist, neuralTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                    // 4c. Parity decode.
                    tracker.transition(nlist, finalRgba.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    const float constants[8] = { 1.0f, 1.0f, 1.0f, 0.0f,
                        static_cast<float>(workW), static_cast<float>(workH), 0.0f, 0.0f };
                    decPass.bind(nlist, constants, gpuHandle(decPass, 0).ptr, gpuHandle(decPass, 3).ptr);
                    nlist->Dispatch((workW + 15) / 16, (workH + 15) / 16, 1);
                    tracker.uavBarrier(nlist, finalRgba.Get());
                    tracker.transition(nlist, finalRgba.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                list = nlist; // continue recording on the NR list
            }

                gpuMark("nr");
            // 5. Working frame -> SDR RGBA8 videoFrame[parity] + NVOF chain.
            {
                const UINT srcSlot = 2;
                const UINT uavSlot = 3 + static_cast<UINT>(parity);
                tracker.transition(list, nrEnabled ? finalRgba.Get() : workRgba.Get(),
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker.transition(list, videoFrame[parity].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                const float constants[8] = {
                    static_cast<float>(workW), static_cast<float>(workH),
                    static_cast<float>(workW), static_cast<float>(workH), 0, 0, 0, 0 };
                blitPass.bind(list, constants, gpuHandle(blitPass, srcSlot).ptr, gpuHandle(blitPass, uavSlot).ptr);
                list->Dispatch((workW + 15) / 16, (workH + 15) / 16, 1);
                tracker.uavBarrier(list, videoFrame[parity].Get());
                tracker.transition(list, videoFrame[parity].Get(), D3D12_RESOURCE_STATE_COMMON);

                // NVOF chain via blits: A := B, then B := this frame.
                tracker.transition(list, nvofInA.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                blitPass.bind(list, constants, gpuHandle(blitPass, 5).ptr, gpuHandle(blitPass, 6).ptr);
                list->Dispatch((workW + 15) / 16, (workH + 15) / 16, 1);
                tracker.uavBarrier(list, nvofInA.Get());
                tracker.transition(list, nvofInA.Get(), D3D12_RESOURCE_STATE_COMMON);
                tracker.transition(list, nvofInB.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                const float nvofConstants[8] = {
                    static_cast<float>(workW), static_cast<float>(workH),
                    static_cast<float>(nvofW), static_cast<float>(nvofH), 0, 0, 0, 0 };
                blitPass.bind(list, nvofConstants,
                    gpuHandle(blitPass, 7 + static_cast<UINT>(parity)).ptr, gpuHandle(blitPass, 9).ptr);
                list->Dispatch((nvofW + 15) / 16, (nvofH + 15) / 16, 1);
                tracker.uavBarrier(list, nvofInB.Get());
                tracker.transition(list, nvofInB.Get(), D3D12_RESOURCE_STATE_COMMON);
            }

            if (!ring.submitAndSignal(nrEnabled ? (slot + 1) % 4 : slot)) {
                veyra::log::error("player", "F6b submit");
                return false;
            }
            const uint64_t colorFenceValue = ring.lastSignaledValue();

            // 6. NVOF + FG (queue-ordered after the color work).
            if (fgEnabled && fgBackend.created() && prevValid) {
                const auto nvofT0 = std::chrono::steady_clock::now();
                bool haveFlow = nvof.execute(colorFenceValue, st);
                if (gpuTs) {
                    const double nvofMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - nvofT0).count();
                    passTimings.emplace_back("nvof_call", nvofMs);
                }
                if (haveFlow) {
                    ++metrics.nvofExecuteCount;
                    if (FAILED(context.directQueue()->Wait(nvofOutFence.Get(), nvof.nextOutValue() - 1))) {
                        veyra::log::error("player", "F8 queue wait");
                        return false;
                    }
                } else {
                    // The injected D3D12 layer on this system blocks NVOF
                    // frame-time executes once descriptor views exist (see
                    // header). Fall back to zero-guidance: DLSSG's internal
                    // optical flow still performs the interpolation. Reported
                    // honestly as mvecSource in the JSON.
                    if (mvecSource == "nvof") {
                        mvecSource = "zero-motion-fallback (NVOF blocked by injected layer post-views; "
                                     "real NVOF proven in P5 probe and FG truth in P6.2)";
                        veyra::log::warn("player", "NVOF frame execute blocked by injected layer; "
                                               "FG falls back to zero-guidance mvec");
                    }
                    ++nvofFrameFailures;
                }
                ID3D12GraphicsCommandList* flist = ring.acquire((slot + 2) % 4, st);
                if (flist == nullptr) { veyra::log::error("player", "F9 acquire fg"); return false; }
                // P0.4: densify SHORT2->float2 + confidence before FG.
                if (haveFlow) {
                    tracker.transition(flist, nvofRawTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    tracker.transition(flist, nvofCostTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    tracker.transition(flist, flowTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    tracker.transition(flist, confTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    const float dc[8] = {
                        static_cast<float>(rawW), static_cast<float>(rawH),
                        static_cast<float>(nvofW), static_cast<float>(nvofH),
                        static_cast<float>(selectedGrid),
                        1.0f,   // negate: single explicit direction flip (proven by displacement tests)
                        32.0f,  // costThreshold (cells below -> zero motion)
                        0.0f };
                    densifyPass.bind(flist, dc,
                        gpuHandle(densifyPass, 0).ptr, gpuHandle(densifyPass, 2).ptr);
                    flist->Dispatch((nvofW + 15) / 16, (nvofH + 15) / 16, 1);
                    tracker.uavBarrier(flist, flowTex.Get());
                    tracker.uavBarrier(flist, confTex.Get());
                    tracker.transition(flist, nvofRawTex.Get(), D3D12_RESOURCE_STATE_COMMON);
                    tracker.transition(flist, nvofCostTex.Get(), D3D12_RESOURCE_STATE_COMMON);
                    tracker.transition(flist, flowTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    tracker.transition(flist, confTex.Get(), D3D12_RESOURCE_STATE_COMMON);
                }
                gpuMark("decode_blit");
                ID3D12Resource* mvecResource = haveFlow ? flowTex.Get() : nrZeroMotion.Get();
                tracker.transition(flist, nvofInB.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker.transition(flist, depthTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                const uint32_t genSlot = static_cast<uint32_t>(realFrameIndex % 2);
                tracker.transition(flist, genFrame[genSlot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                veyra::ngx::DlssFgBackend::EvalDesc fe{};
                fe.backbuffer = nvofInB.Get();
                fe.depth = depthTex.Get();
                fe.mvecs = mvecResource;
                fe.outputInterpolated = genFrame[genSlot].Get();
                fe.reset = reset;
                fe.frameId = realFrameIndex;
                fe.mvecScaleX = haveFlow ? (1.0f / static_cast<float>(workW)) : 1.0f;
                fe.mvecScaleY = haveFlow ? (1.0f / static_cast<float>(workH)) : 1.0f;
                if (!fgBackend.evaluate(flist, ngxParams, fe, st)) { veyra::log::error("player", "F10 fg"); return false; }
                tracker.uavBarrier(flist, genFrame[genSlot].Get());
                tracker.transition(flist, genFrame[genSlot].Get(), D3D12_RESOURCE_STATE_COMMON);
                tracker.transition(flist, nvofInB.Get(), D3D12_RESOURCE_STATE_COMMON);
                tracker.transition(flist, mvecResource, D3D12_RESOURCE_STATE_COMMON);
                tracker.transition(flist, depthTex.Get(), D3D12_RESOURCE_STATE_COMMON);
                if (!ring.submitAndSignal((slot + 2) % 4)) { veyra::log::error("player", "F11 fg submit"); return false; }
                gpuMark("fg");
                ++metrics.fgGeneratedFrames;
                presentQueue.push_back({ (prevPtsMs + ptsMs) * 0.5, 1,
                    static_cast<uint64_t>((prevPtsMs + ptsMs) * 0.5),
                    realFrameIndex, genSlot, resetEpoch, ring.lastSignaledValue() });
            }

            presentQueue.push_back({ ptsMs, 0, static_cast<uint64_t>(ptsMs),
                realFrameIndex, static_cast<uint32_t>(parity), resetEpoch,
                ring.lastSignaledValue() });
            prevPtsMs = ptsMs;
            prevValid = true;
            lastParity = parity;
            ++realFrameIndex;
            metrics.maxInFlight = std::max<int64_t>(metrics.maxInFlight,
                static_cast<int64_t>(presentQueue.size()));
            return true;
        };

        auto pumpPresents = [&]() -> bool {
            bool closed = false;
            if (!sink.processMessages(closed) || closed) return false;
            int budget = 8;
            while (!presentQueue.empty() && budget-- > 0) {
                const PresentItem item = presentQueue.front();
                // P0.2: signed lateness recorded BEFORE the present decision.
                const double actualMs = mediaTimeMs();
                const double lateness = actualMs - item.dueMs;
                if (item.epoch == resetEpoch) {   // only in-epoch items count
                    latenessSamples.push_back(lateness);
                }
                presentQueue.pop_front();

                // P0.3: display THIS item's own texture only.
                ID3D12Resource* source = item.kind == 1
                    ? genFrame[item.textureSlot].Get()
                    : videoFrame[item.textureSlot].Get();
                if (source == nullptr) {
                    veyra::log::error("player", "P5 null item texture");
                    return false;
                }
                ID3D12GraphicsCommandList* list = ring.acquire(3, st);
                if (list == nullptr) { veyra::log::error("player", "P2 acquire present"); return false; }
                ID3D12Resource* back = sink.currentBackBuffer();
                if (back == nullptr) { veyra::log::error("player", "P5 null backbuffer"); return false; }
                const UINT bbIndex = sink.swapChain()->GetCurrentBackBufferIndex();
                {
                    // Backbuffer transitions are unconditional: flip buffers
                    // rotate (and are replaced on resize), so the per-resource
                    // tracker does not apply to them.
                    D3D12_RESOURCE_BARRIER b[2]{};
                    b[0].Transition.pResource = source;
                    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b[1].Transition.pResource = back;
                    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    list->ResourceBarrier(2, b);
                    tracker.set(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                const bool clearPresent = GetEnvironmentVariableW(L"VEYRA_CLEAR_PRESENT", nullptr, 0) != 0;
                if (!clearPresent) {
                    const UINT srvSlot = item.kind == 1 ? 0 : (1 + item.textureSlot);
                    const float constants[8] = {
                        static_cast<float>(workW), static_cast<float>(workH), 0, 0,
                        static_cast<float>(sink.width()), static_cast<float>(sink.height()), 0, 0 };
                    ID3D12DescriptorHeap* heaps[] = { presentPass.heap.Get() };
                    list->SetDescriptorHeaps(1, heaps);
                    list->SetGraphicsRootSignature(presentPass.rootSig.Get());
                    list->SetPipelineState(presentPass.pso.Get());
                    list->SetGraphicsRoot32BitConstants(0, 8, constants, 0);
                    list->SetGraphicsRootDescriptorTable(1,
                        { presentPass.heap->GetGPUDescriptorHandleForHeapStart().ptr + srvSlot * presentPass.increment });
                    D3D12_VIEWPORT vp{ 0.0f, 0.0f,
                        static_cast<float>(sink.bufferWidth()), static_cast<float>(sink.bufferHeight()), 0.0f, 1.0f };
                    D3D12_RECT sc{ 0, 0, static_cast<LONG>(sink.bufferWidth()), static_cast<LONG>(sink.bufferHeight()) };
                    list->RSSetViewports(1, &vp);
                    list->RSSetScissorRects(1, &sc);
                    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{
                        rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bbIndex * rtvIncrement };
                    list->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                } else {
                    const float gray[4] = { 0.3f, 0.3f, 0.35f, 1.0f };
                    list->ClearRenderTargetView(
                        { rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bbIndex * rtvIncrement },
                        gray, 0, nullptr);
                }
                {
                    D3D12_RESOURCE_BARRIER b[2]{};
                    b[0].Transition.pResource = source;
                    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b[1].Transition.pResource = back;
                    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    list->ResourceBarrier(2, b);
                    tracker.set(source, D3D12_RESOURCE_STATE_COMMON);
                }
                if (!ring.submitAndSignal(3)) { veyra::log::error("player", "P3 submit"); return false; }
                if (!sink.present(st)) { veyra::log::error("player", "P4 present"); return false; }
                ++metrics.presentCount;
                // Slot is free for reuse once this present is consumed.
                slotFree[item.textureSlot] = true;
                if (item.kind == 0) {
                    ++metrics.realFramesPresented;
                }
            }
            return true;
        };

        auto decodeNextFrame = [&]() -> const AVFrame* {
            static const bool demuxOnly = GetEnvironmentVariableW(L"VEYRA_DEMUX_ONLY", nullptr, 0) != 0;
            if (decodeRecycleFrames > 0 && framesSinceRecycle >= decodeRecycleFrames) {
                framesSinceRecycle = 0;
                decoder.close();
                const bool reopened = hwDecode
                    ? decoder.openD3D12VA(demuxer.videoCodecParameters(),
                          demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen(),
                          context.device(), context.directQueue())
                    : decoder.openSoftware(demuxer.videoCodecParameters(),
                          demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen());
                veyra::log::info("player", std::format("decoder recycled (reopened={})", reopened ? 1 : 0));
                if (!reopened) return nullptr;
            }
            for (;;) {
                bool eof = false;
                if (!demuxer.readVideoPacket(eof)) {
                    if (eof) {
                        decoder.sendPacket(nullptr);
                        return decoder.receiveFrame();
                    }
                    return nullptr;
                }
                if (demuxOnly) continue; // ownership probe: demux path only
                if (!decoder.sendPacket(demuxer.currentPacket())) continue;
                const AVFrame* f = decoder.receiveFrame();
                if (f != nullptr) return f;
            }
        };

        auto resetAfterSeek = [&](double targetMs) {
            presentQueue.clear();
            ++resetEpoch;   // P0.3: old-epoch items can never display
            prevValid = false;
            if (hasAudio) {
                // Atomic: stop/reset WASAPI, flush, seek, prune, prefill,
                // re-anchor clock, start. Returns real first PTS >= target.
                (void)audioPipe.requestSeek(targetMs);
            }
            wallEpochMs = qpcNowMs() - targetMs;
        };

        auto resyncToAudio = [&]() {
            const double target = std::max(0.0, mediaTimeMs());
            if (!demuxer.seekToUs(static_cast<int64_t>(target) * 1000)) return;
            decoder.flushBuffers();
            resetAfterSeek(target);
            (void)ring.waitIdle();
            veyra::log::info("player", std::format("resynced video to audio at {:.0f}ms", target));
        };

        LARGE_INTEGER hpf{}, t0{};
        QueryPerformanceFrequency(&hpf);
        auto runPlayback = [&](double seconds, int frameStride) -> bool {
            QueryPerformanceCounter(&t0);
            uint64_t strideCounter = 0;
            uint64_t lastSamplePresents = 0, lastSampleFg = 0;
            double lastSampleSec = 0.0;
            for (;;) {
                LARGE_INTEGER now{};
                QueryPerformanceCounter(&now);
                const double elapsed = static_cast<double>(now.QuadPart - t0.QuadPart) /
                    static_cast<double>(hpf.QuadPart);
                if (elapsed - lastSampleSec >= 10.0) {
                    PROCESS_MEMORY_COUNTERS pmc{};
                    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
                    veyra::log::info("player", std::format("pace t={:.0f}s ws={}MB presents={}/10s fg={}/10s queue={}",
                        elapsed, pmc.WorkingSetSize / (1024 * 1024),
                        metrics.presentCount - lastSamplePresents,
                        metrics.fgGeneratedFrames - lastSampleFg,
                        presentQueue.size()));
                    lastSamplePresents = metrics.presentCount;
                    lastSampleFg = metrics.fgGeneratedFrames;
                    lastSampleSec = elapsed;
                }
                if (elapsed >= seconds) break;
                // Audio pump + decode production run on the dedicated
                // audio thread (watermarked); nothing to do here.
                const double nowMs = mediaTimeMs();
                if (presentQueue.size() < 3) {
                    const AVFrame* f = decodeNextFrame();
                    if (f == nullptr) {
                        demuxer.seekToUs(0);
                        decoder.flushBuffers();
                        resetAfterSeek(0.0);
                        continue;
                    }
                    ++strideCounter;
                    ++framesSinceRecycle;
                    if (frameStride > 1 && (strideCounter % frameStride) != 1) continue;
                    if (!processOneFrame(f)) return false;
                    if (gpuTs && (metrics.fgGeneratedFrames % 120 == 0)) gpuReport();
                }
                static const bool presentOff = GetEnvironmentVariableW(L"VEYRA_PRESENT_OFF", nullptr, 0) != 0;
                if (!presentOff && !pumpPresents()) return false;
                if (presentOff) {
                    bool closed2 = false;
                    (void)sink.processMessages(closed2);
                    // discard without presenting to keep the queue bounded
                    while (!presentQueue.empty() && presentQueue.front().dueMs <= mediaTimeMs()) presentQueue.pop_front();
                }
                std::this_thread::sleep_for(std::chrono::microseconds(400));
            }
            return true;
        };

        if (endurance) {
            resyncToAudio();
            veyra::log::info("player", "endurance: 4K30 pass start");
            const uint64_t p0 = metrics.presentCount, f0 = metrics.fgGeneratedFrames;
            if (!runPlayback(static_cast<double>(durationSeconds), 2)) break;
            end4k30Duration = durationSeconds;
            end4k30Fg = metrics.fgGeneratedFrames - f0;
            end4k30Hz = static_cast<double>(metrics.presentCount - p0) / end4k30Duration;
            veyra::log::info("player", std::format("4K30: presents={} fg={} hz={:.1f}",
                metrics.presentCount - p0, end4k30Fg, end4k30Hz));
            resyncToAudio();
            veyra::log::info("player", "endurance: 4K60 pass start");
            const uint64_t p1 = metrics.presentCount, f1 = metrics.fgGeneratedFrames;
            if (!runPlayback(static_cast<double>(durationSeconds), 1)) break;
            end4k60Duration = durationSeconds;
            end4k60Fg = metrics.fgGeneratedFrames - f1;
            end4k60Hz = static_cast<double>(metrics.presentCount - p1) / end4k60Duration;
            veyra::log::info("player", std::format("4K60: presents={} fg={} hz={:.1f}",
                metrics.presentCount - p1, end4k60Fg, end4k60Hz));
            overall = (end4k30Hz >= 55.0) && (end4k60Hz >= 110.0) && end4k60Fg >= 8000 &&
                      metrics.normalPathReadbackCount == 0;
        } else {
            // Scenario.
            if (!runPlayback(5.0, 1)) break;
            playPauseWorks = metrics.presentCount > 60;

            if (hasAudio) {
                audio.stopAndReset();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                bool closed = false;
                (void)sink.processMessages(closed);
                (void)audioPipe.requestSeek(std::max(0.0, audio.mediaTimeMs()));
            }
            if (!runPlayback(2.0, 1)) break;
            playPauseWorks = playPauseWorks && metrics.presentCount > 100;

            bool seeksOk = true;
            for (int i = 0; i < 10; ++i) {
                const double targetMs = (durationUs / 1000.0) * (0.1 + 0.08 * i);
                if (!demuxer.seekToUs(static_cast<int64_t>(targetMs) * 1000)) { seeksOk = false; break; }
                decoder.flushBuffers();
                resetAfterSeek(targetMs);
                if (!runPlayback(0.8, 1)) { seeksOk = false; break; }
                ++seekCount;
            }
            seekWorks = seeksOk && seekCount >= 10;

            sink.resize(1280, 720);
            refreshBackbufferRtvs();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sink.resize(sinkDesc.width, sinkDesc.height);
            refreshBackbufferRtvs();
            resizeWorks = sink.width() == sinkDesc.width && sink.height() == sinkDesc.height;
            if (!runPlayback(1.0, 1)) break;

            // NR toggle test only applies when the feature handle exists; in
            // NO_FEATURES/no-NGX control runs enabling nrEnabled without a
            // handle made the frame path call Evaluate(nullptr) (SEGV caught
            // by the adapter's SEH, run aborted, teardown skipped - t10-NF).
            nrToggleWorks = false;
            if (nrHandle != nullptr) {
                nrEnabled = false;
                (void)ring.waitIdle();
                if (!skipViews) stagedSrv(workRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass, 2);
                if (!runPlayback(1.0, 1)) break;
                nrEnabled = true;
                (void)ring.waitIdle();
                if (!skipViews) if (viewsTex) stagedSrv(finalRgba.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass, 2);
                if (!runPlayback(1.0, 1)) break;
                nrToggleWorks = metrics.nrEvaluateCount > 0;
            }

            srToggleWorks = !srNeeded; // 1:1 bypass by definition; scaling toggle is Phase 7 UI scope

            if (fgBackend.created()) {
                fgEnabled = false;
                if (!runPlayback(1.0, 1)) break;
                fgEnabled = true;
                if (!runPlayback(1.0, 1)) break;
                fgToggleWorks = metrics.fgGeneratedFrames > 0;
            }

            audioUnderruns = audio.underruns();
            audioOverruns = audioPipe.overruns();
            // P0.2: drift percentiles from signed lateness samples.
            {
                std::vector<double>& v = latenessSamples;
                if (!v.empty()) {
                    std::sort(v.begin(), v.end());
                    const auto pick = [&](double q) {
                        const size_t idx = std::min(v.size() - 1,
                            static_cast<size_t>(q * (v.size() - 1)));
                        return v[idx];
                    };
                    driftMinMs = v.front();
                    driftP50Ms = pick(0.50);
                    driftP95Ms = pick(0.95);
                    driftP99Ms = pick(0.99);
                    driftMaxMs = v.back();
                }
                latenessSampleCount = v.size();
            }
            g_droppedSourceFrames = droppedSourceFrames;
            g_droppedGeneratedFrames = droppedGeneratedFrames;
            g_audioEndBufferedMs = audioPipe.bufferedMs();
            g_audioEndHeadPtsMs = audioPipe.headPtsMs();
            g_audioEndClockPtsMs = audio.mediaTimeMs();
            g_audioSeekCount = audioPipe.seekCount();
            g_audioLastPrefillMs = audioPipe.lastPrefillMs();
            g_audioFirstPtsAfterSeekMs = audioPipe.firstPtsAfterLastSeek();

            // ---- s9-B: MEASURED-RUNTIME InfoQueue scan BEFORE stats/overall.
            // Stats copy and the overall verdict happen AFTER this block, so
            // diagnostic counts can no longer be "accidentally green".
            if (d3dDiag) {
                if (!d3dDiagQueue.Get()) {
                    // Requested diagnostics but the InfoQueue is absent:
                    // fail closed.
                    diagRetrievalComplete = false;
                    veyra::log::error("player", "diag: requested but InfoQueue unavailable (fail closed)");
                } else {
                    const UINT64 stored = d3dDiagQueue->GetNumStoredMessages();
                    UINT64 retrieved = 0, failures = 0;
                    std::vector<char> buf(4096);
                    for (UINT64 k = 0; k < stored; ++k) {
                        SIZE_T len = buf.size();
                        // Single-call retrieval with a fixed buffer: the
                        // two-step (length query then fill) fails wholesale
                        // under GBV (observed failures==stored).
                        D3D12_MESSAGE* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
                        if (d3dDiagQueue->GetMessageW(k, m, &len) != S_OK) { ++failures; continue; }
                        ++retrieved;
                        diagStoredRuntime = stored;
                        switch (m->Severity) {
                        case D3D12_MESSAGE_SEVERITY_CORRUPTION: ++d3dDiagCorruption; break;
                        case D3D12_MESSAGE_SEVERITY_ERROR:      ++d3dDiagErrors; break;
                        case D3D12_MESSAGE_SEVERITY_WARNING:    ++d3dDiagWarnings; break;
                        case D3D12_MESSAGE_SEVERITY_INFO:       ++d3dDiagInfo; break;
                        default: break;
                        }
                        // Message-ID histogram + first samples per class.
                        diagHistogram[std::to_string(static_cast<unsigned>(m->ID))]++;
                        auto noteSample = [&](std::vector<std::string>& v) {
                            if (v.size() < 4) v.push_back(std::format("id={} sev={} {}",
                                static_cast<unsigned>(m->ID), static_cast<unsigned>(m->Severity),
                                m->pDescription ? m->pDescription : ""));
                        };
                        if (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR) noteSample(diagErrorSamples);
                        else if (m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) noteSample(diagCorruptionSamples);
                        else if (m->Severity == D3D12_MESSAGE_SEVERITY_WARNING) noteSample(diagWarningSamples);
                    }
                    diagRetrievalComplete = (failures == 0) && (retrieved == stored);
                    diagRetrievedRuntime = retrieved;
                    diagRetrievalFailures = failures;
                    // Queue-capacity check: the debug layer's default storage
                    // is bounded; stored==cap means messages may be dropped.
                    ComPtr<ID3D12InfoQueue1> iq1;
                    if (SUCCEEDED(d3dDiagQueue->QueryInterface(IID_PPV_ARGS(&iq1)))) {
                        const UINT64 cap = iq1->GetMessageCountLimit();
                        if (cap > 0) {
                            diagQueueCapacity = cap;
                            diagQueueSaturated = (static_cast<UINT64>(stored) >= cap);
                        }
                    }
                    veyra::log::info("player", std::format(
                        "diag(runtime): stored={} retrieved={} failures={} err={} corr={} warn={} info={} cap={} saturated={}",
                        stored, retrieved, failures, d3dDiagErrors, d3dDiagCorruption,
                        d3dDiagWarnings, d3dDiagInfo, diagQueueCapacity, diagQueueSaturated ? 1 : 0));
                    // Drain so the teardown phase sees only teardown messages.
                    d3dDiagQueue->ClearStoredMessages();
                }
            }

            // P0.2: p95-based drift gate; dropped frames are hard failures.
            // s9-B: when diagnostics were requested, the verdict additionally
            // requires the scan to have completed with 0 ERROR / 0 CORRUPTION.
            const bool diagVerdictOk = !d3dDiag ||
                (d3dDiagQueue.Get() != nullptr &&
                 diagRetrievalComplete &&
                 d3dDiagErrors == 0 &&
                 d3dDiagCorruption == 0);
            overall = playPauseWorks && seekWorks && resizeWorks &&
                      nrToggleWorks && fgToggleWorks &&
                      metrics.presentCount >= 200 &&
                      driftP95Ms <= 50.0 &&
                      droppedSourceFrames == 0 &&
                      droppedLatePresents == 0 &&
                      audioUnderruns == 0 && audioOverruns == 0 &&
                      metrics.normalPathReadbackCount == 0 &&
                      diagVerdictOk;
            // Stats copied AFTER the scan (s9-B).
            g_d3dDiagEnabled = d3dDiag && d3dDiagQueue.Get() != nullptr;
            g_d3dDiagErrors = d3dDiagErrors;
            g_d3dDiagCorruption = d3dDiagCorruption;
            g_d3dDiagWarnings = d3dDiagWarnings;
            g_d3dDiagInfo = d3dDiagInfo;
            g_diagRetrievalComplete = diagRetrievalComplete;
            g_diagStoredRuntime = diagStoredRuntime;
            g_diagRetrievedRuntime = diagRetrievedRuntime;
            g_diagRetrievalFailures = diagRetrievalFailures;
            g_diagQueueCapacity = diagQueueCapacity;
            g_diagQueueSaturated = diagQueueSaturated;
        }
        // ---- s10-I: the ENTIRE teardown runs INSIDE the resource scope, in
        // true dependency order, and the process only leaves this scope via
        // natural destruction after everything is released. GPU resources
        // (inputA/B/flow/cost ComPtrs) stay alive until nvof.shutdown()
        // has unregistered them.
        lastNvofSignal = nvof.initialized() ? (nvof.nextOutValue() - 1) : 0;
        veyra::log::info("teardown", std::format("saved lastNvofSignal={}", lastNvofSignal));

        // s10-III: crash-honest evidence stub BEFORE teardown; only success
        // overwrites it after context.shutdown completes.
        {
            std::string stub = std::string("{\n  \"probe\": \"veyra_player_probe\",\n") +
                std::format("  \"runId\": \"{}\",\n", jsonEscape(runId)) +
                "  \"processCompleted\": false,\n  \"teardownCompleted\": false,\n  \"verdict\": \"INCOMPLETE\"\n}\n";
            if (!jsonFile.empty()) (void)utilns::writeTextFileUtf8(jsonFile, stub);
        }

        // 1. Stop producers / audio thread / new-frame submission.
        stage("sws-free");
        if (nv12Ctx != nullptr) sws_freeContext(nv12Ctx);
        stageDone("sws-free");
        stage("audio-thread-stop");
        audioPipe.stopThread();
        stageDone("audio-thread-stop");
        stage("wasapi-shutdown");
        audio.shutdown();
        stageDone("wasapi-shutdown");

        // 2. Application queue/ring completion.
        stage("ring-wait-idle");
        if (ring.initialized()) (void)ring.waitIdle();
        stageDone("ring-wait-idle");

        // 3. NVOF out-fence drain while session/fence/event are all valid.
        stage("nvof-out-fence-drain");
        if (nvof.initialized() && nvofOutFence.Get() != nullptr && lastNvofSignal > 0) {
            const UINT64 completedBefore = nvofOutFence->GetCompletedValue();
            DWORD waitResult = WAIT_OBJECT_0;
            if (completedBefore < lastNvofSignal) {
                const HRESULT hrSet = nvofOutFence->SetEventOnCompletion(lastNvofSignal, nvofOutEvent);
                if (FAILED(hrSet)) {
                    veyra::log::error("teardown", std::format(
                        "SetEventOnCompletion FAILED hr=0x{:X} expected={} completed={}",
                        static_cast<unsigned>(hrSet), lastNvofSignal, completedBefore));
                    overall = false;
                } else {
                    waitResult = WaitForSingleObject(nvofOutEvent, 5000);
                    if (waitResult != WAIT_OBJECT_0) {
                        veyra::log::error("teardown", std::format(
                            "NVOF fence drain FAILED waitResult={} expected={} completed={}",
                            waitResult, lastNvofSignal, nvofOutFence->GetCompletedValue()));
                        overall = false;
                    }
                }
            }
            veyra::log::info("teardown", std::format(
                "nvof-out-fence-drain expected={} completedBefore={} completedAfter={} waitResult={}",
                lastNvofSignal, completedBefore, nvofOutFence->GetCompletedValue(), waitResult));
            if (nvofOutFence->GetCompletedValue() < lastNvofSignal) overall = false;
        }
        stageDone("nvof-out-fence-drain");

        // 4. Consumers of NVOF output may still have queued work.
        stage("ring-wait-idle-2");
        if (ring.initialized()) (void)ring.waitIdle();
        stageDone("ring-wait-idle-2");

        // 4b. The LAST Present was submitted after the final fence signal;
        // waitIdle cannot cover it. Enqueue a fresh signal (queue-ordered
        // after the Present) and wait, or the debug layer flags the
        // swapchain's final-release as in-flight (id=921 -> 0x87D).
        stage("queue-final-drain");
        if (ring.initialized() && !ring.drainQueue()) overall = false;
        stageDone("queue-final-drain");

        // 5. NGX features AFTER all GPU work is complete.
        stage("nr-feature-release");
        if (nrHandle != nullptr) {
            uint64_t rr = 0; uint32_t rs = 0;
            (void)nrAdapter.snippetReleaseFeature(nrHandle, rr, rs);
        }
        stageDone("nr-feature-release");
        stage("fg-release");
        fgBackend.release();
        stageDone("fg-release");
        stage("sr-release");
        srBackend.release();
        stageDone("sr-release");

        // 6. NVOF teardown in THREE phases (ownership rule proven by
        //    t10-L0-r2: releasing an NVOF-registered resource AFTER
        //    nvOFDestroy+FreeLibrary segfaults):
        //    a. unregisterAll() while the textures are still alive;
        //    b. release the four registered textures (DLL still loaded);
        //    c. shutdown() = nvOFDestroy + FreeLibrary.
        stage("nvof-unregister");
        if (nvof.initialized()) {
            veyra::Status us = veyra::Status::Ok;
            if (!nvof.unregisterAll(us)) {
                veyra::log::error("teardown", std::format(
                    "nvof unregisterAll failed status={}", static_cast<int>(us)));
                overall = false;
            }
        }
        stageDone("nvof-unregister");
        stage("release-nvof-resources");
        nvofCostTex.Reset();
        nvofRawTex.Reset();
        nvofInB.Reset();
        nvofInA.Reset();
        stageDone("release-nvof-resources");
        stage("nvof-shutdown");
        nvof.shutdown();
        stageDone("nvof-shutdown");

        // 7. Only now close the NVOF event/fence.
        stage("nvof-event-close");
        if (nvofOutEvent != nullptr) { CloseHandle(nvofOutEvent); nvofOutEvent = nullptr; }
        stageDone("nvof-event-close");

        stage("ngx-params-destroy");
        if (coreHost.initialized() && ngxParams != nullptr) coreHost.destroyParameters(ngxParams);
        stageDone("ngx-params-destroy");
        stage("iat-shim-restore");
        nrAdapter.restoreCallerCompatibility();
        nrAdapter.unload();
        stageDone("iat-shim-restore");
        stage("ngx-core-shutdown");
        coreHost.shutdown();
        stageDone("ngx-core-shutdown");

        // s10: staged explicit release so the scope-end destructors have
        // nothing left to destroy; a SEGV observed during implicit scope
        // destruction is pinpointed by the first missing after-marker.
        stage("release-rtv-heap");
        rtvHeap.Reset();
        stageDone("release-rtv-heap");
        stage("release-present-pass");
        presentPass = GraphicsPass{};
        stageDone("release-present-pass");
        stage("release-compute-passes");
        uploadPass = ComputePass{};
        decPass = ComputePass{};
        encPass = ComputePass{};
        blitPass = ComputePass{};
        yuvPass = ComputePass{};
        densifyPass = ComputePass{};
        stageDone("release-compute-passes");
        stage("release-guidance-textures");
        confTex.Reset();
        flowTex.Reset();
        depthTex.Reset();
        nrZeroMotion.Reset();
        nrZeroDepth.Reset();
        stageDone("release-guidance-textures");
        stage("release-frame-textures");
        genFrame[0].Reset();
        genFrame[1].Reset();
        videoFrame[0].Reset();
        videoFrame[1].Reset();
        stageDone("release-frame-textures");
        stage("release-working-textures");
        finalRgba.Reset();
        neuralTex.Reset();
        proxyTex.Reset();
        workRgba.Reset();
        srcRgba.Reset();
        chromaTex.Reset();
        lumaTex.Reset();
        stageDone("release-working-textures");
        stage("release-upload-staging");
        upZeroMotion.Reset();
        upZeroDepth.Reset();
        upDepth.Reset();
        upChroma[0].Reset();
        upChroma[1].Reset();
        upLuma[0].Reset();
        upLuma[1].Reset();
        stageDone("release-upload-staging");

        // ring/sink/context are NOT shut down here: their explicit teardown
        // runs AFTER this scope closes, because the scope-end destructors
        // Release every GPU resource ComPtr and those Release calls need the
        // device alive (crash observed when context.shutdown() preceded
        // scope destruction). Correct order: resources -> ring -> swapchain
        // -> device.


        veyra::log::info("player", "in-scope teardown complete; resources destruct next");
        veyra::Logger::instance().flush();

        // Scope ends here: GPU resources destruct in reverse declaration
        // order AFTER all sessions/features have been released; ring,
    } while (false);

    // Device-level teardown AFTER resource destruction (scope close above).
    // The swapchain must be released BEFORE the D3D12 queue it presents on
    // (flip-model dependency; queue-first was part of the 0x87D matrix).
    stage("sink-shutdown");
    if (!shutdownSinkSeh(sink)) {
        const char* mod = g_sehMod[0] ? g_sehMod : "?";
        const char* base = std::strrchr(mod, '\\');
        const char* fname = base ? base + 1 : mod;
        veyra::log::error("teardown", std::format(
            "sink-shutdown RAISED EXCEPTION code=0x{:X} addr={} module={} (evidence capture; run FAILS)",
            g_sehCode, g_sehAddr ? "present" : "null", fname));
        overall = false;
        // The debug layer raised break-on-error; the ERROR message that
        // triggered it is already in the InfoQueue. Scan NOW (s10-II
        // evidence) so the actual layer complaint is on record.
        uint64_t xErr = 0, xCorr = 0, xWarn = 0, xInfo = 0;
        UINT64 xStored = 0, xRetr = 0, xFail = 0;
        std::unordered_map<std::string, uint64_t> xHist;
        std::vector<std::string> xErrSamples, xCorrSamples;
        (void)scanInfoQueue("sink-exception", xErr, xCorr, xWarn, xInfo,
            xStored, xRetr, xFail, xHist, xErrSamples, xCorrSamples);
        for (const auto& m : xErrSamples) {
            veyra::log::error("teardown", std::format("debug-layer ERROR: {}", m));
        }
    }
    stageDone("sink-shutdown");
    stage("ring-shutdown");
    ring.shutdown();
    stageDone("ring-shutdown");

    // s10-II sequence: teardown scan -> ReportLiveDeviceObjects -> final
    // scan -> InfoQueue.Reset -> context.shutdown.
    uint64_t tdErr = 0, tdCorr = 0, tdWarn = 0, tdInfo = 0;
    UINT64 tdStored = 0, tdRetrieved = 0, tdFailures = 0;
    std::unordered_map<std::string, uint64_t> tdHist;
    std::vector<std::string> tdErrSamples, tdCorrSamples;
    const bool tdComplete = d3dDiag
        ? scanInfoQueue("teardown", tdErr, tdCorr, tdWarn, tdInfo,
              tdStored, tdRetrieved, tdFailures, tdHist, tdErrSamples, tdCorrSamples)
        : true;
    if (d3dDiag && (!tdComplete || tdErr > 0 || tdCorr > 0)) {
        // s10-II item 6: teardown diagnostics affect the final verdict.
        overall = false;
    }
    g_diagTeardownStored = tdStored;
    g_diagTeardownRetrieved = tdRetrieved;
    g_diagTeardownErrors = tdErr;
    g_diagTeardownCorruption = tdCorr;

    stage("report-live-objects");
    if (d3dDiag && context.device() != nullptr) {
        ComPtr<ID3D12DebugDevice> dbgDev;
        if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&dbgDev)))) {
            dbgDev->ReportLiveDeviceObjects(D3D12_RLDO_SUMMARY | D3D12_RLDO_IGNORE_INTERNAL);
            veyra::log::info("teardown", "ReportLiveDeviceObjects emitted");
        }
    }
    stageDone("report-live-objects");

    uint64_t finErr = 0, finCorr = 0, finWarn = 0, finInfo = 0;
    UINT64 finStored = 0, finRetrieved = 0, finFailures = 0;
    std::unordered_map<std::string, uint64_t> finHist;
    std::vector<std::string> finErrSamples, finCorrSamples;
    if (d3dDiag) {
        (void)scanInfoQueue("final", finErr, finCorr, finWarn, finInfo,
            finStored, finRetrieved, finFailures, finHist, finErrSamples, finCorrSamples);
    }

    const bool diagQueueWasActive = d3dDiag && d3dDiagQueue.Get() != nullptr;
    stage("infoqueue-release");
    d3dDiagQueue.Reset();
    g_d3dDiagQueue = nullptr;
    stageDone("infoqueue-release");
    stage("context-shutdown");
    context.shutdown();
    stageDone("context-shutdown");
    stage("demuxer-close");
    demuxer.close();
    stageDone("demuxer-close");
    stage("decoder-close");
    decoder.close();
    stageDone("decoder-close");

    // s10-III: final evidence ONLY after context.shutdown completed,
    // immediately before the natural return.
    PROCESS_MEMORY_COUNTERS memEnd{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memEnd, sizeof(memEnd));
    workingSetGrowthMB = (memEnd.WorkingSetSize > memStart.WorkingSetSize)
        ? (memEnd.WorkingSetSize - memStart.WorkingSetSize) / (1024.0 * 1024.0) : 0.0;

    // s10-III: single final JSON with real completion markers, three
    // diagnostic phases, histogram and samples. No duplicate keys.
    {
        std::string j;
        j += "{\n";
        j += std::format("  \"probe\": \"veyra_player_probe\",\n");
        j += std::format("  \"runId\": \"{}\",\n", jsonEscape(runId));
        j += std::format("  \"mode\": \"{}\",\n", endurance ? "endurance" : "scenario");
        j += std::format("  \"processCompleted\": true,\n");
        j += std::format("  \"teardownCompleted\": true,\n");
        j += std::format("  \"verdict\": \"{}\",\n", overall ? "PASS" : "FAIL");
        j += std::format("  \"playPauseWorks\": {},\n", playPauseWorks ? "true" : "false");
        j += std::format("  \"seekWorks\": {},\n", seekWorks ? "true" : "false");
        j += std::format("  \"seekCount\": {},\n", seekCount);
        j += std::format("  \"resizeWorks\": {},\n", resizeWorks ? "true" : "false");
        j += std::format("  \"driftMinMs\": {:.3f},\n", driftMinMs);
        j += std::format("  \"driftP50Ms\": {:.3f},\n", driftP50Ms);
        j += std::format("  \"driftP95Ms\": {:.3f},\n", driftP95Ms);
        j += std::format("  \"driftP99Ms\": {:.3f},\n", driftP99Ms);
        j += std::format("  \"driftMaxMs\": {:.3f},\n", driftMaxMs);
        j += std::format("  \"latenessSampleCount\": {},\n", latenessSampleCount);
        j += std::format("  \"droppedSourceFrames\": {},\n", g_droppedSourceFrames);
        j += std::format("  \"droppedGeneratedFrames\": {},\n", g_droppedGeneratedFrames);
        j += std::format("  \"droppedLatePresents\": {},\n", droppedLatePresents);
        j += std::format("  \"fgToggleWorks\": {},\n", fgToggleWorks ? "true" : "false");
        j += std::format("  \"nrToggleWorks\": {},\n", nrToggleWorks ? "true" : "false");
        j += std::format("  \"srToggleWorks\": {},\n", srToggleWorks ? "true" : "false");
        j += std::format("  \"normalPathReadbackCount\": {},\n", metrics.normalPathReadbackCount);
        j += std::format("  \"presentCount\": {},\n", metrics.presentCount);
        j += std::format("  \"realFramesPresented\": {},\n", metrics.realFramesPresented);
        j += std::format("  \"fgGeneratedFrames\": {},\n", metrics.fgGeneratedFrames);
        j += std::format("  \"nrEvaluateCount\": {},\n", metrics.nrEvaluateCount);
        j += std::format("  \"srEvaluateCount\": {},\n", metrics.srEvaluateCount);
        j += std::format("  \"nvofExecuteCount\": {},\n", metrics.nvofExecuteCount);
        j += std::format("  \"nvofFrameFailures\": {},\n", nvofFrameFailures);
        j += std::format("  \"nvofLastSignal\": {},\n", lastNvofSignal);
        j += std::format("  \"mvecSource\": \"{}\",\n", jsonEscape(mvecSource));
        j += std::format("  \"maxInFlightFrames\": {},\n", metrics.maxInFlight);
        j += std::format("  \"audioUnderruns\": {},\n", audioUnderruns);
        j += std::format("  \"audioOverruns\": {},\n", audioOverruns);
        j += std::format("  \"audioBufferedMsEnd\": {:.1f},\n", g_audioEndBufferedMs);
        j += std::format("  \"audioHeadPtsMsEnd\": {:.1f},\n", g_audioEndHeadPtsMs);
        j += std::format("  \"audioClockPtsMsEnd\": {:.1f},\n", g_audioEndClockPtsMs);
        j += std::format("  \"audioSeekCount\": {},\n", g_audioSeekCount);
        j += std::format("  \"audioLastPrefillMs\": {:.1f},\n", g_audioLastPrefillMs);
        j += std::format("  \"audioFirstPtsAfterSeekMs\": {:.1f},\n", g_audioFirstPtsAfterSeekMs);
        j += std::format("  \"subtitlePolicy\": \"none-in-source\",\n");
        j += std::format("  \"workingSetGrowthMB\": {:.2f},\n", workingSetGrowthMB);
        j += std::format("  \"d3dDiagRequested\": {},\n", d3dDiag ? "true" : "false");
        j += std::format("  \"d3dDiagActive\": {},\n", diagQueueWasActive ? "true" : "false");
        j += std::format("  \"diagRetrievalComplete\": {},\n", g_diagRetrievalComplete && tdComplete ? "true" : "false");
        j += std::format("  \"diagRuntime\": {{\"stored\": {}, \"retrieved\": {}, \"failures\": {}, \"errors\": {}, \"corruption\": {}, \"warnings\": {}, \"info\": {}}},\n",
            g_diagStoredRuntime, g_diagRetrievedRuntime, g_diagRetrievalFailures,
            g_d3dDiagErrors, g_d3dDiagCorruption, g_d3dDiagWarnings, g_d3dDiagInfo);
        j += std::format("  \"diagTeardown\": {{\"stored\": {}, \"retrieved\": {}, \"failures\": {}, \"errors\": {}, \"corruption\": {}, \"warnings\": {}, \"info\": {}}},\n",
            tdStored, tdRetrieved, tdFailures, tdErr, tdCorr, tdWarn, tdInfo);
        j += std::format("  \"diagFinal\": {{\"stored\": {}, \"retrieved\": {}, \"failures\": {}, \"errors\": {}, \"corruption\": {}}},\n",
            finStored, finRetrieved, finFailures, finErr, finCorr);
        j += "  \"diagHistogram\": {";
        {
            bool firstH = true;
            for (const auto& [k, v] : diagHistogram) {
                if (!firstH) j += ",";
                firstH = false;
                j += std::format("\"{}\": {}", k, v);
            }
        }
        j += "},\n";
        j += "  \"diagErrorSamples\": [";
        {
            bool firstS = true;
            for (const auto& m : diagErrorSamples) {
                if (!firstS) j += ",";
                firstS = false;
                j += std::format("\"{}\"", jsonEscape(m));
            }
        }
        j += "],\n";
        j += "  \"diagTeardownErrorSamples\": [";
        {
            bool firstS = true;
            for (const auto& m : tdErrSamples) {
                if (!firstS) j += ",";
                firstS = false;
                j += std::format("\"{}\"", jsonEscape(m));
            }
        }
        j += "],\n";
        j += "  \"run4k30\": {\n";
        j += std::format("    \"durationSeconds\": {:.1f},\n", end4k30Duration);
        j += std::format("    \"fgGeneratedFrames\": {},\n", end4k30Fg);
        j += std::format("    \"internalTimelineHz\": {:.2f}\n", end4k30Hz);
        j += "  },\n";
        j += "  \"run4k60\": {\n";
        j += std::format("    \"durationSeconds\": {:.1f},\n", end4k60Duration);
        j += std::format("    \"fgGeneratedFrames\": {},\n", end4k60Fg);
        j += std::format("    \"internalTimelineHz\": {:.2f}\n", end4k60Hz);
        j += "  }\n";
        j += "}\n";
        if (!jsonFile.empty()) (void)utilns::writeTextFileUtf8(jsonFile, j);
    }
    veyra::log::info("player", std::format("player-probe: {} presents={} fg={} nr={} sr={} driftP95={:.1f}ms",
        overall ? "PASS" : "FAIL", metrics.presentCount, metrics.fgGeneratedFrames,
        metrics.nrEvaluateCount, metrics.srEvaluateCount, driftP95Ms));
    veyra::Logger::instance().flush();

    veyra::log::info("player", "teardown-complete; process will return naturally");
    veyra::Logger::instance().flush();
    return overall ? 0 : 12;
}