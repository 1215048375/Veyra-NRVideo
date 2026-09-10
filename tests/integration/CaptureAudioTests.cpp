#include "veyra/sink/CaptureAudioSession.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string_view>
int main(int argc,char** argv){
    using namespace veyra::sink;using Clock=std::chrono::steady_clock;
    struct Pacer {
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        ~Pacer(){if(timer)CloseHandle(timer);}
        void until(Clock::time_point due){
            const auto ticks=std::chrono::duration_cast<std::chrono::nanoseconds>(due-Clock::now()).count()/100;
            if(ticks<=0)return;
            LARGE_INTEGER when{};when.QuadPart=-ticks;
            if(timer&&SetWaitableTimer(timer,&when,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,100);
            else std::this_thread::sleep_until(due);
        }
    } pacer;
    const bool endpointTest=argc==2&&std::string_view(argv[1])=="--endpoint-loss";
    const bool fastDrift=argc==2&&std::string_view(argv[1])=="--drift-fast";
    const bool driftTest=fastDrift||(argc==2&&std::string_view(argv[1])=="--drift-slow");
    if(endpointTest)SetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_ENDPOINT_LOSS",L"1");
    CaptureAudioSession audio;WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;
    if(!audio.configure(f)||!audio.start())return 2;audio.setGain(0);
    std::vector<int16_t> pcm(960,0);const auto start=Clock::now();
    if(driftTest){
        std::vector<double> errors;unsigned missing=0;
        const double speed=fastDrift?1.001:.999;
        for(unsigned i=0;i<11980;++i){
            pacer.until(start+std::chrono::microseconds(int64_t(i*10000/speed)));
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            if(!audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0))return 6;
            if(i>=8)audio.videoPresented(i*10.0-80,host);
            const auto s=audio.snapshot();if(i>1000){if(s.running&&s.skewMs)errors.push_back(std::abs(*s.skewMs));else ++missing;}
            if(i&&i%2000==0)std::cout<<"DRIFT seconds="<<i/100<<" skewMs="<<s.skewMs.value_or(-999)<<" resets="<<s.resets<<" queueMs="<<s.bufferedMs<<" correctionPpm="<<s.driftCorrectionPpm<<std::endl;
        }
        const auto state=audio.snapshot();audio.stop();std::sort(errors.begin(),errors.end());
        const double p95=errors.empty()?999:errors[(errors.size()*95+99)/100-1];
        const bool pass=p95<=30&&missing<100&&state.resets==1&&state.overflows==0&&state.bufferHighWaterMs<=500;
        std::cout<<(pass?"PASS ":"FAIL ")<<"120s capture clock speed="<<speed<<" p95SkewMs="<<p95<<" missing="<<missing<<" resets="<<state.resets<<" highWaterMs="<<state.bufferHighWaterMs<<'\n';return pass?0:1;
    }
    double sum=0;unsigned count=0;bool bounded=true,sawReconnecting=false;
    for(unsigned i=0;i<(endpointTest?350u:160u);++i){
        const auto due=start+std::chrono::milliseconds(i*10);pacer.until(due);
        const auto time=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
        if(!audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0))return 3;
        if(i>=8)audio.videoPresented(i*10.0-80,time);
        const auto s=audio.snapshot();bounded&=s.bufferedMs<=520;
        sawReconnecting|=!s.error.empty();
        if(i>(endpointTest?240u:70u)&&s.running&&s.skewMs){sum+=std::abs(*s.skewMs);++count;}
    }
    const auto state=audio.snapshot();
    std::cout<<"capture_audio meanAbsSkewMs="<<(count?sum/count:-1)<<" samples="<<count<<" compensationMs="<<state.compensationMs<<" queueMs="<<state.bufferedMs<<" resets="<<state.resets<<" overflows="<<state.overflows<<" underruns="<<state.underruns<<'\n';
    bool ok=count>=60&&sum/count<25&&state.compensationMs>=65&&state.compensationMs<=95&&bounded&&state.overflows==0;
    if(endpointTest){ok=ok&&sawReconnecting&&state.error.empty()&&state.endpointRetries>=2&&state.running;audio.stop();std::cout<<(ok?"PASS ":"FAIL ")<<"owned WASAPI endpoint recovered without reopening capture retries="<<state.endpointRetries<<'\n';return ok?0:1;}
    std::cout<<(ok?"PASS":"FAIL")<<" synthetic capture PTS with 80ms video delay; real WASAPI, no physical capture\n";
    unsigned sequence=160;
    auto phase=[&](unsigned mode,int offset,double delay,double expectedComp,double expectedSkew){
        audio.setSync(mode,offset);double error=0;unsigned samples=0;bool boundedPhase=true;
        for(unsigned j=0;j<150;++j,++sequence){
            pacer.until(start+std::chrono::milliseconds(sequence*10));
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            if(!audio.push(pcm.data(),pcm.size()*2,sequence*10.0,false))return false;
            audio.videoPresented(sequence*10.0-delay,host);const auto s=audio.snapshot();
            boundedPhase&=s.bufferedMs<=520;
            if(j>70&&s.running&&s.skewMs){error+=std::abs(*s.skewMs-expectedSkew);++samples;}
        }
        const auto s=audio.snapshot();const bool pass=samples>=60&&error/samples<28&&std::abs(s.compensationMs-expectedComp)<15&&boundedPhase&&s.overflows==0;
        std::cout<<(pass?"PASS ":"FAIL ")<<"capture phase mode="<<mode<<" delay="<<delay<<" compensation="<<s.compensationMs<<" meanSkewError="<<(samples?error/samples:-1)<<" resets="<<s.resets<<" queueMs="<<s.bufferedMs<<" endpointMs="<<s.endpointBufferedMs<<" underruns="<<s.underruns<<" correctionPpm="<<s.driftCorrectionPpm<<'\n';return pass;
    };
    ok=phase(0,0,160,160,0)&&ok;
    ok=phase(1,100,80,100,-20)&&ok;
    ok=phase(2,0,80,0,80)&&ok;
    ok=phase(1,-100,80,0,80)&&ok;
    const auto beforeStall=audio.snapshot().resets;
    std::this_thread::sleep_for(std::chrono::milliseconds(350));sequence+=35;
    ok=phase(0,0,80,80,0)&&ok;
    const bool recovered=audio.snapshot().resets>beforeStall;
    std::cout<<(recovered?"PASS ":"FAIL ")<<"audio reanchors real PCM after ingress stall\n";ok=recovered&&ok;
    audio.stop();
    const auto stopped=audio.snapshot();const bool stoppedClean=!stopped.available&&!stopped.running&&!stopped.skewMs&&stopped.bufferedMs==0;
    std::cout<<(stoppedClean?"PASS ":"FAIL ")<<"stopped audio has no stale clock or queued state\n";ok=stoppedClean&&ok;
    if(!audio.start())return 4;
    ok=phase(0,0,80,80,0)&&ok;
    audio.stop();
    if(!audio.start())return 5;
    audio.setSync(0,0);
    bool burstAccepted=true;
    for(unsigned i=0;i<600;++i)burstAccepted=audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0)&&burstAccepted;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const auto burst=audio.snapshot();
    const bool burstBound=burstAccepted&&burst.bufferHighWaterMs<=500&&burst.overflows>0;
    std::cout<<(burstBound?"PASS ":"FAIL ")<<"combined raw/converting/PCM burst budget highWaterMs="<<burst.bufferHighWaterMs<<" overflows="<<burst.overflows<<'\n';ok=burstBound&&ok;
    audio.stop();
    return ok?0:1;
}
