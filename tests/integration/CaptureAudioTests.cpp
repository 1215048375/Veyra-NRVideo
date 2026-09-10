#include "veyra/sink/CaptureAudioSession.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <cmath>
int main(){
    using namespace veyra::sink;using Clock=std::chrono::steady_clock;
    CaptureAudioSession audio;WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;
    if(!audio.configure(f)||!audio.start())return 2;audio.setGain(0);
    std::vector<int16_t> pcm(960,0);const auto start=Clock::now();
    double sum=0;unsigned count=0;bool bounded=true;
    for(unsigned i=0;i<160;++i){
        const auto due=start+std::chrono::milliseconds(i*10);std::this_thread::sleep_until(due);
        const auto time=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
        if(!audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0))return 3;
        if(i>=8)audio.videoPresented(i*10.0-80,time);
        const auto s=audio.snapshot();bounded&=s.bufferedMs<=520;
        if(i>70&&s.running&&s.skewMs){sum+=std::abs(*s.skewMs);++count;}
    }
    const auto state=audio.snapshot();
    std::cout<<"capture_audio meanAbsSkewMs="<<(count?sum/count:-1)<<" samples="<<count<<" compensationMs="<<state.compensationMs<<" queueMs="<<state.bufferedMs<<" resets="<<state.resets<<" overflows="<<state.overflows<<" underruns="<<state.underruns<<'\n';
    bool ok=count>=60&&sum/count<25&&state.compensationMs>=65&&state.compensationMs<=95&&bounded&&state.overflows==0;
    std::cout<<(ok?"PASS":"FAIL")<<" synthetic capture PTS with 80ms video delay; real WASAPI, no physical capture\n";
    unsigned sequence=160;
    auto phase=[&](unsigned mode,int offset,double delay,double expectedComp,double expectedSkew){
        audio.setSync(mode,offset);double error=0;unsigned samples=0;bool boundedPhase=true;
        for(unsigned j=0;j<150;++j,++sequence){
            std::this_thread::sleep_until(start+std::chrono::milliseconds(sequence*10));
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            if(!audio.push(pcm.data(),pcm.size()*2,sequence*10.0,false))return false;
            audio.videoPresented(sequence*10.0-delay,host);const auto s=audio.snapshot();
            boundedPhase&=s.bufferedMs<=520;
            if(j>70&&s.running&&s.skewMs){error+=std::abs(*s.skewMs-expectedSkew);++samples;}
        }
        const auto s=audio.snapshot();const bool pass=samples>=60&&error/samples<28&&std::abs(s.compensationMs-expectedComp)<15&&boundedPhase&&s.overflows==0;
        std::cout<<(pass?"PASS ":"FAIL ")<<"capture phase mode="<<mode<<" delay="<<delay<<" compensation="<<s.compensationMs<<" meanSkewError="<<(samples?error/samples:-1)<<" resets="<<s.resets<<'\n';return pass;
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
    return ok?0:1;
}
