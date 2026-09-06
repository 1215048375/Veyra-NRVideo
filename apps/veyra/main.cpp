#include <windows.h>
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
enum {Open=101,Play,Stop,Save,Nr,Sr,Fg,Seek,Info,Capture,Export,Realtime,Recent};
veyra::engine::EngineController engine;
HWND mainWindow=nullptr,video=nullptr,statusBar=nullptr,seekBar=nullptr;
std::wstring currentFile,autoInput;bool paused=false,dragging=false;int smokeSeconds=0;ULONGLONG startTick=0;int resultCode=0;
std::wstring exportOutput;unsigned exportFrames=0;unsigned cancelAfterMs=0;bool exportHevc=false;veyra::engine::PlayerOptions initialOptions;
std::vector<veyra::engine::SubtitleCue> subtitles;HWND subtitleLabel=nullptr;
HFONT font=nullptr;bool smokeControls=false;int smokeStep=0;std::wstring smokeSave;
HWND captureWindow=nullptr,captureDevice=nullptr,captureFormat=nullptr,captureAudio=nullptr;
std::vector<veyra::source::CaptureFormat> captureFormats;
void openFile(const std::wstring&);
void refreshFormats(){SendMessageW(captureFormat,CB_RESETCONTENT,0,0);int i=int(SendMessageW(captureDevice,CB_GETCURSEL,0,0));captureFormats=i>=0?veyra::source::CaptureCardSource::formats(i):std::vector<veyra::source::CaptureFormat>{};for(auto& f:captureFormats)SendMessageW(captureFormat,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(f.label.c_str()));if(!captureFormats.empty())SendMessageW(captureFormat,CB_SETCURSEL,0,0);}
LRESULT CALLBACK captureProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_CREATE){auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style,int y){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,12,y,510,200,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);};
captureDevice=add(L"COMBOBOX",L"",1,CBS_DROPDOWNLIST|WS_VSCROLL,15);captureFormat=add(L"COMBOBOX",L"",2,CBS_DROPDOWNLIST|WS_VSCROLL,55);captureAudio=add(L"COMBOBOX",L"",3,CBS_DROPDOWNLIST|WS_VSCROLL,95);
auto devices=veyra::source::CaptureCardSource::devices();for(auto& name:devices)SendMessageW(captureDevice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(!devices.empty())SendMessageW(captureDevice,CB_SETCURSEL,0,0);else SendMessageW(captureDevice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No capture device found — connect card and reopen"));
SendMessageW(captureAudio,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No audio (select HDMI input below to monitor audio)"));for(auto& name:veyra::source::CaptureCardSource::devices(true))SendMessageW(captureAudio,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));SendMessageW(captureAudio,CB_SETCURSEL,0,0);refreshFormats();
CreateWindowExW(0,L"STATIC",L"Device / actual format / HDMI audio. NR, SR and FG use the main window settings. Hardware acceptance is pending your test. FG needs one arriving source frame of lookahead.",WS_CHILD|WS_VISIBLE,12,140,510,64,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
CreateWindowExW(0,L"BUTTON",L"Start capture",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,12,211,180,32,hwnd,reinterpret_cast<HMENU>(4),GetModuleHandleW(nullptr),nullptr);return 0;}
if(msg==WM_COMMAND){if(LOWORD(wp)==1&&HIWORD(wp)==CBN_SELCHANGE)refreshFormats();if(LOWORD(wp)==4){int d=int(SendMessageW(captureDevice,CB_GETCURSEL,0,0)),f=int(SendMessageW(captureFormat,CB_GETCURSEL,0,0)),a=int(SendMessageW(captureAudio,CB_GETCURSEL,0,0))-1;if(d>=0&&f>=0&&size_t(f)<captureFormats.size()){openFile(std::format(L"capture:{}:{}:{}",d,captureFormats[f].index,a));DestroyWindow(hwnd);}else MessageBoxW(hwnd,L"No supported 1080p/4K 30/60 format selected.",L"Capture",MB_OK);}return 0;}if(msg==WM_DESTROY){captureWindow=nullptr;return 0;}return DefWindowProcW(hwnd,msg,wp,lp);}
void showCapture(){if(captureWindow){SetForegroundWindow(captureWindow);return;}WNDCLASSW wc{};wc.lpfnWndProc=captureProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);captureWindow=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Capture card setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,550,295,mainWindow,nullptr,wc.hInstance,nullptr);}
veyra::engine::PlayerOptions options(){return {IsDlgButtonChecked(mainWindow,Nr)==BST_CHECKED,IsDlgButtonChecked(mainWindow,Sr)==BST_CHECKED,IsDlgButtonChecked(mainWindow,Fg)==BST_CHECKED,IsDlgButtonChecked(mainWindow,Realtime)==BST_CHECKED};}
std::wstring fileDialog(bool save){wchar_t name[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFile=name;ofn.nMaxFile=32768;
ofn.lpstrFilter=save?L"PNG image\0*.png\0JPEG image\0*.jpg\0":L"Video / image\0*.mp4;*.mkv;*.mov;*.avi;*.ts;*.png;*.jpg;*.jpeg\0All files\0*.*\0";
ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);ofn.lpstrDefExt=save?L"png":nullptr;
return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?name:L"";}
void openFile(const std::wstring& file){if(file.empty())return;currentFile=file;subtitles=veyra::engine::loadSrt(std::filesystem::path(file).replace_extension(L".srt").wstring());paused=false;SetWindowTextW(GetDlgItem(mainWindow,Play),L"Pause");engine.open(video,file,options());SetWindowTextW(mainWindow,(L"Veyra — "+std::filesystem::path(file).filename().wstring()).c_str());WritePrivateProfileStringW(L"Player",L"Recent",file.c_str(),(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/veyra.ini").wstring().c_str());}
HWND control(const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x,int y,int w,int h){auto hwnd=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return hwnd;}
void layout(){RECT r{};GetClientRect(mainWindow,&r);MoveWindow(video,8,52,std::max(1L,r.right-16),std::max(1L,r.bottom-160),TRUE);if(subtitleLabel)MoveWindow(subtitleLabel,8,std::max(56L,r.bottom-104),std::max(1L,r.right-16),32,TRUE);MoveWindow(seekBar,8,std::max(56L,r.bottom-71),std::max(1L,r.right-16),24,TRUE);MoveWindow(statusBar,12,std::max(80L,r.bottom-42),std::max(1L,r.right-24),36,TRUE);}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{mainWindow=hwnd;font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
control(L"BUTTON",L"Open video / image",Open,BS_PUSHBUTTON,10,10,150,30);control(L"BUTTON",L"Play",Play,BS_PUSHBUTTON,168,10,68,30);control(L"BUTTON",L"Stop",Stop,BS_PUSHBUTTON,244,10,62,30);control(L"BUTTON",L"Save image",Save,BS_PUSHBUTTON,314,10,95,30);
control(L"BUTTON",L"DLSS NR",Nr,BS_AUTOCHECKBOX,425,10,85,30);control(L"BUTTON",L"SR 4K",Sr,BS_AUTOCHECKBOX,518,10,78,30);control(L"BUTTON",L"FG 2X",Fg,BS_AUTOCHECKBOX,600,10,78,30);CheckDlgButton(hwnd,Nr,BST_CHECKED);
control(L"BUTTON",L"Capture",Capture,BS_PUSHBUTTON,690,10,75,30);control(L"BUTTON",L"Export video",Export,BS_PUSHBUTTON,773,10,103,30);control(L"BUTTON",L"Logs",Info,BS_PUSHBUTTON,884,10,60,30);
control(L"BUTTON",L"Live 1080",Realtime,BS_AUTOCHECKBOX,952,10,112,30);
control(L"BUTTON",L"Recent",Recent,BS_PUSHBUTTON,1072,10,75,30);
subtitleLabel=control(L"STATIC",L"",0,SS_CENTER,8,640,1100,32);
video=control(L"STATIC",L"",0,SS_BLACKRECT,8,52,1100,610);seekBar=control(TRACKBAR_CLASSW,L"",Seek,TBS_HORZ,8,675,1100,24);SendMessageW(seekBar,TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));statusBar=control(L"STATIC",L"Open a local video or PNG/JPEG. Experimental local runtime; not an official NVIDIA product.",0,SS_LEFT,12,708,1080,36);
DragAcceptFiles(hwnd,TRUE);SetTimer(hwnd,1,200,nullptr);layout();return 0;}
case WM_SIZE:layout();return 0;
case WM_DROPFILES:{wchar_t file[32768]{};DragQueryFileW(reinterpret_cast<HDROP>(wp),0,file,32768);DragFinish(reinterpret_cast<HDROP>(wp));openFile(file);return 0;}
case WM_COMMAND:switch(LOWORD(wp)){
case Open:openFile(fileDialog(false));break;
case Recent:{wchar_t recent[32768]{};GetPrivateProfileStringW(L"Player",L"Recent",L"",recent,32768,(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/veyra.ini").wstring().c_str());openFile(recent);break;}
case Play:{auto s=engine.snapshot();if(!currentFile.empty()&&(!s.running||(s.duration>0&&s.position>=s.duration-0.1))){openFile(currentFile);break;}paused=!paused;engine.pause(paused);SetWindowTextW(GetDlgItem(hwnd,Play),paused?L"Play":L"Pause");break;}
case Stop:engine.stop();break;
case Save:{auto name=fileDialog(true);if(!name.empty())engine.saveFrame(name);break;}
case Realtime:case Nr:case Sr:case Fg:{auto opts=options();auto ini=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/veyra.ini").wstring();WritePrivateProfileStringW(L"Player",L"NR",opts.nr?L"1":L"0",ini.c_str());WritePrivateProfileStringW(L"Player",L"SR",opts.sr?L"1":L"0",ini.c_str());WritePrivateProfileStringW(L"Player",L"FG",opts.fg?L"1":L"0",ini.c_str());WritePrivateProfileStringW(L"Player",L"Realtime",opts.realtime?L"1":L"0",ini.c_str());if(!currentFile.empty())openFile(currentFile);break;}
case Capture:showCapture();break;
case Export:{if(currentFile.empty())break;wchar_t name[32768]{};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=hwnd;d.lpstrFile=name;d.nMaxFile=32768;d.lpstrFilter=L"MP4 video\0*.mp4\0";d.lpstrDefExt=L"mp4";d.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(GetSaveFileNameW(&d)){int codec=MessageBoxW(hwnd,L"Use HEVC? Yes = HEVC, No = H.264. Current NR / SR / FG options apply. Existing files are never overwritten.",L"Video export",MB_YESNOCANCEL);if(codec!=IDCANCEL)engine.startExport(currentFile,name,options(),codec==IDYES);}break;}
case Info:ShellExecuteW(hwnd,L"open",(std::filesystem::path(VEYRA_PROJECT_ROOT)/"logs").wstring().c_str(),nullptr,nullptr,SW_SHOW);break;
}return 0;
case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==seekBar){dragging=LOWORD(wp)==TB_THUMBTRACK;auto s=engine.snapshot();if(!dragging&&s.duration>0)engine.seek(s.duration*SendMessageW(seekBar,TBM_GETPOS,0,0)/10000.0);}return 0;
case WM_KEYDOWN:if(wp==VK_SPACE){SendMessageW(hwnd,WM_COMMAND,Play,0);return 0;}if(wp==VK_F11){static WINDOWPLACEMENT old{sizeof(old)};static bool full=false;full=!full;if(full){GetWindowPlacement(hwnd,&old);SetWindowLongPtrW(hwnd,GWL_STYLE,WS_POPUP|WS_VISIBLE);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(hwnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtrW(hwnd,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetWindowPlacement(hwnd,&old);SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}return 0;}break;
case WM_TIMER:{if(!autoInput.empty()){auto file=autoInput;autoInput.clear();openFile(file);startTick=GetTickCount64();}if(smokeControls&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(smokeStep==0&&elapsed>2200){engine.pause(true);smokeStep=1;}
if(smokeStep==1&&elapsed>2600){engine.seek(1.0);smokeStep=2;}
if(smokeStep==2&&elapsed>3200){engine.pause(false);smokeStep=3;}
if(smokeStep==3&&elapsed>4100&&!smokeSave.empty()){engine.saveFrame(smokeSave);smokeStep=4;}
}auto s=engine.snapshot();SetWindowTextW(subtitleLabel,veyra::engine::subtitleAt(subtitles,s.position).c_str());auto text=std::format(L"{}\r\n{:.1f} / {:.1f}s   {:.1f} processed fps   lateness {:+.1f}ms   source {} / generated {}",s.status,s.position,s.duration,s.fps,s.lateMs,s.frames,s.generated);SetWindowTextW(statusBar,text.c_str());if(!dragging&&s.duration>0)SendMessageW(seekBar,TBM_SETPOS,TRUE,LPARAM(s.position/s.duration*10000));
if(smokeSeconds>0&&startTick&&GetTickCount64()-startTick>ULONGLONG(smokeSeconds)*1000){veyra::log::info("app",std::format("smoke frames={} generated={} failed={} latenessMs={:.2f} absLatenessP95Ms={:.2f} controlsStep={}",s.frames,s.generated,s.failed,s.lateMs,s.lateP95Ms,smokeStep));resultCode=s.frames>0&&!s.failed?0:1;PostMessageW(hwnd,WM_CLOSE,0,0);}return 0;}
case WM_CLOSE:KillTimer(hwnd,1);engine.stop();DestroyWindow(hwnd);return 0;
case WM_DESTROY:DeleteObject(font);PostQuitMessage(resultCode);return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
std::filesystem::create_directories(std::filesystem::path(VEYRA_PROJECT_ROOT)/"logs");veyra::Logger::instance().openFile((std::filesystem::path(VEYRA_PROJECT_ROOT)/"logs/veyra-app.log").wstring());
auto ini=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/veyra.ini").wstring();initialOptions.nr=GetPrivateProfileIntW(L"Player",L"NR",1,ini.c_str())!=0;initialOptions.sr=GetPrivateProfileIntW(L"Player",L"SR",0,ini.c_str())!=0;initialOptions.fg=GetPrivateProfileIntW(L"Player",L"FG",0,ini.c_str())!=0;initialOptions.realtime=GetPrivateProfileIntW(L"Player",L"Realtime",1,ini.c_str())!=0;
int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--smoke-seconds"&&i+1<argc)smokeSeconds=std::clamp(_wtoi(argv[++i]),1,240);else if(arg==L"--export-out"&&i+1<argc)exportOutput=argv[++i];else if(arg==L"--max-frames"&&i+1<argc)exportFrames=std::max(1,_wtoi(argv[++i]));else if(arg==L"--cancel-after-ms"&&i+1<argc)cancelAfterMs=std::clamp(_wtoi(argv[++i]),1,240000);else if(arg==L"--hevc")exportHevc=true;else if(arg==L"--smoke-controls")smokeControls=true;else if(arg==L"--smoke-save"&&i+1<argc)smokeSave=argv[++i];else if(arg==L"--native")initialOptions.realtime=false;else if(arg==L"--realtime")initialOptions.realtime=true;else if(arg==L"--fg")initialOptions.fg=true;else if(arg==L"--sr")initialOptions.sr=true;else if(arg==L"--no-nr")initialOptions.nr=false;else autoInput=arg;}LocalFree(argv);
if(!exportOutput.empty()){std::atomic<bool> cancel{false},finished{false};std::thread cancelTimer;if(cancelAfterMs)cancelTimer=std::thread([&]{const auto start=GetTickCount64();while(!finished&&GetTickCount64()-start<cancelAfterMs)std::this_thread::sleep_for(std::chrono::milliseconds(5));if(!finished)cancel=true;});bool ok=veyra::engine::exportVideo(autoInput,exportOutput,initialOptions,exportHevc,cancel,[](double,const std::wstring& s){OutputDebugStringW(s.c_str());},exportFrames);finished=true;if(cancelTimer.joinable())cancelTimer.join();veyra::log::info("app",std::format("export result={}",ok));CoUninitialize();return ok?0:cancel?3:1;}
WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"VeyraApp";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&wc);
auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Veyra — Local Experimental",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,nullptr,instance,nullptr);if(!hwnd)return 1;CheckDlgButton(hwnd,Nr,initialOptions.nr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Sr,initialOptions.sr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Fg,initialOptions.fg?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Realtime,initialOptions.realtime?BST_CHECKED:BST_UNCHECKED);ShowWindow(hwnd,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}CoUninitialize();return int(msg.wParam);
}
