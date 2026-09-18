#pragma once
#include "veyra/sink/ImageExportSink.h"
#include <algorithm>
#include <cmath>
namespace veyra::engine {
// Both images share normalized source coordinates, including SR-sized results.
inline void sampleImageComparison(const sink::RgbaImage& original,const sink::RgbaImage& enhanced,
    double u,double v,int mode,float split,uint8_t* dst){
    if(u<0||u>=1||v<0||v>=1){dst[0]=dst[1]=dst[2]=0;dst[3]=255;return;}
    const auto& image=(mode==1||(mode==2&&u<split))?original:enhanced;
    const double px=std::clamp(u*image.width-.5,0.0,double(image.width-1)),py=std::clamp(v*image.height-.5,0.0,double(image.height-1));
    const auto ix=uint32_t(px),iy=uint32_t(py),jx=std::min(ix+1,image.width-1),jy=std::min(iy+1,image.height-1);
    const double fx=px-ix,fy=py-iy;
    for(unsigned c=0;c<4;++c){auto at=[&](uint32_t x,uint32_t y){return image.pixels[(size_t(y)*image.width+x)*4+c];};
        dst[c]=uint8_t(std::clamp(std::lround((at(ix,iy)*(1-fx)+at(jx,iy)*fx)*(1-fy)+(at(ix,jy)*(1-fx)+at(jx,jy)*fx)*fy),0L,255L));}
}
}
