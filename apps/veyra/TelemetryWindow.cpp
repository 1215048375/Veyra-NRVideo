#include "TelemetryWindow.h"
#include "veyra/Log.h"
#include <shellapi.h>
#include <filesystem>
#include "DpiWindow.h"
#include <sstream>
#include <iomanip>
namespace veyra::ui {
namespace {
unsigned windowDpi=96;HWND window=nullptr;engine::EngineController* engine=nullptr;HFONT font=nullptr;std::wstring preview;
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
std::wstring value(std::optional<double> v){if(!v)return L"未能测量";std::wostringstream o;o<<std::fixed<<std::setprecision(3)<<*v;return o.str();}
void refresh(){auto s=engine->snapshot();const wchar_t* names[]={L"上传/颜色转换",L"DLSS SR",L"NVOF GPU队列区间（含同步）",L"DLSS5 NR",L"NR变化量合成",L"DLSSG 子帧1",L"DLSSG 子帧2",L"DLSSG 子帧3",L"DLSSG批次",L"最终blit"};std::wostringstream o;
    o<<L"GPU时间戳（毫秒）；未执行不记作0。样本帧 "<<s.metrics.identity.sourceFrameId<<L" / epoch "<<s.metrics.identity.epoch<<L" / 设置版本 "<<s.metrics.identity.settingsRevision<<L"\r\n";
    for(size_t i=0;i<s.metrics.gpu.size();++i){const auto& g=s.metrics.gpu[i];o<<names[i]<<L"："<<(g.state==diagnostics::SampleState::NotExecuted?L"未执行":g.state==diagnostics::SampleState::Pending?L"待GPU完成":value(g.milliseconds))<<L"\r\n";}
    o<<L"\r\nCPU与调度（毫秒，独立于GPU）：取帧 "<<value(s.metrics.decodeCpuMs)<<L" / 图提交 "<<value(s.metrics.submitCpuMs)<<L" / GPU就绪等待 "<<value(s.metrics.gpuWaitCpuMs)<<L"\r\n截止时间等待 "<<value(s.metrics.deadlineWaitCpuMs)<<L" / Present调用 "<<value(s.metrics.presentCpuMs)<<L"\r\n源帧 "<<s.metrics.sourceFrames<<L" / 有效生成 "<<s.metrics.validGenerated<<L" / 实际提交 "<<s.metrics.submitted<<L" / 过期补帧 "<<s.metrics.expired<<L"\r\n当前批次容量 "<<s.metrics.queueWatermark<<L"（上限4）；采集入口容量1；采集丢弃 "<<s.captureDropped<<L"\r\n实际提交频率（最近1秒观察）："<<value(s.submissionFps)<<L"fps；实际显示扫描率/光子延迟：未测\r\n";
    const auto flowName=s.applied.flow==engine::FlowQuality::Performance?L"性能":s.applied.flow==engine::FlowQuality::Balanced?L"平衡":L"质量";
    o<<L"光流请求："<<flowName<<L"；实际SDK perf="<<s.flowPerf<<L"；grid=4（已验证SDK能力）\r\n内容节奏："<<(s.contentFps?std::to_wstring(s.contentFps)+L"fps":L"未确认（静态或证据不足）")<<L"；保留源时间戳，重复内容不计有效生成\r\n"<<s.status;
    SetDlgItemTextW(window,1,o.str().c_str());
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){case WM_CREATE:{preview.clear();window=h;auto dpi=GetDpiForWindow(h);windowDpi=dpi;font=CreateFontW(-MulDiv(14,dpi,96),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto add=[&](const wchar_t* c,const wchar_t* t,int id,DWORD style,int y,int height){auto child=CreateWindowExW(WS_EX_CLIENTEDGE,c,t,WS_CHILD|WS_VISIBLE|style,10,MulDiv(y,dpi,96),MulDiv(820,dpi,96),MulDiv(height,dpi,96),h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);};
    add(L"EDIT",L"",1,ES_MULTILINE|ES_READONLY|WS_VSCROLL,10,340);add(L"BUTTON",L"查看 / 刷新脱敏诊断预览",3,BS_PUSHBUTTON,360,28);add(L"EDIT",L"点击上方按钮查看诊断。复制只会复制这里预览过的脱敏文字。",2,ES_MULTILINE|ES_READONLY|WS_VSCROLL,398,150);add(L"BUTTON",L"复制上方脱敏预览",4,BS_PUSHBUTTON,558,28);add(L"BUTTON",L"打开本应用日志目录",5,BS_PUSHBUTTON,596,28);SetTimer(h,1,250,nullptr);refresh();return 0;}
case WM_TIMER:refresh();return 0;
case WM_COMMAND:if(LOWORD(w)==3){preview=wide(Logger::instance().diagnosticReport());SetDlgItemTextW(h,2,preview.c_str());}else if(LOWORD(w)==4&&!preview.empty()){
    if(OpenClipboard(h)){HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(preview.size()+1)*sizeof(wchar_t));if(data){if(void* p=GlobalLock(data)){memcpy(p,preview.c_str(),(preview.size()+1)*sizeof(wchar_t));GlobalUnlock(data);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data))GlobalFree(data);}else GlobalFree(data);}CloseClipboard();}
}else if(LOWORD(w)==5)ShellExecuteW(h,L"open",(std::filesystem::path(VEYRA_PROJECT_ROOT)/"logs").c_str(),nullptr,nullptr,SW_SHOW);return 0;
case WM_DPICHANGED:changeDpi(h,w,l,windowDpi,font);return 0;
case WM_CLOSE:DestroyWindow(h);return 0;case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;return 0;}return DefWindowProcW(h,m,w,l);}
}
void showTelemetry(HWND parent,engine::EngineController& controller){engine=&controller;if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraTelemetry";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&wc);auto dpi=GetDpiForWindow(parent);CreateWindowExW(0,wc.lpszClassName,L"性能与诊断中心",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(860,dpi,96),MulDiv(675,dpi,96),parent,nullptr,wc.hInstance,nullptr);}
}
