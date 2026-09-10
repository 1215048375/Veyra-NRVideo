#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <functional>
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/engine/PreviewView.h"
#include "veyra/sink/CaptureAudioSession.h"
namespace veyra::sink { struct RgbaImage; }
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::engine {
class FrameFlowWindow;
struct PlayerOptions { bool nr=true,sr=false,fg=false,realtime=true; uint32_t fgMultiplier=2; EnhancementSettings settings;
    bool captureReplayForTest=false; // file-backed live scheduler test; never enabled by UI
    bool captureReplayDisableFgAdmissionForTest=false; // controlled scheduler A/B only
    EnhancementSettings snapshot()const{auto s=settings;s.nr=nr;s.sr=sr;s.multiplier=fg?fgMultiplier:1;s.nrPolicy=realtime?pipeline::NrSizePolicy::Realtime:pipeline::NrSizePolicy::Native;return s;}
    static PlayerOptions from(EnhancementSettings s){PlayerOptions o;o.nr=s.nr;o.sr=s.sr;o.fg=s.multiplier>1;o.fgMultiplier=std::max(2u,s.multiplier);o.realtime=s.nrPolicy==pipeline::NrSizePolicy::Realtime;o.settings=s;return o;}
};
enum class TransportState { Empty, Opening, Playing, Paused, Ended, Stopping, Failed };
struct PlayerSnapshot {
    TransportState transport=TransportState::Empty;
    uint64_t sessionId=0,rejectedRevision=0;float volume=1;bool muted=false,audioAvailable=false;
    std::wstring status=L"请打开视频或图片";
    EnhancementSettings desired,applied;bool applying=false;
    bool nrActive=false,srActive=false,fgActive=false;
    std::wstring backendWarning;
    sink::CaptureAudioState captureAudio;
    bool audioRebuffering=false;
    bool audioEndpointRecovering=false;HRESULT audioEndpointError=S_OK;uint64_t audioEndpointRecoveries=0;
    double position=0,duration=0,fps=0,lateMs=0,lateP95Ms=0;
    diagnostics::FrameMetrics metrics;
    std::optional<double> submissionFps;
    uint32_t flowPerf=0;int contentFps=0;
    uint64_t frames=0,generated=0;
    uint64_t captureReceived=0,captureDropped=0,nrEvaluated=0,nvofExecuted=0;
    uint64_t captureRateSkipped=0,processedCompleted=0;
    bool captureHalfRate=false;
    double captureFps=0,captureReadAgeMs=0,captureAgeMs=0,captureAgeP95Ms=0;
    double schedulingWaitP95Ms=0,processCpuP95Ms=0,presentCpuP95Ms=0;
    bool running=false,failed=false,image=false,capture=false;
};
class EngineController {
public:
    EngineController();
    ~EngineController();
    bool idle()const;
    bool requestSettings(EnhancementSettings);
    void open(HWND video,const std::wstring& path,PlayerOptions options);
    void stop();
    void comparison(int mode,bool base,float split=.5f){comparisonMode_=mode;comparisonBase_=base;comparisonSplit_=std::clamp(split,0.0f,1.0f);}
    void pause(bool p);
    void previewView(PreviewView view){if(!std::isfinite(view.zoom)||!std::isfinite(view.centerX)||!std::isfinite(view.centerY))return;std::lock_guard lock(mutex_);view.zoom=std::clamp(view.zoom,.05f,64.0f);previewView_=view;}
    PreviewView previewView()const{std::lock_guard lock(mutex_);return previewView_;}
    void setVolume(float gain,bool mute);
    void seek(double seconds){seekSeconds_.store(seconds);}
    void saveFrame(const std::wstring& path);
    void startExport(const std::wstring& input,const std::wstring& output,PlayerOptions,bool hevc);
    PlayerSnapshot snapshot()const;
private:
    void run(HWND,std::wstring,PlayerOptions);
    void runLargeImage(HWND,const sink::RgbaImage&,PlayerOptions,gfx::D3D12DeviceContext&,gfx::CommandSlotRing&);
    void post(std::function<void()>);
    void dispatch();
    void status(const std::wstring&,bool failed=false);
    mutable std::mutex mutex_;
    PlayerSnapshot snapshot_;
    std::shared_ptr<FrameFlowWindow> activeFlow_;
    PreviewView previewView_;
    std::wstring savePath_;
    std::thread worker_;
    std::condition_variable wake_;std::function<void()> pending_;bool shutdown_=false,busy_=false;
    EnhancementSettings desired_;uint64_t nextRevision_=1;
    std::atomic<bool> stop_{false},paused_{false},muted_{false};
    std::atomic<float> volume_{1};uint64_t sessionId_=0;
    std::atomic<double> seekSeconds_{-1};
    std::atomic<int> comparisonMode_{0};std::atomic<bool> comparisonBase_{false};std::atomic<float> comparisonSplit_{.5f};
};
}
