#pragma once
#include "SubtitleOverlay.h"
#include "Theme.h"
#include "veyra/engine/PreviewView.h"
namespace veyra::ui {
inline RECT comparisonLineRect(int w,int h,int sourceW,int sourceH,engine::PreviewView view,float split,int thickness){
    if(w<=0||h<=0||sourceW<=0||sourceH<=0)return {};
    const float scale=std::min(float(w)/sourceW,float(h)/sourceH)*view.zoom;
    const int x=int(std::lround(w*.5f+(std::clamp(split,0.0f,1.0f)-view.centerX)*sourceW*scale));
    const int top=std::clamp(int(std::floor(h*.5f-view.centerY*sourceH*scale)),0,h),bottom=std::clamp(int(std::ceil(h*.5f+(1-view.centerY)*sourceH*scale)),0,h);
    if(x<0||x>=w)return {};
    return {std::max(0,x-thickness/2),top,std::min(w,x+(thickness+1)/2),bottom};
}
inline void paintComparisonLine(HWND overlay,HWND video,RECT box,int color){
    const int width=box.right-box.left,height=box.bottom-box.top;
    if(width<1||height<1){ShowWindow(overlay,SW_HIDE);return;}
    RECT previous{};GetWindowRect(overlay,&previous);MapWindowPoints(nullptr,video,reinterpret_cast<POINT*>(&previous),2);
    if(EqualRect(&box,&previous)&&GetPropW(overlay,L"comparison.color")==HANDLE(INT_PTR(color+1))){ShowWindow(overlay,SW_SHOWNOACTIVATE);return;}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return;}
    const uint32_t colors[]={0xff00e5ff,0xffff20bc,0xffffe600};auto pixels=static_cast<uint32_t*>(bits);const int border=std::max(1,dip(video,1));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[y*width+x]=(x<border||x>=width-border)?0xff101018:colors[std::clamp(color,0,2)];
    auto old=SelectObject(memory,bitmap);SIZE size{width,height};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    SetWindowPos(overlay,HWND_TOP,box.left,box.top,width,height,SWP_NOACTIVATE);
    if(UpdateLayeredWindow(overlay,screen,nullptr,&size,memory,&origin,0,&blend,ULW_ALPHA)){SetPropW(overlay,L"comparison.color",HANDLE(INT_PTR(color+1)));ShowWindow(overlay,SW_SHOWNOACTIVATE);}
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
}
