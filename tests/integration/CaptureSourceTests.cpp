// Optional real-device integration test. Saves no media; validates the product
// source's deferred Run, bounded mailbox, and reader-buffer lifetime.
#include "veyra/source/CaptureCardSource.h"
#include <windows.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <vector>
#include <cstring>
extern "C" {
#include <libavutil/frame.h>
}
int wmain(int argc,wchar_t** argv){
    if(argc==2&&wcscmp(argv[1],L"--list")==0){
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto devices=veyra::source::CaptureCardSource::devices();
        for(unsigned d=0;d<devices.size();++d){wprintf(L"DEVICE %u %ls\n",d,devices[d].c_str());for(const auto& f:veyra::source::CaptureCardSource::formats(d))wprintf(L"FORMAT %u:%d %ls\n",d,f.index,f.label.c_str());}
        CoUninitialize();return devices.empty()?1:0;
    }
    if(argc!=2){printf("Specify capture:device:format:audio; real hardware required\n");return 2;}
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    veyra::source::CaptureCardSource source;veyra::source::SourceOpenDesc d;d.path=argv[1];
    int failures=0;auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    if(!source.configure(d)){printf("FAIL configure\n");return 3;}
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    check(!source.info().opened&&source.metrics().received==0,"configure does not start capture/audio");
    if(!source.start()){printf("FAIL start\n");return 4;}
    const AVFrame* frame=nullptr;veyra::pipeline::FramePacket packet;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    auto readFrame=[&](){while(std::chrono::steady_clock::now()<deadline){auto r=source.read(packet,&frame);if(r==veyra::source::SourceReadStatus::Frame)return true;if(r==veyra::source::SourceReadStatus::Error)return false;}return false;};
    if(!readFrame()){printf("FAIL first frame\n");return 5;}
    const auto initialPts=packet.pts.toDouble();const auto initialReceived=source.metrics().received;
    const int rowBytes=frame->width*(frame->format==AV_PIX_FMT_YUYV422?2:frame->format==AV_PIX_FMT_NV12?1:4);
    std::vector<unsigned char> row(frame->data[0],frame->data[0]+rowBytes);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto stalled=source.metrics();
    check(stalled.received>=initialReceived+3,"callbacks continue while reader retains frame");
    check(stalled.dropped>=2,"bounded pending frame overwritten while reader stalls");
    check(memcmp(row.data(),frame->data[0],row.size())==0,"returned frame remains owned and unchanged");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    check(readFrame(),"read after consumer stall");
    auto recovered=source.metrics();
    check(packet.pts.toDouble()>initialPts+.15,"read skips stale frames rather than draining FIFO");
    check(veyra::pipeline::hasFrameFlag(packet.flags,veyra::pipeline::FrameFlagBits::Drop),"drop invalidates temporal history");
    check(recovered.readAgeMs<100,"latest frame callback age below 100ms");
    printf("received=%llu dropped=%llu callbackFps=%.3f readAgeMs=%.3f\n",recovered.received,recovered.dropped,recovered.callbackFps,recovered.readAgeMs);
    source.close();CoUninitialize();return failures?1:0;
}
