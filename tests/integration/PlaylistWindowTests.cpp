#include "../../apps/veyra/ui/PlaylistWindow.h"
#include "../../apps/veyra/ui/Theme.h"
#include <iostream>
#include <stdexcept>

int main() {
    ULONG_PTR graphics=0;Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphics,&input,nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    HWND owner=CreateWindowExW(0,L"STATIC",L"Playlist test owner",WS_OVERLAPPEDWINDOW,0,0,600,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    unsigned checks=0;int result=0;
    auto require=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    try {
        using namespace veyra;
        for (UINT dpi : {96u,144u,192u}) {
            ui::smokeLayoutDpi=dpi;
            engine::Playlist queue;queue.add(L"C:/媒体/第一集.mp4");queue.add(L"C:/媒体/第二集.mkv");queue.add(L"C:/媒体/第三集.mov");
            int plays=0,recents=0;
            auto window=ui::showPlaylistWindow(owner,queue,[&](size_t index){++plays;queue.bind(index,100+plays);ui::refreshPlaylistWindow();},[&]{++recents;});
            require(window!=nullptr,"window created");
            ShowWindow(window,SW_HIDE);
            auto list=GetDlgItem(window,710);require(SendMessageW(list,LB_GETCOUNT,0,0)==3,"list populated");
            for (auto size : {SIZE{500,360},SIZE{640,480},SIZE{900,600}}) {
                SetWindowPos(window,nullptr,0,0,MulDiv(size.cx,dpi,96),MulDiv(size.cy,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
                RECT client{};GetClientRect(window,&client);
                std::vector<RECT> rects;
                for(int id=710;id<=721;++id) {
                    RECT rect{};GetWindowRect(GetDlgItem(window,id),&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
                    require(rect.left>=0&&rect.top>=0&&rect.right<=client.right&&rect.bottom<=client.bottom,"controls contained at all DPI/sizes");
                    for(auto previous:rects){RECT intersection{};require(!IntersectRect(&intersection,&rect,&previous),"controls do not overlap");}
                    rects.push_back(rect);
                }
            }
            SendMessageW(list,LB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(710,LBN_DBLCLK),LPARAM(list));
            require(plays==1&&queue.current==1,"double click invokes real panel callback");
            SendMessageW(window,WM_COMMAND,715,0);require(queue.current==0&&queue.entries[0].find(L"第二集")!=std::wstring::npos,"move up follows current");
            SendMessageW(window,WM_COMMAND,714,0);require(plays==2&&queue.current==1,"next invokes callback");
            SendMessageW(window,WM_COMMAND,713,0);require(plays==3&&queue.current==0,"previous invokes callback");
            SendDlgItemMessageW(window,719,CB_SETCURSEL,2,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(719,CBN_SELCHANGE),0);
            require(queue.mode==engine::PlaylistMode::RepeatOne,"mode selector updates policy");
            SendMessageW(window,WM_COMMAND,721,0);require(recents==1,"recent entry retained");
            SendMessageW(list,LB_SETCURSEL,0,0);MSG key{};key.hwnd=list;key.message=WM_KEYDOWN;key.wParam=VK_DELETE;
            require(ui::playlistWindowMessage(key)&&queue.entries.size()==2&&!queue.current,"Delete removes selected current without invoking playback");
            require(plays==3,"remove never starts another video");
            SendMessageW(window,WM_COMMAND,718,0);require(queue.entries.empty()&&SendMessageW(list,LB_GETCOUNT,0,0)==0,"clear updates view");
            DestroyWindow(window);
        }
        std::cout<<"PASS "<<checks<<" playlist window checks (96/144/192 DPI)\n";
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';result=1;}
    DestroyWindow(owner);veyra::ui::glassTextTheme().reset();Gdiplus::GdiplusShutdown(graphics);return result;
}


