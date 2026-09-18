#include "veyra/engine/EngineController.h"
#include "veyra/sink/WasapiAudioSink.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace veyra;
using namespace std::chrono_literals;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void until(F ready){
    const auto end=std::chrono::steady_clock::now()+8s;
    while(!ready()){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        require(std::chrono::steady_clock::now()<end,"wait timeout");std::this_thread::sleep_for(5ms);
    }
}
void pcm(const std::filesystem::path& dir){
    const auto file=dir/"rate.wav";std::ofstream f(file,std::ios::binary);
    auto word=[&](uint32_t n,int bytes){for(int i=0;i<bytes;++i)f.put(char(n>>(8*i)));};
    constexpr unsigned frames=44100/10;
    f.write("RIFF",4);word(36+frames*4,4);f.write("WAVEfmt ",8);word(16,4);word(1,2);word(2,2);word(44100,4);word(44100*4,4);word(4,2);word(16,2);f.write("data",4);word(frames*4,4);
    for(unsigned i=0;i<frames;++i){const auto value=int16_t(10000*std::sin(i*.1));word(uint16_t(value),2);word(uint16_t(-value),2);}f.close();
    for(double rate:{.2,.3,1.,1.7,3.}){
        sink::AudioPipeline pipe;pipe.setPlaybackRate(rate);require(pipe.open(file.wstring()),"PCM open");pipe.startThread(nullptr);
        until([&]{return pipe.decodingComplete();});
        require(std::abs(pipe.tailPtsMs()-100)<.1,"media duration retained after resampling");
        std::vector<float> buffer(512*2);double pts;size_t count=0;double energy=0;size_t n;
        while((n=pipe.pull(buffer.data(),512,&pts))){count+=n;for(size_t i=0;i<n;++i){require(std::abs(buffer[i*2]+buffer[i*2+1])<1e-6,"stereo channels retained");energy+=std::abs(buffer[i*2]);}}
        require(std::abs(double(count)-4800/rate)<2,"PCM sample count matches requested speed");require(energy>10,"real PCM retained");require(pipe.overruns()==0,"bounded PCM queue");pipe.stopThread();
        std::cout<<"PCM rate="<<rate<<" frames="<<count<<" PASS\n";
    }
}
void measure(engine::EngineController& player,double rate){
    std::this_thread::sleep_for(350ms);
    const auto first=player.snapshot();const auto begin=std::chrono::steady_clock::now();
    until([&]{return std::chrono::steady_clock::now()-begin>1500ms;});
    const auto last=player.snapshot();const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    const double measured=(last.position-first.position)/wall;
    std::cout<<"ENGINE requested="<<rate<<" measured="<<measured<<" audio="<<last.audioAvailable<<" position="<<last.position<<" lateMs="<<last.lateMs<<'\n';
    require(!last.failed&&last.running,"playback remains active");require(std::abs(measured-rate)<.13,"real presented media advance matches speed");
}
void video(HWND window,const std::wstring& file,bool audio){
    engine::EngineController player;engine::PlayerOptions options;
    player.setVolume(0,false);player.setPlaybackRate(.2);player.open(window,file,options);
    until([&]{return player.snapshot().frames>5||player.snapshot().failed;});
    require(player.snapshot().audioAvailable==audio,"expected audio path");measure(player,.2);
    for(double rate:{3.,1.}){
        const auto before=player.snapshot().position;player.setPlaybackRate(rate);
        until([&]{return player.snapshot().position>before+.1||player.snapshot().failed;});measure(player,rate);
    }
    player.pause(true);until([&]{return player.snapshot().transport==engine::TransportState::Paused;});
    player.setPlaybackRate(.5);player.seek(5);until([&]{const auto s=player.snapshot();return s.seekPresented==s.seekRequested&&std::abs(s.position-5)<.1;});
    const auto paused=player.snapshot().position;std::this_thread::sleep_for(250ms);
    require(player.snapshot().position==paused&&player.snapshot().transport==engine::TransportState::Paused,"paused rate change and seek remain paused");
    player.pause(false);until([&]{return player.snapshot().position>paused+.1;});measure(player,.5);
    player.stop();until([&]{return player.idle();});
}
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)return 2;
    const std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);pcm(dir);
    HWND window=CreateWindowExW(0,L"STATIC",L"Playback rate test",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"preview window");video(window,argv[2],false);video(window,argv[3],true);DestroyWindow(window);
    std::cout<<"PASS playback rate PCM and actual GPU/audio clock integration\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
