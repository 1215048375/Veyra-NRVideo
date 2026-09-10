#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/sink/WasapiAudioSink.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
extern "C" {
#include <libswresample/swresample.h>
}
namespace veyra::sink {
namespace {
int64_t hostTime(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;}
}
struct CaptureAudioSession::Impl : AudioPcmSource {
    struct Chunk {std::vector<uint8_t> bytes;double pts=0;bool discontinuity=false;};
    WAVEFORMATEX format{};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Chunk> input;
    size_t inputBytes=0,convertingBytes=0;
    std::deque<float> pcm;
    double headPts=0;
    bool haveHead=false,pendingReset=false;
    std::atomic<bool> stop{false};
    std::atomic<float> gain{1};
    std::atomic<unsigned> mode{0};std::atomic<int> offset{0};
    double videoPts=0,videoHost=0;bool haveVideo=false;
    double ingressMapping=0;bool haveIngress=false;
    int64_t lastArrival=0;
    CaptureAudioState state;
    std::thread thread;
    static constexpr double maxQueuedMs=500;
    double queuedMs()const{return 1000.0*(pcm.size()/2)/kAudioRate+1000.0*(inputBytes+convertingBytes)/format.nAvgBytesPerSec;}
    void queueChanged(){state.bufferedMs=queuedMs();state.bufferHighWaterMs=std::max(state.bufferHighWaterMs,state.bufferedMs);}
    void clearPcm(){std::lock_guard lock(mutex);pcm.clear();haveHead=false;queueChanged();}
    void fail(const wchar_t* reason){std::lock_guard lock(mutex);state.available=false;state.running=false;state.skewMs.reset();state.error=reason;}
    size_t pull(float* dst,size_t frames,double* pts)override{
        std::lock_guard lock(mutex);
        const size_t take=std::min(frames,pcm.size()/2);
        *pts=haveHead?headPts:-1;
        for(size_t i=0;i<take*2;++i){dst[i]=pcm.front();pcm.pop_front();}
        headPts+=1000.0*take/kAudioRate;
        queueChanged();
        return take;
    }
    void run(){
        AudioRenderer renderer;
        bool endpointReady=false;int64_t retryAt=0;
        const bool injectEndpointLoss=GetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_ENDPOINT_LOSS",nullptr,0)>0;
        bool injected=false;uint64_t endpointPumps=0;
        SwrContext* swr=nullptr;
        AVChannelLayout out=AV_CHANNEL_LAYOUT_STEREO,in{};av_channel_layout_default(&in,format.nChannels);
        const auto inputFormat=format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT?AV_SAMPLE_FMT_FLT:format.wBitsPerSample==16?AV_SAMPLE_FMT_S16:AV_SAMPLE_FMT_S32;
        const int configured=swr_alloc_set_opts2(&swr,&out,AV_SAMPLE_FMT_FLT,kAudioRate,&in,inputFormat,format.nSamplesPerSec,0,nullptr);
        av_channel_layout_uninit(&in);
        if(configured<0||!swr||swr_init(swr)<0){log::error("capture-audio","resampler initialization failed");swr_free(&swr);renderer.shutdown();fail(L"音频重采样初始化失败");return;}
        const auto releaseSwr=[](SwrContext* value){swr_free(&value);};
        std::unique_ptr<SwrContext,decltype(releaseSwr)> resampler(swr,releaseSwr);
        double anchorHostMinusPts=0;
        int64_t lastReset=0;
        unsigned appliedMode=mode.load();int appliedOffset=offset.load();
        uint64_t lastUnderruns=0;
        auto recoverEndpoint=[&]{
            const auto error=renderer.lastError();renderer.shutdown();endpointReady=false;retryAt=hostTime()+5000000;
            clearPcm();swr_close(swr);if(swr_init(swr)<0){fail(L"音频重采样重置失败");return false;}
            std::lock_guard lock(mutex);input.clear();inputBytes=convertingBytes=0;pendingReset=false;haveVideo=false;
            state.available=false;state.running=false;state.skewMs.reset();state.endpointBufferedMs=0;
            state.error=L"音频输出断开，正在重连（"+std::to_wstring(unsigned(error))+L"）";queueChanged();return true;
        };
        while(!stop){
            if(!endpointReady){
                const auto time=hostTime();
                if(time<retryAt){std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(20),[&]{return stop.load();});continue;}
                {std::lock_guard lock(mutex);++state.endpointRetries;}
                if(!renderer.start()){if(!recoverEndpoint())break;continue;}
                endpointReady=true;lastUnderruns=renderer.underruns();
                {std::lock_guard lock(mutex);state.error.clear();pendingReset=true;}
            }
            renderer.setGain(gain);
            Chunk chunk;bool reset=false;double vPts=0,vHost=0,ingress=0;bool video=false;int64_t arrival=0;
            {
                std::unique_lock lock(mutex);
                if(input.empty())wake.wait_for(lock,std::chrono::milliseconds(2));
                if(!input.empty()){chunk=std::move(input.front());input.pop_front();inputBytes-=chunk.bytes.size();convertingBytes=chunk.bytes.size();}
                reset=pendingReset;pendingReset=false;video=haveVideo;vPts=videoPts;vHost=videoHost;
                ingress=ingressMapping;
                arrival=lastArrival;
            }
            if(mode!=appliedMode||offset!=appliedOffset){reset=true;appliedMode=mode;appliedOffset=offset;}
            if(reset||chunk.discontinuity){
                renderer.stopAndReset();clearPcm();swr_close(swr);
                if(swr_init(swr)<0){fail(L"音频重采样重置失败");break;}
                std::lock_guard lock(mutex);++state.resets;
            }
            if(!chunk.bytes.empty()){
                const int inputFrames=int(chunk.bytes.size()/format.nBlockAlign);
                const auto delay=swr_get_delay(swr,format.nSamplesPerSec);
                const int capacity=swr_get_out_samples(swr,inputFrames);
                if(capacity<=0||capacity>int(kAudioRate)){log::error("capture-audio","invalid converted block capacity");fail(L"音频转换块大小无效");break;}
                std::vector<float> converted(size_t(capacity)*2);
                uint8_t* dst[]={reinterpret_cast<uint8_t*>(converted.data())};const uint8_t* src[]={chunk.bytes.data()};
                const int count=swr_convert(swr,dst,capacity,src,inputFrames);
                if(count<0){log::error("capture-audio",std::format("convert failed code={}",count));fail(L"音频转换失败");break;}
                const double start=chunk.pts-1000.0*delay/format.nSamplesPerSec;
                if(haveHead&&std::abs(start-(headPts+1000.0*(pcm.size()/2)/kAudioRate))>50){
                    renderer.stopAndReset();clearPcm();
                    std::lock_guard lock(mutex);++state.resets;
                }
                {
                    std::lock_guard lock(mutex);convertingBytes=0;
                    if(pendingReset){queueChanged();continue;}
                    if(queuedMs()+1000.0*count/kAudioRate>maxQueuedMs){
                        input.clear();inputBytes=0;pcm.clear();haveHead=false;pendingReset=true;++state.overflows;
                    }else{
                        if(!haveHead){headPts=start;haveHead=true;}
                        pcm.insert(pcm.end(),converted.begin(),converted.begin()+size_t(count)*2);
                    }
                    queueChanged();
                }
            }
            // Both streams use graph PTS. Relate the latest displayed video PTS
            // to host time; the endpoint's own queued frames are not added again.
            const double now=double(hostTime())/10000;
            const bool fresh=video&&now-vHost<500;
            const double requested=appliedMode==0&&fresh?vHost-vPts-ingress:appliedMode==1?double(appliedOffset):0;
            const double target=std::clamp(requested,0.0,250.0);
            const double mapping=ingress+target;
            const bool limited=requested<0||requested>250;
            if(!renderer.started()&&haveHead&&!pcm.empty()&&(appliedMode!=0||fresh)){
                // Live recovery discards expired sound rather than replaying
                // an obsolete half-second after a GPU stall or graph reset.
                const size_t expired=std::min(pcm.size()/2,size_t(std::max(0.0,(now-mapping-headPts-5)*kAudioRate/1000)));
                {std::lock_guard lock(mutex);for(size_t i=0;i<expired*2;++i)pcm.pop_front();headPts+=1000.0*expired/kAudioRate;queueChanged();}
                const bool due=now>=mapping+headPts;
                if(due&&!pcm.empty()){
                    if(!renderer.startAnchored(*this)){if(!recoverEndpoint())break;continue;}
                    anchorHostMinusPts=now-renderer.mediaTimeMs();lastReset=hostTime();
                }
            }
            if(renderer.started()){
                // Test only: release this session's endpoint, never change a
                // system device or interfere with another application's audio.
                if(injectEndpointLoss&&!injected&&++endpointPumps==60){injected=true;log::warn("capture-audio-test","inject owned endpoint loss");if(!recoverEndpoint())break;continue;}
                double pts=-1;if(!renderer.pumpOnce(*this,&pts)){if(!recoverEndpoint())break;continue;}
                const auto underruns=renderer.underruns();
                if(underruns>lastUnderruns&&hostTime()-arrival>1000000){
                    // Silence advances IAudioClock without consuming media.
                    // Re-anchor the next real block instead of retaining that drift.
                    renderer.stopAndReset();lastReset=hostTime();
                    std::lock_guard lock(mutex);++state.resets;
                }
                lastUnderruns=underruns;
                // Large video-delay changes need a bounded re-anchor. Never
                // chase every jitter sample by stopping the audio endpoint.
                if(appliedMode==0&&fresh&&hostTime()-lastReset>10000000&&std::abs(mapping-anchorHostMinusPts)>40){
                    renderer.stopAndReset();lastReset=hostTime();
                    std::lock_guard lock(mutex);++state.resets;
                }
            }
            {
                std::lock_guard lock(mutex);state.available=true;state.running=renderer.started();state.limited=limited;
                queueChanged();
                state.compensationMs=target;state.underruns=renderer.underruns();
                state.endpointBufferedMs=renderer.bufferedMs();
                const auto audioPts=renderer.mediaTimeMs();
                state.skewMs=fresh&&std::isfinite(audioPts)?std::optional<double>(audioPts-(vPts+now-vHost)):std::nullopt;
            }
        }
        renderer.shutdown();std::lock_guard lock(mutex);state.available=false;state.running=false;state.skewMs.reset();state.endpointBufferedMs=0;
    }
};
CaptureAudioSession::CaptureAudioSession():p_(std::make_unique<Impl>()){}
CaptureAudioSession::~CaptureAudioSession(){stop();}
bool CaptureAudioSession::configure(const WAVEFORMATEX& f){
    if(p_->thread.joinable()||f.nChannels<1||f.nChannels>2||f.nSamplesPerSec<8000||f.nSamplesPerSec>192000||
        (f.wFormatTag!=WAVE_FORMAT_PCM&&f.wFormatTag!=WAVE_FORMAT_IEEE_FLOAT)||
        (f.wBitsPerSample!=16&&f.wBitsPerSample!=32)||(f.wFormatTag==WAVE_FORMAT_IEEE_FLOAT&&f.wBitsPerSample!=32)||
        f.nBlockAlign!=f.nChannels*f.wBitsPerSample/8||f.nAvgBytesPerSec!=f.nBlockAlign*f.nSamplesPerSec)return false;
    p_->format=f;return true;
}
bool CaptureAudioSession::start(){
    if(!p_->format.nBlockAlign||p_->thread.joinable())return false;
    {std::lock_guard lock(p_->mutex);p_->input.clear();p_->inputBytes=p_->convertingBytes=0;p_->pcm.clear();p_->haveHead=p_->haveVideo=p_->haveIngress=p_->pendingReset=false;p_->state={};}
    p_->stop=false;
    try{p_->thread=std::thread([this]{try{p_->run();}catch(const std::exception& e){log::error("capture-audio",std::format("audio thread failed: {}",e.what()));p_->fail(L"音频线程异常，请重新打开采集");}});}
    catch(const std::exception& e){log::error("capture-audio",std::format("audio thread start failed: {}",e.what()));p_->fail(L"无法创建音频线程");return false;}
    return true;
}
void CaptureAudioSession::stop(){p_->stop=true;p_->wake.notify_all();if(p_->thread.joinable())p_->thread.join();std::lock_guard lock(p_->mutex);p_->input.clear();p_->inputBytes=p_->convertingBytes=0;p_->pcm.clear();p_->state.bufferedMs=0;}
bool CaptureAudioSession::push(const void* data,size_t bytes,double pts,bool discontinuity){
    auto& p=*p_;if(!data||!p.format.nBlockAlign||bytes%p.format.nBlockAlign||bytes>p.format.nAvgBytesPerSec/2||!std::isfinite(pts))return false;if(!bytes)return true;
    std::lock_guard lock(p.mutex);
    if(p.stop||!p.state.error.empty())return true;
    if(discontinuity){p.input.clear();p.inputBytes=0;p.pendingReset=true;p.haveVideo=false;}
    const double mapping=double(hostTime())/10000-pts;
    p.lastArrival=hostTime();
    if(!p.haveIngress||discontinuity){p.ingressMapping=mapping;p.haveIngress=true;}
    else p.ingressMapping=std::min(p.ingressMapping,mapping);
    const double blockMs=1000.0*bytes/p.format.nAvgBytesPerSec;
    if(p.queuedMs()+blockMs>Impl::maxQueuedMs){
        p.input.clear();p.inputBytes=0;p.pendingReset=true;++p.state.overflows;p.queueChanged();
        // Only the audio thread can reset its resampler and converted queue.
        // Drop this callback if that outstanding data still consumes the budget.
        if(p.queuedMs()+blockMs>Impl::maxQueuedMs){p.wake.notify_one();return true;}
    }
    Impl::Chunk c;c.bytes.resize(bytes);memcpy(c.bytes.data(),data,bytes);c.pts=pts;c.discontinuity=discontinuity;
    p.inputBytes+=bytes;p.input.push_back(std::move(c));p.queueChanged();p.wake.notify_one();return true;
}
void CaptureAudioSession::videoPresented(double pts,int64_t time){std::lock_guard lock(p_->mutex);p_->videoPts=pts;p_->videoHost=double(time)/10000;p_->haveVideo=true;}
void CaptureAudioSession::setGain(float value){p_->gain=std::clamp(value,0.0f,1.0f);}
void CaptureAudioSession::setSync(unsigned mode,int offset){p_->mode=std::min(mode,2u);p_->offset=std::clamp(offset,-250,250);}
CaptureAudioState CaptureAudioSession::snapshot()const{std::lock_guard lock(p_->mutex);return p_->state;}
}
