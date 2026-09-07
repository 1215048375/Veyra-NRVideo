#include "SubtitleOverlay.h"
#include "Theme.h"
#include "veyra/Log.h"
#include <format>
namespace veyra::ui {
namespace {
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_NCHITTEST)return HTTRANSPARENT;if(msg==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}return DefWindowProcW(h,msg,wp,lp);}
}
HWND createSubtitleOverlay(HWND parent){WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraSubtitleOverlay";RegisterClassW(&wc);return CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,nullptr);}
void updateSubtitleOverlay(HWND h,const std::wstring& text,int size){
    RECT r{};GetClientRect(h,&r);if(text.empty()||r.right<1||r.bottom<1){ShowWindow(h,SW_HIDE);SetWindowTextW(h,L"");return;}
    wchar_t old[4096]{};GetWindowTextW(h,old,4096);const auto extent=MAKELONG(r.right,r.bottom);if(text==old&&GetPropW(h,L"subtitle.extent")==HANDLE(INT_PTR(extent))&&GetPropW(h,L"subtitle.size")==HANDLE(INT_PTR(dip(h,size)))){ShowWindow(h,SW_SHOWNOACTIVATE);return;}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return;}auto previous=SelectObject(memory,bitmap);memset(bits,0,size_t(r.right)*r.bottom*4);
    {using namespace Gdiplus;Bitmap canvas(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));Graphics graphics(&canvas);graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);FontFamily family(L"Microsoft YaHei UI");StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);GraphicsPath path;path.AddString(text.c_str(),int(text.size()),&family,FontStyleBold,float(dip(h,size)),RectF(4,0,float(r.right-8),float(r.bottom)),&format);Pen outline(Color(220,0,0,0),float(dip(h,3)));outline.SetLineJoin(LineJoinRound);graphics.DrawPath(&outline,&path);SolidBrush white(Color(255,246,246,246));graphics.FillPath(&white,&path);}
    SIZE dimensions{r.right,r.bottom};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};if(UpdateLayeredWindow(h,screen,nullptr,&dimensions,memory,&origin,0,&blend,ULW_ALPHA)){SetWindowTextW(h,text.c_str());SetPropW(h,L"subtitle.extent",HANDLE(INT_PTR(extent)));SetPropW(h,L"subtitle.size",HANDLE(INT_PTR(dip(h,size))));ShowWindow(h,SW_SHOWNOACTIVATE);if(!GetPropW(h,L"subtitle.logged")){log::info("subtitle",std::format("layered update succeeded extent={}x{}",r.right,r.bottom));SetPropW(h,L"subtitle.logged",HANDLE(1));}}else if(!GetPropW(h,L"subtitle.error")){log::error("subtitle",std::format("UpdateLayeredWindow failed error={}",GetLastError()));SetPropW(h,L"subtitle.error",HANDLE(1));}
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
}
