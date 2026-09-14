#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/source/CaptureMediaType.h"
#include "veyra/source/NativeCaptureSink.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/Log.h"
#include "veyra/sink/AudioFormat.h"
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <format>
#include <chrono>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::source {
using Microsoft::WRL::ComPtr;
namespace {
// Legacy qedit interfaces are intentionally declared without obsolete qedit.h,
// which conflicts with modern D3D headers. ABI follows Microsoft DirectShow.
struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) ISampleGrabberCB: IUnknown {virtual HRESULT STDMETHODCALLTYPE SampleCB(double,IMediaSample*)=0;virtual HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)=0;};
struct __declspec(uuid("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")) ISampleGrabber:IUnknown {virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*,long*)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**)=0;virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB*,long)=0;};
const CLSID SampleGrabberClass={0xc1f400a0,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
const CLSID NullRendererClass={0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
void freeType(AM_MEDIA_TYPE* t,bool pointer=true){if(!t)return;CoTaskMemFree(t->pbFormat);if(t->pUnk)t->pUnk->Release();if(pointer)CoTaskMemFree(t);}
std::vector<ComPtr<IMoniker>> monikers(bool audio){std::vector<ComPtr<IMoniker>> out;ComPtr<ICreateDevEnum> de;ComPtr<IEnumMoniker> en;if(FAILED(CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&de)))||de->CreateClassEnumerator(audio?CLSID_AudioInputDeviceCategory:CLSID_VideoInputDeviceCategory,&en,0)!=S_OK)return out;for(;;){ComPtr<IMoniker> m;if(en->Next(1,&m,nullptr)!=S_OK)break;out.push_back(m);}return out;}
bool bind(unsigned index,bool audio,ComPtr<IBaseFilter>& filter){auto list=monikers(audio);return index<list.size()&&SUCCEEDED(list[index]->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)));}
bool configuration(unsigned device,ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b,ComPtr<IBaseFilter>& f,ComPtr<IAMStreamConfig>& c){return SUCCEEDED(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g)))&&SUCCEEDED(CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&b)))&&SUCCEEDED(b->SetFiltergraph(g.Get()))&&bind(device,false,f)&&SUCCEEDED(g->AddFilter(f.Get(),L"Capture card"))&&SUCCEEDED(b->FindInterface(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,f.Get(),IID_PPV_ARGS(&c)));}
}
struct CaptureCardSource::Impl:ISampleGrabberCB {
    using Clock=std::chrono::steady_clock;
    std::atomic<ULONG> refs{1};std::mutex mutex;std::condition_variable wake;
    // One pending frame plus one reader-owned frame, never an IMediaSample
    // reference. Holding the producer's sole RGB32 sample starves its allocator.
    AVFrame* frame=nullptr;AVFrame* pendingFrame=nullptr;bool pending=false,callbackError=false,configured=false;
    double pendingTime=0,lastPts=0,readAgeMs=0;bool pendingDiscontinuity=false;
    int64_t nominalDuration100ns=0;pipeline::Rational pendingDuration=pipeline::Rational::unknown();
    uint64_t received=0,dropped=0,lastDrop=0,sequence=0;
    Clock::time_point pendingArrival{},readArrival{},firstArrival{},latestArrival{};
    ComPtr<IGraphBuilder> graph;ComPtr<ICaptureGraphBuilder2> builder;ComPtr<IBaseFilter> device,grabFilter,nullFilter,audioFilter;ComPtr<IAMStreamConfig> config;ComPtr<ISampleGrabber> grab;ComPtr<IMediaControl> control;ComPtr<IMediaEvent> events;
    float lastAudioGain=-1;bool audioGainSupported=false;
    ComPtr<IBaseFilter> audioSink;ComPtr<IReferenceClock> referenceClock;
    std::unique_ptr<sink::CaptureAudioSession> audioSession;
    std::wstring audioError;
    SourceInfo info;CaptureMediaLayout layout;Clock::time_point lastFrame;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** pp)override{if(!pp)return E_POINTER;*pp=nullptr;if(id==IID_IUnknown||id==__uuidof(ISampleGrabberCB)){*pp=static_cast<ISampleGrabberCB*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE SampleCB(double time,IMediaSample* sample)override{
        const auto arrival=Clock::now();BYTE* data=nullptr;
        REFERENCE_TIME sampleStart=0,sampleEnd=0;
        const bool sampleTime=sample&&sample->GetTime(&sampleStart,&sampleEnd)==S_OK;
        const bool valid=sample&&std::isfinite(time)&&SUCCEEDED(sample->GetPointer(&data))&&data&&
            sample->GetActualDataLength()>=LONG(layout.sampleBytes);
        {
            std::lock_guard lock(mutex);
            if(!valid||!pendingFrame){callbackError=true;}
            else {
                // Copy directly into our bounded mailbox; read() swaps frames
                // under this lock, so the frame consumed by the GPU is untouched.
                if(!copyCaptureSample(layout,data,size_t(sample->GetActualDataLength()),*pendingFrame)){callbackError=true;wake.notify_one();return S_OK;}
                if(pending)++dropped;
                // Inspect consecutive callbacks, not consecutive mailbox reads.
                // Preserve a driver/clock break when its sample is overwritten.
                pendingDiscontinuity=captureDiscontinuity(pending,pendingDiscontinuity,
                    sample->IsDiscontinuity()==S_OK,received>0,pendingTime,time,info.averageFps);
                pending=true;pendingTime=time;pendingArrival=arrival;
                pendingDuration=captureDuration(sampleStart,sampleEnd,sampleTime,nominalDuration100ns);
                if(!received)firstArrival=arrival;
                ++received;latestArrival=arrival;
            }
        }
        wake.notify_one();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)override{return E_NOTIMPL;}
};
CaptureCardSource::CaptureCardSource():p_(std::make_unique<Impl>()){}
CaptureCardSource::~CaptureCardSource(){close();}
std::vector<std::wstring> CaptureCardSource::devices(bool audio){std::vector<std::wstring> result;for(auto& m:monikers(audio)){ComPtr<IPropertyBag> bag;VARIANT v;VariantInit(&v);std::wstring name=L"Unknown capture device";if(SUCCEEDED(m->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&bag)))&&SUCCEEDED(bag->Read(L"FriendlyName",&v,nullptr))&&v.vt==VT_BSTR)name=v.bstrVal;VariantClear(&v);result.push_back(name);}return result;}
std::vector<CaptureFormat> CaptureCardSource::formats(unsigned device){ComPtr<IGraphBuilder> g;ComPtr<ICaptureGraphBuilder2>b;ComPtr<IBaseFilter>f;ComPtr<IAMStreamConfig>c;std::vector<CaptureFormat> out;if(!configuration(device,g,b,f,c))return out;int count=0,size=0;if(FAILED(c->GetNumberOfCapabilities(&count,&size))||size<1||size>65536)return out;std::vector<BYTE> caps(size);for(int i=0;i<count;++i){AM_MEDIA_TYPE* t=nullptr;if(FAILED(c->GetStreamCaps(i,&t,caps.data())))continue;BITMAPINFOHEADER* bm=nullptr;REFERENCE_TIME duration=0;if(t->formattype==FORMAT_VideoInfo&&t->cbFormat>=sizeof(VIDEOINFOHEADER)){auto* vi=reinterpret_cast<VIDEOINFOHEADER*>(t->pbFormat);bm=&vi->bmiHeader;duration=vi->AvgTimePerFrame;}else if(t->formattype==FORMAT_VideoInfo2&&t->cbFormat>=sizeof(VIDEOINFOHEADER2)){auto* vi=reinterpret_cast<VIDEOINFOHEADER2*>(t->pbFormat);bm=&vi->bmiHeader;duration=vi->AvgTimePerFrame;}
        // Device capabilities, not a 1080p/2160p 30/60 preset list. Keep native
        // indices so the selected row opens the exact driver media type.
        if(bm&&bm->biWidth>0&&bm->biWidth<=3840&&std::abs(int64_t(bm->biHeight))>0&&std::abs(int64_t(bm->biHeight))<=2160&&duration>0){unsigned w=bm->biWidth,h=unsigned(std::abs(int64_t(bm->biHeight)));double fps=1e7/duration;
            const auto pixel=capturePixelName(t->subtype);CaptureMediaLayout layout;const bool valid=captureMediaLayout(*t,layout);const bool knownRaw=capturePacking(t->subtype)!=CapturePacking::Unknown;
            const wchar_t* support=valid?((layout.format==AV_PIX_FMT_P010||layout.format==AV_PIX_FMT_P016)?L"原生 · SDR":L"原生"):knownRaw?L"布局/颜色暂不支持":L"需系统解码/转换";
            out.push_back({i,w,h,fps,std::format(L"{} x {} @ {:.2f} fps · {} · {} [format {}]",w,h,fps,pixel,support,i)});}freeType(t);}return out;}
const SourceInfo& CaptureCardSource::info()const{return p_->info;}
bool CaptureCardSource::setAudioGain(float gain){
    auto& p=*p_;if(p.audioSession){p.audioSession->setGain(gain);return p.audioSession->snapshot().available;}if(!p.graph||!p.audioFilter)return false;
    if(gain==p.lastAudioGain)return p.audioGainSupported;
    ComPtr<IBasicAudio> audio;HRESULT hr=p.graph.As(&audio);
    if(SUCCEEDED(hr)){long attenuation=gain<=0?-10000:long(std::clamp(2000.0*std::log10(double(gain)),-10000.0,0.0));hr=audio->put_Volume(attenuation);}
    p.lastAudioGain=gain;p.audioGainSupported=SUCCEEDED(hr);log::info("capture-audio",std::format("application gain={} hr=0x{:X}",gain,unsigned(hr)));return p.audioGainSupported;
}
void CaptureCardSource::videoPresented(double pts,int64_t time){if(p_->audioSession)p_->audioSession->videoPresented(pts,time);}
void CaptureCardSource::videoReset(){if(p_->audioSession)p_->audioSession->videoReset();}
void CaptureCardSource::setAudioSync(unsigned mode,int offset){if(p_->audioSession)p_->audioSession->setSync(mode,offset);}
sink::CaptureAudioState CaptureCardSource::audioState()const{auto state=p_->audioSession?p_->audioSession->snapshot():sink::CaptureAudioState{};if(!p_->audioError.empty())state.error=p_->audioError;return state;}
bool CaptureCardSource::open(const SourceOpenDesc& desc){return configure(desc)&&start();}
bool CaptureCardSource::configure(const SourceOpenDesc& desc){close();p_->lastAudioGain=-1;auto& p=*p_;unsigned index=0;int format=0,audio=-1;if(swscanf_s(desc.path.c_str(),L"capture:%u:%d:%d",&index,&format,&audio)!=3)return false;
    if(!configuration(index,p.graph,p.builder,p.device,p.config))return false;
    int count=0,size=0;if(FAILED(p.config->GetNumberOfCapabilities(&count,&size))||format<0||format>=count||size<=0||size>65536)return false;
    std::vector<BYTE> caps(size);AM_MEDIA_TYPE* native=nullptr;if(FAILED(p.config->GetStreamCaps(format,&native,caps.data())))return false;
    HRESULT hr=p.config->SetFormat(native);const GUID requestedSubtype=native->subtype;
    log::info("capture",std::format("SetFormat device={} nativeIndex={} subtype=0x{:08X} hr=0x{:08X}",index,format,native->subtype.Data1,uint32_t(hr)));freeType(native);if(FAILED(hr))return false;
    // Read the driver-negotiated type back. Native YUY2/NV12/RGB32 connects
    // directly to our terminal filter: no intelligent-connect converter.
    native=nullptr;hr=p.config->GetFormat(&native);if(FAILED(hr)||!native){freeType(native);return false;}
    CaptureMediaLayout nativeLayout;const bool nativeSupported=captureMediaLayout(*native,nativeLayout);
    if(!nativeSupported&&capturePacking(native->subtype)!=CapturePacking::Unknown){log::error("capture","Known raw format has unsupported layout/color metadata; refusing implicit RGB conversion");freeType(native);return false;}
    if(nativeSupported&&(nativeLayout.format==AV_PIX_FMT_P010||nativeLayout.format==AV_PIX_FMT_P016)&&desc.legacyCaptureRgbForDiagnostic){log::error("capture","10/16-bit capture cannot use the legacy 8-bit RGB diagnostic converter");freeType(native);return false;}
    const bool direct=nativeSupported&&!desc.legacyCaptureRgbForDiagnostic;
    AM_MEDIA_TYPE connected{};
    if(direct){
        ComPtr<IPin> input,output;
        hr=createNativeCaptureSink(*native,[&p](IMediaSample* sample){REFERENCE_TIME a=0,b=0;const auto timeHr=sample->GetTime(&a,&b);if(FAILED(timeHr))return timeHr;return p.SampleCB(double(a)/1e7,sample);},p.grabFilter,input);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Native frame mailbox");
        if(SUCCEEDED(hr))hr=p.builder->FindPin(p.device.Get(),PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,FALSE,0,&output);
        if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(output.Get(),input.Get(),native);
        if(SUCCEEDED(hr))hr=input->ConnectionMediaType(&connected);
        log::info("capture",std::format("native ConnectDirect subtype=0x{:08X} hr=0x{:08X} converters=0",native->subtype.Data1,uint32_t(hr)));
    }else{
        log::warn("capture",std::format("explicit RGB32 compatibility path subtype=0x{:08X} diagnostic={} (decoder/color converter may be inserted)",requestedSubtype.Data1,desc.legacyCaptureRgbForDiagnostic));
        hr=CoCreateInstance(SampleGrabberClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.grabFilter));
        if(SUCCEEDED(hr))hr=p.grabFilter.As(&p.grab);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Decoded RGB compatibility mailbox");
        AM_MEDIA_TYPE want{};want.majortype=MEDIATYPE_Video;want.subtype=MEDIASUBTYPE_RGB32;want.formattype=FORMAT_VideoInfo;
        if(SUCCEEDED(hr))hr=p.grab->SetMediaType(&want);
        if(SUCCEEDED(hr))hr=CoCreateInstance(NullRendererClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.nullFilter));
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.nullFilter.Get(),L"Video sink");
        if(SUCCEEDED(hr))hr=p.builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,p.device.Get(),p.grabFilter.Get(),p.nullFilter.Get());
        if(SUCCEEDED(hr))hr=p.grab->GetConnectedMediaType(&connected);
    }
    freeType(native);const bool layoutValid=SUCCEEDED(hr)&&captureMediaLayout(connected,p.layout);freeType(&connected,false);
    if(!layoutValid){log::error("capture",std::format("unsupported negotiated layout/connect failure hr=0x{:08X}",uint32_t(hr)));return false;}
    unsigned colorOverride=0;swscanf_s(desc.path.c_str(),L"capture:%u:%d:%d:%u",&index,&format,&audio,&colorOverride);
    if(colorOverride>2)return false;
    if(colorOverride){
        if(p.layout.format!=AV_PIX_FMT_P010&&p.layout.format!=AV_PIX_FMT_P016){log::error("capture-color","Explicit HDR requires P010/P016; select a 10/16-bit capture format");return false;}
        p.layout.color.transfer=colorOverride==1?pipeline::TransferFunction::PQ:pipeline::TransferFunction::HLG;
        p.layout.color.matrix=pipeline::YuvMatrix::BT2020NCL;p.layout.color.primaries=pipeline::ColorPrimaries::BT2020;
        p.layout.color.transferAssumed=p.layout.color.matrixAssumed=p.layout.color.primariesAssumed=false;
        log::info("capture-color",std::format("manual override={} BT2020; range retains negotiated metadata",colorOverride==1?"PQ":"HLG"));
    }
    p.info={};p.info.kind=pipeline::SourceKind::CaptureCard;p.info.width=p.layout.width;p.info.height=p.layout.height;p.info.averageFps=p.layout.duration>0?1e7/p.layout.duration:0;p.info.duration=pipeline::Rational::unknown();p.info.color=p.layout.color;
    p.nominalDuration100ns=p.layout.duration;
    log::info("capture-color",std::format("format={} stride={} rowBytes={} bytes={} bottomUp={} matrix={} assumed={} range={} assumed={} workingTransfer={} assumed={} (explicit transfer contract)",int(p.layout.format),p.layout.stride,p.layout.rowBytes,p.layout.sampleBytes,p.layout.bottomUp,int(p.info.color.matrix),p.info.color.matrixAssumed,int(p.info.color.range),p.info.color.rangeAssumed,int(p.info.color.transfer),p.info.color.transferAssumed));
    if(audio>=0){
        const bool connected=[&]{
        if(!bind(unsigned(audio),true,p.audioFilter)||FAILED(p.graph->AddFilter(p.audioFilter.Get(),L"Capture audio")))return false;
        ComPtr<IPin> audioPin;
        if(FAILED(p.builder->FindPin(p.audioFilter.Get(),PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Audio,FALSE,0,&audioPin)))return false;
        ComPtr<IEnumMediaTypes> types;if(FAILED(audioPin->EnumMediaTypes(&types)))return false;
        // Preserve the device's actual speaker layout. Enumeration order is
        // commonly stereo first even when native 5.1 is available.
        auto releaseType=[](AM_MEDIA_TYPE* type){freeType(type);};
        using AudioType=std::unique_ptr<AM_MEDIA_TYPE,decltype(releaseType)>;
        std::vector<AudioType> audioTypes;
        for(;;){AM_MEDIA_TYPE* type=nullptr;if(types->Next(1,&type,nullptr)!=S_OK)break;
            AudioType owned(type,releaseType);sink::WavePcmFormat pcm;
            if(type->formattype==FORMAT_WaveFormatEx&&sink::parseWavePcm(type->pbFormat,type->cbFormat,pcm))audioTypes.push_back(std::move(owned));
        }
        std::stable_sort(audioTypes.begin(),audioTypes.end(),[](const auto& a,const auto& b){
            return reinterpret_cast<const WAVEFORMATEX*>(a->pbFormat)->nChannels>reinterpret_cast<const WAVEFORMATEX*>(b->pbFormat)->nChannels;
        });
        bool connectedAudio=false;
        for(const auto& owned:audioTypes){
            auto* type=owned.get();
            auto session=std::make_unique<sink::CaptureAudioSession>();ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
            if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat&&type->cbFormat>=sizeof(WAVEFORMATEX)&&session->configure(*reinterpret_cast<WAVEFORMATEX*>(type->pbFormat),type->cbFormat)){
                auto* target=session.get();
                hr=createNativeAudioSink(*type,[target](IMediaSample* sample){
                    BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
                    if(FAILED(sample->GetPointer(&bytes))||FAILED(sample->GetTime(&begin,&end)))return VFW_E_SAMPLE_TIME_NOT_SET;
                    return target->push(bytes,size_t(sample->GetActualDataLength()),double(begin)/10000,sample->IsDiscontinuity()==S_OK)?S_OK:E_FAIL;
                },candidate,terminal);
                if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio PCM");
                // Request small input blocks before connection; downstream
                // playback cannot undo time spent filling a driver buffer.
                // Some devices reject this advisory API, so do not fail capture.
                if(SUCCEEDED(hr))suggestCaptureAudioBuffering(audioPin.Get(),*reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat));
                if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
                if(SUCCEEDED(hr)){p.audioSink=candidate;p.audioSession=std::move(session);connectedAudio=true;}
                else if(candidate)p.graph->RemoveFilter(candidate.Get());
                log::info("capture-audio",std::format("PCM ConnectDirect hr=0x{:X}",unsigned(hr)));
            }
            if(connectedAudio)break;
        }
        return connectedAudio;
        }();
        if(!connected){
            p.audioError=L"采集音频设备或 PCM 格式不可用；视频继续运行";
            log::warn("capture-audio","audio connection unavailable; retaining video capture");
            if(p.audioFilter)p.graph->RemoveFilter(p.audioFilter.Get());
            p.audioFilter.Reset();
        }
        // Pin the common graph clock before Run; removing DirectShow's audio
        // renderer must not silently change the capture graph's reference.
        ComPtr<IMediaFilter> mediaFilter;
        if(FAILED(CoCreateInstance(CLSID_SystemClock,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.referenceClock)))||
            FAILED(p.graph.As(&mediaFilter))||FAILED(mediaFilter->SetSyncSource(p.referenceClock.Get())))return false;
    }
    if(FAILED(p.graph.As(&p.control))||FAILED(p.graph.As(&p.events)))return false;
    if(p.grab&&(FAILED(p.grab->SetBufferSamples(FALSE))||FAILED(p.grab->SetCallback(&p,0))))return false;
    for(auto** f:{&p.frame,&p.pendingFrame}){
        *f=av_frame_alloc();if(!*f)return false;
        (*f)->format=p.layout.format;(*f)->width=p.info.width;(*f)->height=p.info.height;
        if(av_frame_get_buffer(*f,32)<0)return false;
    }
    p.configured=true;
    // Log the upstream type after DirectShow has finished negotiation. The
    // RGB32 output's nominal FPS alone is not proof of actual callback cadence.
    AM_MEDIA_TYPE* actual=nullptr;const auto formatHr=p.config->GetFormat(&actual);
    veyra::log::info("capture",std::format("configured {}x{} nominalFps={:.3f} upstreamSubtype=0x{:08X} formatHr=0x{:X} mailbox=1 ownedBuffers=2 deferredRun=1 audioDevice={}",
        p.info.width,p.info.height,p.info.averageFps,actual?unsigned(actual->subtype.Data1):0,unsigned(formatHr),audio));freeType(actual);
    return true;
}
bool CaptureCardSource::start(){
    auto& p=*p_;if(p.info.opened)return true;if(!p.configured||!p.control)return false;
    if(p.audioSession&&!p.audioSession->start())log::warn("capture-audio","audio start failed; retaining video capture");
    p.lastFrame=Impl::Clock::now();const auto hr=p.control->Run();p.info.opened=SUCCEEDED(hr);
    veyra::log::info("capture",std::format("Run hr=0x{:X} actual={}x{} nominalFps={:.3f} mailbox=1 ownedBuffers=2",unsigned(hr),p.info.width,p.info.height,p.info.averageFps));return p.info.opened;
}
CaptureMetrics CaptureCardSource::metrics()const{
    auto& p=*p_;std::lock_guard lock(p.mutex);CaptureMetrics m;
    m.received=p.received;m.delivered=p.sequence;m.dropped=p.dropped;m.readAgeMs=p.readAgeMs;
    if(p.received>1){const double elapsed=std::chrono::duration<double>(p.latestArrival-p.firstArrival).count();if(elapsed>0)m.callbackFps=(p.received-1)/elapsed;}
    if(p.sequence)m.frameAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();return m;
}
SourceReadStatus CaptureCardSource::read(pipeline::FramePacket& packet,const AVFrame** frame){return readWithWait(packet,frame,30);}
SourceReadStatus CaptureCardSource::tryRead(pipeline::FramePacket& packet,const AVFrame** frame){return readWithWait(packet,frame,0);}
SourceReadStatus CaptureCardSource::readWithWait(pipeline::FramePacket& packet,const AVFrame** frame,unsigned milliseconds){auto& p=*p_;*frame=nullptr;if(!p.info.opened)return SourceReadStatus::Error;
    long code=0;LONG_PTR a=0,b=0;while(p.events&&p.events->GetEvent(&code,&a,&b,0)==S_OK){p.events->FreeEventParams(code,a,b);if(code==EC_DEVICE_LOST||code==EC_ERRORABORT)return SourceReadStatus::Error;}
    double time=0;uint32_t flags=0;uint64_t sequence=0;pipeline::Rational duration;
    {
        std::unique_lock lock(p.mutex);if(milliseconds)p.wake.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return p.pending||p.callbackError;});
        if(p.callbackError)return SourceReadStatus::Error;
        if(!p.pending)return Impl::Clock::now()-p.lastFrame>std::chrono::seconds(3)?SourceReadStatus::Error:SourceReadStatus::Waiting;
        std::swap(p.frame,p.pendingFrame);p.pending=false;time=p.pendingTime;p.readArrival=p.pendingArrival;
        duration=p.pendingDuration;
        p.readAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();
        if(!p.sequence)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Open);
        if(p.dropped!=p.lastDrop)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Drop);
        if(p.pendingDiscontinuity)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
        p.lastDrop=p.dropped;p.lastPts=time;++p.sequence;sequence=p.received;
    }
    p.frame->pts=static_cast<int64_t>(time*10000000);p.frame->duration=duration.isUnknown()?0:duration.to100ns();p.frame->time_base={1,10000000};packet={};packet.pts={p.frame->pts,10000000};packet.duration=duration;packet.colorInfo=p.info.color;packet.sourceKind=pipeline::SourceKind::CaptureCard;packet.sequence=sequence;packet.flags=flags;packet.sourceEpoch=1;
    packet.arrivalHost100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(p.readArrival.time_since_epoch()).count()/100;
    *frame=p.frame;p.lastFrame=Impl::Clock::now();return SourceReadStatus::Frame;
}
void CaptureCardSource::close()noexcept{
    auto& p=*p_;if(p.control)p.control->Stop();if(p.grab)p.grab->SetCallback(nullptr,0);
    if(p.audioSession)p.audioSession->stop();p.audioError.clear();
    p.events.Reset();p.control.Reset();p.grab.Reset();p.nullFilter.Reset();p.grabFilter.Reset();p.audioSink.Reset();p.audioFilter.Reset();p.config.Reset();p.device.Reset();p.builder.Reset();p.graph.Reset();p.referenceClock.Reset();p.audioSession.reset();
    av_frame_free(&p.frame);av_frame_free(&p.pendingFrame);p.info={};
    p.sequence=p.received=p.dropped=p.lastDrop=0;p.pending=p.callbackError=p.configured=false;p.lastPts=p.readAgeMs=0;
}
}
