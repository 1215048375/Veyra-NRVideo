#include "../../apps/veyra/ui/ImageBrowser.h"
#include "../../apps/veyra/ui/Theme.h"
#include "veyra/Log.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

int main(int argc,char** argv){
    using namespace veyra::ui;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Gdiplus::GdiplusStartup(&token,&input,nullptr);
    veyra::Logger::instance().setConsoleEnabled(false);
    auto folder=std::filesystem::path("out/playlist")/("thumbnails-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    auto first=folder/"0.png";
    {Gdiplus::Bitmap bitmap(640,640,PixelFormat32bppARGB);Gdiplus::Graphics g(&bitmap);g.Clear(Gdiplus::Color(255,30,70,100));Gdiplus::SolidBrush brush(Gdiplus::Color(255,255,180,20));g.FillEllipse(&brush,60,60,520,520);
    CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    if(bitmap.Save(first.c_str(),&png,nullptr)!=Gdiplus::Ok)return 1;}
    std::vector<std::wstring> files{std::filesystem::absolute(first).wstring()};
    for(int i=1;i<620;++i){auto file=folder/(std::to_string(i)+".png");std::filesystem::copy_file(first,file);files.push_back(std::filesystem::absolute(file).wstring());}
    auto owner=CreateWindowW(L"STATIC",L"test",WS_POPUP,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    auto browser=createImageBrowser(owner);loadImageBrowser(browser,files);
    bool passed=true;
    try{
        if(argc>1){
            smokeLayoutDpi=96;SetWindowPos(browser,nullptr,0,0,900,600,SWP_NOZORDER|SWP_NOACTIVATE);
            AlphaRaster colorCanvas;colorCanvas.create(nullptr,900,600);
            for(auto [name,expected]:{std::pair{"srgb.png",128},std::pair{"highlight.png",245},std::pair{"gamma3.png",99}}){
                loadImageBrowser(browser,{std::filesystem::absolute(std::filesystem::path(argv[1])/name).wstring()});
                auto deadline=GetTickCount64()+5000;COLORREF pixel=0;
                do{MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))DispatchMessageW(&msg);SendMessageW(browser,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(colorCanvas.dc),PRF_CLIENT);GdiFlush();pixel=GetPixel(colorCanvas.dc,90,114);
                    if(abs(int(GetRValue(pixel))-expected)<=3&&abs(int(GetGValue(pixel))-expected)<=3&&abs(int(GetBValue(pixel))-expected)<=3)break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }while(GetTickCount64()<deadline);
                std::cout<<"color "<<name<<" RGB="<<int(GetRValue(pixel))<<","<<int(GetGValue(pixel))<<","<<int(GetBValue(pixel))<<" expected="<<expected<<"\n";
                if(abs(int(GetRValue(pixel))-expected)>3||abs(int(GetGValue(pixel))-expected)>3||abs(int(GetBValue(pixel))-expected)>3)throw std::runtime_error("thumbnail color conversion failed");
            }
            loadImageBrowser(browser,files);
        }
        for(UINT dpi:{96u,192u}){
            smokeLayoutDpi=dpi;SetWindowPos(browser,nullptr,0,0,dip(browser,900),dip(browser,600),SWP_NOZORDER|SWP_NOACTIVATE);
            RECT r{};GetClientRect(browser,&r);AlphaRaster canvas;canvas.create(nullptr,r.right,r.bottom);
            auto ready=[&]{MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}SendMessageW(browser,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(canvas.dc),PRF_CLIENT);GdiFlush();
                // DWM glass consumes premultiplied BGRA, not just the RGB values
                // returned by GetPixel. The entire browser must be opaque.
                for(size_t i=0;i<size_t(canvas.width)*canvas.height;++i)if((canvas.pixels[i]>>24)!=255)throw std::runtime_error("browser output has transparent alpha under DWM glass");
                // All fully visible cells must contain image pixels, not a loading label.
                const int cols=r.right/dip(browser,180),rows=(r.bottom-dip(browser,42))/dip(browser,178);
                for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){auto color=GetPixel(canvas.dc,dip(browser,x*180+90),dip(browser,42+y*178+72));if(GetRValue(color)<220||GetGValue(color)<130||GetBValue(color)>70)return false;}return true;};
            auto await=[&]{auto end=GetTickCount64()+5000;while(!ready()){if(GetTickCount64()>end)throw std::runtime_error("visible thumbnail remained a placeholder");std::this_thread::sleep_for(std::chrono::milliseconds(10));}};
            await();for(int page=0;page<45;++page){SendMessageW(browser,WM_VSCROLL,SB_PAGEDOWN,0);await();}
            SendMessageW(browser,WM_VSCROLL,SB_TOP,0);await();
            std::cout<<"PASS 620 thumbnails, cache eviction, return to top, every visible tile has image pixels; DPI="<<dpi<<"\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";passed=false;}
    DestroyWindow(owner);Gdiplus::GdiplusShutdown(token);CoUninitialize();return passed?0:1;
}
