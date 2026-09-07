#pragma once
#include "Theme.h"
#include "veyra/engine/EngineController.h"
#include <format>
namespace veyra::ui {
struct ChromeLayout {
    int w,h,left,top,viewWidth,viewHeight,right,panelWidth,bottom;
    bool pro,drawer;
    ChromeLayout(int width,int height,bool professional,bool showDrawer,int inspectorWidth=320):w(width),h(height),pro(professional),drawer(showDrawer){
        panelWidth=pro?(w>=1180?std::clamp(inspectorWidth,296,420):w>=960?296:showDrawer?296:0):0;
        left=pro?(w>=960?84:68):(w>=1000?64:24);top=pro?68:(h>=720?104:76);
        right=w-panelWidth-20;viewWidth=pro?((w>=960||showDrawer)?right-left-14:w-left-20):w-left*2;
        viewHeight=pro?std::max(160,h-306):std::max(180,std::min(int(viewWidth*9.0/16),h-top-138));
        bottom=top+viewHeight+14;
    }
};
inline void chromeText(HDC dc,HWND window,std::wstring value,int x,int y,int w,int h,int size,COLORREF c,int weight=FW_NORMAL,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER){auto font=makeFont(window,size,weight);auto old=SelectObject(dc,font);SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);RECT r{dip(window,x),dip(window,y),dip(window,x+w),dip(window,y+h)};DrawTextW(dc,value.c_str(),-1,&r,flags|DT_END_ELLIPSIS);SelectObject(dc,old);DeleteObject(font);}
inline void paintChrome(HWND window,HDC dc,const ChromeLayout& l,const engine::PlayerSnapshot& s,bool full){
    RECT client{};GetClientRect(window,&client);if(full){FillRect(dc,&client,bgBrush());return;}
    auto rect=[&](int x,int y,int w,int h){return RECT{dip(window,x),dip(window,y),dip(window,x+w),dip(window,y+h)};};
    using namespace Gdiplus;Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);
    if(!l.pro){LinearGradientBrush bg(Point(0,0),Point(0,client.bottom),color(cinema),color(RGB(20,31,41)));g.FillRectangle(&bg,0,0,client.right,client.bottom);
        // Restrained ambient shadow below the single cinema surface.
        for(int i=18;i>0;--i){auto r=rect(l.left-i/2,l.top+i/2,l.viewWidth+i,l.viewHeight+88+i/2);GraphicsPath p;rounded(p,{float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top)},float(dip(window,5)));SolidBrush shadow(Color(BYTE(3),0,0,0));g.FillPath(&shadow,&p);}
        roundRect(dc,rect(l.left,l.top,l.viewWidth,l.viewHeight+88),cinemaPanel,dip(window,5));
        chromeText(dc,window,L"V E Y R A",l.left,l.h-46,160,24,12,RGB(118,133,144),FW_MEDIUM);
        chromeText(dc,window,L"本地增强播放器",l.w-238,l.h-46,174,24,12,RGB(118,133,144));
    }else{
        FillRect(dc,&client,bgBrush());roundRect(dc,rect(4,4,64,l.h-8),RGB(16,17,18),dip(window,22));
        roundRect(dc,rect(l.left,l.top,l.viewWidth,l.viewHeight),panel,dip(window,20));
        roundRect(dc,rect(l.left,l.bottom,l.viewWidth,l.h-l.bottom-20),panel,dip(window,20));
        if(l.panelWidth){roundRect(dc,rect(l.right,l.top,l.panelWidth,l.viewHeight),panel,dip(window,20));roundRect(dc,rect(l.right,l.bottom,l.panelWidth,l.h-l.bottom-20),panel,dip(window,20));}
        chromeText(dc,window,L"专业工作台",l.left,18,180,24,13,secondary);
        const auto& resolution=s.metrics.resolution;
        auto extent=[](uint32_t w,uint32_t h){return w&&h?std::format(L"{} × {}",w,h):std::wstring(L"—");};
        const int column=std::min(154,(l.viewWidth-40)/3),size=l.viewWidth<600?13:17;
        const std::wstring dimensions[]={extent(resolution.source.width,resolution.source.height),extent(resolution.base.width,resolution.base.height),!s.applied.nr?L"关闭":extent(resolution.nr.width,resolution.nr.height)};
        const wchar_t* headings[]={L"SOURCE",L"OUTPUT",L"NR PROCESS"};
        for(int i=0;i<3;++i){chromeText(dc,window,headings[i],l.left+20+i*column,l.bottom+94,column-4,20,10,secondary);chromeText(dc,window,dimensions[i],l.left+20+i*column,l.bottom+118,column-4,30,size,textColor);}
        if(l.panelWidth){
            chromeText(dc,window,s.transport==engine::TransportState::Playing?L"实时处理状态":L"最近处理样本",l.right+22,l.bottom+16,l.panelWidth-44,24,13,textColor,FW_MEDIUM);
            chromeText(dc,window,L"NR单阶段 · 16.7 ms 参考刻度",l.right+22,l.bottom+42,l.panelWidth-44,16,10,secondary);
            const auto sample=s.metrics.gpu[size_t(diagnostics::GpuStage::Nr)];
            float cx=float(dip(window,l.right+86)),cy=float(dip(window,l.bottom+105)),radius=float(dip(window,45));Pen track(color(line),float(dip(window,3)));Pen arc(color(accent),float(dip(window,3)));g.DrawArc(&track,cx-radius,cy-radius,radius*2,radius*2,140,260);if(sample.milliseconds)g.DrawArc(&arc,cx-radius,cy-radius,radius*2,radius*2,140,float(std::clamp(*sample.milliseconds/16.667,0.0,1.0)*260));
            chromeText(dc,window,sample.milliseconds?std::format(L"{:.1f}",*sample.milliseconds):L"—",l.right+41,l.bottom+78,90,38,27,textColor,FW_NORMAL,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
            chromeText(dc,window,L"NR · ms",l.right+43,l.bottom+112,86,20,10,secondary,FW_NORMAL,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
            chromeText(dc,window,L"源帧处理",l.right+158,l.bottom+65,130,20,11,secondary);
            chromeText(dc,window,s.frames?std::format(L"{:.1f} fps",s.fps):L"未测",l.right+158,l.bottom+88,130,30,19,textColor);
            chromeText(dc,window,s.applied.nrPolicy==pipeline::NrSizePolicy::Realtime?L"实时档 · 非原生4K NR":L"原生 NR",l.right+22,l.bottom+170,l.panelWidth-44,22,11,secondary);
        }
    }
}
}
