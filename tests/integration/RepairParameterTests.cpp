#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <iostream>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
int main(){using namespace veyra;CoInitializeEx(nullptr,COINIT_MULTITHREADED);gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);Status st;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;bool ok=ctx.initialize(dd,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=1920;gd.sourceHeight=gd.workHeight=1080;gd.enableNr=true;gd.enableFg=false;gd.enableSr=false;gd.enableNvofStandalone=true;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();ok=ok&&graph.initialize(gd)&&graph.createViews();
AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=1920;f->height=1080;f->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(f,32)>=0;
for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){auto* p=f->data[0]+y*f->linesize[0]+x*4;float noise=float((x*73+y*91)%31)-15;bool face=(x-960)*(x-960)/2+(y-540)*(y-540)<200*200;float base=95+45*std::sin(x*.02f)*std::cos(y*.026f)+noise;p[0]=uint8_t(std::clamp(base+(face?55:0),0.f,255.f));p[1]=uint8_t(std::clamp(base+(face?15:0),0.f,255.f));p[2]=uint8_t(std::clamp(base-(face?10:0),0.f,255.f));p[3]=255;}
sink::RgbaImage baseline;unsigned ordinal=0;const char* names[]={"baseline","intensity","localTone","localStructure","skin-experimental","style-experimental","autoMask-experimental","UI-experimental","residual-total-zero"};
for(unsigned mode=0;ok&&mode<9;++mode){engine::EnhancementSettings s;s.revision=mode+1;s.multiplier=1;if(mode==1)s.model.intensity=0;if(mode==2)s.model.tone=0;if(mode==3)s.model.structure=0;if(mode==4)s.model.skin=1.5f;if(mode==5)s.model.style=1;if(mode==6)s.model.autoMask=1;if(mode==7)s.model.uiCorrection=1;if(mode==8)s.residual.total=0;ok=graph.applySettings(s);sink::RgbaImage result;
for(int j=0;ok&&j<4;++j){pipeline::EnhanceGraph::FrameOutputs out;ok=graph.process(f,(ordinal++)*20.0,j==0,out);if(j==3&&ok)ok=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);}
if(!ok)break;if(mode==0)baseline=result;double mae=0;uint64_t changed=0;for(size_t k=0;k<result.pixels.size();k+=4)for(int c=0;c<3;++c){int d=std::abs(int(result.pixels[k+c])-baseline.pixels[k+c]);mae+=d;changed+=d!=0;}mae/=result.width*result.height*3.0;std::cout<<"PARAM name="<<names[mode]<<" appliedRevision="<<s.revision<<" changedRGB="<<changed<<" meanAbs8bit="<<mae<<" effect="<<(changed?"observed-on-synthetic":"not-proven-on-synthetic")<<'\n';}
ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;}
