#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
using namespace veyra;
using namespace std::chrono_literals;
int wmain(int argc,wchar_t**argv){
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",L"1");
    if(argc!=3&&!(argc==4&&(wcscmp(argv[3],L"--fruc")==0||wcscmp(argv[3],L"--half-rate")==0||wcscmp(argv[3],L"--overload")==0||wcscmp(argv[3],L"--overload-baseline")==0)))return 2;SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(argv[2]);Logger::instance().openFile((std::filesystem::path(argv[2])/"engine.log").wstring());Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"Live scheduler replay",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 3;engine::EngineController engine;engine::PlayerOptions options;options.nr=false;options.fg=false;options.captureReplayForTest=true;
    const bool halfRate=argc==4&&wcscmp(argv[3],L"--half-rate")==0;
    const bool overload=argc==4&&std::wstring(argv[3]).find(L"--overload")==0;
    if(argc==4&&wcscmp(argv[3],L"--fruc")==0)options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Fruc;
    if(overload){options.nr=options.sr=options.fg=true;options.realtime=false;options.fgMultiplier=4;options.settings.videoSrQuality=4;options.captureReplayDisableFgAdmissionForTest=wcscmp(argv[3],L"--overload-baseline")==0;SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",nullptr);}
    if(halfRate)options.settings.content=engine::ContentRate::Capture60To30;
    int failures=0;auto check=[&](bool pass,const char* s){std::cout<<(pass?"PASS ":"FAIL ")<<s<<std::endl;if(!pass)++failures;};
    auto until=[&](auto predicate,int seconds=8){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(std::chrono::steady_clock::now()<deadline){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}auto s=engine.snapshot();if(s.failed){std::wcerr<<s.status<<'\n';return false;}if(predicate(s))return true;std::this_thread::sleep_for(5ms);}return false;};
    engine.open(window,argv[1],options);
    if(overload){
        check(until([](const auto& s){return s.frames>=150;},35),"native4K NR + VSR high + FG4 completes bounded overload replay");
        const auto s=engine.snapshot();const auto& c=s.metrics.flow.counters;
        check(c.fgCandidate==c.fgEvaluated+c.fgSkippedBeforeEval&&c.presentationBatchHighWater<=2&&c.commandSlotHighWater<=6,"overload work accounting and resource bounds");
        check(options.captureReplayDisableFgAdmissionForTest?c.fgSkippedBeforeEval==0:c.fgSkippedBeforeEval>0,"overload comparison uses requested admission policy");
        std::cout<<"OVERLOAD baseline="<<options.captureReplayDisableFgAdmissionForTest<<" frames="<<s.frames<<" processedFps="<<s.fps<<" realPresented="<<c.realPresented<<" generatedPresented="<<c.generatedPresented<<" expired="<<c.generatedExpiredAfterEval<<" skipped="<<c.fgSkippedBeforeEval<<" evaluated="<<c.fgEvaluated<<" ageP95Ms="<<s.captureAgeP95Ms<<std::endl;
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"overload shutdown releases leases");DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    check(until([](const auto& s){return s.capture&&s.frames>=8&&s.metrics.submitted>=4;}),"file replay runs actual live worker and presents");
    if(halfRate){auto s=engine.snapshot();check(s.captureHalfRate&&s.captureRateSkipped>=s.frames-2,"60fps input is sampled before GPU processing");}
    check(engine.snapshot().processedCompleted>0,"real-only GPU completions are counted without FG");
    if(halfRate){
        check(until([](const auto& s){return s.processedCompleted>=80;}),"sampled capture sustains more than two seconds");
        const auto s=engine.snapshot();check(s.fps>=27&&s.fps<=33,"GPU completed throughput is about 30fps, not transport60");
    }
    auto state=engine.snapshot();auto settings=state.desired;settings.nr=true;settings.multiplier=2;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.nr&&s.applied.multiplier==2&&!s.applying&&s.nrEvaluated>2&&s.generated>0;}),"NR and 2x transaction while two batches are in flight");
    state=engine.snapshot();const auto liveRevision=state.applied.revision;check(state.metrics.identity.settingsRevision==state.applied.revision,"GPU metrics belong to applied revision");
    const auto& flow=state.metrics.flow;
    check(flow.latest.sameWindow(state.sessionId,state.metrics.identity)&&flow.counters.fgEvaluated>0,"flow counters carry actual session, epoch and revision");
    check(flow.counters.commandSlotHighWater<=6&&flow.counters.presentationBatchHighWater<=2,"command and presentation queues remain bounded");
    check(flow.counters.fgCandidate==flow.counters.fgSkippedBeforeEval+flow.counters.fgEvaluated,"each candidate is accounted before evaluate");
    engine.pause(true);check(until([](const auto& s){return s.transport==engine::TransportState::Paused;}),"pause returns without queue deadlock");
    check(engine.snapshot().fps==0,"paused FPS is zero rather than a retained session average");
    settings=engine.snapshot().desired;settings.model.style=1;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.model.style==1&&!s.applying;}),"settings apply while paused");
    const auto pausedRevision=engine.snapshot().applied.revision;
    engine.pause(false);const auto before=engine.snapshot().frames;check(until([&](const auto& s){return s.frames>=before+8&&s.metrics.submitted>0;}),"resume resets history and restarts bounded presentation");
    settings=engine.snapshot().desired;settings.multiplier=4;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.multiplier==4&&!s.applying&&s.metrics.submitted>8;}),"4x uses worker without lease overwrite");
    settings=engine.snapshot().desired;settings.nr=false;settings.multiplier=1;engine.requestSettings(settings);
    check(until([](const auto& s){return !s.applied.nr&&s.applied.multiplier==1&&!s.applying&&s.metrics.sourceFrames>4;}),"disable features and keep live source open");
    check(engine.snapshot().metrics.validGenerated==0,"disabled revision cannot retain prior generated-frame count");
    check(engine.snapshot().metrics.flow.counters.fgEvaluated==0&&engine.snapshot().metrics.flow.counters.generatedPresented==0,"late FG completion never enters disabled revision flow counters");
    if(halfRate){
        settings=engine.snapshot().desired;settings.content=engine::ContentRate::Transport;engine.requestSettings(settings);
        check(until([](const auto& s){return !s.applying&&!s.captureHalfRate&&s.metrics.sourceFrames>=80;}),"switch back to original capture cadence live");
        const auto s=engine.snapshot();check(s.fps>=55&&s.fps<=65,"GPU completed throughput returns to about 60fps");
    }
    engine.stop();const auto stopLimit=std::chrono::steady_clock::now()+5s;while(!engine.idle()&&std::chrono::steady_clock::now()<stopLimit)std::this_thread::sleep_for(5ms);check(engine.idle(),"stop drains presenter before graph shutdown");
    Logger::instance().flush();std::ifstream log(std::filesystem::path(argv[2])/"engine.log");std::string line;bool liveFresh=false,pausedCached=false;
    while(std::getline(log,line))if(line.find("source-identity")!=std::string::npos){
        if(line.find("cached=false revision="+std::to_string(liveRevision)+" ")!=std::string::npos)liveFresh=true;
        if(line.find("cached=true revision="+std::to_string(pausedRevision)+" ")!=std::string::npos)pausedCached=true;
        if(line.find("cached=true revision="+std::to_string(liveRevision)+" ")!=std::string::npos)++failures;
    }
    check(liveFresh,"running capture settings process fresh source frame, never cached frame");
    check(pausedCached,"paused settings retain cached-frame preview");
    DestroyWindow(window);CoUninitialize();std::cout<<"failures="<<failures<<" (synthetic file replay, not physical capture)\n";return failures?1:0;
}
