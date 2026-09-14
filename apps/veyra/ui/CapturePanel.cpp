#include "CapturePanel.h"
#include "Theme.h"
#include "SettingHelp.h"
#include "veyra/source/CaptureCardSource.h"
#include <future>
#include <format>
namespace veyra::ui {
namespace {
HWND window=nullptr;HFONT font=nullptr;std::function<void(const std::wstring&)> start;
struct Query {int device=-1;std::vector<std::wstring> video,audio;std::vector<source::CaptureFormat> formats;};
std::future<Query> pending;bool busy=false;int queriedDevice=-1;ULONGLONG queryStarted=0;
std::vector<source::CaptureFormat> formats;
std::function<bool()> readSdr;std::function<bool(bool)> setSdr;
void query(int device){if(busy)return;busy=true;queriedDevice=device;queryStarted=GetTickCount64();SetDlgItemTextW(window,8,L"正在查询设备能力…当前播放继续");EnableWindow(GetDlgItem(window,4),FALSE);EnableWindow(GetDlgItem(window,5),FALSE);EnableWindow(GetDlgItem(window,1),FALSE);
    pending=std::async(std::launch::async,[device]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);Query result;result.device=device;try{if(device<0){result.video=source::CaptureCardSource::devices();result.audio=source::CaptureCardSource::devices(true);}else result.formats=source::CaptureCardSource::formats(unsigned(device));}catch(...){}CoUninitialize();return result;});
}
void arrange(){RECT r{};GetClientRect(window,&r);const int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window));const int ys[]={0,44,114,184,412,412,14,84,344,154,254,224,294};for(int id=1;id<=12;++id){int x=id==5?width-152:16;int w=id==4?width-184:id==5?136:width-32;MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,ys[id]),dip(window,w),dip(window,(id<=3||id==10)?180:id==8?56:id==4||id==5?36:24),TRUE);}}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);titleTheme(h);auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style){auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);};
    for(int i=1;i<=3;++i)add(L"COMBOBOX",L"",i,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"COMBOBOX",L"",10,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"输入颜色（变更需重连）",11,0);
    for(auto name:{L"自动识别 SDR / HDR · 设备元数据",L"手动 HDR10 / PQ · BT.2020",L"手动 HLG · BT.2020 / 1000nit参考"})SendDlgItemMessageW(h,10,CB_ADDSTRING,0,LPARAM(name));SendDlgItemMessageW(h,10,CB_SETCURSEL,0,0);
    add(L"BUTTON",L"转为 SDR 显示（所有预览，立即生效）",12,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"BUTTON",L"连接并开始观看",4,BS_PUSHBUTTON|WS_TABSTOP);marked(GetDlgItem(h,4));add(L"BUTTON",L"刷新设备",5,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"视频输入设备",6,0);add(L"STATIC",L"设备实际支持的格式",7,0);add(L"STATIC",L"",8,0);add(L"STATIC",L"HDMI 音频监听",9,0);
    installDialogHelp(h,{{12,L"收到HDR也转成SDR显示，不用改PS5或Windows。增强照常用；只改预览，视频导出不受影响。切换会短暂停顿，截图跟随当前画面。关闭后跟随显示器。"},{10,L"设备没报HDR信息时手动指定，需选P010/P016。P010也可能装SDR，别给普通画面强戴HDR帽子。"},{1,L"选采集卡的视频设备。别把摄像头误请来直播PS5。"},{2,L"选设备真实提供的分辨率、帧率和像素格式。清晰度、带宽和延迟都受它影响。"},{3,L"选择对应的采集音频设备，也可不采声音。画面和声音要认对门。"},{4,L"按当前格式连接采集卡，并应用当前增强设置。"},{5,L"重新扫描设备和格式。设备被其他软件占用时，刷新不一定能抢回来。"}});
    EnableWindow(GetDlgItem(h,4),FALSE);arrange();SetTimer(h,1,100,nullptr);if(!busy)query(-1);return 0;}
case WM_TIMER:SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);if(busy&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto result=pending.get();busy=false;EnableWindow(GetDlgItem(h,5),TRUE);EnableWindow(GetDlgItem(h,1),TRUE);if(result.device<0){SendDlgItemMessageW(h,1,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,3,CB_RESETCONTENT,0,0);formats.clear();for(auto& name:result.video)SendDlgItemMessageW(h,1,CB_ADDSTRING,0,LPARAM(name.c_str()));SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(L"不监听音频"));for(auto& name:result.audio)SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(name.c_str()));SendDlgItemMessageW(h,3,CB_SETCURSEL,0,0);SetDlgItemTextW(h,8,result.video.empty()?L"未找到采集设备。连接后点击刷新。":L"请选择设备以查询实际格式；点击连接前不会中断当前视频。");}
    else{formats=std::move(result.formats);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);for(auto& format:formats)SendDlgItemMessageW(h,2,CB_ADDSTRING,0,LPARAM(format.label.c_str()));if(!formats.empty())SendDlgItemMessageW(h,2,CB_SETCURSEL,0,0);EnableWindow(GetDlgItem(h,4),!formats.empty());SetDlgItemTextW(h,8,formats.empty()?L"未读到有效的4K以内采集格式，或设备正被其他应用占用。":L"连接后使用当前增强设置。格式与音频变更需要重新连接。");}}
    else if(busy&&GetTickCount64()-queryStarted>5000)SetDlgItemTextW(h,8,L"设备查询耗时较长。可以关闭此面板，当前播放不受影响。");return 0;
case WM_COMMAND:if(LOWORD(wp)==12&&HIWORD(wp)==BN_CLICKED){const bool enabled=SendDlgItemMessageW(h,12,BM_GETCHECK,0,0)==BST_CHECKED;if(!setSdr(enabled))SetDlgItemTextW(h,8,L"当前正在切换增强，请稍后再试。");SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);}else if(LOWORD(wp)==1&&HIWORD(wp)==CBN_SELCHANGE)query(int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0)));else if(LOWORD(wp)==5)query(-1);else if(LOWORD(wp)==4){int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0)),format=int(SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0)),audio=int(SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0))-1;if(!busy&&device==queriedDevice&&format>=0&&size_t(format)<formats.size()){start(std::format(L"capture:{}:{}:{}:{}",device,formats[format].index,audio,int(SendDlgItemMessageW(h,10,CB_GETCURSEL,0,0))));DestroyWindow(h);}}return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_SIZE:arrange();return 0;
case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);auto old=font;font=makeFont(h);EnumChildWindows(h,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();return 0;}
case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(h);return 0;}break;
case WM_CLOSE:DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showCapturePanel(HWND parent,std::function<void(const std::wstring&)> callback,std::function<bool()> read,std::function<bool(bool)> write){start=std::move(callback);readSdr=std::move(read);setSdr=std::move(write);if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"采集卡 · 连接设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,560),dip(parent,514),parent,nullptr,wc.hInstance,nullptr);}
}
