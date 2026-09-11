#include "RemotePlayPanel.h"
#include "Theme.h"
#include "veyra/Log.h"
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
enum {Host=1,Account,PairPin,Quality,CodecChoice,Pair,Connect,Cancel,Scan,Wake,LoginPin,SendPin,StatusText,Help,Bitrate,Forget,ViewOnly,Calibrate,DecodeChoice};
std::function<bool()> calibrate;
std::function<void(source::RemotePlayConnectDesc)> connect;
std::function<void(std::string)> login;
std::function<RemotePlayPanelStatus()> connectionStatus;std::function<void()> disconnect;bool watching=false;
std::jthread worker;std::atomic<bool> done=false;bool busy=false,closing=false;
struct Outcome {std::wstring message;std::vector<remoteplay::DiscoveredConsole> hosts;std::filesystem::path savedPath;};
std::mutex mutex;Outcome outcome;
struct SavedProfile {std::string host;std::filesystem::path path;};
std::vector<SavedProfile> profiles;std::filesystem::path currentProfile;
constexpr uint32_t bitrates[]={5000,10000,15000,20000,30000,50000,80000,100000};
auto profilePath(){return currentProfile;}
std::filesystem::path pathForHost(std::string_view host){
    for(const auto& profile:profiles)if(profile.host==host)return profile.path;
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))return {};
    std::string id;const auto* bytes=reinterpret_cast<const unsigned char*>(&guid);
    for(size_t i=0;i<sizeof(guid);++i)id+=std::format("{:02x}",bytes[i]);
    return runtime::localDataDirectory()/"remoteplay"/(id+".dat");
}
void loadSelection(){
    if(auto saved=remoteplay::loadProfile(currentProfile)){
        SetDlgItemTextW(window,Host,std::wstring(saved->host.begin(),saved->host.end()).c_str());
        auto id=remoteplay::accountIdToBase64(saved->credentials.accountId);SetDlgItemTextW(window,Account,std::wstring(id.begin(),id.end()).c_str());
        SendDlgItemMessageW(window,Quality,CB_SETCURSEL,(saved->video.height==1080?2:0)+(saved->video.fps==60?1:0),0);
        SendDlgItemMessageW(window,CodecChoice,CB_SETCURSEL,saved->video.codec==remoteplay::Codec::H265?1:0,0);
        const auto found=std::find(std::begin(bitrates),std::end(bitrates),saved->video.bitrateKbps);
        SendDlgItemMessageW(window,Bitrate,CB_SETCURSEL,found==std::end(bitrates)?2:found-std::begin(bitrates),0);
    }
}
void refreshProfiles(){
    profiles.clear();SendDlgItemMessageW(window,Host,CB_RESETCONTENT,0,0);
    auto add=[](const std::filesystem::path& path){if(auto saved=remoteplay::loadProfile(path))profiles.push_back({saved->host,path});};
    add(runtime::localDataDirectory()/"remoteplay-profile.dat");
    std::error_code ec;const auto directory=runtime::localDataDirectory()/"remoteplay";
    size_t scanned=0;
    for(std::filesystem::directory_iterator it(directory,ec),end;!ec&&it!=end&&profiles.size()<64&&scanned<256;it.increment(ec),++scanned){
        const auto id=it->path().stem().wstring();
        if(it->path().extension()==".dat"&&id.size()==32&&std::all_of(id.begin(),id.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))add(it->path());
    }
    int selected=-1;for(size_t i=0;i<profiles.size();++i){auto& p=profiles[i];SendDlgItemMessageW(window,Host,CB_ADDSTRING,0,LPARAM(std::wstring(p.host.begin(),p.host.end()).c_str()));if(p.path==currentProfile)selected=int(i);}
    if(selected<0&&!profiles.empty()){selected=0;currentProfile=profiles[0].path;}
    if(selected>=0){SendDlgItemMessageW(window,Host,CB_SETCURSEL,selected,0);loadSelection();}
    else currentProfile.clear();
}
std::wstring text(int id){auto h=GetDlgItem(window,id);std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.resize(wcslen(s.c_str()));return s;}
std::string ascii(int id){auto s=text(id);if(std::any_of(s.begin(),s.end(),[](wchar_t c){return c>127;}))return {};std::string result;result.reserve(s.size());for(auto c:s)result.push_back(static_cast<char>(c));return result;}
remoteplay::VideoProfile video(){
    remoteplay::VideoProfile p;const auto q=SendDlgItemMessageW(window,Quality,CB_GETCURSEL,0,0);
    p.width=q<2?1280:1920;p.height=q<2?720:1080;p.fps=(q==0||q==2)?30:60;
    p.codec=SendDlgItemMessageW(window,CodecChoice,CB_GETCURSEL,0,0)==1?remoteplay::Codec::H265:remoteplay::Codec::H264;const auto rate=SendDlgItemMessageW(window,Bitrate,CB_GETCURSEL,0,0);p.bitrateKbps=bitrates[std::clamp<int>(int(rate),0,7)];return p;
}
void buttons(){for(int id:{Pair,Connect,Scan,Wake,Host,Account,PairPin,Quality,CodecChoice,Bitrate,Forget})EnableWindow(GetDlgItem(window,id),!busy);EnableWindow(GetDlgItem(window,Cancel),busy);}
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
    move(Host,160,18,270,180);move(Scan,440,18,112,28);
    move(Account,160,62,392,28);move(PairPin,160,106,180,28);move(Pair,352,106,200,30);
    move(Quality,160,150,220,150);move(CodecChoice,392,150,160,150);
    move(Connect,160,199,180,36);move(Wake,352,199,200,36);
    move(LoginPin,160,250,180,28);move(SendPin,352,250,200,30);
    move(Bitrate,160,295,180,180);move(Forget,352,295,80,32);move(Cancel,440,295,112,32);move(StatusText,20,341,530,64);move(ViewOnly,20,408,400,28);move(Calibrate,432,408,120,28);move(DecodeChoice,160,442,392,150);move(Help,20,485,530,86);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;closing=false;font=makeFont(h);titleTheme(h);
    auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x=0,int y=0,int w=1,int height=1){
        auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,dip(h,x),dip(h,y),dip(h,w),dip(h,height),h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);return c;
    };
    add(L"BUTTON",L"仅观看（手柄连接 PS5；更改后需重新连接）",ViewOnly,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"BUTTON",L"校准陀螺仪",Calibrate,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"解码（重连生效）",106,0,20,445,138,25);
    add(L"COMBOBOX",L"",DecodeChoice,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(auto label:{L"自动 · 优先硬解",L"CPU 软件解码",L"D3D12VA 硬件解码"})SendDlgItemMessageW(h,DecodeChoice,CB_ADDSTRING,0,LPARAM(label));
    SendDlgItemMessageW(h,DecodeChoice,CB_SETCURSEL,std::min(2u,GetPrivateProfileIntW(L"RemotePlay",L"DecodeMode",0,(runtime::localDataDirectory()/"veyra.ini").c_str())),0);
    add(L"STATIC",L"PS5 地址",100,0,20,21,130,25);add(L"STATIC",L"PSN Account ID",101,0,20,65,130,25);
    add(L"STATIC",L"8 位配对码",102,0,20,109,130,25);add(L"STATIC",L"串流格式",103,0,20,153,130,25);add(L"STATIC",L"登录 PIN（可选）",104,0,20,253,135,25);
    add(L"STATIC",L"码率",105,0,20,298,130,25);add(L"COMBOBOX",L"",Host,CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_TABSTOP);SendDlgItemMessageW(h,Host,CB_LIMITTEXT,253,0);
    for(int id:{Account,PairPin,LoginPin}){add(L"EDIT",L"",id,WS_TABSTOP|ES_AUTOHSCROLL|((id==PairPin||id==LoginPin)?ES_PASSWORD:0));SendDlgItemMessageW(h,id,EM_SETLIMITTEXT,id==Host?253:id==Account?24:8,0);}
    add(L"COMBOBOX",L"",Quality,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"720p · 30 fps",L"720p · 60 fps",L"1080p · 30 fps",L"1080p · 60 fps"})SendDlgItemMessageW(h,Quality,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,Quality,CB_SETCURSEL,3,0);
    add(L"COMBOBOX",L"",CodecChoice,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"H.264 · SDR",L"H.265 · SDR"})SendDlgItemMessageW(h,CodecChoice,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,CodecChoice,CB_SETCURSEL,0,0);
    add(L"COMBOBOX",L"",Bitrate,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto rate:bitrates){auto label=std::format(L"{} Mbps",rate/1000);SendDlgItemMessageW(h,Bitrate,CB_ADDSTRING,0,LPARAM(label.c_str()));}SendDlgItemMessageW(h,Bitrate,CB_SETCURSEL,2,0);
    add(L"BUTTON",L"删除配对",Forget,BS_PUSHBUTTON|WS_TABSTOP);
    for(auto [id,label]:{std::pair{Pair,L"配对并保存"},{Connect,L"连接并观看"},{Cancel,L"取消操作"},{Scan,L"查找主机"},{Wake,L"唤醒已配对 PS5"},{SendPin,L"提交登录 PIN"}})add(L"BUTTON",label,id,BS_PUSHBUTTON|WS_TABSTOP);
    marked(GetDlgItem(h,Connect));
    add(L"STATIC",L"首次使用先配对，之后直接连接。",StatusText,0);
    add(L"STATIC",L"PS5：设置 → 系统 → 远程游玩 → 启用远程游玩 → 关联设备。\nAccount ID 填账号数字 ID 或对应 8 字节 Base64，不是昵称或密码。\n电脑与 PS5 先连接同一局域网。配对信息仅在本机加密保存。\n登录 PIN 仅在 PS5 提示时填写；取消操作不会关闭当前视频。",Help,0);
    refreshProfiles();
    buttons();arrange();SetTimer(h,1,100,nullptr);return 0;}
case WM_TIMER:if(busy&&done){if(worker.joinable())worker.join();busy=false;Outcome result;{std::lock_guard lock(mutex);result=std::move(outcome);}if(!result.savedPath.empty()){currentProfile=result.savedPath;refreshProfiles();}buttons();SetDlgItemTextW(h,StatusText,result.message.c_str());if(!result.hosts.empty())SetDlgItemTextW(h,Host,std::wstring(result.hosts[0].host.begin(),result.hosts[0].host.end()).c_str());if(closing)DestroyWindow(h);}
    if(window&&watching&&!busy){auto state=connectionStatus();SetDlgItemTextW(h,StatusText,state.message.c_str());SetDlgItemTextW(h,Cancel,state.active?L"断开连接":L"取消操作");EnableWindow(GetDlgItem(h,Cancel),state.active);if(!state.active)watching=false;}return 0;
case WM_COMMAND:switch(LOWORD(wp)){
    case Calibrate:if(calibrate&&calibrate())MessageBoxW(h,L"将手柄平放并保持静止，关闭此提示后返回播放器。\n采集 120 个稳定样本完成校准；10 秒内不稳定则保留原校准。",L"陀螺仪校准",MB_OK);else MessageBoxW(h,L"请先连接串流及带陀螺仪的电脑手柄。仅观看模式不使用电脑手柄。",L"无法校准",MB_OK);break;
    case Host:if(HIWORD(wp)==CBN_SELCHANGE&&!busy){const auto index=SendDlgItemMessageW(h,Host,CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<profiles.size()){currentProfile=profiles[size_t(index)].path;loadSelection();}}break;
    case Forget:if(!busy&&!currentProfile.empty()){std::error_code ec;const bool removed=std::filesystem::remove(currentProfile,ec);if(removed){currentProfile.clear();refreshProfiles();if(profiles.empty()){SetDlgItemTextW(h,Account,L"");SetDlgItemTextW(h,Host,L"");}SetDlgItemTextW(h,StatusText,L"配对已删除。现有串流会保留；下次连接需选择其他主机或重新配对。");}else SetDlgItemTextW(h,StatusText,L"删除失败，请检查文件权限。");}break;
    case Pair:{if(busy)break;auto host=ascii(Host),id=ascii(Account),pin=ascii(PairPin);auto account=remoteplay::accountIdFromBase64(id);if(!account)account=remoteplay::accountIdFromDecimal(id);
        if(!remoteplay::validHost(host)||!account||!remoteplay::parsePairingPin(pin)){SetDlgItemTextW(h,StatusText,L"请填写有效主机地址、Account ID 和 8 位配对码。");break;}
        auto targetPath=pathForHost(host);if(targetPath.empty()){SetDlgItemTextW(h,StatusText,L"无法分配配对存档，请重试。");break;}auto format=video();SetDlgItemTextW(h,PairPin,L"");
        launch([host=std::move(host),account=*account,pin=std::move(pin),format,targetPath](std::stop_token stop)mutable{
            auto result=remoteplay::pairLocalPs5(host,account,pin,stop);SecureZeroMemory(pin.data(),pin.size());Outcome out;
            if(result.result.ok&&!stop.stop_requested()){remoteplay::NativeConnectRequest request;request.host=host;request.video=format;request.credentials=std::move(result.credentials);if(remoteplay::saveProfile(targetPath,request)){out.savedPath=targetPath;out.message=L"配对成功并已加密保存，可以连接。";}else out.message=L"配对成功，但保存失败。请检查目录权限后重试。";}
            else out.message=result.canceled||stop.stop_requested()?L"已取消配对":std::format(L"配对失败（{}），检查 PS5 配对码、Account ID 与网络。",result.result.code);return out;});break;}
    case Connect:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());auto host=ascii(Host);if(!saved||!remoteplay::validHost(host)){SetDlgItemTextW(h,StatusText,L"请先完成配对，并填写有效主机地址。");break;}saved->host=host;saved->video=video();if(!remoteplay::saveProfile(currentProfile,*saved)){SetDlgItemTextW(h,StatusText,L"保存连接设置失败，请检查目录权限。");break;}source::RemotePlayConnectDesc desc;saved->viewOnly=IsDlgButtonChecked(h,ViewOnly)==BST_CHECKED;desc.request=std::move(*saved);const auto decode=std::clamp<int>(int(SendDlgItemMessageW(h,DecodeChoice,CB_GETCURSEL,0,0)),0,2);desc.decodeMode=static_cast<source::RemotePlayConnectDesc::DecodeMode>(decode);WritePrivateProfileStringW(L"RemotePlay",L"DecodeMode",std::to_wstring(decode).c_str(),(runtime::localDataDirectory()/"veyra.ini").c_str());connect(std::move(desc));watching=true;EnableWindow(GetDlgItem(h,Cancel),TRUE);SetDlgItemTextW(h,StatusText,L"连接已开始。需要登录 PIN 时在下方提交。关闭面板不停止串流。");break;}
    case Cancel:if(busy&&worker.joinable())worker.request_stop();else if(watching)disconnect();break;
    case Scan:launch([](std::stop_token stop){Outcome out;auto report=remoteplay::discoverLocalPs5(stop);for(const auto& line:report.diagnostics)veyra::log::info("remoteplay-discovery",line);out.hosts=std::move(report.hosts);out.message=stop.stop_requested()?L"已取消查找":out.hosts.empty()?(report.error?std::format(L"主机搜索发生错误（{}），详情见日志；可手填 IP。",report.error):L"搜索完成，未收到 PS5 回应；可手填 IP，检查主机开机和局域网。"):L"已填入发现的 PS5 地址。多台主机可手工输入目标 IP。";return out;});break;
    case Wake:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());if(!saved){SetDlgItemTextW(h,StatusText,L"需要先配对才能唤醒。");break;}saved->host=ascii(Host);launch([request=std::move(*saved)](std::stop_token stop){Outcome out;if(stop.stop_requested()){out.message=L"已取消唤醒";return out;}auto r=remoteplay::wakeLocalPs5(request);out.message=r.ok?L"已发送唤醒请求，等待 PS5 启动后点击连接。":std::format(L"唤醒请求失败（{}）",r.code);return out;});break;}
    case SendPin:{auto pin=ascii(LoginPin);if(pin.empty()||!std::all_of(pin.begin(),pin.end(),[](char c){return c>='0'&&c<='9';})){SetDlgItemTextW(h,StatusText,L"登录 PIN 必须为数字。");break;}login(std::move(pin));SetDlgItemTextW(h,LoginPin,L"");break;}
    }return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_CLOSE:if(busy){closing=true;worker.request_stop();SetDlgItemTextW(h,StatusText,L"正在取消并清理连接…");}else DestroyWindow(h);return 0;
case WM_DESTROY:if(worker.joinable()){worker.request_stop();worker.join();}KillTimer(h,1);DeleteObject(font);window=nullptr;busy=false;return 0;
case WM_SIZE:arrange();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showRemotePlayPanel(HWND parent,std::function<void(source::RemotePlayConnectDesc)> start,std::function<void(std::string)> pin,std::function<RemotePlayPanelStatus()> status,std::function<void()> stop,std::function<bool()> calibration){
    calibrate=std::move(calibration);
    connect=std::move(start);login=std::move(pin);connectionStatus=std::move(status);disconnect=std::move(stop);if(window){SetForegroundWindow(window);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraRemotePlaySetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"PS5 · Remote Play",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,590),dip(parent,625),parent,nullptr,wc.hInstance,nullptr);
}
}
