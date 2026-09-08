// Manual DWM visual test: an independent window beneath Veyra. It supplies
// real desktop colors; the player never reads or captures these pixels.
// Closes itself after 180 seconds. Press Space to change the background.
#include <windows.h>
namespace {
bool blue=false;
LRESULT CALLBACK fixture(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:SetTimer(h,1,30000,nullptr);SetTimer(h,2,180000,nullptr);return 0;
    case WM_TIMER:if(w==2){DestroyWindow(h);return 0;}[[fallthrough]];
    case WM_KEYDOWN:if(m==WM_KEYDOWN&&w!=VK_SPACE)break;blue=!blue;SetWindowTextW(h,blue?L"Veyra backdrop fixture - BLUE":L"Veyra backdrop fixture - RED");InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);auto color=CreateSolidBrush(blue?RGB(16,76,242):RGB(242,34,22));FillRect(dc,&r,color);DeleteObject(color);
        auto checker=CreateSolidBrush(blue?RGB(32,170,255):RGB(255,120,32));for(int y=0;y<r.bottom;y+=80)for(int x=0;x<r.right;x+=80)if((x/80+y/80)%2){RECT tile{x,y,x+80,y+80};FillRect(dc,&tile,checker);}DeleteObject(checker);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));DrawTextW(dc,blue?L"BLUE - independent desktop window":L"RED - independent desktop window",-1,&r,DT_CENTER|DT_SINGLELINE);EndPaint(h,&ps);return 0;}
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=fixture;c.lpszClassName=L"VeyraDesktopBackdropFixture";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);auto h=CreateWindowExW(0,c.lpszClassName,L"Veyra backdrop fixture - RED",WS_OVERLAPPEDWINDOW,work.left,work.top,work.right-work.left,work.bottom-work.top,nullptr,nullptr,instance,nullptr);ShowWindow(h,SW_SHOWMAXIMIZED);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
}
