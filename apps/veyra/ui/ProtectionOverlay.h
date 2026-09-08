#pragma once
#include "SubtitleOverlay.h"
#include "Theme.h"
namespace veyra::ui {
// Selection-only overlay. Never reads or modifies the video swapchain.
inline void protectionOutline(HWND h,HWND video,RECT box){
    RECT r{};GetClientRect(video,&r);if(r.right<1||r.bottom<1)return;
    SetWindowPos(h,HWND_TOP,0,0,r.right,r.bottom,SWP_NOACTIVATE);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return;}
    auto previous=SelectObject(memory,bitmap);memset(bits,0,size_t(r.right)*r.bottom*4);
    {using namespace Gdiplus;Bitmap canvas(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));Graphics g(&canvas);Pen pen(Color(255,240,132,49),float(dip(h,2)));g.DrawRectangle(&pen,Rect(box.left,box.top,std::max(1L,box.right-box.left),std::max(1L,box.bottom-box.top)));}
    SIZE size{r.right,r.bottom};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(UpdateLayeredWindow(h,screen,nullptr,&size,memory,&origin,0,&blend,ULW_ALPHA))ShowWindow(h,SW_SHOWNOACTIVATE);
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
}
