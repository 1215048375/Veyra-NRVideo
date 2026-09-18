#include "ImageBatchWindow.h"
#include "ImageFolder.h"
#include "Theme.h"
#include <shobjidl.h>
#include <wrl/client.h>
#include <format>
namespace veyra::ui {namespace {
HWND window=nullptr;engine::EngineController* controller=nullptr;std::filesystem::path folder;
std::function<engine::EnhancementSettings()> settings;HFONT font=nullptr;bool started=false;
enum {Folder=810,Choose,Start,Cancel,Progress,Summary,Hint};
void resize(){RECT r{};GetClientRect(window,&r);auto place=[&](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,y),dip(window,w),dip(window,h),TRUE);};int w=MulDiv(r.right,96,layoutDpi(window));
    place(Folder,16,14,w-132,28);place(Choose,w-108,12,92,30);place(Hint,16,54,w-32,42);
    place(Progress,16,106,w-32,24);place(Summary,16,144,w-32,58);place(Start,16,216,244,34);place(Cancel,w-116,216,100,34);
}
void refresh(){if(!window)return;const auto p=controller->imageBatchProgress();
    EnableWindow(GetDlgItem(window,Start),!p.active&&!folder.empty());EnableWindow(GetDlgItem(window,Choose),!p.active);
    SetDlgItemTextW(window,Cancel,p.active?L"取消任务":L"关闭");
    if(started){SendDlgItemMessageW(window,Progress,PBM_SETPOS,p.total?WPARAM(p.completed*10000/p.total):0,0);
        auto text=std::format(L"{} {}/{} · 新渲染 {} · 已有缓存 {} · 失败 {}\n{}",p.active?L"正在处理":p.cancelled?L"已取消":p.failed?L"结束（含失败）":L"完成",p.completed,p.total,p.rendered,p.skipped,p.failed,p.current);SetDlgItemTextW(window,Summary,text.c_str());}
}
void choose(){Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(SUCCEEDED(hr))hr=dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
    if(SUCCEEDED(hr))hr=dialog->Show(window);Microsoft::WRL::ComPtr<IShellItem> item;if(SUCCEEDED(hr))hr=dialog->GetResult(&item);
    PWSTR path=nullptr;if(SUCCEEDED(hr))hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);if(SUCCEEDED(hr)){folder=path;SetDlgItemTextW(window,Folder,folder.c_str());}CoTaskMemFree(path);refresh();
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){
case WM_CREATE:{window=h;font=makeFont(h);auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style){auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);};
    add(L"STATIC",folder.c_str(),Folder,SS_PATHELLIPSIS);add(L"BUTTON",L"选择目录…",Choose,BS_PUSHBUTTON);
    add(L"STATIC",L"只处理当前文件夹的图片。已有缓存保持原效果；改参数不会重渲染。\n添加图片后再次开始，只补齐新增或缺失的缓存。",Hint,0);
    add(PROGRESS_CLASSW,L"",Progress,PBS_SMOOTH);SendDlgItemMessageW(h,Progress,PBM_SETRANGE32,0,10000);
    add(L"STATIC",L"准备就绪",Summary,0);add(L"BUTTON",L"开始 / 扫描并补齐新增图片",Start,BS_PUSHBUTTON);add(L"BUTTON",L"关闭",Cancel,BS_PUSHBUTTON);
    SetTimer(h,1,150,nullptr);resize();refresh();return 0;}
case WM_SIZE:resize();return 0;
case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={dip(h,600),dip(h,320)};return 0;
case WM_TIMER:refresh();return 0;
case WM_COMMAND:if(LOWORD(w)==Choose)choose();else if(LOWORD(w)==Start){ImageFolder images;std::error_code ec;if(!images.load(folder,ec)){MessageBoxW(h,L"目录中没有可读取的 PNG / JPG / JPEG 图片。",L"文件夹预渲染",MB_OK);return 0;}started=true;controller->renderImageFolder(std::move(images.files),settings());refresh();}else if(LOWORD(w)==Cancel){if(controller->imageBatchProgress().active){controller->stop();refresh();}else DestroyWindow(h);}return 0;
case WM_CLOSE:if(controller->imageBatchProgress().active)controller->stop();DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);font=nullptr;window=nullptr;started=false;return 0;
}return DefWindowProcW(h,m,w,l);}
}
void showImageBatchWindow(HWND owner,engine::EngineController& engine,std::filesystem::path initial,std::function<engine::EnhancementSettings()> getSettings){
    if(window){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);return;}
    controller=&engine;folder=std::move(initial);settings=std::move(getSettings);started=false;
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraImageBatch";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);RegisterClassW(&wc);
    auto h=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"整个文件夹预渲染",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,dip(owner,660),dip(owner,330),owner,nullptr,wc.hInstance,nullptr);ShowWindow(h,SW_SHOW);
}
}
