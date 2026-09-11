#pragma once

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
#include <format>
#include <mutex>
#include <limits>
#include <thread>
#include <vector>

#include "veyra/Log.h"
#include "veyra/sink/AudioPcmSource.h"
#include "veyra/sink/AudioFrameTimeline.h"

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
enum class AudioFadeResult { NotPlaying, Drained, Cancelled, TimedOut, Failed };

class AudioPipeline : public AudioPcmSource {
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
    bool decodingComplete() const { return decodedEof_.load(); }

    // Pull up to maxFrames stereo frames; sets the PTS of the first pulled
    // frame. Returns frames pulled (0 legal; caller writes silence and it is
    // counted as an underrun by the pump only when the endpoint had space).
    size_t pull(float* dst, size_t maxFrames, double* firstPtsMs) override;

    void stopThread();
    void setPaused(bool value) { paused_.store(value); }

    // File playback: audio is the master clock. Prefill while explicitly held
    // (open/seek/settings rebuild/pause); once released the device clock runs
    // at one-times speed regardless of video lag — slow enhancement drops
    // preview frames instead of pausing sound. Coverage is published for
    // diagnostics only.
    void holdForVideo();
    void videoReady(double ptsMs);
    void videoPresented(double nextPtsMs);
    bool waitingForVideo() const { return videoWaiting_.load(); }
    uint64_t videoWaitCount() const { return videoWaitCount_.load(); }

    // Seek protocol (engine thread calls; audio thread executes). Blocks
    // until the audio thread finished the atomic re-sequence and prefilled.
    // Returns the PTS of the first buffered sample after seek.
    double requestSeek(double targetMs);

    void runOnAudioThread(AudioRenderer* renderer, bool ownEndpoint = false);
    void startThread(AudioRenderer* renderer, bool ownEndpoint = false);
    bool endpointRecovering()const{return endpointRecovering_.load();}
    HRESULT endpointError()const{return endpointError_.load();}
    uint64_t endpointRecoveries()const{return endpointRecoveries_.load();}
    bool clockExhausted()const{return clockExhausted_.load();}
    // Fault injection is consumed by the audio owner; never release COM on a caller thread.
    void requestEndpointLossForTest(){endpointLossForTest_=true;}

private:
    struct Segment {
        double startPtsMs;
        size_t frames;
    };

    void pushDecoded(const AVFrame* frame);
    void pushConverted(int frames);
    void decodeBlock();
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
    bool drainSent_ = false;
    std::atomic<bool> decodedEof_{false};
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
    std::atomic<bool> seekRequested_{false};
    bool seekDone_ = true;
    double seekTargetMs_ = 0.0;

    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<uint64_t> seekCount_{0};
    std::atomic<double> lastPrefillMs_{0.0};
    std::atomic<double> firstPtsAfterSeek_{-1.0};
    std::vector<float> converted_{std::vector<float>(kAudioRate)};

    std::thread thread_;
    std::atomic<bool> endpointRecovering_{false},clockExhausted_{false},endpointLossForTest_{false};
    std::atomic<HRESULT> endpointError_{S_OK};
    std::atomic<uint64_t> endpointRecoveries_{0};
    std::atomic<bool> videoSync_{false},videoHold_{false},videoWaiting_{false};
    std::atomic<double> videoLimitMs_{std::numeric_limits<double>::infinity()};
    std::atomic<uint64_t> videoWaitCount_{0};
};

class AudioRenderer {
public:
    ~AudioRenderer(){shutdown();}
    bool start();
    void setGain(float value){gain_.store(value);}

    // Write actual PCM before starting the endpoint and its media clock.
    bool startAnchored(AudioPcmSource& pipeline, bool paused = false);
    void setPaused(bool value);

    // Event-driven pump for ONE event cycle. Writes real data when the ring
    // has it, silence otherwise (underrun counted). Returns false on hard
    // failure.
    bool waitForEvent();
    bool pumpOnce(AudioPcmSource& pipeline, double* firstWrittenPtsMs, bool wait = true);

    // Master clock, or NaN when unstarted/reset or the device clock failed.
    double mediaTimeMs() const;

    // Atomic seek support: stop + reset the endpoint; the clock is invalid
    // until the next startAnchored.
    void stopAndReset();
    // Audio-owner only; drains a 5ms fade before a controlled reset, bounded to 80ms.
    AudioFadeResult fadeAndReset(AudioPcmSource& source,const std::atomic<bool>& cancel);

    bool started() const;
    uint64_t underruns() const;
    uint64_t framesWritten() const;
    HRESULT lastError()const{return lastError_.load();}
    double bufferedMs()const{return bufferedMs_.load();}
    double capacityMs()const{std::lock_guard lock(endpointMutex_);return 1000.0*bufferFrames_/sampleRate_;}

    void shutdown();

private:
    bool checked(HRESULT hr,const char* operation)const{if(SUCCEEDED(hr))return true;if(lastError_.exchange(hr)!=hr)log::error("audio",std::format("{} hr=0x{:08X}",operation,unsigned(hr)));return false;}
    mutable std::atomic<HRESULT> lastError_{S_OK};
    // Protect endpoint lifetime/anchor from readers; pump remains audio-owner only.
    mutable std::mutex endpointMutex_;
    std::atomic<double> bufferedMs_{0};
    mutable std::mutex timelineMutex_;
    AudioFrameTimeline outputTimeline_;
    bool timedPcm_=false;
    std::atomic<uint64_t> timelineWriteFrame_{0};
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
    std::atomic<bool> running_{false};
    std::atomic<bool> fading_{false};bool pausedEndpoint_=false;
    float lastRawLeft_=0,lastRawRight_=0;
    bool comInited_ = false;
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> framesWritten_{0};
    std::vector<float> chunk_{std::vector<float>(8192 * 2)};
};

} // namespace veyra::sink
