#pragma once
#include <cstring>
#include <thread>
#include <chrono>
#include <libavutil/buffer.h>

// Diagnostic-only readback and queue gate: keep two different input images
// pending at once so descriptor overwrites cannot hide behind a CPU wait.
inline bool hardwareImportFixtures(veyra::gfx::D3D12DeviceContext& ctx,
                                  veyra::gfx::CommandSlotRing& ring) {
 using namespace veyra;
 bool passed=true;
 for(unsigned slices:{1u,3u}) {
  pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
  gd.sourceWidth=gd.workWidth=64;gd.sourceHeight=gd.workHeight=32;
  gd.enableNr=gd.enableSr=gd.enableFg=false;
  if(!graph.initialize(gd)||!graph.createViews())return false;
  gfx::ComPtr<ID3D12Resource> textures[2],uploads[2];
  AVFrame* frames[2]{};
  for(unsigned i=0;i<2;++i) {
   D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
   d.Width=64;d.Height=32;d.DepthOrArraySize=UINT16(slices);d.MipLevels=1;
   d.Format=DXGI_FORMAT_NV12;d.SampleDesc.Count=1;
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
   if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&textures[i]))))return false;
   const UINT slice=slices-1;
   D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];UINT rows[2];UINT64 rowBytes[2],size[2];
   ctx.device()->GetCopyableFootprints(&d,slice,1,0,&fp[0],&rows[0],&rowBytes[0],&size[0]);
   ctx.device()->GetCopyableFootprints(&d,slice+slices,1,(size[0]+511)&~UINT64(511),&fp[1],&rows[1],&rowBytes[1],&size[1]);
   D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=fp[1].Offset+fp[1].Footprint.RowPitch*rows[1];b.Height=1;b.DepthOrArraySize=1;b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   hp.Type=D3D12_HEAP_TYPE_UPLOAD;
   if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploads[i]))))return false;
   void* mapped=nullptr;D3D12_RANGE empty{};if(FAILED(uploads[i]->Map(0,&empty,&mapped)))return false;
   std::memset(mapped,0,size_t(b.Width));
   for(unsigned p=0;p<2;++p)for(unsigned y=0;y<rows[p];++y)
    std::memset(static_cast<uint8_t*>(mapped)+fp[p].Offset+y*fp[p].Footprint.RowPitch,p?128:(i?235:16),size_t(rowBytes[p]));
   uploads[i]->Unmap(0,nullptr);
   Status st;uint32_t slot;auto* list=ring.acquireNext(slot,st);if(!list)return false;
   for(unsigned p=0;p<2;++p){D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=textures[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.SubresourceIndex=slice+p*slices;src.pResource=uploads[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp[p];list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
   pipeline::StateTracker states;states.set(textures[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST);states.transition(list,textures[i].Get(),D3D12_RESOURCE_STATE_COMMON);
   if(!ring.submitAndSignal(slot))return false;
   frames[i]=av_frame_alloc();frames[i]->format=AV_PIX_FMT_D3D12;frames[i]->width=64;frames[i]->height=32;
   frames[i]->color_range=AVCOL_RANGE_MPEG;frames[i]->colorspace=AVCOL_SPC_BT709;frames[i]->color_trc=AVCOL_TRC_BT709;
   frames[i]->buf[0]=av_buffer_allocz(sizeof(AVD3D12VAFrame));if(!frames[i]->buf[0])return false;
   frames[i]->data[0]=frames[i]->buf[0]->data;
   auto* native=reinterpret_cast<AVD3D12VAFrame*>(frames[i]->data[0]);native->texture=textures[i].Get();native->subresource_index=slice;
  }
  ring.drainQueue();
  gfx::ComPtr<ID3D12Fence> gate;if(FAILED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))))return false;
  if(FAILED(ctx.directQueue()->Wait(gate.Get(),1)))return false;
  // Watchdog releases the gate even on failure; no unbounded test hang.
  std::jthread watchdog([gate]{std::this_thread::sleep_for(std::chrono::seconds(2));gate->Signal(1);});
  pipeline::EnhanceGraph::FrameOutputs out[2];bool ok=true;
  for(unsigned i=0;i<2;++i)ok=graph.process(frames[i],i*33.333,i==0,out[i],i+1)&&ok;
  gate->Signal(1);
  for(unsigned i=0;i<2&&ok;++i){sink::RgbaImage image;if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out[i].videoSlot),image))return false;
   unsigned bad=0;for(size_t p=0;p<image.pixels.size();++p)if(p%4!=3&&std::abs(int(image.pixels[p])-(i?255:0))>2)++bad;
   std::cout<<"QUEUED_IMPORT array="<<slices<<" frame="<<i<<" wrongChannels="<<bad<<std::endl;passed=passed&&bad==0;
  }
  passed=passed&&ok;ring.drainQueue();out[0]={};out[1]={};graph.shutdown();for(auto*& f:frames)av_frame_free(&f);
 }
 return passed;
}
