#pragma once
#include <windows.h>
namespace veyra::ui {
inline void changeDpi(HWND window,WPARAM wp,LPARAM lp,unsigned& dpi,HFONT& font){
    const unsigned next=HIWORD(wp);struct Scale{unsigned oldDpi,newDpi;HWND parent;};Scale scale{dpi,next,window};
    EnumChildWindows(window,[](HWND child,LPARAM value)->BOOL{auto& s=*reinterpret_cast<Scale*>(value);RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,s.parent,reinterpret_cast<POINT*>(&r),2);MoveWindow(child,MulDiv(r.left,s.newDpi,s.oldDpi),MulDiv(r.top,s.newDpi,s.oldDpi),MulDiv(r.right-r.left,s.newDpi,s.oldDpi),MulDiv(r.bottom-r.top,s.newDpi,s.oldDpi),TRUE);return TRUE;},reinterpret_cast<LPARAM>(&scale));
    auto oldFont=font;font=CreateFontW(-MulDiv(14,next,96),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");EnumChildWindows(window,[](HWND child,LPARAM value)->BOOL{SendMessageW(child,WM_SETFONT,WPARAM(value),TRUE);return TRUE;},LPARAM(font));DeleteObject(oldFont);dpi=next;auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
}
}
