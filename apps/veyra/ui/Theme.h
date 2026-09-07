#pragma once
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <objbase.h>
#include <ocidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <string>
#pragma comment(lib,"uxtheme.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"gdiplus.lib")
#include <dwmapi.h>
namespace veyra::ui {
inline constexpr COLORREF background=RGB(9,10,11),panel=RGB(27,28,30),raised=RGB(40,41,43),line=RGB(58,60,63),textColor=RGB(236,238,240),secondary=RGB(151,157,164),accent=RGB(230,122,49),cinema=RGB(32,48,62),cinemaPanel=RGB(23,34,43);
inline HBRUSH bgBrush(){static auto b=CreateSolidBrush(background);return b;}
inline HBRUSH panelBrush(){static auto b=CreateSolidBrush(panel);return b;}
inline HBRUSH raisedBrush(){static auto b=CreateSolidBrush(raised);return b;}
inline int dip(HWND h,int v){return MulDiv(v,GetDpiForWindow(h),96);}
inline HFONT makeFont(HWND h,int size=14,int weight=FW_NORMAL){return CreateFontW(-dip(h,size),0,0,0,weight,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");}
inline void titleTheme(HWND h){BOOL enabled=TRUE;DwmSetWindowAttribute(h,20,&enabled,sizeof(enabled));DWORD corner=2;DwmSetWindowAttribute(h,33,&corner,sizeof(corner));}
inline COLORREF surface(HWND h){auto prop=GetPropW(h,L"veyra.surface");return prop?COLORREF(uintptr_t(prop)-1):panel;}
inline void surface(HWND h,COLORREF c){SetPropW(h,L"veyra.surface",HANDLE(uintptr_t(c)+1));InvalidateRect(h,nullptr,FALSE);}
inline Gdiplus::Color color(COLORREF c,BYTE a=255){return Gdiplus::Color(a,GetRValue(c),GetGValue(c),GetBValue(c));}
inline void rounded(Gdiplus::GraphicsPath& p,Gdiplus::RectF r,float radius){float d=std::min({radius*2,r.Width,r.Height});p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();}
inline void roundRect(HDC dc,RECT r,COLORREF c,int radius){Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::GraphicsPath path;rounded(path,{float(r.left),float(r.top),float(r.right-r.left-1),float(r.bottom-r.top-1)},float(radius));Gdiplus::SolidBrush brush(color(c));g.FillPath(&brush,&path);}
inline void fillSurface(HDC dc,RECT r,HWND h){auto b=CreateSolidBrush(surface(h));FillRect(dc,&r,b);DeleteObject(b);}
inline LRESULT colors(UINT msg,WPARAM w,LPARAM l=0){HDC dc=reinterpret_cast<HDC>(w);HWND h=reinterpret_cast<HWND>(l);COLORREF bg=msg==WM_CTLCOLOREDIT?raised:surface(h);SetTextColor(dc,msg==WM_CTLCOLOREDIT?textColor:secondary);SetBkColor(dc,bg);SetBkMode(dc,TRANSPARENT);SetDCBrushColor(dc,bg);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
enum class Icon { None,Play,Pause,Stop,Volume,Muted,Fullscreen,Video,Capture,Image,Subtitle,Minimize,Maximize,Close,Back };
inline void icon(HWND h,Icon icon,bool caption=false){SetPropW(h,L"veyra.icon",HANDLE(INT_PTR(icon)));if(caption)SetPropW(h,L"veyra.caption",HANDLE(1));else RemovePropW(h,L"veyra.caption");InvalidateRect(h,nullptr,FALSE);}
inline void drawIcon(HDC dc,Icon icon,float x,float y,float size,COLORREF c){using namespace Gdiplus;Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);Pen pen(color(c),std::max(1.4f,size/13));pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);SolidBrush brush(color(c));const float a=size/2;auto line=[&](float x1,float y1,float x2,float y2){g.DrawLine(&pen,x+x1*a,y+y1*a,x+x2*a,y+y2*a);};
    switch(icon){case Icon::Play:{PointF p[]={{x-a*.45f,y-a*.8f},{x+a*.85f,y},{x-a*.45f,y+a*.8f}};g.FillPolygon(&brush,p,3);break;}case Icon::Pause:g.FillRectangle(&brush,x-a*.55f,y-a*.7f,a*.35f,a*1.4f);g.FillRectangle(&brush,x+a*.2f,y-a*.7f,a*.35f,a*1.4f);break;
    case Icon::Stop:g.FillRectangle(&brush,x-a*.55f,y-a*.55f,a*1.1f,a*1.1f);break;
    case Icon::Volume:case Icon::Muted:{PointF p[]={{x-a*.9f,y-a*.28f},{x-a*.5f,y-a*.28f},{x,y-a*.7f},{x,y+a*.7f},{x-a*.5f,y+a*.28f},{x-a*.9f,y+a*.28f}};g.DrawPolygon(&pen,p,6);if(icon==Icon::Muted){line(.35f,-.4f,.85f,.4f);line(.35f,.4f,.85f,-.4f);}else{g.DrawArc(&pen,x-a*.4f,y-a*.6f,a*1.2f,a*1.2f,-60,120);g.DrawArc(&pen,x-a*.7f,y-a*.95f,a*1.9f,a*1.9f,-60,120);}break;}
    case Icon::Fullscreen:for(int sx:{-1,1})for(int sy:{-1,1}){line(sx*.85f,sy*.35f,sx*.85f,sy*.85f);line(sx*.35f,sy*.85f,sx*.85f,sy*.85f);}break;
    case Icon::Video:g.DrawRectangle(&pen,x-a*.85f,y-a*.65f,a*1.7f,a*1.3f);{PointF p[]={{x-a*.2f,y-a*.35f},{x+a*.4f,y},{x-a*.2f,y+a*.35f}};g.FillPolygon(&brush,p,3);}break;
    case Icon::Capture:g.DrawRectangle(&pen,x-a*.85f,y-a*.6f,a*1.7f,a*1.2f);line(-.4f,1,.4f,1);line(0,.6f,0,1);g.FillEllipse(&brush,x-a*.16f,y-a*.16f,a*.32f,a*.32f);break;
    case Icon::Image:g.DrawRectangle(&pen,x-a*.85f,y-a*.7f,a*1.7f,a*1.4f);line(-.6f,.4f,-.1f,-.2f);line(-.1f,-.2f,.3f,.2f);line(.3f,.2f,.65f,-.1f);g.FillEllipse(&brush,x+a*.25f,y-a*.5f,a*.24f,a*.24f);break;
    case Icon::Subtitle:g.DrawRectangle(&pen,x-a*.9f,y-a*.6f,a*1.8f,a*1.2f);line(-.6f,.2f,-.1f,.2f);line(.15f,.2f,.6f,.2f);break;
    case Icon::Minimize:line(-.6f,0,.6f,0);break;case Icon::Maximize:g.DrawRectangle(&pen,x-a*.55f,y-a*.55f,a*1.1f,a*1.1f);break;
    case Icon::Close:line(-.5f,-.5f,.5f,.5f);line(-.5f,.5f,.5f,-.5f);break;case Icon::Back:line(.2f,-.6f,-.5f,0);line(-.5f,0,.2f,.6f);break;default:break;}
}
inline LRESULT CALLBACK buttonPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_MOUSEMOVE){if(!GetPropW(h,L"veyra.hover")){SetPropW(h,L"veyra.hover",HANDLE(1));TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);InvalidateRect(h,nullptr,FALSE);}}
    if(m==WM_MOUSELEAVE){RemovePropW(h,L"veyra.hover");InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);fillSurface(dc,r,h);
        const bool marked=GetPropW(h,L"veyra.accent")!=nullptr,selected=GetPropW(h,L"veyra.selected")!=nullptr,hover=GetPropW(h,L"veyra.hover")!=nullptr,down=SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED,check=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED,ghost=GetPropW(h,L"veyra.ghost")!=nullptr;
        const auto ico=static_cast<Icon>(INT_PTR(GetPropW(h,L"veyra.icon")));bool caption=GetPropW(h,L"veyra.caption")!=nullptr;
        COLORREF fill=marked?accent:(down||selected?line:hover?raised:surface(h));if(!ghost||hover||selected||marked)roundRect(dc,r,fill,ico==Icon::Play||ico==Icon::Pause?std::min(r.right,r.bottom)/2:dip(h,8));
        wchar_t value[512]{};GetWindowTextW(h,value,512);auto f=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);COLORREF ink=!IsWindowEnabled(h)?RGB(90,96,103):marked?RGB(24,31,36):selected?textColor:hover?textColor:secondary;SetTextColor(dc,ink);
        if(ico!=Icon::None){drawIcon(dc,ico,caption?float(dip(h,18)):r.right/2.f,r.bottom/2.f,float(dip(h,ico==Icon::Play||ico==Icon::Pause?22:17)),ink);if(caption){r.left=dip(h,34);DrawTextW(dc,value,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}}
        else if((GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX){RECT toggle{r.right-dip(h,40),r.bottom/2-dip(h,8),r.right-dip(h,10),r.bottom/2+dip(h,8)};roundRect(dc,toggle,check?accent:line,dip(h,8));RECT knob{toggle.left+dip(h,check?16:3),toggle.top+dip(h,3),toggle.left+dip(h,check?26:13),toggle.top+dip(h,13)};roundRect(dc,knob,check?panel:secondary,dip(h,5));r.left=dip(h,12);r.right-=dip(h,46);DrawTextW(dc,value,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}else DrawTextW(dc,value,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(GetFocus()==h){RECT outline{};GetClientRect(h,&outline);InflateRect(&outline,-3,-3);DrawFocusRect(dc,&outline);}SelectObject(dc,old);EndPaint(h,&ps);return 0;}
    if(m==WM_ENABLE||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==BM_SETCHECK||m==WM_SETTEXT||m==BM_SETSTATE){auto result=DefSubclassProc(h,m,w,l);InvalidateRect(h,nullptr,FALSE);return result;}
    return DefSubclassProc(h,m,w,l);
}
inline LRESULT CALLBACK comboPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);fillSurface(dc,r,h);roundRect(dc,r,raised,dip(h,7));int index=int(SendMessageW(h,CB_GETCURSEL,0,0));wchar_t value[512]{};if(index>=0&&SendMessageW(h,CB_GETLBTEXTLEN,index,0)<512)SendMessageW(h,CB_GETLBTEXT,index,LPARAM(value));auto old=SelectObject(dc,HFONT(SendMessageW(h,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(h)?secondary:RGB(85,88,92));RECT label=r;label.left+=dip(h,12);label.right-=dip(h,26);DrawTextW(dc,value,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);auto pen=CreatePen(PS_SOLID,dip(h,1),secondary);auto op=SelectObject(dc,pen);int x=r.right-dip(h,15),y=r.bottom/2;MoveToEx(dc,x-dip(h,3),y-dip(h,1),nullptr);LineTo(dc,x,y+dip(h,2));LineTo(dc,x+dip(h,3),y-dip(h,1));SelectObject(dc,op);DeleteObject(pen);SelectObject(dc,old);EndPaint(h,&ps);return 0;}
    auto result=DefSubclassProc(h,m,w,l);if(m==CB_SETCURSEL||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_ENABLE)InvalidateRect(h,nullptr,FALSE);return result;
}
inline LRESULT CALLBACK trackPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);fillSurface(dc,r,h);int minimum=int(SendMessageW(h,TBM_GETRANGEMIN,0,0)),maximum=int(SendMessageW(h,TBM_GETRANGEMAX,0,0));float progress=float(SendMessageW(h,TBM_GETPOS,0,0)-minimum)/std::max(1,maximum-minimum);int margin=dip(h,6),y=r.bottom/2,x=margin+int((r.right-margin*2)*progress);RECT rail{margin,y-dip(h,1),r.right-margin,y+dip(h,2)},fill=rail;fill.right=x;roundRect(dc,rail,line,dip(h,2));roundRect(dc,fill,IsWindowEnabled(h)?accent:secondary,dip(h,2));RECT dot{x-dip(h,5),y-dip(h,5),x+dip(h,5),y+dip(h,5)};roundRect(dc,dot,IsWindowEnabled(h)?accent:line,dip(h,5));InflateRect(&dot,-dip(h,2),-dip(h,2));roundRect(dc,dot,surface(h),dip(h,3));EndPaint(h,&ps);return 0;}
    if(m==WM_LBUTTONDOWN||m==WM_MOUSEMOVE&&GetCapture()==h||m==WM_LBUTTONUP&&GetCapture()==h){if(!IsWindowEnabled(h))return 0;if(m==WM_LBUTTONDOWN)SetCapture(h);RECT r{};GetClientRect(h,&r);int minimum=int(SendMessageW(h,TBM_GETRANGEMIN,0,0)),maximum=int(SendMessageW(h,TBM_GETRANGEMAX,0,0));float fraction=std::clamp(float(GET_X_LPARAM(l)-dip(h,6))/std::max(1L,r.right-dip(h,12)),0.0f,1.0f);int value=minimum+int(fraction*(maximum-minimum));SendMessageW(h,TBM_SETPOS,TRUE,value);SendMessageW(GetParent(h),WM_HSCROLL,MAKEWPARAM(m==WM_LBUTTONUP?TB_THUMBPOSITION:TB_THUMBTRACK,value),LPARAM(h));if(m==WM_LBUTTONUP)ReleaseCapture();return 0;}
    auto result=DefSubclassProc(h,m,w,l);if(m==TBM_SETPOS||m==WM_ENABLE||m==WM_SIZE)InvalidateRect(h,nullptr,FALSE);return result;
}
inline void themeControl(HWND h){wchar_t cls[32]{};GetClassNameW(h,cls,32);SetWindowTheme(h,L"DarkMode_Explorer",nullptr);if(_wcsicmp(cls,L"BUTTON")==0)SetWindowSubclass(h,buttonPaint,900,0);else if(_wcsicmp(cls,L"COMBOBOX")==0){SetWindowLongPtrW(h,GWL_EXSTYLE,GetWindowLongPtrW(h,GWL_EXSTYLE)&~WS_EX_CLIENTEDGE);SetWindowSubclass(h,comboPaint,901,0);SendMessageW(h,CB_SETITEMHEIGHT,WPARAM(-1),dip(h,30));}else if(_wcsicmp(cls,TRACKBAR_CLASSW)==0)SetWindowSubclass(h,trackPaint,902,0);}
inline void marked(HWND h,bool on=true){if(on)SetPropW(h,L"veyra.accent",HANDLE(1));else RemovePropW(h,L"veyra.accent");InvalidateRect(h,nullptr,FALSE);}
inline void ghost(HWND h,bool on=true){if(on)SetPropW(h,L"veyra.ghost",HANDLE(1));else RemovePropW(h,L"veyra.ghost");InvalidateRect(h,nullptr,FALSE);}
inline void selected(HWND h,bool on){if(on)SetPropW(h,L"veyra.selected",HANDLE(1));else RemovePropW(h,L"veyra.selected");InvalidateRect(h,nullptr,FALSE);}
inline void setText(HWND h,const std::wstring& value){wchar_t old[4096]{};GetWindowTextW(h,old,4096);if(value!=old)SetWindowTextW(h,value.c_str());}
}
