#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include <iostream>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext_d3d12va.h>
}
using namespace veyra;
#include "HardwareImportFixtures.h"
// Explicit diagnostic readback. Compare the entire GPU-converted image, not
// merely successful decode/evaluate return codes or one non-black pixel.
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;
 if(!ctx.initialize({},st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 3;
 std::vector<sink::RgbaImage> reference;double worst=0;
 for(bool hardware:{false,true}){
  source::MediaFileSource source;source::SourceOpenDesc sd;sd.path=argv[1];sd.preferHardwareDecode=hardware;sd.d3d12Device=ctx.device();sd.d3d12Queue=ctx.directQueue();
  if(!source.open(sd))return 4;
  pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
  gd.sourceWidth=gd.workWidth=source.info().width;gd.sourceHeight=gd.workHeight=source.info().height;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
  if(!graph.initialize(gd)||!graph.createViews())return 5;
  for(unsigned i=0;i<4;++i){
   pipeline::FramePacket packet;const AVFrame* frame=nullptr;source::SourceReadStatus result;
   do{result=source.read(packet,&frame);}while(result==source::SourceReadStatus::Waiting);
   if(result!=source::SourceReadStatus::Frame||(frame->format==AV_PIX_FMT_D3D12)!=hardware)return 6;
   if(hardware){auto* native=reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);auto d=native->texture->GetDesc();std::cout<<"GPU_IMPORT frame="<<i<<" visible="<<frame->width<<"x"<<frame->height<<" resource="<<d.Width<<"x"<<d.Height<<" array="<<d.DepthOrArraySize<<" slice="<<native->subresource_index<<" format="<<d.Format<<std::endl;}
   pipeline::EnhanceGraph::FrameOutputs out;
   if(!graph.process(frame,i*1000.0/30,i==0,out,i+1,&packet.colorInfo))return 7;
   sink::RgbaImage image;if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image))return 8;
   if(i==0&&!sink::saveImage(std::wstring(argv[2])+(hardware?L"-hardware.png":L"-software.png"),image))return 9;
   if(!hardware)reference.push_back(std::move(image));
   else{
    auto& ref=reference[i];if(ref.width!=image.width||ref.height!=image.height)return 10;
    double sum=0;for(size_t p=0;p<ref.pixels.size();++p)if(p%4!=3)sum+=std::abs(int(ref.pixels[p])-int(image.pixels[p]));
    double error=sum/(image.width*image.height*3.0);worst=std::max(worst,error);
    std::cout<<"PIXEL_COMPARE frame="<<i<<" meanAbsoluteError="<<error<<std::endl;
   }
   out={};
  }
  ring.drainQueue();graph.shutdown();source.close();
 }
 const bool fixtures=hardwareImportFixtures(ctx,ring);
 ring.shutdown();ctx.shutdown();CoUninitialize();
 std::cout<<"FULL_IMAGE_PASS="<<(worst<2.0)<<" worst="<<worst<<std::endl;
 return worst<2&&fixtures?0:11;
}
