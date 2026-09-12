#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/source/RemotePlaySource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"
#include <chrono>
#include <thread>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
// Local, opt-in source diagnostic. Never rewrites pairing or sends console input.
int wmain(int argc,wchar_t** argv){
 if(argc!=6||std::wstring_view(argv[1])!=L"--last-paired-ps5")return 2;
 const int codec=_wtoi(argv[3]),fps=_wtoi(argv[4]),kbps=_wtoi(argv[5]);
 if((codec!=0&&codec!=1)||(fps!=30&&fps!=60)||kbps<5000||kbps>100000)return 2;
 const auto dir=remoteplay::profileDirectory();wchar_t name[80]{};
 GetPrivateProfileStringW(L"RemotePlay",L"LastProfile",L"",name,80,(dir/L"settings.ini").c_str());
 std::wstring profile=name;if(profile.empty()||profile.find_first_of(L"/\\:")!=profile.npos)return 3;
 auto saved=remoteplay::loadProfile(dir/profile);if(!saved)return 4;
 std::filesystem::create_directories(argv[2]);Logger::instance().openFile((std::filesystem::path(argv[2])/L"source.log").wstring());Logger::instance().setConsoleEnabled(false);
 _putenv_s("VEYRA_TEST_PS5_QUALITY_TRACE","1");
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;
 if(!ctx.initialize({},st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 5;
 source::RemotePlaySource source;source::RemotePlayConnectDesc desc;desc.request=std::move(*saved);
 desc.request.video={1920,1080,uint32_t(fps),uint32_t(kbps),codec?remoteplay::Codec::H265:remoteplay::Codec::H264};desc.decodeMode=source::RemotePlayConnectDesc::DecodeMode::Hardware;
 ctx.device()->AddRef();desc.decodeDevice={ctx.device(),[](ID3D12Device* d){d->Release();}};
 if(!source.connect(desc))return 6;
 const auto start=std::chrono::steady_clock::now();int last=-1;uint64_t frames=0,baseBytes=0;bool captured=false,failed=false;
 auto measuredAt=start;AVFrame* picture=nullptr;pipeline::ColorDescription color;
 while(std::chrono::steady_clock::now()-start<std::chrono::seconds(35)){
  pipeline::FramePacket p;const AVFrame* frame=nullptr;const auto result=source.read(p,&frame);
  if(result==source::SourceReadStatus::Error){std::cout<<"SOURCE_FAILED code="<<source.sessionSnapshot().errorCode<<std::endl;failed=true;break;}
  const auto now=std::chrono::steady_clock::now();const int seconds=int(std::chrono::duration_cast<std::chrono::seconds>(now-start).count());
  if(result==source::SourceReadStatus::Frame&&frame){++frames;if(seconds>=25&&!picture){picture=av_frame_clone(frame);color=p.colorInfo;}}
  float pcm[960];source.pullAudio(pcm,480,nullptr);
  if(seconds!=last&&seconds%5==0){last=seconds;auto s=source.sessionSnapshot();auto n=source.nativeSnapshot();
   std::cout<<"QUALITY t="<<seconds<<" codec="<<codec<<" fps="<<fps<<" requestKbps="<<kbps<<" decoded="<<s.decodedFrames<<" videoMbps="<<s.videoMbps<<" serverTargetRaw="<<n.serverTargetBitrate<<" reports="<<n.qualityReports<<" loss="<<s.framesLost<<" rejected="<<n.callbackRejected<<" width="<<source.info().width<<" height="<<source.info().height<<std::endl;
   if(seconds==10){baseBytes=s.receivedBytes;measuredAt=now;}
  }
  if(result!=source::SourceReadStatus::Frame)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 auto s=source.sessionSnapshot();const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-measuredAt).count();
 std::cout<<"SOURCE_MEAN Mbps="<<double(s.receivedBytes-baseBytes)*8/elapsed/1e6<<" frames="<<frames<<" errors="<<s.errorCode<<std::endl;
 // Single diagnostic readback after measurement; no enhancement or resizing.
 if(picture){pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=picture->width;gd.sourceHeight=gd.workHeight=picture->height;gd.enableNr=gd.enableSr=gd.enableFg=false;
  if(graph.initialize(gd)&&graph.createViews()){pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;
   captured=graph.process(picture,0,true,out,1,&color)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image)&&sink::saveImage((std::filesystem::path(argv[2])/L"source.png").wstring(),image);
   out={};ring.drainQueue();}graph.shutdown();av_frame_free(&picture);
 }
 source.close();ring.shutdown();ctx.shutdown();CoUninitialize();
 std::cout<<"SOURCE_DIAGNOSTIC frames="<<frames<<" captured="<<captured<<" no_enhancement=1"<<std::endl;return !failed&&frames>100&&captured?0:1;
}
