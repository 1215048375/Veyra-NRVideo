#include "SettingsWindow.h"
#include "veyra/engine/PresetStore.h"
#include <filesystem>
#include "DpiWindow.h"
#include <sstream>
#include <iomanip>
#include <array>
namespace veyra::ui {
namespace {
unsigned windowDpi=96;HWND window=nullptr;engine::EngineController* controller=nullptr;HFONT font=nullptr;
engine::PresetStore store(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/user-presets.v1");bool loaded=false;
const wchar_t* labels[]={L"模型强度 [0,1]",L"局部明暗 [0,1]",L"局部结构 [0,1]",L"肤质结构 [-1/0–2]（效果未证实）",L"风格 [0,2整数]（实验）",L"自动遮罩 [0/1]（实验）",L"画面内UI修正 [0/1]（效果未证实）",L"总变化强度 [0,2]",L"暗化变化 [0,2]",L"亮化变化 [0,2]",L"色彩变化 [0,2]",L"明度变化 [0,2]"};
void loadStore(){if(!loaded){store.load();loaded=true;}}
void message(const std::wstring& text){SetDlgItemTextW(window,401,text.c_str());}
void populate(engine::EnhancementSettings s){float v[]={s.model.intensity,s.model.tone,s.model.structure,s.model.skin,float(s.model.style),float(s.model.autoMask),float(s.model.uiCorrection),s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance};for(int i=0;i<12;++i){std::wostringstream o;o<<std::setprecision(7)<<v[i];SetDlgItemTextW(window,100+i,o.str().c_str());}CheckDlgButton(window,200,s.nr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(window,201,s.sr?BST_CHECKED:BST_UNCHECKED);SendDlgItemMessageW(window,202,CB_SETCURSEL,s.multiplier-1,0);SendDlgItemMessageW(window,203,CB_SETCURSEL,int(s.nrPolicy),0);SendDlgItemMessageW(window,204,CB_SETCURSEL,int(s.flow),0);SendDlgItemMessageW(window,205,CB_SETCURSEL,int(s.content),0);}
bool read(engine::EnhancementSettings& s){s=controller->snapshot().desired;float v[12]{};for(int i=0;i<12;++i){wchar_t b[64]{};GetDlgItemTextW(window,100+i,b,64);wchar_t* end=nullptr;v[i]=wcstof(b,&end);if(end==b||*end||!std::isfinite(v[i])){message(L"请输入完整的有限数值；未提交设置");return false;}}
    for(int i=4;i<7;++i)if(v[i]!=std::floor(v[i])||v[i]<0||v[i]>(i==4?2:1)){message(L"风格/遮罩/UI修正必须为整数");return false;}
    s.model={v[0],v[1],v[2],v[3],int(v[4]),int(v[5]),int(v[6])};s.residual={v[7],v[8],v[9],v[10],v[11]};s.nr=IsDlgButtonChecked(window,200)==BST_CHECKED;s.sr=IsDlgButtonChecked(window,201)==BST_CHECKED;s.multiplier=uint32_t(SendDlgItemMessageW(window,202,CB_GETCURSEL,0,0)+1);s.nrPolicy=static_cast<pipeline::NrSizePolicy>(SendDlgItemMessageW(window,203,CB_GETCURSEL,0,0));s.flow=static_cast<engine::FlowQuality>(SendDlgItemMessageW(window,204,CB_GETCURSEL,0,0));s.content=static_cast<engine::ContentRate>(SendDlgItemMessageW(window,205,CB_GETCURSEL,0,0));if(!s.validate().empty()){message(L"参数超出显示范围，整套设置未提交");return false;}return true;}
void refreshPresets(){auto list=GetDlgItem(window,300);SendMessageW(list,CB_RESETCONTENT,0,0);for(auto& p:store.entries())SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.name.c_str()));if(!store.entries().empty())SendMessageW(list,CB_SETCURSEL,0,0);}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;const auto dpi=GetDpiForWindow(h);windowDpi=dpi;font=CreateFontW(-MulDiv(15,dpi,96),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style,int x,int y,int w,int height){auto c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(height,dpi,96),h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;};
    for(int i=0;i<12;++i){add(L"STATIC",labels[i],0,0,12,12+i*29,290,24);add(L"EDIT",L"",100+i,ES_AUTOHSCROLL|WS_TABSTOP,307,10+i*29,90,25);}
    add(L"BUTTON",L"启用DLSS5 NR",200,BS_AUTOCHECKBOX,420,12,190,25);add(L"BUTTON",L"SR输出4K底图",201,BS_AUTOCHECKBOX,420,42,190,25);
    auto combo=[&](int id,int y,std::initializer_list<const wchar_t*> names){auto c=add(L"COMBOBOX",L"",id,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,420,y,260,180);for(auto n:names)SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(n));};
    combo(202,78,{L"关闭补帧",L"DLSSG 2X",L"DLSSG 3X（需硬件支持）",L"DLSSG 4X（需硬件支持）"});combo(203,114,{L"实时NR：约1080内部处理",L"原生NR：性能成本较高"});combo(204,150,{L"光流：性能",L"光流：平衡",L"光流：质量"});combo(205,186,{L"采用源时间戳",L"自动识别内容节奏",L"识别30fps内容节奏",L"识别50fps内容节奏",L"识别60fps内容节奏"});
    add(L"BUTTON",L"整套应用",210,BS_PUSHBUTTON|WS_TABSTOP,420,226,125,30);add(L"BUTTON",L"恢复内建默认",211,BS_PUSHBUTTON,552,226,128,30);
    add(L"STATIC",L"实验参数沿用本地Feature18键；并非NVIDIA官方预设。\n实际效果需逐项验证。导出冻结启动时快照。",0,0,420,270,265,72);
    add(L"COMBOBOX",L"",300,CBS_DROPDOWNLIST|WS_VSCROLL,12,375,300,180);add(L"EDIT",L"新预设",301,ES_AUTOHSCROLL,325,375,355,26);
    const wchar_t* buttons[]={L"载入预设",L"新建",L"复制当前",L"重命名",L"删除",L"设为默认"};for(int i=0;i<6;++i)add(L"BUTTON",buttons[i],310+i,BS_PUSHBUTTON,12+i*113,413,108,30);
    add(L"STATIC",L"",400,0,12,455,668,65);add(L"STATIC",L"",401,0,12,525,668,70);loadStore();refreshPresets();populate(controller->snapshot().desired);message(store.error());SetTimer(h,1,250,nullptr);return 0;}
case WM_COMMAND:{const int id=LOWORD(wp);engine::EnhancementSettings s;wchar_t name[128]{};GetDlgItemTextW(h,301,name,128);const auto index=size_t(SendDlgItemMessageW(h,300,CB_GETCURSEL,0,0));bool ok=true;
    if(id==210){if(read(s)&&controller->requestSettings(s))message(L"设置已请求；在真实帧边界确认Applied后生效");}
    else if(id==211){s={};populate(s);controller->requestSettings(s);message(L"已请求恢复内建默认；用户预设保留");}
    else if(id==310&&index<store.entries().size()){s=store.entries()[index].settings;populate(s);controller->requestSettings(s);}
    else if(id==311){ok=store.put(name,{});refreshPresets();}
    else if(id==312){if(read(s))ok=store.put(name,s);refreshPresets();}
    else if(id==313){ok=store.rename(index,name);refreshPresets();}
    else if(id==314){ok=store.erase(index);refreshPresets();}
    else if(id==315)ok=store.setDefault(index);
    if(!ok)message(store.error());return 0;}
case WM_TIMER:{auto s=controller->snapshot();std::wostringstream o;o<<L"期望版本 "<<s.desired.revision<<L" / 已应用版本 "<<s.applied.revision<<(s.applying?L"（应用中）":L"")<<L"\n已应用：强度 "<<s.applied.model.intensity<<L" / 明暗 "<<s.applied.model.tone<<L" / 结构 "<<s.applied.model.structure<<L" / 变化量 "<<s.applied.residual.total<<L" / 倍率 "<<s.applied.multiplier<<L"\n"<<s.status;SetDlgItemTextW(h,400,o.str().c_str());return 0;}
case WM_DPICHANGED:changeDpi(h,wp,lp,windowDpi,font);return 0;
case WM_CLOSE:DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
engine::EnhancementSettings defaultSettings(){loadStore();return store.defaultSettings();}
void showSettings(HWND parent,engine::EngineController& engine){controller=&engine;if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraSettings";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);auto dpi=GetDpiForWindow(parent);CreateWindowExW(0,wc.lpszClassName,L"参数与用户预设",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(720,dpi,96),MulDiv(645,dpi,96),parent,nullptr,wc.hInstance,nullptr);}
}
