#include "SettingsWindow.h"
#include "ui/Theme.h"
#include "veyra/engine/PresetStore.h"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <array>
namespace veyra::ui {
namespace {
HWND window=nullptr;engine::EngineController* controller=nullptr;HFONT font=nullptr;
std::function<void(engine::EnhancementSettings)> apply;
engine::PresetStore store(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/user-presets.v1");bool loaded=false,dirty=false,populating=false,enhancementEnabled=true;int page=0,scroll=0,contentHeight=0;uint64_t displayedRevision=0;
struct Item{HWND h;int page,x,y,w,height;};std::vector<Item> items;
const wchar_t* labels[]={L"模型强度",L"局部明暗",L"局部结构",L"肤质 · 未证实",L"风格 · 实验",L"自动遮罩 · 实验",L"UI修正 · 未证实",L"总变化强度",L"暗化变化",L"亮化变化",L"色彩变化",L"明度变化"};
void loadStore(){if(!loaded){store.load();loaded=true;}}
void message(const std::wstring& text){SetDlgItemTextW(window,401,text.c_str());}
void populate(engine::EnhancementSettings s){populating=true;float v[]={s.model.intensity,s.model.tone,s.model.structure,s.model.skin,float(s.model.style),float(s.model.autoMask),float(s.model.uiCorrection),s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance};for(int i=0;i<12;++i){std::wostringstream o;o<<std::setprecision(7)<<v[i];SetDlgItemTextW(window,100+i,o.str().c_str());SendDlgItemMessageW(window,600+i,TBM_SETPOS,TRUE,LPARAM(v[i]*(i>=4&&i<=6?1:100)));}CheckDlgButton(window,200,s.nr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(window,201,s.sr?BST_CHECKED:BST_UNCHECKED);SendDlgItemMessageW(window,202,CB_SETCURSEL,s.multiplier-1,0);SendDlgItemMessageW(window,203,CB_SETCURSEL,int(s.nrPolicy),0);SendDlgItemMessageW(window,204,CB_SETCURSEL,int(s.flow),0);SendDlgItemMessageW(window,205,CB_SETCURSEL,int(s.content),0);displayedRevision=s.revision;populating=false;dirty=false;}
bool read(engine::EnhancementSettings& s){s=controller->snapshot().desired;float v[12]{};for(int i=0;i<12;++i){wchar_t b[64]{};GetDlgItemTextW(window,100+i,b,64);wchar_t* end=nullptr;v[i]=wcstof(b,&end);if(end==b||*end||!std::isfinite(v[i])){message(L"请输入完整的有限数值；未提交设置");return false;}}
    for(int i=4;i<7;++i)if(v[i]!=std::floor(v[i])||v[i]<0||v[i]>(i==4?2:1)){message(L"风格/遮罩/UI修正必须为整数");return false;}
    s.model={v[0],v[1],v[2],v[3],int(v[4]),int(v[5]),int(v[6])};s.residual={v[7],v[8],v[9],v[10],v[11]};s.nr=IsDlgButtonChecked(window,200)==BST_CHECKED;s.sr=IsDlgButtonChecked(window,201)==BST_CHECKED;s.multiplier=uint32_t(SendDlgItemMessageW(window,202,CB_GETCURSEL,0,0)+1);s.nrPolicy=static_cast<pipeline::NrSizePolicy>(SendDlgItemMessageW(window,203,CB_GETCURSEL,0,0));s.flow=static_cast<engine::FlowQuality>(SendDlgItemMessageW(window,204,CB_GETCURSEL,0,0));s.content=static_cast<engine::ContentRate>(SendDlgItemMessageW(window,205,CB_GETCURSEL,0,0));if(!s.validate().empty()){message(L"参数越界，未提交。悬停数值框查看允许范围。");return false;}return true;}

void refreshPresets(){auto list=GetDlgItem(window,300);SendMessageW(list,CB_RESETCONTENT,0,0);for(auto& p:store.entries())SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.name.c_str()));if(!store.entries().empty())SendMessageW(list,CB_SETCURSEL,0,0);PostMessageW(GetParent(window),WM_APP+42,0,0);}
void arrange(){if(!window)return;RECT r{};GetClientRect(window,&r);int width=MulDiv(r.right,96,GetDpiForWindow(window)),height=MulDiv(r.bottom,96,GetDpiForWindow(window));const int sticky=88,viewport=std::max(1,height-sticky);contentHeight=0;for(auto& item:items)if(item.page==page)contentHeight=std::max(contentHeight,item.y+(GetDlgCtrlID(item.h)>=202&&GetDlgCtrlID(item.h)<=205?36:item.height)+12);scroll=std::clamp(scroll,0,std::max(0,contentHeight-viewport));
    auto batch=BeginDeferWindowPos(int(items.size()));for(auto& item:items){bool fixed=item.page==-1;bool visible=item.page==page||fixed;int w=item.w<0?width-item.x-12:item.w;int y=fixed?(GetDlgCtrlID(item.h)==400?0:42):item.y-scroll+sticky;int itemHeight=fixed?42:item.height;batch=DeferWindowPos(batch,item.h,fixed?HWND_TOP:nullptr,dip(window,item.x),dip(window,y),dip(window,std::max(1,w)),dip(window,itemHeight),SWP_NOACTIVATE|(fixed?0:SWP_NOZORDER)|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));if(!fixed){auto clip=CreateRectRgn(0,dip(window,std::max(0,sticky-y)),dip(window,std::max(1,w)),dip(window,std::max(0,std::min(itemHeight,height-y))));SetWindowRgn(item.h,clip,FALSE);}}EndDeferWindowPos(batch);InvalidateRect(window,nullptr,FALSE);
}
HWND add(const wchar_t* cls,const wchar_t* text,int id,DWORD style,int group,int x,int y,int width,int height){auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,WPARAM(font),TRUE);themeControl(h);items.push_back({h,group,x,y,width,height});return h;}
void button(const wchar_t* title,int id,int group,int x,int y,int width=-1){add(L"BUTTON",title,id,BS_PUSHBUTTON|WS_TABSTOP,group,x,y,width,36);}
void combo(int id,int group,int y,std::initializer_list<const wchar_t*> names){auto h=add(L"COMBOBOX",L"",id,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,group,12,y,-1,200);for(auto name:names)SendMessageW(h,CB_ADDSTRING,0,LPARAM(name));}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);items.clear();
    add(L"BUTTON",L"DLSS5 NR 增强",200,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,12,-1,36);
    add(L"BUTTON",L"SR 输出4K底图",201,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,56,-1,36);
    combo(203,0,104,{L"实时NR · 约1080内部处理",L"原生NR · 高性能成本"});
    add(L"STATIC",L"模型参数",1100,0,0,12,146,-1,24);
    for(int i=0;i<12;++i){const int y=174+i*62+(i>=7?32:0);add(L"STATIC",labels[i],1000+i,0,0,12,y,176,28);add(L"EDIT",L"",100+i,ES_AUTOHSCROLL|WS_TABSTOP|ES_RIGHT,0,202,y,-1,28);const wchar_t* ranges[]={L"范围：0–1",L"范围：0–1",L"范围：0–1",L"-1表示默认；其余范围0–2（效果未证实）",L"整数0、1、2（实验）",L"整数0或1（实验）",L"整数0或1（效果未证实）",L"范围：0–2",L"范围：0–2",L"范围：0–2",L"范围：0–2",L"范围：0–2"};SetPropW(GetDlgItem(h,100+i),L"veyra.tip",HANDLE(ranges[i]));auto slider=add(TRACKBAR_CLASSW,L"",600+i,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,12,y+32,-1,16);SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(i==3?-100:0,i>=4&&i<=6?(i==4?2:1):i<3?100:200));}
    add(L"STATIC",L"增强变化量",1101,0,0,12,606,-1,24);
    add(L"STATIC",L"肤质 / 风格 / 遮罩 / UI键为本地实验参数。未验证的效果不会标成可用能力。",1102,0,0,12,956,-1,70);
    button(L"应用整套设置",210,0,12,1034);marked(GetDlgItem(h,210));button(L"恢复内建默认",211,0,12,1078);
    add(L"STATIC",L"补帧与运动估算",1103,0,1,12,12,-1,30);
    combo(202,1,52,{L"关闭补帧",L"DLSSG 2X",L"DLSSG 3X · 需硬件支持",L"DLSSG 4X · 需硬件支持"});
    combo(204,1,104,{L"光流 · 性能",L"光流 · 平衡",L"光流 · 质量"});
    combo(205,1,156,{L"采用源时间戳",L"自动识别内容节奏",L"识别30fps内容节奏",L"识别50fps内容节奏",L"识别60fps内容节奏"});
    add(L"STATIC",L"运动为估算输入。倍率不代表实际显示帧率。比较时只展示真实帧；采集补帧需要等待下一张源帧。",1104,0,1,12,210,-1,100);
    button(L"应用补帧与当前参数",212,1,12,326);marked(GetDlgItem(h,212));
    add(L"STATIC",L"用户预设",1105,0,2,12,12,-1,30);combo(300,2,56,{});add(L"EDIT",L"新预设",301,ES_AUTOHSCROLL|WS_TABSTOP,2,12,108,-1,36);SendDlgItemMessageW(h,301,EM_SETLIMITTEXT,48,0);
    const wchar_t* names[]={L"载入所选预设",L"新建内建默认预设",L"保存当前参数为新预设",L"重命名所选",L"删除所选",L"设为启动增强默认"};for(int i=0;i<6;++i)button(names[i],310+i,2,12,164+i*44);
    add(L"STATIC",L"启动界面始终为日常模式。默认预设只决定增强参数；预设文件损坏时保留原文件。",1106,0,2,12,438,-1,82);
    add(L"STATIC",L"原生画质导出",1107,0,3,12,12,-1,32);combo(500,3,60,{L"H.264 · MP4",L"HEVC · MP4"});SendDlgItemMessageW(h,500,CB_SETCURSEL,0,0);
    add(L"STATIC",L"冻结启动时整套参数；NR按原生尺寸处理。保留兼容音轨。VFR不改写为CFR；字幕不烧录。",1108,0,3,12,108,-1,94);
    button(L"选择位置并导出视频",501,3,12,212);marked(GetDlgItem(h,501));button(L"保存当前图片 / 视频帧",502,3,12,256);
    button(L"暂停 / 继续导出",503,3,12,310);button(L"取消导出",504,3,12,354);
    add(L"BUTTON",L"优先观看 · 降低导出占用",505,BS_AUTOCHECKBOX|WS_TABSTOP,3,12,406,-1,36);CheckDlgButton(h,505,BST_CHECKED);
    add(L"STATIC",L"",506,0,3,12,458,-1,120);add(L"STATIC",L"",507,0,3,12,588,-1,80);
    add(L"STATIC",L"",400,0,-1,12,900,-1,92);add(L"STATIC",L"",401,0,-1,12,996,-1,86);
    loadStore();refreshPresets();populate(controller->snapshot().desired);message(store.error());SetTimer(h,1,250,nullptr);arrange();return 0;}
case WM_SIZE:arrange();return 0;
case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,panelBrush());int height=MulDiv(r.bottom,96,GetDpiForWindow(h));if(contentHeight>height){float ratio=float(height)/contentHeight;int thumb=std::max(dip(h,30),int(r.bottom*ratio));int y=int((r.bottom-thumb)*float(scroll)/std::max(1,contentHeight-height));RECT bar{r.right-dip(h,5),y,r.right-dip(h,2),y+thumb};roundRect(dc,bar,line,dip(h,2));}EndPaint(h,&ps);return 0;}
case WM_LBUTTONDOWN:case WM_MOUSEMOVE:if(msg==WM_LBUTTONDOWN&&GET_X_LPARAM(lp)<([](HWND w){RECT r{};GetClientRect(w,&r);return r.right;})(h)-dip(h,12))break;if(msg==WM_LBUTTONDOWN)SetCapture(h);if(GetCapture()==h){RECT r{};GetClientRect(h,&r);int height=MulDiv(r.bottom,96,GetDpiForWindow(h));scroll=int(float(GET_Y_LPARAM(lp))/std::max(1L,r.bottom)*std::max(0,contentHeight-height));arrange();return 0;}break;
case WM_LBUTTONUP:if(GetCapture()==h)ReleaseCapture();return 0;
case WM_VSCROLL:{switch(LOWORD(wp)){case SB_LINEUP:scroll-=40;break;case SB_LINEDOWN:scroll+=40;break;case SB_PAGEUP:scroll-=240;break;case SB_PAGEDOWN:scroll+=240;break;case SB_THUMBTRACK:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(h,SB_VERT,&si);scroll=si.nTrackPos;break;}}arrange();return 0;}
case WM_MOUSEWHEEL:scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*90;arrange();return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_COMMAND:{const int id=LOWORD(wp);if(HIWORD(wp)==EN_SETFOCUS){for(auto& item:items)if(GetDlgCtrlID(item.h)==id&&item.page==page){RECT r{};GetClientRect(h,&r);int height=MulDiv(r.bottom,96,GetDpiForWindow(h))-88;if(item.y<scroll)scroll=item.y;if(item.y+item.height>scroll+height)scroll=item.y+item.height-height;arrange();break;}}if(!populating&&(HIWORD(wp)==EN_CHANGE&&id>=100&&id<=111||HIWORD(wp)==CBN_SELCHANGE&&id>=202&&id<=205||id==200||id==201)){dirty=true;message(L"参数草稿尚未应用。无效草稿保留；切换面板不会提交。");}
    engine::EnhancementSettings s;wchar_t name[128]{};GetDlgItemTextW(h,301,name,128);const auto index=size_t(SendDlgItemMessageW(h,300,CB_GETCURSEL,0,0));bool ok=true;
    if(id==210||id==212){KillTimer(h,2);if(read(s)){apply(s);dirty=false;message(L"设置已请求；在真实帧边界确认已应用后生效");}}
    else if(id==211){s={};populate(s);apply(s);message(L"已请求恢复内建默认；用户预设保留");}
    else if(id==310&&index<store.entries().size()){s=store.entries()[index].settings;populate(s);apply(s);}
    else if(id==311){ok=store.put(name,{});refreshPresets();}
    else if(id==312){if(read(s))ok=store.put(name,s);refreshPresets();}
    else if(id==313){ok=store.rename(index,name);refreshPresets();}
    else if(id==314&&index<store.entries().size()){if(MessageBoxW(h,(L"删除预设“"+store.entries()[index].name+L"”？当前画质不受影响。").c_str(),L"删除预设",MB_YESNO|MB_ICONQUESTION)==IDYES){ok=store.erase(index);refreshPresets();}}
    else if(id==315)ok=store.setDefault(index);
    else if(id>=501&&id<=505)SendMessageW(GetParent(h),WM_APP+41,id,id==501?SendDlgItemMessageW(h,500,CB_GETCURSEL,0,0):id==505?IsDlgButtonChecked(h,505):0);
    if(!ok)message(store.error());return 0;}
case WM_HSCROLL:{int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));if(id>=600&&id<612){int index=id-600;float v=float(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0))/(index>=4&&index<=6?1:100);if(index==3&&v<0)v=-1;std::wostringstream o;o<<std::setprecision(4)<<v;SetDlgItemTextW(h,100+index,o.str().c_str());SetTimer(h,2,120,nullptr);}return 0;}
case WM_TIMER:{if(wp==2){KillTimer(h,2);engine::EnhancementSettings setting;if(read(setting)){apply(setting);dirty=false;message(enhancementEnabled?L"已请求参数；帧边界确认生效":L"增强关闭中：已保存待启用参数");}return 0;}auto s=controller->snapshot();if(enhancementEnabled&&!dirty&&displayedRevision!=s.desired.revision)populate(s.desired);std::wostringstream o;o<<L"期望版本 "<<s.desired.revision<<L" / 已应用 "<<s.applied.revision<<(s.applying?L" · 应用中":L"")<<L"\n强度 "<<s.applied.model.intensity<<L" · 变化量 "<<s.applied.residual.total<<L" · 倍率 "<<s.applied.multiplier<<L"X";setText(GetDlgItem(h,400),o.str());return 0;}
case WM_DESTROY:KillTimer(h,1);KillTimer(h,2);DeleteObject(font);window=nullptr;items.clear();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
engine::EnhancementSettings defaultSettings(){loadStore();return store.defaultSettings();}
std::vector<std::wstring> presetNames(){loadStore();std::vector<std::wstring> out;for(auto& p:store.entries())out.push_back(p.name);return out;}
bool presetAt(size_t index,engine::EnhancementSettings& out){loadStore();if(index>=store.entries().size())return false;out=store.entries()[index].settings;return true;}
HWND createSettingsPanel(HWND parent,engine::EngineController& engine,std::function<void(engine::EnhancementSettings)> callback){controller=&engine;apply=std::move(callback);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraInspector";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"专业参数",WS_CHILD|WS_CLIPCHILDREN,0,0,328,500,parent,nullptr,wc.hInstance,nullptr);}
void settingsVisibility(bool visible){if(window&&!visible)KillTimer(window,2);}
void settingsPage(int value){if(window)KillTimer(window,2);page=std::clamp(value,0,3);scroll=0;arrange();}
void settingsEnabled(bool enabled,const engine::EnhancementSettings& configured){enhancementEnabled=enabled;if(window&&!enabled&&!dirty)populate(configured);if(window&&!enabled)message(L"增强已关闭。修改只保存待启用配置；开启后整套应用。");}
void settingsDpi(){if(!window)return;auto old=font;font=makeFont(window);for(auto& item:items)SendMessageW(item.h,WM_SETFONT,WPARAM(font),TRUE);DeleteObject(old);arrange();}
void exportPanelStatus(const engine::ExportJobSnapshot& job,bool canExport,bool canSave){if(!window)return;setText(GetDlgItem(window,506),job.state==engine::ExportState::Idle?L"尚无导出任务":job.message+L"\n"+std::to_wstring(int(job.progress*100))+L"% · 源帧 "+std::to_wstring(job.sourceFrames)+L" / 编码 "+std::to_wstring(job.encoded)+L"\n生成 "+std::to_wstring(job.generated)+L" / CFR占位 "+std::to_wstring(job.holds)+L"\n作业 "+std::to_wstring(job.jobId)+L" · 冻结版本 "+std::to_wstring(job.frozenRevision));setText(GetDlgItem(window,507),job.output.empty()?L"目标由保存窗口选择":L"输出："+std::filesystem::path(job.output).filename().wstring());EnableWindow(GetDlgItem(window,501),canExport&&!job.active());EnableWindow(GetDlgItem(window,502),canSave);EnableWindow(GetDlgItem(window,503),job.state==engine::ExportState::Running||job.state==engine::ExportState::Paused);EnableWindow(GetDlgItem(window,504),job.active());}
}
