#pragma once
#include "Theme.h"
#include "veyra/engine/EngineController.h"
#include <format>
namespace veyra::ui {
struct ChromeLayout {
    int w,h,left,top,viewWidth,viewHeight,right,panelWidth,bottom,statusTop;
    bool pro,drawer;
    ChromeLayout(int width,int height,bool professional,bool showDrawer,int inspectorWidth=320,bool mini=false):w(width),h(height),pro(professional),drawer(showDrawer){
        panelWidth=pro?(w>=1180?std::clamp(inspectorWidth,296,420):w>=960?296:showDrawer?296:0):0;
        left=pro?(w>=960?84:68):68;top=pro?68:0;
        right=w-panelWidth-20;viewWidth=pro?((w>=960||showDrawer)?right-left-14:w-left-20):w-left;
        viewHeight=pro?std::max(160,h-354):h-(mini?28:136);
        bottom=top+viewHeight+(pro?14:0);
        statusTop=std::min(h-186,std::max(top+336,h-330));
    }
};
inline void chromeText(HDC dc,HWND window,std::wstring value,int x,int y,int w,int h,int size,COLORREF c,int weight=FW_NORMAL,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER){auto font=makeFont(window,size,weight);auto old=SelectObject(dc,font);SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);RECT r{dip(window,x),dip(window,y),dip(window,x+w),dip(window,y+h)};glassText(dc,value.c_str(),-1,&r,flags|DT_END_ELLIPSIS);SelectObject(dc,old);DeleteObject(font);}
inline void paintChrome(HWND window,HDC dc,const ChromeLayout& l,const engine::PlayerSnapshot& s,bool full){
    RECT client{};GetClientRect(window,&client);if(!copyGlass(dc,client,window))FillRect(dc,&client,bgBrush());if(full)return;
    using namespace Gdiplus;AlphaGraphics drawing(dc);auto& g=drawing.get();g.SetSmoothingMode(SmoothingModeAntiAlias);
    if(l.pro){
        chromeText(dc,window,L"专业工作台",l.left,18,l.w<960?112:180,24,13,secondary);
        const auto& resolution=s.metrics.resolution;
        auto extent=[](uint32_t w,uint32_t h){return w&&h?std::format(L"{} × {}",w,h):std::wstring(L"—");};
        const int column=std::min(154,(l.viewWidth-40)/3),size=l.viewWidth<600?13:17;
        const std::wstring dimensions[]={extent(resolution.source.width,resolution.source.height),extent(resolution.base.width,resolution.base.height),!s.applied.nr?L"关闭":extent(resolution.nr.width,resolution.nr.height)};
        const wchar_t* headings[]={L"SOURCE",L"OUTPUT",L"NR PROCESS"};
        for(int i=0;i<3;++i){chromeText(dc,window,headings[i],l.left+20+i*column,l.bottom+142,column-4,20,10,secondary);chromeText(dc,window,dimensions[i],l.left+20+i*column,l.bottom+166,column-4,30,size,textColor);}
    }
}
}
