#include "RemotePlayPanel.h"
#include "Theme.h"
#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/remoteplay/Discovery.h"
#include "veyra/RuntimePaths.h"
#include <atomic>
#include <format>
#include <mutex>
#include <thread>
namespace veyra::ui {
namespace {
HWND window=nullptr;HFONT font=nullptr;
enum {Host=1,Account,PairPin,Quality,CodecChoice,Pair,Connect,Cancel,Scan,Wake,LoginPin,SendPin,StatusText,Help};
std::function<void(source::RemotePlayConnectDesc)> connect;
std::function<void(std::string)> login;
std::jthread worker;std::atomic<bool> done=false;bool busy=false,closing=false;
struct Outcome {std::wstring message;std::vector<remoteplay::DiscoveredConsole> hosts;};
std::mutex mutex;Outcome outcome;
auto profilePath(){return runtime::localDataDirectory()/"remoteplay-profile.dat";}
std::wstring text(int id){auto h=GetDlgItem(window,id);std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.resize(wcslen(s.c_str()));return s;}
std::string ascii(int id){auto s=text(id);if(std::any_of(s.begin(),s.end(),[](wchar_t c){return c>127;}))return {};std::string result;result.reserve(s.size());for(auto c:s)result.push_back(static_cast<char>(c));return result;}
remoteplay::VideoProfile video(){
    remoteplay::VideoProfile p;const auto q=SendDlgItemMessageW(window,Quality,CB_GETCURSEL,0,0);
    p.width=q<2?1280:1920;p.height=q<2?720:1080;p.fps=(q==0||q==2)?30:60;
    p.codec=SendDlgItemMessageW(window,CodecChoice,CB_GETCURSEL,0,0)==1?remoteplay::Codec::H265:remoteplay::Codec::H264;return p;
}
void buttons(){for(int id:{Pair,Connect,Scan,Wake,Host,Account,PairPin,Quality,CodecChoice})EnableWindow(GetDlgItem(window,id),!busy);EnableWindow(GetDlgItem(window,Cancel),busy);}
template<class Work> void launch(Work work){
    if(busy)return;if(worker.joinable())worker.join();busy=true;done=false;buttons();SetDlgItemTextW(window,StatusText,L"正在处理…可随时取消");
    worker=std::jthread([work=std::move(work)](std::stop_token stop)mutable{
        Outcome result;
        try{result=work(stop);}catch(...){result.message=L"操作失败；请检查网络、输入和可用磁盘空间。";}
        {std::lock_guard lock(mutex);outcome=std::move(result);}done=true;
    });
}
void arrange(){
    auto move=[](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,y),dip(window,w),dip(window,h),TRUE);};
    move(Host,160,18,270,28);move(Scan,440,18,112,28);
    move(Account,160,62,392,28);move(PairPin,160,106,180,28);move(Pair,352,106,200,30);
    move(Quality,160,150,220,150);move(CodecChoice,392,150,160,150);
    move(Connect,160,199,180,36);move(Wake,352,199,200,36);
    move(LoginPin,160,250,180,28);move(SendPin,352,250,200,30);
    move(Cancel,440,295,112,32);move(StatusText,20,341,530,64);move(Help,20,414,530,116);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;closing=false;font=makeFont(h);titleTheme(h);
    auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x=0,int y=0,int w=1,int height=1){
        auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,dip(h,x),dip(h,y),dip(h,w),dip(h,height),h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);return c;
    };
    add(L"STATIC",L"PS5 地址",100,0,20,21,130,25);add(L"STATIC",L"PSN Account ID",101,0,20,65,130,25);
    add(L"STATIC",L"8 位配对码",102,0,20,109,130,25);add(L"STATIC",L"串流格式",103,0,20,153,130,25);add(L"STATIC",L"登录 PIN（可选）",104,0,20,253,135,25);
    for(int id:{Host,Account,PairPin,LoginPin}){add(L"EDIT",L"",id,WS_TABSTOP|ES_AUTOHSCROLL|((id==PairPin||id==LoginPin)?ES_PASSWORD:0));SendDlgItemMessageW(h,id,EM_SETLIMITTEXT,id==Host?253:id==Account?24:8,0);}
    add(L"COMBOBOX",L"",Quality,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"720p · 30 fps",L"720p · 60 fps",L"1080p · 30 fps",L"1080p · 60 fps"})SendDlgItemMessageW(h,Quality,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,Quality,CB_SETCURSEL,3,0);
    add(L"COMBOBOX",L"",CodecChoice,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"H.264 · SDR",L"H.265 · SDR"})SendDlgItemMessageW(h,CodecChoice,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,CodecChoice,CB_SETCURSEL,0,0);
    for(auto [id,label]:{std::pair{Pair,L"配对并保存"},{Connect,L"连接并观看"},{Cancel,L"取消操作"},{Scan,L"查找主机"},{Wake,L"唤醒已配对 PS5"},{SendPin,L"提交登录 PIN"}})add(L"BUTTON",label,id,BS_PUSHBUTTON|WS_TABSTOP);
    marked(GetDlgItem(h,Connect));
    add(L"STATIC",L"首次使用先配对，之后直接连接。",StatusText,0);
    add(L"STATIC",L"PS5：设置 → 系统 → 远程游玩 → 启用远程游玩 → 关联设备。\nAccount ID 填账号数字 ID 或对应 8 字节 Base64，不是昵称或密码。\n电脑与 PS5 先连接同一局域网。配对信息仅在本机加密保存。\n登录 PIN 仅在 PS5 提示时填写；取消操作不会关闭当前视频。",Help,0);
    if(auto saved=remoteplay::loadProfile(profilePath())){SetDlgItemTextW(h,Host,std::wstring(saved->host.begin(),saved->host.end()).c_str());auto id=remoteplay::accountIdToBase64(saved->credentials.accountId);SetDlgItemTextW(h,Account,std::wstring(id.begin(),id.end()).c_str());SendDlgItemMessageW(h,Quality,CB_SETCURSEL,(saved->video.height==1080?2:0)+(saved->video.fps==60?1:0),0);SendDlgItemMessageW(h,CodecChoice,CB_SETCURSEL,saved->video.codec==remoteplay::Codec::H265?1:0,0);}
    buttons();arrange();SetTimer(h,1,100,nullptr);return 0;}
case WM_TIMER:if(busy&&done){if(worker.joinable())worker.join();busy=false;Outcome result;{std::lock_guard lock(mutex);result=std::move(outcome);}buttons();SetDlgItemTextW(h,StatusText,result.message.c_str());if(!result.hosts.empty())SetDlgItemTextW(h,Host,std::wstring(result.hosts[0].host.begin(),result.hosts[0].host.end()).c_str());if(closing)DestroyWindow(h);}return 0;
case WM_COMMAND:switch(LOWORD(wp)){
    case Pair:{if(busy)break;auto host=ascii(Host),id=ascii(Account),pin=ascii(PairPin);auto account=remoteplay::accountIdFromBase64(id);if(!account)account=remoteplay::accountIdFromDecimal(id);
        if(!remoteplay::validHost(host)||!account||!remoteplay::parsePairingPin(pin)){SetDlgItemTextW(h,StatusText,L"请填写有效主机地址、Account ID 和 8 位配对码。");break;}
        auto format=video();SetDlgItemTextW(h,PairPin,L"");
        launch([host=std::move(host),account=*account,pin=std::move(pin),format](std::stop_token stop)mutable{
            auto result=remoteplay::pairLocalPs5(host,account,pin,stop);SecureZeroMemory(pin.data(),pin.size());Outcome out;
            if(result.result.ok&&!stop.stop_requested()){remoteplay::NativeConnectRequest request;request.host=host;request.video=format;request.credentials=std::move(result.credentials);out.message=remoteplay::saveProfile(profilePath(),request)?L"配对成功并已加密保存，可以连接。":L"配对成功，但保存失败。请检查目录权限后重试。";}
            else out.message=result.canceled||stop.stop_requested()?L"已取消配对":std::format(L"配对失败（{}），检查 PS5 配对码、Account ID 与网络。",result.result.code);return out;});break;}
    case Connect:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());auto host=ascii(Host);if(!saved||!remoteplay::validHost(host)){SetDlgItemTextW(h,StatusText,L"请先完成配对，并填写有效主机地址。");break;}saved->host=host;saved->video=video();source::RemotePlayConnectDesc desc;desc.request=std::move(*saved);connect(std::move(desc));SetDlgItemTextW(h,StatusText,L"连接已开始。需要登录 PIN 时在下方提交。关闭面板不停止串流。");break;}
    case Cancel:if(worker.joinable())worker.request_stop();break;
    case Scan:launch([](std::stop_token stop){Outcome out;out.hosts=remoteplay::discoverLocalPs5(stop);out.message=stop.stop_requested()?L"已取消查找":out.hosts.empty()?L"未找到 PS5，可手工填写主机 IP；请检查防火墙和局域网。":L"已填入发现的 PS5 地址。多台主机可手工输入目标 IP。";return out;});break;
    case Wake:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());if(!saved){SetDlgItemTextW(h,StatusText,L"需要先配对才能唤醒。");break;}saved->host=ascii(Host);launch([request=std::move(*saved)](std::stop_token stop){Outcome out;if(stop.stop_requested()){out.message=L"已取消唤醒";return out;}auto r=remoteplay::wakeLocalPs5(request);out.message=r.ok?L"已发送唤醒请求，等待 PS5 启动后点击连接。":std::format(L"唤醒请求失败（{}）",r.code);return out;});break;}
    case SendPin:{auto pin=ascii(LoginPin);if(pin.empty()||!std::all_of(pin.begin(),pin.end(),[](char c){return c>='0'&&c<='9';})){SetDlgItemTextW(h,StatusText,L"登录 PIN 必须为数字。");break;}login(std::move(pin));SetDlgItemTextW(h,LoginPin,L"");break;}
    }return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_CLOSE:if(busy){closing=true;worker.request_stop();SetDlgItemTextW(h,StatusText,L"正在取消并清理连接…");}else DestroyWindow(h);return 0;
case WM_DESTROY:if(worker.joinable()){worker.request_stop();worker.join();}KillTimer(h,1);DeleteObject(font);window=nullptr;busy=false;return 0;
case WM_SIZE:arrange();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showRemotePlayPanel(HWND parent,std::function<void(source::RemotePlayConnectDesc)> start,std::function<void(std::string)> pin){
    connect=std::move(start);login=std::move(pin);if(window){SetForegroundWindow(window);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraRemotePlaySetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"PS5 · Remote Play",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,590),dip(parent,580),parent,nullptr,wc.hInstance,nullptr);
}
}
