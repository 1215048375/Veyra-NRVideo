#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/ExportJobManager.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

using namespace veyra;
namespace {
int failures=0;
void check(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;failures+=!ok;}

void sourceTest(const std::wstring& path,unsigned expected,bool damaged){
    source::MediaFileSource input;source::SourceOpenDesc desc;desc.path=path;desc.preferHardwareDecode=false;
    if(!input.open(desc)){check(false,"source opens");return;}
    pipeline::FramePacket packet;const AVFrame* frame=nullptr;
    unsigned count=0;auto state=source::SourceReadStatus::Error;
    while(count<=expected&&(state=input.read(packet,&frame))==source::SourceReadStatus::Frame){
        if(!frame||frame->width!=int(input.info().width)||frame->height!=int(input.info().height)){check(false,"source never returns mismatched extent");return;}
        ++count;
    }
    if(damaged){
        check(state==source::SourceReadStatus::Error&&count>0&&count<expected,"bad input is an error, not short successful EOF");
        check(!input.errorMessage().empty(),"source explains failure");
        check(input.read(packet,&frame)==source::SourceReadStatus::Error&&frame==nullptr,"error stays latched");
    }else check(state==source::SourceReadStatus::Eos&&count==expected,"healthy input drains every frame");
    check(input.seek({0,1}),"seek after EOF/error accepted");
    check(input.errorMessage().empty()&&input.read(packet,&frame)==source::SourceReadStatus::Frame,"seek clears error and restarts decoding");
    input.close();check(input.open(desc)&&input.read(packet,&frame)==source::SourceReadStatus::Frame,"reopen clears error and restarts decoding");
}

void graphTest(){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status)){check(false,"graph device initialized");return;}
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=32;desc.sourceHeight=desc.workHeight=16;
    desc.enableNr=desc.enableSr=desc.enableFg=false;desc.noFeatures=true;
    if(!graph.initialize(desc)||!graph.createViews()){check(false,"graph initialized");return;}
    pipeline::EnhanceGraph::FrameOutputs out;
    // No pixel allocation: a rejected input must never touch any plane.
    for(auto size:{std::pair{64,32},std::pair{16,8},std::pair{0,16}}){
        AVFrame invalid{};invalid.width=size.first;invalid.height=size.second;invalid.format=AV_PIX_FMT_YUV420P;
        check(!graph.process(&invalid,0,true,out)&&out.batch.count==0,"wrong extent rejected before pixel access");
    }
    AVFrame invalid{};invalid.width=32;invalid.height=16;invalid.format=AV_PIX_FMT_NONE;
    check(!graph.process(&invalid,0,true,out),"empty decoder frame rejected");
    AVFrame* valid=av_frame_alloc();valid->width=32;valid->height=16;valid->format=AV_PIX_FMT_YUV420P;
    if(av_frame_get_buffer(valid,32)<0){av_frame_free(&valid);check(false,"valid frame allocation");return;}
    for(int p=0;p<3;++p)for(int y=0;y<(p?8:16);++y)std::fill_n(valid->data[p]+y*valid->linesize[p],p?16:32,uint8_t(128));
    check(graph.process(valid,0,true,out),"valid frame accepted after rejection");
    sink::RgbaImage image;
    check(sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image)&&image.width==32&&image.height==16&&!image.pixels.empty()&&image.pixels[0]>0,"valid output remains readable and nonblack");
    out={};ring.drainQueue();graph.shutdown();av_frame_free(&valid);
}

void playbackTest(const std::wstring& path,bool overload){
    SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",overload?L"150":nullptr);
    HWND window=CreateWindowExW(0,L"STATIC",L"File safety regression",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    engine::EngineController player;engine::PlayerOptions options;options.nr=options.sr=options.fg=false;player.setVolume(0,true);
    auto until=[&](auto predicate){
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<deadline){
            auto snapshot=player.snapshot();if(snapshot.failed)return false;if(predicate(snapshot))return true;
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }return false;
    };
    player.open(window,path,options);
    check(until([](const auto& s){return s.transport==engine::TransportState::Ended;}),"playback reaches EOS without crash");
    auto end=player.snapshot();
    check(end.position>=1.96&&end.frames>0,"last source frame reaches presentation");
    if(overload)check(end.previewSkipped>0,"overload actually used preview catch-up");
    player.pause(true);player.seek(1.9);const auto request=player.snapshot().seekRequested;
    check(until([&](const auto& s){return s.seekPresented==request&&s.position>=1.9;}),"paused tail seek presents requested frame");
    player.pause(false);
    check(until([](const auto& s){return s.transport==engine::TransportState::Ended;}),"resume after paused tail seek reaches EOS");
    player.stop();check(until([&](const auto&){return player.idle();}),"playback worker closes");DestroyWindow(window);
    SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
}

bool corruptLastVideoPacket(const std::filesystem::path& file){
    const auto u8=file.u8string();const std::string name(u8.begin(),u8.end());AVFormatContext* input=nullptr;
    if(avformat_open_input(&input,name.c_str(),nullptr,nullptr)<0)return false;
    int64_t position=-1;int size=0;AVPacket* packet=av_packet_alloc();
    if(avformat_find_stream_info(input,nullptr)>=0){
        const int video=av_find_best_stream(input,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
        while(av_read_frame(input,packet)>=0){if(packet->stream_index==video){position=packet->pos;size=packet->size;}av_packet_unref(packet);}
    }
    av_packet_free(&packet);avformat_close_input(&input);
    if(position<0||size<=0)return false;
    std::fstream stream(file,std::ios::binary|std::ios::in|std::ios::out);stream.seekp(position);
    const std::vector<char> zeros(size);stream.write(zeros.data(),size);return bool(stream);
}

void exportFaultTest(const std::wstring& path,const std::wstring& output,const std::wstring& mode){
    std::atomic<bool> cancel=false;bool injected=false;std::wstring finalMessage;
    engine::PlayerOptions options;options.nr=options.sr=options.fg=false;unsigned boundaries=0;
    const bool ok=engine::exportVideo(path,output,options,false,cancel,[&](double progress,const std::wstring& message){
        finalMessage=message;
        if(progress==.999){
            if(mode==L"export-corrupt-output"){injected=corruptLastVideoPacket(output+L".partial");check(injected,"damaged final encoded packet before integrity check");}
            else if(mode==L"export-cancel-verify"){cancel=true;injected=true;}
        }
    },0,[&]{if(mode==L"export-abort-boundary"&&++boundaries>10){injected=true;return false;}return true;});
    check(injected&&!ok,"injected failure/cancellation cannot report success");
    check(!std::filesystem::exists(output)&&std::filesystem::exists(output+L".partial"),"failed output stays partial, never promoted");
    check(finalMessage.find(L"完成")==std::wstring::npos,"completion message does not claim success");
}

void exportWorkerTest(const std::wstring& path,const std::wstring& output){
    engine::ExportJobManager job;engine::PlayerOptions options;options.nr=options.sr=options.fg=false;
    if(!job.start(path,output,options.snapshot(),false)){check(false,"export worker starts");return;}
    engine::ExportJobSnapshot result;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    do{result=job.poll();if(!result.active())break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(std::chrono::steady_clock::now()<deadline);
    check(result.state==engine::ExportState::Failed,"independent worker reports source failure");
    check(result.message.find(L"未生成输出文件")!=std::wstring::npos,"UI preserves preflight failure without claiming partial exists");
    check(!std::filesystem::exists(output)&&!std::filesystem::exists(output+L".partial"),"preflight failure creates neither final nor partial");
}
}

int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring_view(argv[1])==L"--export-worker")return engine::runExportWorker(reinterpret_cast<HANDLE>(std::stoull(argv[2])));
    if(argc<2)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);const std::wstring mode=argv[1];
    if(mode==L"graph")graphTest();
    else if(argc==4&&(mode==L"source-good"||mode==L"source-bad"))sourceTest(argv[2],unsigned(std::stoul(argv[3])),mode==L"source-bad");
    else if(argc==3&&(mode==L"play-eof"||mode==L"play-overload-eof"))playbackTest(argv[2],mode==L"play-overload-eof");
    else if(argc==4&&mode==L"export-worker-failure")exportWorkerTest(argv[2],argv[3]);
    else if(argc==4&&mode.starts_with(L"export-"))exportFaultTest(argv[2],argv[3],mode);
    else return 2;
    CoUninitialize();return failures?1:0;
}
