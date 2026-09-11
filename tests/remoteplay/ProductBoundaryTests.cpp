#include "veyra/source/RemotePlaySessionSource.h"
#include "veyra/remoteplay/ControllerInput.h"
#include "veyra/remoteplay/Discovery.h"
#include <string_view>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#include <stdexcept>
namespace veyra::source {
struct RemotePlaySessionSourceTestAccess {
static void mailbox(){
    RemotePlaySessionSource s;s.started_=true;
    AVFrame* raw=av_frame_alloc();raw->width=16;raw->height=16;raw->format=AV_PIX_FMT_YUV420P;
    if(av_frame_get_buffer(raw,32)<0)throw std::bad_alloc();
    pipeline::FramePacket packet;SourceInfo info;info.width=16;info.height=16;
    packet.sequence=1;packet.flags=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Open);s.publishDecoded(raw,packet,info);
    packet.sequence=2;packet.flags=0;s.publishDecoded(raw,packet,info);av_frame_free(&raw);
    const AVFrame* frame=nullptr;pipeline::FramePacket out;
    if(s.read(out,&frame)!=SourceReadStatus::Frame||!frame||out.sequence!=2||s.skipped()!=1||!pipeline::breaksHistory(out.flags))throw std::runtime_error("latest decoded mailbox");
    if(s.read(out,&frame)!=SourceReadStatus::Waiting)throw std::runtime_error("mailbox replay");
}
};
}

int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--discover"){
        const auto report=veyra::remoteplay::discoverLocalPs5({});
        for(const auto& line:report.diagnostics)std::cout<<line<<'\n';
        for(const auto& host:report.hosts)std::cout<<"PS5_HOST="<<host.host<<" standby="<<host.standby<<'\n';
        std::cout<<"DISCOVERY_RESULT hosts="<<report.hosts.size()<<" error="<<report.error<<'\n';
        return report.error&&report.hosts.empty()?5:0;
    }
    std::stop_source cancelled;cancelled.request_stop();
    if(!veyra::remoteplay::discoverLocalPs5(cancelled.get_token()).hosts.empty())return 6;
    veyra::source::RemotePlaySessionSourceTestAccess::mailbox();
    veyra::source::RemotePlaySessionSource source;
    for(int i=0;i<3;++i){
        veyra::source::RemotePlayConnectDesc request;request.request.host="invalid://host";
        if(source.connect(std::move(request)))return 1;
        source.close();if(source.info().opened)return 2;
    }
    veyra::remoteplay::ControllerInput input;
    if(!input.start()){std::cerr<<"SDL_GAMEPAD_INITIALIZATION_FAILED\n";return 3;}
    for(int i=0;i<3;++i)if(input.poll(false)!=veyra::remoteplay::ControllerState{})return 4;
    input.stop();input.stop();
    std::cout<<"REMOTEPLAY_BOUNDARY_PASS invalid_connect_reopen=3 decoded_latest_mailbox=1 SDL_initialized=1 unfocused_neutral=1 PS5_NOT_TESTED=1\n";
    return 0;
}
