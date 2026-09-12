#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/source/RemotePlaySource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <chrono>
#include <thread>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}
using namespace veyra;
namespace veyra::source {
struct RemotePlaySourceTestAccess {
 static std::vector<uint8_t> takeFirstAu(RemotePlaySource& source){
  auto sample=source.inbox_->tryVideo(remoteplay::monotonic100ns());if(!sample)return {};
  if(!sample->configBefore)return {};
  std::vector<uint8_t> bytes;auto config=sample->configBefore->payload.bytes(),au=sample->sample.payload.bytes();
  bytes.insert(bytes.end(),config.begin(),config.end());bytes.insert(bytes.end(),au.begin(),au.end());return bytes;
 }
 static AVFrame* decode(RemotePlaySource& source,const std::vector<uint8_t>& bytes,bool software){
  if(software)source.decodeMode_=RemotePlayConnectDesc::DecodeMode::Software;
  if(!source.openDecoder(remoteplay::Codec::H264,1920,1080))return nullptr;
  pipeline::FramePacket packet;
  if(!source.submitPacket(bytes,1,packet)||source.ready_.empty())return nullptr;
  return av_frame_clone(source.ready_.front().frame.get());
 }
};
}
// Explicit opt-in live diagnostic. Reuses the last local DPAPI profile only;
// never prints keys, changes pairing, or sends a power command to the console.
int wmain(int argc,wchar_t** argv){
 if(argc!=3||std::wstring_view(argv[1])!=L"--last-paired-ps5")return 2;
 const auto dir=remoteplay::profileDirectory();wchar_t name[80]{};
 GetPrivateProfileStringW(L"RemotePlay",L"LastProfile",L"",name,80,(dir/L"settings.ini").c_str());
 std::wstring profile=name;if(profile.empty()||profile.find_first_of(L"/\\:")!=profile.npos)return 3;
 auto saved=remoteplay::loadProfile(dir/profile);if(!saved)return 4;
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;
 if(!ctx.initialize({},st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 5;
 source::RemotePlaySource source;source::RemotePlayConnectDesc desc;desc.request=std::move(*saved);
 desc.request.video={1920,1080,60,80000,remoteplay::Codec::H264};desc.decodeMode=source::RemotePlayConnectDesc::DecodeMode::Hardware;
 ctx.device()->AddRef();desc.decodeDevice={ctx.device(),[](ID3D12Device* d){d->Release();}};
 if(!source.connect(desc))return 6;
 AVFrame* frame=nullptr;pipeline::FramePacket packet;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
 std::vector<uint8_t> encoded;
 while(std::chrono::steady_clock::now()<deadline){encoded=source::RemotePlaySourceTestAccess::takeFirstAu(source);
  if(!encoded.empty()){frame=source::RemotePlaySourceTestAccess::decode(source,encoded,false);break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 if(!frame){source.close();return 7;}
 // Decode the identical compressed AU in software, not a transfer of an
 // already corrupted GPU image: the oracle must include decoder correctness.
 source::RemotePlaySource cpuSource;
 AVFrame* cpu=source::RemotePlaySourceTestAccess::decode(cpuSource,encoded,true);if(!cpu)return 8;
 sink::RgbaImage images[2];unsigned i=0;
 for(auto* input:{frame,cpu}){pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
  gd.sourceWidth=gd.workWidth=input->width;gd.sourceHeight=gd.workHeight=input->height;gd.enableNr=gd.enableSr=gd.enableFg=false;
  if(!graph.initialize(gd)||!graph.createViews())return 9;pipeline::EnhanceGraph::FrameOutputs out;
  if(!graph.process(input,0,true,out,1)||!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),images[i]))return 10;
  if(!sink::saveImage(std::wstring(argv[2])+(i?L"-reference.png":L"-hardware.png"),images[i]))return 11;
  ++i;out={};ring.drainQueue();graph.shutdown();
 }
 double error=0;for(size_t p=0;p<images[0].pixels.size();++p)if(p%4!=3)error+=std::abs(int(images[0].pixels[p])-int(images[1].pixels[p]));
 error/=images[0].width*images[0].height*3.0;
 std::cout<<"PS5_HARDWARE_IMPORT width="<<frame->width<<" height="<<frame->height<<" meanError="<<error<<std::endl;
 av_frame_free(&cpu);av_frame_free(&frame);source.close();ring.shutdown();ctx.shutdown();CoUninitialize();return error<2?0:12;
}
