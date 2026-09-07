#pragma once

// WasapiAudioSink - product audio playback extracted verbatim from the
// verified player_probe implementation (Phase 6 audio evidence: event-driven
// shared mode, PTS-anchored master clock, atomic seek, bounded ring, never
// dropping decoded audio in player mode).
//
// AudioPipeline: FFmpeg audio demux/decode + watermarked bounded ring
// (low 250ms / prefill 500ms / high 1000ms, 2s hard bound).
// AudioRenderer: event-driven WASAPI shared-mode endpoint whose device clock
// is mapped onto media PTS through an explicit anchor set at Start and after
// every seek restart.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "veyra/Log.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct SwrContext;
typedef struct AVFrame AVFrame;

namespace veyra::sink {

constexpr double kAudioLowWatermarkMs = 250.0;
constexpr double kAudioPrefillMs = 500.0;
constexpr double kAudioHighWatermarkMs = 1000.0;
constexpr uint32_t kAudioRate = 48000;

class AudioRenderer; // forward: pipeline thread needs the renderer

class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    bool open(const std::wstring& path);

    double bufferedMs() const;
    // PTS (ms) of the first unconsumed buffered sample; < 0 when empty.
    double headPtsMs() const;
    // Decoded-ahead PTS (ms) of the last buffered sample end; -1 if empty.
    double tailPtsMs() const;

    uint64_t underruns() const;
    uint64_t overruns() const; // must stay 0
    uint64_t seekCount() const;
    double lastPrefillMs() const;
    double firstPtsAfterLastSeek() const;

    // Pull up to maxFrames stereo frames; sets the PTS of the first pulled
    // frame. Returns frames pulled (0 legal; caller writes silence and it is
    // counted as an underrun by the pump only when the endpoint had space).
    size_t pull(float* dst, size_t maxFrames, double* firstPtsMs);

    void stopThread();
    void setPaused(bool value) { paused_.store(value); }

    // Seek protocol (engine thread calls; audio thread executes). Blocks
    // until the audio thread finished the atomic re-sequence and prefilled.
    // Returns the PTS of the first buffered sample after seek.
    double requestSeek(double targetMs);

    void runOnAudioThread(AudioRenderer* renderer);
    void startThread(AudioRenderer* renderer);

private:
    struct Segment {
        double startPtsMs;
        size_t frames;
    };

    void pushDecoded(const AVFrame* frame);
    void decodeBlock();
    double bufferedMsLocked() const;
    void closeAll();

    friend class AudioThread;

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = nullptr;
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
    std::atomic<bool> paused_{false};
    bool seekRequested_ = false;
    bool seekDone_ = true;
    double seekTargetMs_ = 0.0;

    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<uint64_t> seekCount_{0};
    std::atomic<double> lastPrefillMs_{0.0};
    std::atomic<double> firstPtsAfterSeek_{-1.0};
    std::vector<float> converted_{std::vector<float>(kAudioRate)};

    std::thread thread_;
};

class AudioRenderer {
public:
    bool start();
    void setGain(float value){gain_.store(value);}

    // Called by the audio thread after prefill: begins playback anchored at
    // the PTS of the sample that will be written first.
    bool startAnchored(double firstBufferPtsMs);
    void setPaused(bool value);

    // Event-driven pump for ONE event cycle. Writes real data when the ring
    // has it, silence otherwise (underrun counted). Returns false on hard
    // failure.
    bool pumpOnce(AudioPipeline& pipeline, double* firstWrittenPtsMs);

    // Master clock: real media PTS of the sample currently being played.
    double mediaTimeMs() const;

    // Atomic seek support: stop + reset the endpoint; the clock is invalid
    // until the next startAnchored.
    void stopAndReset();

    bool started() const;
    uint64_t underruns() const;
    uint64_t framesWritten() const;

    void shutdown();

private:
    std::atomic<float> gain_{1};float smoothedGain_=1,loggedGain_=-1;
    IMMDeviceEnumerator* enum_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    IAudioClock* clock_ = nullptr;
    HANDLE event_ = nullptr;
    UINT32 bufferFrames_ = 0;
    uint32_t sampleRate_ = kAudioRate;
    UINT64 clockFrequency_ = 0;
    std::atomic<UINT64> anchorPos_{0};
    std::atomic<double> anchorPtsMs_{0.0};
    std::atomic<bool> started_{false};
    bool running_ = false;
    bool comInited_ = false;
    std::atomic<uint64_t> underruns_{0};
    uint64_t framesWritten_ = 0;
    std::vector<float> chunk_{std::vector<float>(8192 * 2)};
};

} // namespace veyra::sink
