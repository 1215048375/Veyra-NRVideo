#include "veyra/sink/AudioGain.h"
#include "veyra/sink/WasapiAudioSink.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

namespace veyra::sink {

AudioPipeline::AudioPipeline()
    : packet_(av_packet_alloc())
{
}

AudioPipeline::~AudioPipeline()
{
    stopThread();
    closeAll();
}

bool AudioPipeline::open(const std::wstring& path)
{
    const int length=WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
    std::string narrow(length,'\0');
    WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),narrow.data(),length,nullptr,nullptr);
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

double AudioPipeline::bufferedMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return 1000.0 * static_cast<double>(ringFrames_) / kAudioRate;
}

double AudioPipeline::headPtsMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (segments_.empty()) return -1.0;
    return segments_.front().startPtsMs;
}

double AudioPipeline::tailPtsMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (segments_.empty()) return -1.0;
    const Segment& t = segments_.back();
    return t.startPtsMs + 1000.0 * static_cast<double>(t.frames) / kAudioRate;
}

uint64_t AudioPipeline::underruns() const { return underruns_.load(); }
uint64_t AudioPipeline::overruns() const { return overruns_.load(); } // must stay 0
uint64_t AudioPipeline::seekCount() const { return seekCount_.load(); }
double AudioPipeline::lastPrefillMs() const { return lastPrefillMs_.load(); }
double AudioPipeline::firstPtsAfterLastSeek() const { return firstPtsAfterSeek_.load(); }

size_t AudioPipeline::pull(float* dst, size_t maxFrames, double* firstPtsMs)
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

void AudioPipeline::stopThread()
{
    stopFlag_ = true;
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

double AudioPipeline::requestSeek(double targetMs)
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
    return seekDone_ ? firstPtsAfterSeek_.load() : std::numeric_limits<double>::quiet_NaN();
}

void AudioPipeline::pushDecoded(const AVFrame* frame)
{
    // Convert to 48k stereo float in converted_.
    if (swr_ == nullptr) {
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout inLayout = frame->ch_layout.nb_channels > 0
            ? frame->ch_layout : outLayout;
        const int result = swr_alloc_set_opts2(&swr_,
            &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate,
            &inLayout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
            0, nullptr);
        if (result < 0 || swr_ == nullptr || swr_init(swr_) < 0) {
            log::error("audio", "resampler initialization failed");
            return;
        }
    }
    // Output begins at this input PTS minus the samples retained by swr.
    // Tagging it with the input block end shifts audio by one whole block.
    const auto timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
        ? frame->best_effort_timestamp : frame->pts;
    if (timestamp != AV_NOPTS_VALUE) {
        const double ptsMs = timestamp * av_q2d(stream_->time_base) * 1000.0;
        nextPtsMs_ = ptsMs - 1000.0 * swr_get_delay(swr_, frame->sample_rate) / frame->sample_rate;
    }
    const auto capacity = swr_get_out_samples(swr_, frame->nb_samples);
    if (capacity <= 0 || capacity > static_cast<int>(kMaxRingFrames)) {
        log::error("audio", "decoded audio block exceeds bounded conversion capacity");
        return;
    }
    converted_.resize(static_cast<size_t>(capacity) * 2);
    uint8_t* planes[1] = { reinterpret_cast<uint8_t*>(converted_.data()) };
    const int outSamples = swr_convert(swr_, planes,
        static_cast<int>(converted_.size() / 2),
        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    if (outSamples < 0) { log::error("audio", std::format("swr_convert failed code={}", outSamples)); return; }
    pushConverted(outSamples);
}

void AudioPipeline::pushConverted(int frames)
{
    if (frames <= 0) return;
    size_t skip = 0;
    if (discardUntilPtsMs_ >= 0 && nextPtsMs_ < discardUntilPtsMs_) {
        skip = std::min(static_cast<size_t>(frames), static_cast<size_t>(
            std::ceil((discardUntilPtsMs_ - nextPtsMs_) * kAudioRate / 1000.0 - 1e-7)));
    }
    nextPtsMs_ += 1000.0 * skip / kAudioRate;
    const size_t addFrames = static_cast<size_t>(frames) - skip;
    if (!addFrames) return;

    std::unique_lock<std::mutex> lock(mutex_);
    if (ringFrames_ + addFrames > kMaxRingFrames) {
        // Player mode: never drop. This is a hard error (bounded ring
        // should never overflow because production is watermarked).
        overruns_.fetch_add(1);
        veyra::log::error("audio", "ring overflow despite watermarks (bug)");
        return;
    }
    ring_.insert(ring_.end(), converted_.data() + skip * 2, converted_.data() + static_cast<size_t>(frames) * 2);
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
}

void AudioPipeline::decodeBlock()
{
    AVFrame* frame = av_frame_alloc();
    if (!frame) return;
    // Always receive pending frames before sending another packet. EAGAIN
    // leaves the packet owned here until the decoder actually accepts it.
    while (bufferedMs() < kAudioHighWatermarkMs && !stopFlag_ && !seekRequested_ && !decodedEof_) {
        const int received = avcodec_receive_frame(codecCtx_, frame);
        if (received == 0) {
            pushDecoded(frame);
            av_frame_unref(frame);
            continue;
        }
        if (received == AVERROR_EOF) {
            if (swr_) {
                uint8_t* planes[] = {reinterpret_cast<uint8_t*>(converted_.data())};
                const int count = swr_convert(swr_, planes, static_cast<int>(converted_.size() / 2), nullptr, 0);
                if (count > 0) { pushConverted(count); continue; }
                if (count < 0) log::error("audio", std::format("resampler drain failed code={}", count));
            }
            decodedEof_ = true;
            break;
        }
        if (received != AVERROR(EAGAIN)) {
            log::error("audio", std::format("decode receive failed code={}", received));
            decodedEof_ = true;
            break;
        }
        if (demuxEof_) {
            if (drainSent_) { log::error("audio", "decoder requested input after drain"); decodedEof_ = true; break; }
            const int sent = avcodec_send_packet(codecCtx_, nullptr);
            if (sent < 0 && sent != AVERROR_EOF) { log::error("audio", std::format("decoder drain failed code={}", sent)); break; }
            drainSent_ = true;
            continue;
        }
        if (!havePacket_) {
            if (av_read_frame(fmt_, packet_) < 0) {
                demuxEof_ = true;
                continue;
            }
            havePacket_ = true;
            if (packet_->stream_index != streamIndex_) {
                av_packet_unref(packet_);
                havePacket_ = false;
                continue;
            }
        }
        const int sent = avcodec_send_packet(codecCtx_, packet_);
        if (sent == 0) {
            av_packet_unref(packet_);
            havePacket_ = false;
        } else if (sent != AVERROR(EAGAIN)) {
            log::error("audio", std::format("decode send failed code={}", sent));
            decodedEof_ = true;
            break;
        }
    }
    av_frame_free(&frame);
}

void AudioPipeline::closeAll()
{
    if (swr_ != nullptr) swr_free(&swr_);
    if (packet_ != nullptr) av_packet_free(&packet_);
    if (codecCtx_ != nullptr) avcodec_free_context(&codecCtx_);
    if (fmt_ != nullptr) avformat_close_input(&fmt_);
}

bool AudioRenderer::start()
{
    std::lock_guard endpointLock(endpointMutex_);
    if(client_||enum_)return checked(E_UNEXPECTED,"Endpoint already initialized");
    lastError_=S_OK;bufferedMs_=0;smoothedGain_=0;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInited_ = SUCCEEDED(hr);
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return checked(hr,"Initialize COM");
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enum_));
    if (!checked(hr,"Create MMDeviceEnumerator")) return false;
    hr = enum_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (!checked(hr,"GetDefaultAudioEndpoint")) return false;
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&client_));
    if (!checked(hr,"Activate AudioClient")) return false;
    WAVEFORMATEX mix{};
    mix.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    mix.nChannels = 2;
    mix.nSamplesPerSec = kAudioRate;
    mix.wBitsPerSample = 32;
    mix.nBlockAlign = 8;
    mix.nAvgBytesPerSec = mix.nSamplesPerSec * mix.nBlockAlign;
    sampleRate_ = kAudioRate;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        10 * 10000, 0, &mix, nullptr);
    const bool initOk = checked(hr,"Initialize AudioClient");
    if (!initOk) return false;
    if (!checked(client_->GetBufferSize(&bufferFrames_),"GetBufferSize")) return false;
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(event_==nullptr)return checked(HRESULT_FROM_WIN32(GetLastError()),"CreateEvent");
    if(!checked(client_->SetEventHandle(event_),"SetEventHandle"))return false;
    if (!checked(client_->GetService(__uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&render_)),"Get RenderClient")) return false;
    if (!checked(client_->GetService(__uuidof(IAudioClock),
            reinterpret_cast<void**>(&clock_)),"Get AudioClock")) return false;
    UINT64 freq = 0;
    if(!checked(clock_->GetFrequency(&freq),"GetFrequency"))return false;
    if(!freq)return checked(E_UNEXPECTED,"Zero audio clock frequency");
    clockFrequency_=freq;
    running_ = true;
    veyra::log::info("audio", std::format("renderer opened {}Hz event-mode buffer={} frames (not started; prefill first)",
        sampleRate_, bufferFrames_));
    return true;
}

bool AudioRenderer::startAnchored(AudioPcmSource& pipeline, bool paused)
{
    std::lock_guard endpointLock(endpointMutex_);
    double firstBufferPtsMs = -1;
    if (!pumpOnce(pipeline, &firstBufferPtsMs) || framesWritten_ == 0) return false;
    UINT64 pos = 0, qpc = 0;
    if (!checked(clock_->GetPosition(&pos, &qpc),"Anchor GetPosition") || !clockFrequency_) return false;
    anchorPos_ = pos;
    anchorPtsMs_.store(firstBufferPtsMs);
    if (!paused && !checked(client_->Start(),"Start")) return false;
    started_ = true;
    veyra::log::info("audio", std::format("renderer ANCHORED ptsMs={:.1f} devicePos={} freq={} paused={}",
        firstBufferPtsMs, pos, clockFrequency_,paused));
    return true;
}

bool AudioRenderer::waitForEvent()
{
    if(!running_)return false;
    if(started_){const auto wait=WaitForSingleObject(event_,50);if(wait==WAIT_FAILED)return checked(HRESULT_FROM_WIN32(GetLastError()),"Wait audio event");}
    return true;
}

bool AudioRenderer::pumpOnce(AudioPcmSource& pipeline, double* firstWrittenPtsMs, bool wait)
{
    if (!running_) return false;
    if(wait&&!waitForEvent())return false;
    UINT32 padding = 0;
    if (!checked(client_->GetCurrentPadding(&padding),"GetCurrentPadding")) return false;
    if(padding>bufferFrames_)return checked(E_UNEXPECTED,"Invalid endpoint padding");
    bufferedMs_=1000.0*padding/sampleRate_;
    const UINT32 avail = bufferFrames_ - padding;
    if (avail == 0) return true;
    // The endpoint can play silence after a live source runs dry. Its device
    // clock may then pass our last write; leave that gap unmapped.
    if(started_&&!padding&&!pipeline.padUnderruns()){
        UINT64 pos=0,qpc=0;
        if(!checked(clock_->GetPosition(&pos,&qpc),"Live write GetPosition"))return false;
        const auto consumed=static_cast<uint64_t>(std::ceil(double(pos-anchorPos_)*sampleRate_/clockFrequency_));
        timelineWriteFrame_=std::max(timelineWriteFrame_.load(),consumed);
    }
    chunk_.resize(static_cast<size_t>(avail) * 2);
    BYTE* dest = nullptr;
    if (!checked(render_->GetBuffer(avail, &dest),"GetBuffer")) return false;
    double firstPts = -1.0;
    const size_t got = pipeline.pull(chunk_.data(), avail, &firstPts);
    const auto endPts=pipeline.lastPullEndPtsMs();
    if (firstWrittenPtsMs) *firstWrittenPtsMs = firstPts;
    if (!started_ && !got) return checked(render_->ReleaseBuffer(0, 0),"Release empty prefill");
    const UINT32 written=started_&&pipeline.padUnderruns()?avail:static_cast<UINT32>(got);
    if (got > 0) {
        const float target=std::clamp(gain_.load(),0.0f,1.0f);
        applyStereoGain(chunk_.data(),got,target,smoothedGain_);
        if(target!=loggedGain_&&std::abs(smoothedGain_-target)<.00001f){loggedGain_=target;log::info("audio-gain",std::format("target={} reached={} framesWritten={} clockPreserved=true applicationPCM=true",target,smoothedGain_,framesWritten_.load()));}
        std::memcpy(dest, chunk_.data(), got * 8);
        if (got < written) {
            std::memset(dest + got * 8, 0, (written - got) * 8);
            underruns_.fetch_add(1);
        }
    } else {
        std::memset(dest, 0, static_cast<size_t>(avail) * 8);
        underruns_.fetch_add(1);
    }
    if (!checked(render_->ReleaseBuffer(written, 0),"ReleaseBuffer")) return false;
    {
        std::lock_guard lock(timelineMutex_);
        if(got&&endPts){timedPcm_=true;outputTimeline_.append(timelineWriteFrame_,got,firstPts,*endPts);}
        outputTimeline_.discardBefore(timelineWriteFrame_>sampleRate_?timelineWriteFrame_-sampleRate_:0);
        timelineWriteFrame_+=written;
    }
    bufferedMs_=1000.0*(padding+written)/sampleRate_;
    framesWritten_ += written;
    return true;
}

double AudioRenderer::mediaTimeMs() const
{
    std::lock_guard endpointLock(endpointMutex_);
    if (!running_ || !started_) return std::numeric_limits<double>::quiet_NaN();
    UINT64 pos = 0, qpc = 0;
    if (!checked(clock_->GetPosition(&pos, &qpc),"Clock GetPosition") || clockFrequency_ == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double consumedMs = 1000.0 * static_cast<double>(pos - anchorPos_) /
        static_cast<double>(clockFrequency_);
    {std::lock_guard lock(timelineMutex_);if(timedPcm_){
        const double frame=consumedMs*sampleRate_/1000;
        if(frame>=timelineWriteFrame_)return std::numeric_limits<double>::quiet_NaN();
        return outputTimeline_.at(frame).value_or(std::numeric_limits<double>::quiet_NaN());
    }}
    return anchorPtsMs_.load() + consumedMs;
}

void AudioRenderer::stopAndReset()
{
    std::lock_guard endpointLock(endpointMutex_);
    started_ = false;
    if (client_) { checked(client_->Stop(),"Stop for reset");checked(client_->Reset(),"Reset"); }
    framesWritten_ = 0;
    timelineWriteFrame_=0;
    bufferedMs_=0;smoothedGain_=0;
    {std::lock_guard lock(timelineMutex_);outputTimeline_.clear();timedPcm_=false;}
}

bool AudioRenderer::started() const { return started_; }
uint64_t AudioRenderer::underruns() const { return underruns_.load(); }
uint64_t AudioRenderer::framesWritten() const { return framesWritten_; }

void AudioRenderer::shutdown()
{
    std::lock_guard endpointLock(endpointMutex_);
    if (client_) { (void)client_->Stop(); (void)client_->Reset(); }
    #define REL(x) if (x) { x->Release(); x = nullptr; }
    REL(clock_); REL(render_); REL(client_); REL(device_); REL(enum_);
    #undef REL
    if (event_ != nullptr) { CloseHandle(event_); event_ = nullptr; }
    running_ = false;
    started_ = false;
    bufferedMs_=0;framesWritten_=0;clockFrequency_=0;
    timelineWriteFrame_=0;
    {std::lock_guard lock(timelineMutex_);outputTimeline_.clear();timedPcm_=false;}
    if (comInited_) { CoUninitialize(); comInited_ = false; }
}

void AudioRenderer::setPaused(bool value)
{
    std::lock_guard endpointLock(endpointMutex_);
    if (!client_ || !started_) return;
    checked(value ? client_->Stop() : client_->Start(),"Pause/resume");
}

void AudioPipeline::runOnAudioThread(AudioRenderer* renderer, bool ownEndpoint)
{
    const auto t0 = std::chrono::steady_clock::now();
    bool endpointReady = renderer && (!ownEndpoint || renderer->start());
    double lastClockMs = 0, recoveryTargetMs = 0;
    auto retryAt = t0;
    auto recovering = [&](HRESULT error) {
        endpointError_ = FAILED(error) ? error : E_FAIL;
        endpointRecovering_ = true;
        recoveryTargetMs = lastClockMs;
        endpointReady = false;
        if (ownEndpoint) renderer->shutdown();
        retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        log::warn("audio-recovery",std::format("endpoint unavailable hr=0x{:08X} holdPtsMs={:.3f}",unsigned(endpointError_.load()),recoveryTargetMs));
    };
    auto rewind = [&](double target) {
        {
            std::lock_guard lock(mutex_);
            ring_.clear();ringFrames_=0;segments_.clear();
            if(havePacket_){av_packet_unref(packet_);havePacket_=false;}
            avcodec_flush_buffers(codecCtx_);
            if(swr_)swr_free(&swr_);
            const int64_t tbTarget=static_cast<int64_t>(target*stream_->time_base.den/1000/stream_->time_base.num);
            const int result=avformat_seek_file(fmt_,streamIndex_,INT64_MIN,tbTarget,INT64_MAX,0);
            if(result<0){log::error("audio",std::format("seek failed code={}",result));clockExhausted_=false;return false;}
            demuxEof_=drainSent_=false;decodedEof_=false;
            nextPtsMs_=target;discardUntilPtsMs_=target;
        }
        decodeBlock();
        discardUntilPtsMs_=-1;
        clockExhausted_=decodedEof_&&headPtsMs()<0;
        return true;
    };
    if(renderer&&ownEndpoint&&!endpointReady)recovering(renderer->lastError());
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
    clockExhausted_=decodedEof_&&firstPts<0;
    if (endpointReady && firstPts >= 0.0) {
        if (!renderer->startAnchored(*this,paused_)) recovering(renderer->lastError());
        else endpointRecovering_=false;
    }
    lastPrefillMs_.store(std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count() * 1000.0);
    veyra::log::info("audio", std::format("startup prefill done in {:.0f}ms firstPtsMs={:.1f} bufferedMs={:.0f}",
        lastPrefillMs_.load(), firstPts, bufferedMs()));

    bool pauseApplied = paused_;
    while (!stopFlag_.load()) {
        const bool pauseNow = paused_.load();
        if(endpointReady){
            const double current=renderer->mediaTimeMs();
            if(std::isfinite(current))lastClockMs=current;
            if(ownEndpoint&&endpointLossForTest_.exchange(false)){
                log::info("audio-recovery-test","release own endpoint on its audio thread; no system device change");
                recovering(AUDCLNT_E_DEVICE_INVALIDATED);
            }else if(ownEndpoint&&FAILED(renderer->lastError()))recovering(renderer->lastError());
        }
        if (pauseNow != pauseApplied && endpointReady) renderer->setPaused(pauseNow);
        pauseApplied = pauseNow;
        // Seek request? Atomic re-sequence.
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (seekRequested_) {
                seekRequested_ = false;
                const double target = seekTargetMs_;
                seekCount_.fetch_add(1);
                lock.unlock();
                if (endpointReady) renderer->stopAndReset();
                lastClockMs=recoveryTargetMs=target;
                const bool seekOk=rewind(target);
                double startPts = seekOk?headPtsMs():std::numeric_limits<double>::quiet_NaN();
                if (seekOk&&startPts < 0.0) startPts = target;
                firstPtsAfterSeek_.store(startPts);
                if(!seekOk&&renderer)recovering(E_FAIL);
                if (seekOk&&endpointReady && !clockExhausted_) {
                    if (!renderer->startAnchored(*this,pauseNow)) recovering(renderer->lastError());
                }
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
        if(renderer&&ownEndpoint&&!endpointReady){
            if(clockExhausted_){endpointRecovering_=false;}
            else if(std::chrono::steady_clock::now()>=retryAt){
                if(renderer->start()){
                    endpointReady=true;
                    const bool rewound=rewind(recoveryTargetMs);
                    if(rewound&&(clockExhausted_||renderer->startAnchored(*this,pauseNow))){
                        endpointRecovering_=false;++endpointRecoveries_;
                        log::info("audio-recovery",std::format("restored mediaPtsMs={:.3f} paused={} recoveries={}",recoveryTargetMs,pauseNow,endpointRecoveries_.load()));
                    }else recovering(renderer->lastError());
                }else recovering(renderer->lastError());
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock,std::chrono::milliseconds(5),[&]{return stopFlag_||seekRequested_;});
            continue;
        }
        if (pauseNow) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        // Regular cycle: pump the endpoint, then top up below high watermark.
        if (renderer != nullptr) {
            double firstPts = -1.0;
            if (!renderer->pumpOnce(*this, &firstPts)) {
                if(!ownEndpoint){log::error("audio","pumpOnce failed");break;}
                recovering(renderer->lastError());continue;
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
    if(renderer&&ownEndpoint)renderer->shutdown();
}

void AudioPipeline::startThread(AudioRenderer* renderer, bool ownEndpoint)
{
    endpointRecovering_=renderer&&ownEndpoint;endpointError_=S_OK;endpointRecoveries_=0;clockExhausted_=false;
    thread_ = std::thread(&AudioPipeline::runOnAudioThread, this, renderer, ownEndpoint);
}

} // namespace veyra::sink
