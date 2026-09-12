#pragma once
#include "Theme.h"
#include "../resource.h"

namespace veyra::ui {
inline LRESULT CALLBACK brandIconProc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT){
        PaintBuffer paint(h,reinterpret_cast<HDC>(wp));fillSurface(paint.dc,paint.rect,h);
        const int size=std::min(paint.rect.right-paint.rect.left,paint.rect.bottom-paint.rect.top);
        auto icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_VEYRA),IMAGE_ICON,size,size,LR_SHARED));
        if(icon)DrawIconEx(paint.dc,(paint.rect.right-size)/2,(paint.rect.bottom-size)/2,icon,size,size,0,nullptr,DI_NORMAL);
        return 0;
    }
    return DefSubclassProc(h,message,wp,lp);
}
}
