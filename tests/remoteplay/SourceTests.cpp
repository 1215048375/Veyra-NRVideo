#include "veyra/source/RemotePlaySource.h"
#include "veyra/pipeline/ColorMetadata.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>
extern "C" {
#include <libavcodec/avcodec.h>
}
extern "C" {
#include <libavformat/avformat.h>
}
#include <fstream>
#include <iostream>
#include <iterator>
using namespace veyra;
using namespace veyra::remoteplay;
using namespace veyra::source;
namespace veyra::source {
struct RemotePlaySourceTestAccess {
static void check(bool ok) { if(!ok) throw std::runtime_error("RemotePlay source regression failed"); }
static std::vector<uint8_t> bytes(const char* path) {
    std::ifstream f(path,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}};
}
static void setup(RemotePlaySource& s) {
    s.origin100ns_=monotonic100ns();
    s.token_=s.inbox_->begin(static_cast<HostTime>(s.origin100ns_));
    s.token_->connected(); s.connected_=true;
    s.clock_.reset(static_cast<HostTime>(s.origin100ns_),60);
    s.info_.width=1280;s.info_.height=720;
    s.info_.color.matrix=pipeline::YuvMatrix::BT601;
    s.info_.color.range=pipeline::ColorRange::Limited;
    s.info_.color.transfer=pipeline::TransferFunction::BT709;
}
static void pcm() {
    RemotePlaySource s;setup(s);
    PcmBlock b;b.generation=s.token_->generation();b.channels=2;b.rate=48000;
    b.arrival100ns=static_cast<HostTime>(s.origin100ns_)+300000;
    for(int i=0;i<480;++i){b.samples.push_back(static_cast<int16_t>(i));b.samples.push_back(static_cast<int16_t>(i));}
    s.token_->audio(std::move(b));float out[960]{};double pts=0;
    auto n1=s.pullAudio(out,100,&pts);auto n2=s.pullAudio(out,380,nullptr);
    std::cout<<"PCM supplied=480 pulled="<<n1+n2<<" first_pts_ms="<<pts<<" expected_relative_ms=30\n";
    check(n1==100 && n2==380 && std::abs(pts-30.0)<0.001);
    for(int i=0;i<380;++i) check(std::abs(out[i*2]-(i+100)/32768.0f)<1e-6f);
    // Network arrival jitter must not change steady sample cadence.
    PcmBlock next;next.generation=s.token_->generation();next.channels=2;next.rate=48000;
    next.firstSample=480;next.arrival100ns=static_cast<HostTime>(s.origin100ns_)+490000;
    next.samples.resize(960,17);check(s.token_->audio(std::move(next)));
    check(s.pullAudio(out,480,&pts)==480 && std::abs(pts-40.0)<0.001);
    // A genuine new segment is returned separately, not concatenated across a gap.
    for(int i=0;i<2;++i){PcmBlock x;x.generation=s.token_->generation();x.channels=2;x.rate=48000;
        x.firstSample=960+static_cast<uint64_t>(i)*480;x.arrival100ns=static_cast<HostTime>(s.origin100ns_)+600000+i*100000;
        x.discontinuity=i==1;x.samples.resize(960,18);check(s.token_->audio(std::move(x)));}
    float large[1920];check(s.pullAudio(large,960,&pts)==480);
    check(s.pullAudio(large,480,&pts)==480 && std::abs(pts-70.0)<0.001);
}
static void splitTest(const char* prefix, Codec codec=Codec::H264) {
    RemotePlaySource s;setup(s);s.request_.video.codec=codec;auto config=bytes((std::string(prefix)+"-config.bin").c_str());
    auto au=bytes((std::string(prefix)+"-au.bin").c_str());
    VideoSample c;c.generation=s.token_->generation();c.width=1280;c.height=720;
    c.codec=codec;c.arrival100ns=monotonic100ns();c.kind=SampleKind::CodecConfig;c.payload=PaddedBytes::copy(config);
    VideoSample f;f.generation=c.generation;f.width=1280;f.height=720;
    f.codec=codec;f.arrival100ns=monotonic100ns();f.wireFrameIndex=65535;f.payload=PaddedBytes::copy(au);
    const auto ca=s.token_->video(std::move(c));const auto fa=s.token_->video(std::move(f));
    pipeline::FramePacket p;const AVFrame* frame=nullptr;auto r=s.read(p,&frame);
    std::cout<<(codec==Codec::H264?"SPLIT_H264":"SPLIT_H265")<<" config_accepted="<<ca<<" au_accepted="<<fa<<" read_status="<<int(r)
        <<" frame="<<bool(frame)<<" session_state="<<int(s.sessionSnapshot().state)<<"\n";
    check(ca && fa && r==SourceReadStatus::Frame && frame && s.sessionSnapshot().state==SessionState::Streaming);
    // Decode across the real 16-bit wrap, retaining a valid increasing PTS.
    const auto firstPts=p.pts.to100ns();
    VideoSample wrapped;wrapped.generation=s.token_->generation();wrapped.width=1280;wrapped.height=720;wrapped.codec=codec;
    wrapped.arrival100ns=monotonic100ns();wrapped.wireFrameIndex=0;wrapped.payload=PaddedBytes::copy(au);
    check(s.token_->video(std::move(wrapped)));check(s.read(p,&frame)==SourceReadStatus::Frame);
    check(!p.pts.isUnknown() && p.pts.to100ns()>firstPts);
    if(codec==Codec::H265)return;
    // Same actual decoder/stream, with SPS/PPS prepended to AU in one packet.
    RemotePlaySource combined;setup(combined);combined.openDecoder(Codec::H264,1920,1080);
    config.insert(config.end(),au.begin(),au.end());pipeline::FramePacket sp;sp.pts={0,60};sp.colorInfo=combined.info_.color;
    bool ok=combined.submitPacket(config,0,sp);
    std::cout<<"COMBINED_H264 ok="<<ok<<" decoded="<<combined.ready_.size();
    if(!combined.ready_.empty()){
        auto& d=combined.ready_.front();std::cout<<" av_matrix="<<int(d.frame->colorspace)
          <<" packet_matrix="<<int(d.packet.colorInfo.matrix)<<" av_range="<<int(d.frame->color_range)
          <<" packet_range="<<int(d.packet.colorInfo.range)
          <<" resolved_matrix="<<int(pipeline::resolveFrameColor(*d.frame,d.packet.colorInfo).matrix)
          <<" info_width="<<combined.info_.width<<" decoded_width="<<d.frame->width;
    }
    std::cout<<"\n";
    pipeline::FramePacket decodedPacket;const AVFrame* decodedView=nullptr;
    auto rr=combined.read(decodedPacket,&decodedView);
    check(rr==SourceReadStatus::Frame && decodedView && combined.info_.width==1280 && combined.sessionSnapshot().state==SessionState::Streaming);
    std::cout<<"REAL_FRAME_STATE read_status="<<int(rr)<<" frame="<<bool(decodedView)
        <<" session_state="<<int(combined.sessionSnapshot().state)<<" streaming_enum="<<int(SessionState::Streaming)<<"\n";
}
static void reorder(const char* path) {
    RemotePlaySource s;setup(s);s.openDecoder(Codec::H264,1280,720);
    AVFormatContext* fmt=nullptr;if(avformat_open_input(&fmt,path,nullptr,nullptr)<0)throw std::runtime_error("open fixture");
    AVPacket* p=av_packet_alloc();uint64_t n=0;int mismatches=0,outputs=0;
    while(av_read_frame(fmt,p)>=0){pipeline::FramePacket sp;sp.pts={static_cast<int64_t>(n),60};sp.colorInfo=s.info_.color;
        if(!s.submitPacket({p->data,static_cast<size_t>(p->size)},n,sp))throw std::runtime_error("submit AU");
        while(!s.ready_.empty()) {auto d=std::move(s.ready_.front());s.ready_.pop_front();++outputs;
            if(d.frame->pts!=d.packet.pts.num){++mismatches;std::cout<<"REORDER actual_packet_id="<<d.frame->pts<<" assigned="<<d.packet.pts.num<<"\n";}}
        ++n;av_packet_unref(p);
    }
    av_packet_free(&p);avformat_close_input(&fmt);
    check(n==12 && outputs==10 && mismatches==0);
    std::cout<<"REORDER input="<<n<<" output_before_eof="<<outputs<<" wrong_pts="<<mismatches<<"\n";
}
};
}
int main(int argc,char**argv) {
    if(argc!=3&&argc!=4)return 2;
    std::cout<<"SOURCE_TEST: real FFmpeg, direct inbox injection; NO PS5/WASAPI/GPU validation\n";
    try{RemotePlaySourceTestAccess::pcm();RemotePlaySourceTestAccess::splitTest(argv[1]);RemotePlaySourceTestAccess::reorder(argv[2]);if(argc==4)RemotePlaySourceTestAccess::splitTest(argv[3],Codec::H265);}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
    std::cout<<"REMOTEPLAY_SOURCE_REGRESSIONS_PASS\n"; return 0;
}
