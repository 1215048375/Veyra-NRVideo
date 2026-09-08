#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/Log.h"
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
    SourceInfo info;bool bottomUp=true;unsigned stride=0;Clock::time_point lastFrame;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** pp)override{if(!pp)return E_POINTER;*pp=nullptr;if(id==IID_IUnknown||id==__uuidof(ISampleGrabberCB)){*pp=static_cast<ISampleGrabberCB*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE SampleCB(double time,IMediaSample* sample)override{
        const auto arrival=Clock::now();BYTE* data=nullptr;
        REFERENCE_TIME sampleStart=0,sampleEnd=0;
        const bool sampleTime=sample&&sample->GetTime(&sampleStart,&sampleEnd)==S_OK;
        const bool valid=sample&&std::isfinite(time)&&SUCCEEDED(sample->GetPointer(&data))&&data&&
            sample->GetActualDataLength()>=LONG(size_t(stride)*info.height);
        {
            std::lock_guard lock(mutex);
            if(!valid||!pendingFrame){callbackError=true;}
            else {
                // Copy directly into our bounded mailbox; read() swaps frames
                // under this lock, so the frame consumed by the GPU is untouched.
                for(unsigned y=0;y<info.height;++y)
                    memcpy(pendingFrame->data[0]+size_t(y)*pendingFrame->linesize[0],
                        data+size_t(bottomUp?info.height-1-y:y)*stride,stride);
                if(pending)++dropped;
                pending=true;pendingTime=time;pendingArrival=arrival;
                pendingDuration=captureDuration(sampleStart,sampleEnd,sampleTime,nominalDuration100ns);
                pendingDiscontinuity=sample->IsDiscontinuity()==S_OK;
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
        if(bm&&bm->biWidth>0&&bm->biWidth<=3840&&abs(bm->biHeight)<=2160&&duration>0){unsigned w=bm->biWidth,h=abs(bm->biHeight);double fps=1e7/duration;if((w==1920&&h==1080||w==3840&&h==2160)&&fps>=29&&fps<=61)out.push_back({i,w,h,fps,std::format(L"{} x {} @ {:.2f} fps [format {}]",w,h,fps,i)});}freeType(t);}return out;}
const SourceInfo& CaptureCardSource::info()const{return p_->info;}
bool CaptureCardSource::setAudioGain(float gain){
    auto& p=*p_;if(!p.graph||!p.audioFilter)return false;
    if(gain==p.lastAudioGain)return p.audioGainSupported;
    ComPtr<IBasicAudio> audio;HRESULT hr=p.graph.As(&audio);
    if(SUCCEEDED(hr)){long attenuation=gain<=0?-10000:long(std::clamp(2000.0*std::log10(double(gain)),-10000.0,0.0));hr=audio->put_Volume(attenuation);}
    p.lastAudioGain=gain;p.audioGainSupported=SUCCEEDED(hr);log::info("capture-audio",std::format("application gain={} hr=0x{:X}",gain,unsigned(hr)));return p.audioGainSupported;
}
bool CaptureCardSource::open(const SourceOpenDesc& desc){return configure(desc)&&start();}
bool CaptureCardSource::configure(const SourceOpenDesc& desc){close();p_->lastAudioGain=-1;auto& p=*p_;unsigned index=0;int format=0,audio=-1;if(swscanf_s(desc.path.c_str(),L"capture:%u:%d:%d",&index,&format,&audio)!=3)return false;
    if(!configuration(index,p.graph,p.builder,p.device,p.config))return false;
    int count=0,size=0;if(FAILED(p.config->GetNumberOfCapabilities(&count,&size))||format<0||format>=count||size<=0||size>65536)return false;std::vector<BYTE> caps(size);AM_MEDIA_TYPE* native=nullptr;if(FAILED(p.config->GetStreamCaps(format,&native,caps.data())))return false;HRESULT hr=p.config->SetFormat(native);freeType(native);if(FAILED(hr))return false;
    if(FAILED(CoCreateInstance(SampleGrabberClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.grabFilter)))||FAILED(p.grabFilter.As(&p.grab))||FAILED(p.graph->AddFilter(p.grabFilter.Get(),L"Latest frame mailbox")))return false;
    AM_MEDIA_TYPE want{};want.majortype=MEDIATYPE_Video;want.subtype=MEDIASUBTYPE_RGB32;want.formattype=FORMAT_VideoInfo;
    if(FAILED(p.grab->SetMediaType(&want))||FAILED(CoCreateInstance(NullRendererClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.nullFilter)))||FAILED(p.graph->AddFilter(p.nullFilter.Get(),L"Video sink"))||FAILED(p.builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,p.device.Get(),p.grabFilter.Get(),p.nullFilter.Get())))return false;
    AM_MEDIA_TYPE connected{};if(FAILED(p.grab->GetConnectedMediaType(&connected)))return false;
    if(connected.formattype!=FORMAT_VideoInfo||connected.cbFormat<sizeof(VIDEOINFOHEADER)){freeType(&connected,false);return false;}auto vi=*reinterpret_cast<VIDEOINFOHEADER*>(connected.pbFormat);freeType(&connected,false);
    if(vi.bmiHeader.biWidth<=0||vi.bmiHeader.biWidth>3840||abs(vi.bmiHeader.biHeight)>2160||vi.bmiHeader.biBitCount!=32)return false;
    p.info={};p.info.kind=pipeline::SourceKind::CaptureCard;p.info.width=vi.bmiHeader.biWidth;p.info.height=abs(vi.bmiHeader.biHeight);p.info.averageFps=vi.AvgTimePerFrame>0?1e7/vi.AvgTimePerFrame:0;p.info.duration=pipeline::Rational::unknown();p.bottomUp=vi.bmiHeader.biHeight>0;p.stride=p.info.width*4;
    p.nominalDuration100ns=vi.AvgTimePerFrame;
    if(audio>=0){if(!bind(unsigned(audio),true,p.audioFilter)||FAILED(p.graph->AddFilter(p.audioFilter.Get(),L"Capture audio"))||FAILED(p.builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Audio,p.audioFilter.Get(),nullptr,nullptr)))return false;}
    if(FAILED(p.graph.As(&p.control))||FAILED(p.graph.As(&p.events))||FAILED(p.grab->SetBufferSamples(FALSE))||FAILED(p.grab->SetCallback(&p,0)))return false;
    for(auto** f:{&p.frame,&p.pendingFrame}){
        *f=av_frame_alloc();if(!*f)return false;
        (*f)->format=AV_PIX_FMT_BGR0;(*f)->width=p.info.width;(*f)->height=p.info.height;(*f)->color_range=AVCOL_RANGE_JPEG;
        if(av_frame_get_buffer(*f,32)<0)return false;
    }
    p.info.color=pipeline::resolveFrameColor(*p.frame);p.configured=true;
    // Log the upstream type after DirectShow has finished negotiation. The
    // RGB32 output's nominal FPS alone is not proof of actual callback cadence.
    AM_MEDIA_TYPE* actual=nullptr;const auto formatHr=p.config->GetFormat(&actual);
    veyra::log::info("capture",std::format("configured {}x{} nominalFps={:.3f} upstreamSubtype=0x{:08X} formatHr=0x{:X} mailbox=1 ownedBuffers=2 deferredRun=1 audioDevice={}",
        p.info.width,p.info.height,p.info.averageFps,actual?unsigned(actual->subtype.Data1):0,unsigned(formatHr),audio));freeType(actual);
    return true;
}
bool CaptureCardSource::start(){
    auto& p=*p_;if(p.info.opened)return true;if(!p.configured||!p.control)return false;
    p.lastFrame=Impl::Clock::now();const auto hr=p.control->Run();p.info.opened=SUCCEEDED(hr);
    veyra::log::info("capture",std::format("Run hr=0x{:X} actual={}x{} nominalFps={:.3f} mailbox=1 ownedBuffers=2",unsigned(hr),p.info.width,p.info.height,p.info.averageFps));return p.info.opened;
}
CaptureMetrics CaptureCardSource::metrics()const{
    auto& p=*p_;std::lock_guard lock(p.mutex);CaptureMetrics m;
    m.received=p.received;m.delivered=p.sequence;m.dropped=p.dropped;m.readAgeMs=p.readAgeMs;
    if(p.received>1){const double elapsed=std::chrono::duration<double>(p.latestArrival-p.firstArrival).count();if(elapsed>0)m.callbackFps=(p.received-1)/elapsed;}
    if(p.sequence)m.frameAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();return m;
}
SourceReadStatus CaptureCardSource::read(pipeline::FramePacket& packet,const AVFrame** frame){auto& p=*p_;*frame=nullptr;if(!p.info.opened)return SourceReadStatus::Error;
    long code=0;LONG_PTR a=0,b=0;while(p.events&&p.events->GetEvent(&code,&a,&b,0)==S_OK){p.events->FreeEventParams(code,a,b);if(code==EC_DEVICE_LOST||code==EC_ERRORABORT)return SourceReadStatus::Error;}
    double time=0;uint32_t flags=0;uint64_t sequence=0;pipeline::Rational duration;
    {
        std::unique_lock lock(p.mutex);p.wake.wait_for(lock,std::chrono::milliseconds(30),[&]{return p.pending||p.callbackError;});
        if(p.callbackError)return SourceReadStatus::Error;
        if(!p.pending)return Impl::Clock::now()-p.lastFrame>std::chrono::seconds(3)?SourceReadStatus::Error:SourceReadStatus::Waiting;
        std::swap(p.frame,p.pendingFrame);p.pending=false;time=p.pendingTime;p.readArrival=p.pendingArrival;
        duration=p.pendingDuration;
        p.readAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();
        if(!p.sequence)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Open);
        if(p.dropped!=p.lastDrop)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Drop);
        const double maxGap=p.info.averageFps>0?2.5/p.info.averageFps:0.1;
        if(p.pendingDiscontinuity||(p.sequence&&(time<=p.lastPts||time-p.lastPts>maxGap)))flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
        p.lastDrop=p.dropped;p.lastPts=time;sequence=++p.sequence;
    }
    p.frame->pts=static_cast<int64_t>(time*10000000);p.frame->duration=duration.isUnknown()?0:duration.to100ns();p.frame->time_base={1,10000000};packet={};packet.pts={p.frame->pts,10000000};packet.duration=duration;packet.colorInfo=p.info.color;packet.sourceKind=pipeline::SourceKind::CaptureCard;packet.sequence=sequence;packet.flags=flags;packet.sourceEpoch=1;*frame=p.frame;p.lastFrame=Impl::Clock::now();return SourceReadStatus::Frame;
}
void CaptureCardSource::close()noexcept{
    auto& p=*p_;if(p.control)p.control->Stop();if(p.grab)p.grab->SetCallback(nullptr,0);
    p.events.Reset();p.control.Reset();p.grab.Reset();p.nullFilter.Reset();p.grabFilter.Reset();p.audioFilter.Reset();p.config.Reset();p.device.Reset();p.builder.Reset();p.graph.Reset();
    av_frame_free(&p.frame);av_frame_free(&p.pendingFrame);p.info={};
    p.sequence=p.received=p.dropped=p.lastDrop=0;p.pending=p.callbackError=p.configured=false;p.lastPts=p.readAgeMs=0;
}
}
