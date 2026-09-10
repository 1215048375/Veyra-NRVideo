#pragma once
#include "veyra/pipeline/ColorMetadata.h"
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <cstring>
#include <limits>
#include <cmath>

namespace veyra::source {
struct CaptureMediaLayout {
    unsigned width=0,height=0,stride=0,rowBytes=0;
    size_t sampleBytes=0;
    bool bottomUp=false;
    AVPixelFormat format=AV_PIX_FMT_NONE;
    REFERENCE_TIME duration=0;
    pipeline::ColorDescription color;
};
// Parse negotiated memory layout, not the UI's requested dimensions. YUV is
// top-down for either sign of biHeight; only RGB DIBs use bottom-up storage.
inline bool captureMediaLayout(const AM_MEDIA_TYPE& type,CaptureMediaLayout& out){
    out={};if(type.majortype!=MEDIATYPE_Video||!type.pbFormat)return false;
    BITMAPINFOHEADER bm{};DWORD flags=0;
    if(type.formattype==FORMAT_VideoInfo&&type.cbFormat>=sizeof(VIDEOINFOHEADER)){
        const auto& v=*reinterpret_cast<const VIDEOINFOHEADER*>(type.pbFormat);bm=v.bmiHeader;out.duration=v.AvgTimePerFrame;
    }else if(type.formattype==FORMAT_VideoInfo2&&type.cbFormat>=sizeof(VIDEOINFOHEADER2)){
        const auto& v=*reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat);bm=v.bmiHeader;out.duration=v.AvgTimePerFrame;flags=v.dwControlFlags;
    }else return false;
    const auto height=std::abs(int64_t(bm.biHeight));
    if(bm.biWidth<=0||bm.biWidth>3840||height<1||height>2160)return false;
    out.width=unsigned(bm.biWidth);out.height=unsigned(height);
    if(type.subtype==MEDIASUBTYPE_YUY2){if(out.width%2)return false;out.format=AV_PIX_FMT_YUYV422;out.rowBytes=out.width*2;}
    else if(type.subtype==MEDIASUBTYPE_NV12){if(out.width%2||out.height%2)return false;out.format=AV_PIX_FMT_NV12;out.rowBytes=out.width;}
    else if(type.subtype==MEDIASUBTYPE_RGB32){out.format=AV_PIX_FMT_BGR0;out.rowBytes=out.width*4;out.bottomUp=bm.biHeight>0;}
    else return false;
    const unsigned rows=out.format==AV_PIX_FMT_NV12?out.height*3/2:out.height;
    out.stride=out.rowBytes;
    if(bm.biSizeImage){
        // Fixed uncompressed allocation may include per-row padding. Reject
        // ambiguous/incomplete layouts instead of reading subsequent rows wrong.
        if(bm.biSizeImage%rows||bm.biSizeImage/rows<out.rowBytes)return false;
        out.stride=bm.biSizeImage/rows;
    }
    out.sampleBytes=size_t(out.stride)*rows;
    if(out.sampleBytes>size_t(std::numeric_limits<LONG>::max()))return false;
    AVFrame frame{};frame.format=out.format;frame.width=int(out.width);frame.height=int(out.height);
    out.color=pipeline::resolveFrameColor(frame);
    // SDR capture is already display-referred R'G'B' after the YUV matrix.
    // Preserve those code values through the sRGB working round trip, like
    // the old RGB ingress, instead of applying a camera OETF a second time.
    out.color.transfer=pipeline::TransferFunction::SRGB;out.color.transferAssumed=true;
    if(flags&AMCONTROL_COLORINFO_PRESENT){
        DXVA2_ExtendedFormat ext{};ext.value=flags;
        if(ext.NominalRange==DXVA2_NominalRange_0_255){out.color.range=pipeline::ColorRange::Full;out.color.rangeAssumed=false;}
        else if(ext.NominalRange==DXVA2_NominalRange_16_235){out.color.range=pipeline::ColorRange::Limited;out.color.rangeAssumed=false;}
        else if(ext.NominalRange!=DXVA2_NominalRange_Unknown)return false;
        if(ext.VideoTransferMatrix==DXVA2_VideoTransferMatrix_BT601){out.color.matrix=pipeline::YuvMatrix::BT601;out.color.matrixAssumed=false;}
        else if(ext.VideoTransferMatrix==DXVA2_VideoTransferMatrix_BT709){out.color.matrix=pipeline::YuvMatrix::BT709;out.color.matrixAssumed=false;}
        else if(ext.VideoTransferMatrix!=DXVA2_VideoTransferMatrix_Unknown)return false;
        if(ext.VideoTransferFunction==DXVA2_VideoTransFunc_10){out.color.transfer=pipeline::TransferFunction::Linear;out.color.transferAssumed=false;}
        else if(ext.VideoTransferFunction==DXVA2_VideoTransFunc_sRGB){out.color.transferAssumed=false;}
        else if(ext.VideoTransferFunction!=DXVA2_VideoTransFunc_Unknown&&ext.VideoTransferFunction!=DXVA2_VideoTransFunc_709&&ext.VideoTransferFunction!=DXVA2_VideoTransFunc_22)return false;
    }
    return true;
}
inline bool copyCaptureSample(const CaptureMediaLayout& layout,const uint8_t* src,size_t bytes,AVFrame& dst){
    if(!src||bytes<layout.sampleBytes||dst.format!=layout.format||dst.width!=int(layout.width)||dst.height!=int(layout.height)||!dst.data[0]||dst.linesize[0]<int(layout.rowBytes))return false;
    if(layout.format==AV_PIX_FMT_NV12&&(!dst.data[1]||dst.linesize[1]<int(layout.width)))return false;
    for(unsigned y=0;y<layout.height;++y)std::memcpy(dst.data[0]+ptrdiff_t(y)*dst.linesize[0],src+size_t(layout.bottomUp?layout.height-1-y:y)*layout.stride,layout.rowBytes);
    if(layout.format==AV_PIX_FMT_NV12)for(unsigned y=0;y<layout.height/2;++y)std::memcpy(dst.data[1]+ptrdiff_t(y)*dst.linesize[1],src+size_t(layout.height+y)*layout.stride,layout.width);
    return true;
}
inline bool equivalentCaptureTypes(const AM_MEDIA_TYPE& a,const AM_MEDIA_TYPE& b){
    CaptureMediaLayout x,y;if(!captureMediaLayout(a,x)||!captureMediaLayout(b,y))return false;
    return x.format==y.format&&x.width==y.width&&x.height==y.height&&x.stride==y.stride&&x.bottomUp==y.bottomUp&&x.duration==y.duration&&
        x.color.range==y.color.range&&x.color.matrix==y.color.matrix&&x.color.transfer==y.color.transfer&&
        x.color.rangeAssumed==y.color.rangeAssumed&&x.color.matrixAssumed==y.color.matrixAssumed&&x.color.transferAssumed==y.color.transferAssumed;
}
}
