#include <windows.h>
#include "SettingsWindow.h"
#include "TelemetryWindow.h"
#include <objbase.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <format>
#include <string>
#include <chrono>
#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/Log.h"
#include "veyra/engine/Subtitles.h"
namespace {
enum {Open=101,Play,Stop,Save,Nr,Sr,Fg,Seek,Info,Capture,Export,Realtime,Recent,Multiplier,Settings,OriginalHold,CompareToggle,Split,Reference,Fullscreen,VideoSurface=1000};
veyra::engine::EngineController engine;
struct ToolbarItem{HWND hwnd;int width;};std::vector<ToolbarItem> toolbar;
bool full=false,holdOriginal=false,referenceBase=false;int compareMode=0;float compareSplit=.5f;WINDOWPLACEMENT windowPlacement{sizeof(windowPlacement)};
HWND mainWindow=nullptr,video=nullptr,statusBar=nullptr,seekBar=nullptr;
std::wstring currentFile,autoInput;bool paused=false,dragging=false,closing=false;int smokeSeconds=0;ULONGLONG startTick=0;int resultCode=0;
std::wstring exportOutput;unsigned exportFrames=0;unsigned cancelAfterMs=0;bool exportHevc=false;veyra::engine::PlayerOptions initialOptions;
std::vector<veyra::engine::SubtitleCue> subtitles;HWND subtitleLabel=nullptr;
HFONT font=nullptr;bool smokeRollback=false,smokeRollbackFlow=false,smokeUi=false;int uiStep=0;bool smokeSettings=false;int settingsStep=0;bool smokeControls=false;int smokeStep=0;std::wstring smokeSave;
HWND captureWindow=nullptr,captureDevice=nullptr,captureFormat=nullptr,captureAudio=nullptr;
std::vector<veyra::source::CaptureFormat> captureFormats;
void openFile(const std::wstring&);
void layout();
void updateComparison(){engine.comparison(holdOriginal?1:compareMode,referenceBase,compareSplit);}
void toggleFullscreen(){full=!full;if(full){GetWindowPlacement(mainWindow,&windowPlacement);SetWindowLongPtrW(mainWindow,GWL_STYLE,WS_POPUP|WS_VISIBLE);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(mainWindow,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtrW(mainWindow,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&windowPlacement.rcNormalPosition,MONITOR_DEFAULTTONEAREST),&mi);auto& r=windowPlacement.rcNormalPosition;if(r.right<mi.rcWork.left||r.left>mi.rcWork.right||r.bottom<mi.rcWork.top||r.top>mi.rcWork.bottom){OffsetRect(&r,mi.rcWork.left-r.left,mi.rcWork.top-r.top);}SetWindowPlacement(mainWindow,&windowPlacement);SetWindowPos(mainWindow,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}SetWindowTextW(GetDlgItem(mainWindow,Fullscreen),full?L"退出全屏":L"全屏 F11");layout();}
LRESULT CALLBACK interaction(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    if(id==OriginalHold){if(m==WM_LBUTTONDOWN){holdOriginal=true;SetCapture(h);updateComparison();return 0;}if(m==WM_LBUTTONUP||m==WM_CAPTURECHANGED){holdOriginal=false;if(GetCapture()==h)ReleaseCapture();updateComparison();return 0;}}
    if(id==VideoSurface&&compareMode==2&&(m==WM_LBUTTONDOWN||m==WM_MOUSEMOVE)&&(m==WM_LBUTTONDOWN||(w&MK_LBUTTON))){if(m==WM_LBUTTONDOWN)SetCapture(h);RECT r{};GetClientRect(h,&r);compareSplit=std::clamp(float(short(LOWORD(l)))/std::max(1L,r.right),0.0f,1.0f);updateComparison();return 0;}
    if(id==VideoSurface&&m==WM_LBUTTONUP&&GetCapture()==h){ReleaseCapture();return 0;}
    return DefSubclassProc(h,m,w,l);
}
void refreshFormats(){SendMessageW(captureFormat,CB_RESETCONTENT,0,0);int i=int(SendMessageW(captureDevice,CB_GETCURSEL,0,0));captureFormats=i>=0?veyra::source::CaptureCardSource::formats(i):std::vector<veyra::source::CaptureFormat>{};for(auto& f:captureFormats)SendMessageW(captureFormat,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(f.label.c_str()));if(!captureFormats.empty())SendMessageW(captureFormat,CB_SETCURSEL,0,0);}
LRESULT CALLBACK captureProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_CREATE){auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style,int y){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,12,y,510,200,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);};
captureDevice=add(L"COMBOBOX",L"",1,CBS_DROPDOWNLIST|WS_VSCROLL,15);captureFormat=add(L"COMBOBOX",L"",2,CBS_DROPDOWNLIST|WS_VSCROLL,55);captureAudio=add(L"COMBOBOX",L"",3,CBS_DROPDOWNLIST|WS_VSCROLL,95);
auto devices=veyra::source::CaptureCardSource::devices();for(auto& name:devices)SendMessageW(captureDevice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(!devices.empty())SendMessageW(captureDevice,CB_SETCURSEL,0,0);else SendMessageW(captureDevice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"未找到采集设备，请连接后重新打开"));
SendMessageW(captureAudio,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"不监听音频（可选择下方HDMI音频输入）"));for(auto& name:veyra::source::CaptureCardSource::devices(true))SendMessageW(captureAudio,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));SendMessageW(captureAudio,CB_SETCURSEL,0,0);refreshFormats();
CreateWindowExW(0,L"STATIC",L"设备 / 实际格式 / HDMI音频。增强采用主界面设置。实卡验收由用户完成；补帧必须等待下一张源帧到达。",WS_CHILD|WS_VISIBLE,12,140,510,64,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
CreateWindowExW(0,L"BUTTON",L"开始采集",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,12,211,180,32,hwnd,reinterpret_cast<HMENU>(4),GetModuleHandleW(nullptr),nullptr);return 0;}
if(msg==WM_COMMAND){if(LOWORD(wp)==1&&HIWORD(wp)==CBN_SELCHANGE)refreshFormats();if(LOWORD(wp)==4){int d=int(SendMessageW(captureDevice,CB_GETCURSEL,0,0)),f=int(SendMessageW(captureFormat,CB_GETCURSEL,0,0)),a=int(SendMessageW(captureAudio,CB_GETCURSEL,0,0))-1;if(d>=0&&f>=0&&size_t(f)<captureFormats.size()){openFile(std::format(L"capture:{}:{}:{}",d,captureFormats[f].index,a));DestroyWindow(hwnd);}else MessageBoxW(hwnd,L"请选择支持的1080p/4K 30/60格式。",L"采集",MB_OK);}return 0;}if(msg==WM_DESTROY){captureWindow=nullptr;return 0;}return DefWindowProcW(hwnd,msg,wp,lp);}
void showCapture(){if(captureWindow){SetForegroundWindow(captureWindow);return;}WNDCLASSW wc{};wc.lpfnWndProc=captureProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);captureWindow=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"采集卡设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,550,295,mainWindow,nullptr,wc.hInstance,nullptr);}
veyra::engine::PlayerOptions options(){return veyra::engine::PlayerOptions::from(engine.snapshot().desired);}

std::wstring fileDialog(bool save){wchar_t name[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFile=name;ofn.nMaxFile=32768;
ofn.lpstrFilter=save?L"PNG图片\0*.png\0JPEG图片\0*.jpg\0":L"视频 / 图片\0*.mp4;*.mkv;*.mov;*.avi;*.ts;*.png;*.jpg;*.jpeg\0所有文件\0*.*\0";
ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);ofn.lpstrDefExt=save?L"png":nullptr;
return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?name:L"";}
void openFile(const std::wstring& file){if(file.empty())return;currentFile=file;subtitles=veyra::engine::loadSrt(std::filesystem::path(file).replace_extension(L".srt").wstring());paused=false;SetWindowTextW(GetDlgItem(mainWindow,Play),L"暂停");engine.open(video,file,options());SetWindowTextW(mainWindow,(L"Veyra — "+std::filesystem::path(file).filename().wstring()).c_str());if(smokeSeconds<=0)WritePrivateProfileStringW(L"Player",L"最近打开",file.c_str(),(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/veyra.ini").wstring().c_str());}
HWND control(const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x,int y,int w,int h){auto hwnd=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(id>=Open&&id<=Fullscreen&&id!=Seek)toolbar.push_back({hwnd,w});return hwnd;}
void layout(){if(!mainWindow||!video)return;RECT r{};GetClientRect(mainWindow,&r);const int dpi=GetDpiForWindow(mainWindow);auto px=[&](int x){return MulDiv(x,dpi,96);};int x=px(8),y=px(8),row=px(32),gap=px(6);for(auto item:toolbar){const int id=GetDlgCtrlID(item.hwnd);ShowWindow(item.hwnd,full&&id!=Fullscreen?SW_HIDE:SW_SHOW);if(full){if(id==Fullscreen)MoveWindow(item.hwnd,std::max(0L,r.right-px(115)),px(8),px(108),row,TRUE);continue;}const int w=px(item.width);if(x+w>r.right-px(8)){x=px(8);y+=row+gap;}wchar_t cls[32]{};GetClassNameW(item.hwnd,cls,32);MoveWindow(item.hwnd,x,y,w,wcscmp(cls,L"ComboBox")==0?px(180):row,TRUE);x+=w+gap;}
    const int top=full?0:y+row+gap,bottom=full?0:px(126);MoveWindow(video,full?0:px(8),top,std::max(1L,r.right-(full?0:px(16))),std::max(1L,r.bottom-top-bottom),TRUE);
    ShowWindow(seekBar,full?SW_HIDE:SW_SHOW);ShowWindow(statusBar,full?SW_HIDE:SW_SHOW);MoveWindow(subtitleLabel,px(8),std::max(0L,r.bottom-px(full?48:120)),std::max(1L,r.right-px(16)),px(32),TRUE);MoveWindow(seekBar,px(8),std::max(0L,r.bottom-px(88)),std::max(1L,r.right-px(16)),px(24),TRUE);MoveWindow(statusBar,px(12),std::max(0L,r.bottom-px(61)),std::max(1L,r.right-px(24)),px(56),TRUE);
    if(full){SetWindowPos(GetDlgItem(mainWindow,Fullscreen),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowPos(subtitleLabel,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
}

LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{mainWindow=hwnd;font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
control(L"BUTTON",L"打开视频/图片",Open,BS_PUSHBUTTON,10,10,150,30);control(L"BUTTON",L"播放",Play,BS_PUSHBUTTON,168,10,68,30);control(L"BUTTON",L"停止",Stop,BS_PUSHBUTTON,244,10,62,30);control(L"BUTTON",L"保存图片",Save,BS_PUSHBUTTON,314,10,95,30);
control(L"BUTTON",L"DLSS5 NR",Nr,BS_AUTOCHECKBOX,425,10,110,30);control(L"BUTTON",L"SR 4K",Sr,BS_AUTOCHECKBOX,518,10,78,30);control(L"BUTTON",L"补帧",Fg,BS_AUTOCHECKBOX,600,10,78,30);CheckDlgButton(hwnd,Nr,BST_CHECKED);
control(L"BUTTON",L"采集",Capture,BS_PUSHBUTTON,690,10,75,30);control(L"BUTTON",L"导出视频",Export,BS_PUSHBUTTON,773,10,103,30);control(L"BUTTON",L"性能/诊断",Info,BS_PUSHBUTTON,884,10,100,30);
control(L"BUTTON",L"实时NR档",Realtime,BS_AUTOCHECKBOX,952,10,100,30);
control(L"BUTTON",L"最近打开",Recent,BS_PUSHBUTTON,1072,10,75,30);
auto mult=control(L"COMBOBOX",L"",Multiplier,CBS_DROPDOWNLIST,1150,10,70,180);for(auto label:{L"2X",L"3X",L"4X"})SendMessageW(mult,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(mult,CB_SETCURSEL,initialOptions.fgMultiplier-2,0);
control(L"BUTTON",L"参数与预设",Settings,BS_PUSHBUTTON,1225,10,100,30);
auto original=control(L"BUTTON",L"按住原图 V",OriginalHold,BS_PUSHBUTTON,0,0,110,30);SetWindowSubclass(original,interaction,OriginalHold,0);
control(L"BUTTON",L"原图/增强切换",CompareToggle,BS_PUSHBUTTON,0,0,120,30);control(L"BUTTON",L"分屏拖动",Split,BS_PUSHBUTTON,0,0,95,30);
auto ref=control(L"COMBOBOX",L"",Reference,CBS_DROPDOWNLIST,0,0,170,180);for(auto label:{L"对比：输入原画",L"对比：增强前底图"})SendMessageW(ref,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(ref,CB_SETCURSEL,0,0);control(L"BUTTON",L"全屏 F11",Fullscreen,BS_PUSHBUTTON,0,0,108,30);
subtitleLabel=control(L"STATIC",L"",0,SS_CENTER,8,640,1100,32);
video=control(L"STATIC",L"",VideoSurface,SS_BLACKRECT|SS_NOTIFY,8,52,1100,610);seekBar=control(TRACKBAR_CLASSW,L"",Seek,TBS_HORZ,8,675,1100,24);SendMessageW(seekBar,TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));statusBar=control(L"STATIC",L"打开本地视频或PNG/JPEG。本地实验运行时，非NVIDIA官方产品。",0,SS_LEFT,12,708,1080,36);
SetWindowSubclass(video,interaction,VideoSurface,0);
DragAcceptFiles(hwnd,TRUE);SetTimer(hwnd,1,200,nullptr);layout();return 0;}
case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout();return 0;
case WM_DROPFILES:{wchar_t file[32768]{};DragQueryFileW(reinterpret_cast<HDROP>(wp),0,file,32768);DragFinish(reinterpret_cast<HDROP>(wp));openFile(file);return 0;}
case WM_COMMAND:switch(LOWORD(wp)){
case Open:openFile(fileDialog(false));break;
case Recent:{wchar_t recent[32768]{};GetPrivateProfileStringW(L"Player",L"最近打开",L"",recent,32768,(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/veyra.ini").wstring().c_str());openFile(recent);break;}
case Play:{auto s=engine.snapshot();if(!currentFile.empty()&&(!s.running||(s.duration>0&&s.position>=s.duration-0.1))){openFile(currentFile);break;}paused=!paused;engine.pause(paused);SetWindowTextW(GetDlgItem(hwnd,Play),paused?L"播放":L"暂停");break;}
case Stop:engine.stop();break;
case Save:{auto name=fileDialog(true);if(!name.empty())engine.saveFrame(name);break;}
case Multiplier:case Realtime:case Nr:case Sr:case Fg:{auto changed=engine.snapshot().desired;const auto id=LOWORD(wp);
if(id==Nr)changed.nr=IsDlgButtonChecked(hwnd,Nr)==BST_CHECKED;
if(id==Sr)changed.sr=IsDlgButtonChecked(hwnd,Sr)==BST_CHECKED;
if(id==Realtime)changed.nrPolicy=IsDlgButtonChecked(hwnd,Realtime)==BST_CHECKED?veyra::pipeline::NrSizePolicy::Realtime:veyra::pipeline::NrSizePolicy::Native;
if(id==Fg)changed.multiplier=IsDlgButtonChecked(hwnd,Fg)==BST_CHECKED?uint32_t(SendDlgItemMessageW(hwnd,Multiplier,CB_GETCURSEL,0,0)+2):1;
if(id==Multiplier&&HIWORD(wp)==CBN_SELCHANGE&&changed.multiplier>1)changed.multiplier=uint32_t(SendDlgItemMessageW(hwnd,Multiplier,CB_GETCURSEL,0,0)+2);
engine.requestSettings(changed);break;}

case Fullscreen:toggleFullscreen();break;
case VideoSurface:if(HIWORD(wp)==STN_DBLCLK)toggleFullscreen();break;
case CompareToggle:compareMode=compareMode==1?0:1;updateComparison();break;
case Split:compareMode=compareMode==2?0:2;updateComparison();break;
case Reference:referenceBase=SendDlgItemMessageW(hwnd,Reference,CB_GETCURSEL,0,0)==1;updateComparison();break;
case Settings:veyra::ui::showSettings(hwnd,engine);break;
case Capture:showCapture();break;
case Export:{if(currentFile.empty())break;wchar_t name[32768]{};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=hwnd;d.lpstrFile=name;d.nMaxFile=32768;d.lpstrFilter=L"MP4视频\0*.mp4\0";d.lpstrDefExt=L"mp4";d.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(GetSaveFileNameW(&d)){int codec=MessageBoxW(hwnd,L"使用HEVC编码？“是”为HEVC，“否”为H.264。导出冻结当前参数，NR默认原生尺寸；不覆盖已有文件。",L"视频导出",MB_YESNOCANCEL);if(codec!=IDCANCEL)engine.startExport(currentFile,name,options(),codec==IDYES);}break;}
case Info:veyra::ui::showTelemetry(hwnd,engine);break;
}return 0;
case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==seekBar){dragging=LOWORD(wp)==TB_THUMBTRACK;auto s=engine.snapshot();if(!dragging&&s.duration>0)engine.seek(s.duration*SendMessageW(seekBar,TBM_GETPOS,0,0)/10000.0);}return 0;
case WM_KEYDOWN:if(wp==VK_SPACE){SendMessageW(hwnd,WM_COMMAND,Play,0);return 0;}if(wp==VK_F11){toggleFullscreen();return 0;}if(wp==VK_ESCAPE&&full){toggleFullscreen();return 0;}if(wp=='V'){holdOriginal=true;updateComparison();return 0;}break;
case WM_KEYUP:if(wp=='V'){holdOriginal=false;updateComparison();return 0;}break;
case WM_SYSKEYDOWN:if(wp==VK_RETURN&&(lp&(1LL<<29))){toggleFullscreen();return 0;}break;
case WM_ACTIVATEAPP:if(!wp){holdOriginal=false;updateComparison();}break;
case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lp);if(!full)SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);auto oldFont=font;font=CreateFontW(-MulDiv(15,HIWORD(wp),96),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");EnumChildWindows(hwnd,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(oldFont);layout();return 0;}

case WM_TIMER:{if(closing){if(engine.idle()){KillTimer(hwnd,1);DestroyWindow(hwnd);}return 0;}if(!autoInput.empty()){auto file=autoInput;autoInput.clear();openFile(file);startTick=GetTickCount64();}if(smokeControls&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(smokeStep==0&&elapsed>2200){engine.pause(true);smokeStep=1;}
if(smokeStep==1&&elapsed>2600){engine.seek(1.0);smokeStep=2;}
if(smokeStep==2&&elapsed>3200){engine.pause(false);smokeStep=3;}
if(smokeStep==3&&elapsed>4100&&!smokeSave.empty()){engine.saveFrame(smokeSave);smokeStep=4;}
}auto s=engine.snapshot();CheckDlgButton(hwnd,Nr,s.desired.nr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Sr,s.desired.sr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Fg,s.desired.multiplier>1?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Realtime,s.desired.nrPolicy==veyra::pipeline::NrSizePolicy::Realtime?BST_CHECKED:BST_UNCHECKED);if(s.desired.multiplier>1)SendDlgItemMessageW(hwnd,Multiplier,CB_SETCURSEL,s.desired.multiplier-2,0);SetWindowTextW(subtitleLabel,veyra::engine::subtitleAt(subtitles,s.position).c_str());
auto text=s.capture?std::format(L"{}\r\n输入 {:.1f} / 已处理 {:.1f}fps | 回调至Present返回p95 {:.1f}ms（非总延迟）| 丢弃 {} | 有效生成 {}",s.status,s.captureFps,s.fps,s.captureAgeP95Ms,s.captureDropped,s.generated):std::format(L"{}\r\n{:.1f} / {:.1f}秒  已处理 {:.1f}fps  提交迟到 {:+.1f}ms  源帧 {} / 有效生成 {}",s.status,s.position,s.duration,s.fps,s.lateMs,s.frames,s.generated);
if(compareMode||holdOriginal)text+=L"\r\n真实帧对比：两侧同一源帧；此模式不展示生成帧。";
const auto problem=veyra::Logger::instance().latestProblem();if(!problem.empty()){int n=MultiByteToWideChar(CP_UTF8,0,problem.data(),int(problem.size()),nullptr,0);std::wstring detail(n,0);MultiByteToWideChar(CP_UTF8,0,problem.data(),int(problem.size()),detail.data(),n);text+=L"\r\n最近问题："+detail.substr(0,100)+L"（详见诊断）";}
SetWindowTextW(statusBar,text.c_str());if(!dragging&&s.duration>0)SendMessageW(seekBar,TBM_SETPOS,TRUE,LPARAM(s.position/s.duration*10000));
if(smokeRollback&&startTick){if(settingsStep==0&&s.frames>20){auto changed=s.desired;if(smokeRollbackFlow)changed.nr=false;else{changed.model.style=2;changed.model.intensity=.35f;}engine.requestSettings(changed);settingsStep=1;}if(settingsStep==1&&s.frames>50&&s.desired.model.style==0&&s.applied.model.style==0&&!s.applying&&(!smokeRollbackFlow||(s.applied.nr&&s.nvofExecuted>30))){settingsStep=3;veyra::log::info("rollback-test",std::format("whole snapshot restored revision={} position={} style={} intensity={}",s.applied.revision,s.position,s.applied.model.style,s.applied.model.intensity));}}
if(smokeUi&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(uiStep==0&&elapsed>2300){SendMessageW(hwnd,WM_KEYDOWN,VK_F11,0);uiStep=1;}
if(uiStep==1&&elapsed>2800){const bool entered=full&&(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_POPUP);SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);veyra::log::info("ui-test",std::format("F11 entered={} Esc restored={}",entered,!full));uiStep=entered&&!full?2:-1;}
if(uiStep==2&&elapsed>3300){SendMessageW(hwnd,WM_SYSKEYDOWN,VK_RETURN,1LL<<29);uiStep=3;}
if(uiStep==3&&elapsed>3800){const bool entered=full;SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(VideoSurface,STN_DBLCLK),LPARAM(video));veyra::log::info("ui-test",std::format("AltEnter entered={} doubleClick restored={}",entered,!full));uiStep=entered&&!full?4:-1;}
if(uiStep==4&&elapsed>4200){compareMode=2;compareSplit=.4f;updateComparison();uiStep=5;}
if(uiStep==5&&elapsed>4800){referenceBase=true;updateComparison();uiStep=6;}
if(uiStep==6&&elapsed>5500){holdOriginal=true;updateComparison();uiStep=7;}
if(uiStep==7&&elapsed>5900){holdOriginal=false;compareMode=0;updateComparison();veyra::log::info("ui-test",std::format("completed={} NR evaluations={} (no fullscreen NR re-create expected)",true,s.nrEvaluated));uiStep=8;}
}
if(smokeSettings&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(settingsStep==0&&s.frames>20){auto change=s.desired;change.model.intensity=.6f;change.model.tone=.7f;change.residual.total=.4f;change.residual.color=.8f;engine.requestSettings(change);settingsStep=1;}
if(settingsStep==1&&s.applied.model.intensity==.6f&&elapsed>3300){auto change=s.desired;change.sr=true;engine.requestSettings(change);settingsStep=2;}
if(settingsStep==2&&s.applied.sr&&elapsed>4500){auto invalid=s.desired;invalid.model.intensity=2;const bool rejected=!engine.requestSettings(invalid);veyra::log::info("settings-test",std::format("invalid-rejected={} position={} revision={} settingsStep={}",rejected,s.position,s.applied.revision,settingsStep));settingsStep=rejected?3:-1;}
}
if(smokeSeconds>0&&startTick&&GetTickCount64()-startTick>ULONGLONG(smokeSeconds)*1000){veyra::log::info("app",std::format("smoke frames={} generated={} failed={} latenessMs={:.2f} absLatenessP95Ms={:.2f} controlsStep={} capture={} processedFps={:.2f} callbackFps={:.2f} captureDropped={} callbackToPresentReturnP95Ms={:.3f} schedulingWaitP95Ms={:.3f} processCpuP95Ms={:.3f} presentCpuP95Ms={:.3f} nrEvaluated={} nvofExecuted={}",s.frames,s.generated,s.failed,s.lateMs,s.lateP95Ms,smokeStep,s.capture,s.fps,s.captureFps,s.captureDropped,s.captureAgeP95Ms,s.schedulingWaitP95Ms,s.processCpuP95Ms,s.presentCpuP95Ms,s.nrEvaluated,s.nvofExecuted));resultCode=s.frames>0&&!s.failed&&(!smokeSettings||settingsStep==3)&&(!smokeRollback||settingsStep==3)&&(!smokeUi||uiStep==8)?0:1;PostMessageW(hwnd,WM_CLOSE,0,0);}return 0;}
case WM_CLOSE:closing=true;engine.stop();SetWindowTextW(statusBar,L"正在释放当前任务资源…");return 0;
case WM_DESTROY:DeleteObject(font);PostQuitMessage(resultCode);return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
std::filesystem::create_directories(std::filesystem::path(VEYRA_PROJECT_ROOT)/"logs");wchar_t logOverride[32768]{};GetEnvironmentVariableW(L"VEYRA_LOG_FILE",logOverride,32768);veyra::Logger::instance().openFile(logOverride[0]?std::wstring(logOverride):(std::filesystem::path(VEYRA_PROJECT_ROOT)/"logs/veyra-app.log").wstring());
initialOptions=veyra::engine::PlayerOptions::from(veyra::ui::defaultSettings());

int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--smoke-seconds"&&i+1<argc)smokeSeconds=std::clamp(_wtoi(argv[++i]),1,240);else if(arg==L"--export-out"&&i+1<argc)exportOutput=argv[++i];else if(arg==L"--max-frames"&&i+1<argc)exportFrames=std::max(1,_wtoi(argv[++i]));else if(arg==L"--cancel-after-ms"&&i+1<argc)cancelAfterMs=std::clamp(_wtoi(argv[++i]),1,240000);else if(arg==L"--hevc")exportHevc=true;else if(arg==L"--smoke-rollback-flow"){smokeRollback=true;smokeRollbackFlow=true;}else if(arg==L"--smoke-rollback")smokeRollback=true;else if(arg==L"--smoke-ui")smokeUi=true;else if(arg==L"--no-fg")initialOptions.fg=false;else if(arg==L"--smoke-settings")smokeSettings=true;else if(arg==L"--smoke-controls")smokeControls=true;else if(arg==L"--smoke-save"&&i+1<argc)smokeSave=argv[++i];else if(arg==L"--native")initialOptions.realtime=false;else if(arg==L"--realtime")initialOptions.realtime=true;else if(arg==L"--fg")initialOptions.fg=true;else if(arg==L"--fg-multiplier"&&i+1<argc){initialOptions.fgMultiplier=std::clamp(_wtoi(argv[++i]),2,4);initialOptions.fg=true;}else if(arg==L"--sr")initialOptions.sr=true;else if(arg==L"--no-nr")initialOptions.nr=false;else if(arg==L"--nr")initialOptions.nr=true;else if(arg==L"--no-sr")initialOptions.sr=false;else autoInput=arg;}LocalFree(argv);
engine.requestSettings(initialOptions.snapshot());
if(!exportOutput.empty()){std::atomic<bool> cancel{false},finished{false};std::thread cancelTimer;if(cancelAfterMs)cancelTimer=std::thread([&]{const auto start=GetTickCount64();while(!finished&&GetTickCount64()-start<cancelAfterMs)std::this_thread::sleep_for(std::chrono::milliseconds(5));if(!finished)cancel=true;});bool ok=veyra::engine::exportVideo(autoInput,exportOutput,initialOptions,exportHevc,cancel,[](double,const std::wstring& s){OutputDebugStringW(s.c_str());},exportFrames);finished=true;if(cancelTimer.joinable())cancelTimer.join();veyra::log::info("app",std::format("export result={}",ok));CoUninitialize();return ok?0:cancel?3:1;}
WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"VeyraApp";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&wc);
auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Veyra — 本地实验版",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,nullptr,instance,nullptr);if(!hwnd)return 1;CheckDlgButton(hwnd,Nr,initialOptions.nr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Sr,initialOptions.sr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Fg,initialOptions.fg?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Realtime,initialOptions.realtime?BST_CHECKED:BST_UNCHECKED);ShowWindow(hwnd,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(GetAncestor(msg.hwnd,GA_ROOT)==hwnd&&(msg.message==WM_KEYDOWN||msg.message==WM_KEYUP||msg.message==WM_SYSKEYDOWN)){
const bool key=msg.wParam==VK_F11||msg.wParam==VK_ESCAPE||msg.wParam==VK_SPACE||msg.wParam=='V'||(msg.wParam==VK_RETURN&&msg.message==WM_SYSKEYDOWN);if(key){SendMessageW(hwnd,msg.message,msg.wParam,msg.lParam);continue;}}
TranslateMessage(&msg);DispatchMessageW(&msg);}CoUninitialize();return int(msg.wParam);
}
