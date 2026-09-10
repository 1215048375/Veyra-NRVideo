#include "Theme.h"
#include <iostream>
using namespace veyra::ui;
namespace {
bool mutating=false;unsigned immediateNativePaint=0,clicks=0;
LRESULT CALLBACK hostProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&LOWORD(w)==2&&HIWORD(w)==BN_CLICKED){++clicks;return 0;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORBTN||m==WM_CTLCOLORLISTBOX){
        if(mutating&&!GetPropW(HWND(l),L"Veyra.EditPrint"))++immediateNativePaint;
        return colors(m,w,l);
    }
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);return 0;}
    return DefWindowProcW(h,m,w,l);
}
}
int main(){
    SetProcessDPIAware();INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR gdip=0;Gdiplus::GdiplusStartup(&gdip,&input,nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=hostProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraPaintTest";RegisterClassW(&wc);
    auto host=CreateWindowW(wc.lpszClassName,L"Veyra control paint regression",WS_OVERLAPPEDWINDOW,0,0,500,450,nullptr,nullptr,wc.hInstance,nullptr);
    GlassBackdrop backdrop;backdrop.attach(host);backdrop.render(500,450,false,false,{});
    int failures=0;auto check=[&](bool pass,const char* what){std::cout<<(pass?"PASS ":"FAIL ")<<what<<'\n';if(!pass)++failures;};
    const wchar_t* classes[]={L"STATIC",L"BUTTON",L"COMBOBOX",TRACKBAR_CLASSW,L"EDIT"};
    DWORD styles[]={SS_LEFT,BS_AUTOCHECKBOX,CBS_DROPDOWNLIST,TBS_HORZ,ES_AUTOHSCROLL};
    HWND controls[5]{};auto font=makeFont(host);
    for(int i=0;i<5;++i){controls[i]=CreateWindowW(classes[i],L"sample",WS_CHILD|WS_VISIBLE|styles[i],12,12+i*60,280,32,host,HMENU(INT_PTR(i+1)),wc.hInstance,nullptr);}
    for(auto h:controls){themeControl(h);SendMessageW(h,WM_SETFONT,WPARAM(font),FALSE);}
    SendMessageW(controls[2],CB_ADDSTRING,0,LPARAM(L"First"));SendMessageW(controls[2],CB_ADDSTRING,0,LPARAM(L"Second"));SendMessageW(controls[2],CB_SETCURSEL,0,0);
    ShowWindow(host,SW_SHOWNOACTIVATE);RedrawWindow(host,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    mutating=true;
    for(int n=0;n<30;++n){
        SetWindowTextW(controls[0],n%2?L"Processing 30 fps":L"Processing 60 fps");
        SendMessageW(controls[1],BM_SETCHECK,n%2,0);SendMessageW(controls[1],BM_SETSTATE,n%2,0);
        SendMessageW(controls[2],CB_SETCURSEL,n%2,0);
        SendMessageW(controls[3],TBM_SETPOS,TRUE,n);
        SetWindowTextW(controls[4],L"0.5");SendMessageW(controls[4],EM_SETSEL,0,-1);
    }
    mutating=false;check(immediateNativePaint==0,"state changes never request immediate native background painting");
    check(SendMessageW(controls[1],BM_GETCHECK,0,0)==BST_CHECKED,"native checkbox model preserved");
    check(SendMessageW(controls[2],CB_GETCURSEL,0,0)==1,"native combo selection preserved");
    check(SendMessageW(controls[3],TBM_GETPOS,0,0)==29,"native trackbar model preserved");
    wchar_t edit[32]{};GetWindowTextW(controls[4],edit,32);check(std::wstring(edit)==L"0.5","native edit text and selection updates preserved");
    SetActiveWindow(host);mutating=true;
    SendMessageW(controls[1],BM_SETSTATE,FALSE,0);SendMessageW(controls[1],BM_CLICK,0,0);
    check(clicks==1&&SendMessageW(controls[1],BM_GETCHECK,0,0)==BST_UNCHECKED,"button click still toggles and notifies owner");
    SetFocus(controls[4]);check(GetFocus()==controls[4],"edit retains keyboard focus");
    SendMessageW(controls[4],EM_SETSEL,0,-1);SendMessageW(controls[4],WM_CHAR,'7',1);
    GetWindowTextW(controls[4],edit,32);check(std::wstring(edit)==L"7","keyboard input replaces selection");
    SendMessageW(controls[3],WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(120,16));
    SendMessageW(controls[3],WM_LBUTTONUP,0,MAKELPARAM(240,16));
    check(SendMessageW(controls[3],TBM_GETPOS,0,0)>80&&GetCapture()!=controls[3],"trackbar drag updates and releases capture");
    SetFocus(controls[1]);mutating=false;
    check(immediateNativePaint==0,"click, focus, typing and drag avoid native background painting");
    SendMessageW(controls[0],WM_SETREDRAW,FALSE,0);SetWindowTextW(controls[0],L"batch update");
    check(GetPropW(controls[0],L"SysSetRedraw")!=nullptr&&!(GetWindowLongPtrW(controls[0],GWL_STYLE)&WS_VISIBLE),"external redraw suppression is preserved");
    SendMessageW(controls[0],WM_SETREDRAW,TRUE,0);
    for(auto h:controls){
        ShowWindow(h,SW_HIDE);SetWindowTextW(h,L"hidden");EnableWindow(h,FALSE);EnableWindow(h,TRUE);
        check(!(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE),"hidden control is not revealed by model update");ShowWindow(h,SW_SHOW);
        RECT rc{};GetClientRect(h,&rc);HDC dc=GetDC(h);AlphaRaster print;print.create(dc,rc.right,rc.bottom);ReleaseDC(h,dc);
        SendMessageW(h,WM_PRINTCLIENT,WPARAM(print.dc),PRF_CLIENT);GdiFlush();unsigned bright=0,painted=0;
        for(int i=0;i<print.width*print.height;++i){const auto p=print.pixels[i];painted+=p!=0;bright+=(p&255)>225&&((p>>8)&255)>225&&((p>>16)&255)>225;}
        check(painted>unsigned(print.width*print.height/2)&&bright<unsigned(print.width*print.height/4),"WM_PRINTCLIENT renders custom dark surface instead of native white field");
    }
    backdrop.detach();DestroyWindow(host);DeleteObject(font);glassTextTheme().reset();Gdiplus::GdiplusShutdown(gdip);
    std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
