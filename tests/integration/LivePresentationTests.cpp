#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>
using namespace veyra;
using namespace std::chrono_literals;
int wmain(int argc,wchar_t**argv){
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",L"1");
    if(argc!=3&&!(argc==4&&(wcscmp(argv[3],L"--fruc")==0||wcscmp(argv[3],L"--half-rate")==0||wcscmp(argv[3],L"--overload")==0||wcscmp(argv[3],L"--overload-baseline")==0||wcscmp(argv[3],L"--fruc-overload")==0||wcscmp(argv[3],L"--fruc-overload-baseline")==0||wcscmp(argv[3],L"--file-overload")==0||wcscmp(argv[3],L"--source-gap")==0||wcscmp(argv[3],L"--file-endpoint")==0||wcscmp(argv[3],L"--reset-rollback")==0)))return 2;SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(argv[2]);Logger::instance().openFile((std::filesystem::path(argv[2])/"engine.log").wstring());Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"Live scheduler replay",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 3;engine::EngineController engine;engine::PlayerOptions options;options.nr=false;options.fg=false;options.captureReplayForTest=true;
    const bool halfRate=argc==4&&wcscmp(argv[3],L"--half-rate")==0;
    const bool sourceGap=argc==4&&wcscmp(argv[3],L"--source-gap")==0;
    if(sourceGap){SetEnvironmentVariableW(L"VEYRA_TEST_REPLAY_SOURCE_GAP",L"1");options.nr=options.fg=true;options.fgMultiplier=2;}
    const bool frucOverload=argc==4&&std::wstring(argv[3]).find(L"--fruc-overload")==0;
    const bool overload=frucOverload||(argc==4&&std::wstring(argv[3]).find(L"--overload")==0);
    const bool fileOverload=argc==4&&wcscmp(argv[3],L"--file-overload")==0;
    const bool fileEndpoint=argc==4&&wcscmp(argv[3],L"--file-endpoint")==0;
    if(fileEndpoint){options.captureReplayForTest=false;SetEnvironmentVariableW(L"VEYRA_TEST_FILE_ENDPOINT_LOSS",L"1");engine.setVolume(0,true);}
    if(argc==4&&wcscmp(argv[3],L"--fruc")==0)options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Fruc;
    if(overload){options.nr=options.sr=options.fg=true;options.realtime=false;options.fgMultiplier=4;options.settings.videoSrQuality=4;options.captureReplayDisableFgAdmissionForTest=std::wstring(argv[3]).ends_with(L"-baseline");SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",nullptr);}
    if(frucOverload)options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Fruc;
    if(halfRate)options.settings.content=engine::ContentRate::Capture60To30;
    if(fileOverload){options.captureReplayForTest=false;options.nr=options.sr=options.fg=true;options.realtime=false;options.fgMultiplier=4;options.settings.videoSrQuality=4;}
    int failures=0;auto check=[&](bool pass,const char* s){std::cout<<(pass?"PASS ":"FAIL ")<<s<<std::endl;if(!pass)++failures;};
    auto until=[&](auto predicate,int seconds=8){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(std::chrono::steady_clock::now()<deadline){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}auto s=engine.snapshot();if(s.failed){std::wcerr<<s.status<<'\n';return false;}if(predicate(s))return true;std::this_thread::sleep_for(5ms);}return false;};
    engine.open(window,argv[1],options);
    if(fileEndpoint){
        check(until([](const auto& s){return s.frames>=20&&s.audioEndpointRecovering;}),"actual player exposes endpoint recovery");
        const auto held=engine.snapshot();
        std::this_thread::sleep_for(300ms);const auto stillHeld=engine.snapshot();
        check(stillHeld.frames<=held.frames+2&&stillHeld.position<=held.position+.04,"video timeline stays held during audio endpoint outage");
        check(until([](const auto& s){return s.audioEndpointRecoveries==1&&!s.audioEndpointRecovering&&s.frames>=50;}),"player resumes after endpoint reanchor");
        check(std::abs(engine.snapshot().lateMs)<30,"restored software A/V skew under 30ms");
        engine.pause(true);engine.seek(.5);
        check(until([](const auto& s){return s.transport==engine::TransportState::Paused&&s.position>=.49&&s.position<.6;}),"paused seek after endpoint recovery");
        const auto pausedFrames=engine.snapshot().frames;engine.pause(false);
        check(until([&](const auto& s){return s.frames>=pausedFrames+10;}),"resume after recovered paused seek");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"recovered file closes without audio thread deadlock");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(sourceGap){
        check(until([](const auto& s){return s.frames==12&&s.metrics.flow.counters.realPresented==12&&s.processedCompleted==12;}),"source Waiting still completes and presents all outstanding real frames");
        check(until([](const auto& s){return s.frames>=20;}),"source resumes after gap without deadlock");
        // The fixture can take roughly a minute in this host and this path intentionally runs
        // every source frame through the live scheduler; leave headroom for
        // host scheduling without changing the bounded 290s test watchdog.
        check(until([](const auto& s){return s.transport==engine::TransportState::Ended;},90),"EOF drains final live batch before marking ended");
        const auto s=engine.snapshot();const auto& c=s.metrics.flow.counters;
        std::cout<<"EOS frames="<<s.frames<<" ready="<<s.processedCompleted<<" presented="<<c.realPresented<<" cancelled="<<c.cancelledBeforePresent<<'\n';
        check(s.frames>20&&s.processedCompleted==s.frames&&c.realPresented==c.realSubmitted,"last real source frame is completed and current epoch fully presented");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"source-gap session stops cleanly");
        Logger::instance().flush();std::ifstream log(std::filesystem::path(argv[2])/"engine.log");std::string line;uint64_t totalPresented=0,totalSubmitted=0;
        const std::regex submittedPattern("realSubmitted=([0-9]+)"),presentedPattern("realPresented=([0-9]+)");
        while(std::getline(log,line))if(line.find("[frame-flow] state=closed ")!=std::string::npos){std::smatch match;if(std::regex_search(line,match,submittedPattern))totalSubmitted+=std::stoull(match[1]);if(std::regex_search(line,match,presentedPattern))totalPresented+=std::stoull(match[1]);}
        check(totalPresented==s.frames&&totalSubmitted==s.frames,"all source frames across EOF discontinuity epochs present exactly once");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(fileOverload){
        engine.setVolume(0,true);bool held=false,resumed=false;double maxLead=0;
        check(until([&](const auto& s){held|=s.audioRebuffering;resumed|=held&&!s.audioRebuffering&&s.frames>10;maxLead=std::max(maxLead,s.lateMs);return s.frames>=100;},35),"overloaded file keeps advancing without dropping source frames or deadlocking");
        check(held&&resumed,"actual GPU overload holds audio and resumes after video catches up");
        const auto before=engine.snapshot();engine.pause(true);engine.seek(.5);
        check(until([](const auto& s){return s.transport==engine::TransportState::Paused&&s.position>=.49&&s.position<.6;}),"paused seek survives an audio overload hold");
        engine.pause(false);check(until([&](const auto& s){return s.frames>before.frames+8;}),"file resumes after overload and seek");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"file overload shutdown");
        std::cout<<"FILE_OVERLOAD held="<<held<<" resumed="<<resumed<<" maxObservedLeadMs="<<maxLead<<'\n';
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
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
        const auto colorSamples=s.metrics.flow.gpuTiming[size_t(diagnostics::GpuStage::Color)].samples;
        std::cout<<"GPU_TIMING sourceFps="<<s.fps<<" colorSamples="<<colorSamples<<'\n';
        check(colorSamples>=20&&colorSamples<=35,"one-second stage window receives completed source-frame timings");
    }
    auto state=engine.snapshot();auto settings=state.desired;settings.nr=true;settings.multiplier=2;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.nr&&s.applied.multiplier==2&&!s.applying&&s.nrEvaluated>2&&s.generated>0;}),"NR and 2x transaction while two batches are in flight");
    check(until([](const auto& s){const auto& r=s.metrics.flow.reset;return r.settingsRevision==s.applied.revision&&r.outcome==diagnostics::ResetOutcome::Completed&&r.totalMs.has_value();}),"settings reset completes only after matching GPU-ready output");
    {
        const auto s=engine.snapshot();const auto& r=s.metrics.flow.reset;double stages=0;bool measured=true;
        for(const auto& ms:r.stageMs){measured&=ms.has_value()&&*ms>=0;if(ms)stages+=*ms;}
        check(r.sessionId==s.sessionId&&r.epoch==s.metrics.identity.epoch&&r.sourceFrameId>0&&measured&&r.totalMs&&*r.totalMs+.01>=stages,"reset identity and disjoint drain/destroy/create/warmup/ready intervals are measured");
    }
    check(until([](const auto& s){const auto& m=s.metrics.flow;return m.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)].samples>0&&m.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromB)].samples>0;}),"presented generated frames have actual A/B latency samples");
    state=engine.snapshot();const auto liveRevision=state.applied.revision;check(state.metrics.identity.settingsRevision==state.applied.revision,"GPU metrics belong to applied revision");
    const auto& flow=state.metrics.flow;
    check(flow.latest.sameWindow(state.sessionId,state.metrics.identity)&&flow.counters.fgEvaluated>0,"flow counters carry actual session, epoch and revision");
    check(flow.counters.commandSlotHighWater<=6&&flow.counters.presentationBatchHighWater<=2,"command and presentation queues remain bounded");
    check(flow.counters.fgCandidate==flow.counters.fgSkippedBeforeEval+flow.counters.fgEvaluated,"each candidate is accounted before evaluate");
    if(argc==4&&wcscmp(argv[3],L"--reset-rollback")==0){
        SetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",L"1");
        settings=state.desired;settings.nr=false;engine.requestSettings(settings);
        const auto rejected=engine.snapshot().desired.revision;
        check(until([&](const auto& s){return s.rejectedRevision==rejected&&!s.applying&&s.applied.nr;}),"failed first evaluation restores previous NR configuration");
        SetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",nullptr);
        check(until([&](const auto& s){const auto& r=s.metrics.flow.reset;return r.settingsRevision==rejected&&r.outcome==diagnostics::ResetOutcome::RolledBack&&r.totalMs.has_value();}),"failed reset keeps rejected revision and rollback timing instead of success");
        check(until([&](const auto& s){return s.frames>state.frames+5&&s.metrics.flow.counters.realReady>0;}),"restored graph produces completed real frames after rollback");
        state=engine.snapshot();
    }
    const auto audioEpoch=state.metrics.identity.epoch;const auto audioFrames=state.frames;
    settings=state.desired;settings.audioSync=engine::AudioSyncMode::Manual;settings.audioOffsetMs=75;
    check(engine.requestSettings(settings),"audio-only change accepted while NR/FG are active");
    check(until([&](const auto& s){return !s.applying&&s.applied.audioOffsetMs==75&&s.frames>=audioFrames+4;}),"audio-only transaction completes while video advances");
    state=engine.snapshot();
    check(state.applied.revision==liveRevision&&state.metrics.identity.epoch==audioEpoch,"audio-only edit retains GPU revision and temporal history");
    settings=state.desired;check(engine.requestSettings(settings)&&engine.snapshot().desired.revision==liveRevision&&!engine.snapshot().applying,"identical settings notification does not enqueue a GPU transaction");
    engine.pause(true);check(until([](const auto& s){return s.transport==engine::TransportState::Paused;}),"pause returns without queue deadlock");
    check(engine.snapshot().fps==0,"paused FPS is zero rather than a retained session average");
    settings=engine.snapshot().desired;settings.audioOffsetMs=90;engine.requestSettings(settings);
    check(until([&](const auto& s){return !s.applying&&s.applied.audioOffsetMs==90&&s.applied.revision==liveRevision;}),"audio edit applies while paused without rerunning video");
    settings=engine.snapshot().desired;settings.model.style=1;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.model.style==1&&!s.applying;}),"settings apply while paused");
    check(until([](const auto& s){return s.metrics.flow.reset.settingsRevision==s.applied.revision&&s.metrics.flow.reset.outcome==diagnostics::ResetOutcome::Completed;}),"paused cached preview reset observes its actual GPU completion");
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
    uint64_t cancelledRevision=0;
    if(argc==4&&wcscmp(argv[3],L"--reset-rollback")==0){
        settings=engine.snapshot().desired;settings.nr=true;settings.multiplier=2;engine.requestSettings(settings);cancelledRevision=engine.snapshot().desired.revision;
        check(until([&](const auto& s){return s.metrics.flow.reset.settingsRevision==cancelledRevision&&s.metrics.flow.reset.outcome==diagnostics::ResetOutcome::InProgress;}),"pending rebuild is observable before first output");
    }
    engine.stop();const auto stopLimit=std::chrono::steady_clock::now()+5s;while(!engine.idle()&&std::chrono::steady_clock::now()<stopLimit)std::this_thread::sleep_for(5ms);check(engine.idle(),"stop drains presenter before graph shutdown");
    if(cancelledRevision){const auto s=engine.snapshot();const auto& r=s.metrics.flow.reset;check(r.settingsRevision==cancelledRevision&&r.outcome==diagnostics::ResetOutcome::Cancelled&&!r.stageMs[size_t(diagnostics::ResetStage::FirstValid)],"stopped rebuild records cancellation without invented valid output");}
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
