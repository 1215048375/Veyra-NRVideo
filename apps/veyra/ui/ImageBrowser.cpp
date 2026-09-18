#include "ImageBrowser.h"
#include "Theme.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <filesystem>
#include <memory>
#include <format>
#include "veyra/Log.h"
#pragma comment(lib,"windowscodecs.lib")
namespace veyra::ui { namespace {
constexpr UINT Ready=WM_APP+73;
struct Thumb{UINT width=0,height=0;std::vector<uint32_t> pixels;};
std::shared_ptr<Thumb> decode(const std::wstring& path){
    using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> convert;ComPtr<IWICBitmapFlipRotator> oriented;
    auto result=std::make_shared<Thumb>();UINT w=0,h=0;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||FAILED(decoder->GetFrame(0,&frame))||FAILED(frame->GetSize(&w,&h))||!w||!h)return result;
    double scale=std::min(1.0,320.0/std::max(w,h));UINT tw=std::max(1u,UINT(w*scale)),th=std::max(1u,UINT(h*scale));
    UINT orientation=1;ComPtr<IWICMetadataQueryReader> metadata;if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata))){PROPVARIANT value{};if(SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&value))&&value.vt==VT_UI2)orientation=value.uiVal;PropVariantClear(&value);}
    const WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
    if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(frame.Get(),tw,th,WICBitmapInterpolationModeFant))||FAILED(factory->CreateBitmapFlipRotator(&oriented))||FAILED(oriented->Initialize(scaler.Get(),transforms[orientation<=8?orientation:1])))return result;
    // Interpret embedded ICC/EXIF color space once, then hand GDI sRGB pixels.
    ComPtr<IWICColorTransform> color;ComPtr<IWICColorContext> destination;
    IWICBitmapSource* source=oriented.Get();UINT contexts=0;
    if(SUCCEEDED(frame->GetColorContexts(0,nullptr,&contexts))&&contexts>0&&contexts<=16){
        std::vector<ComPtr<IWICColorContext>> owned(contexts);std::vector<IWICColorContext*> pointers(contexts);bool allocated=true;
        for(UINT i=0;i<contexts;++i){if(FAILED(factory->CreateColorContext(&owned[i]))){allocated=false;break;}pointers[i]=owned[i].Get();}
        if(allocated&&SUCCEEDED(frame->GetColorContexts(contexts,pointers.data(),&contexts))&&SUCCEEDED(factory->CreateColorContext(&destination))&&SUCCEEDED(destination->InitializeFromExifColorSpace(1))&&SUCCEEDED(factory->CreateColorTransformer(&color))){
            bool transformed=false;for(auto& context:owned){WICColorContextType type=WICColorContextUninitialized;context->GetType(&type);if(type==WICColorContextUninitialized)continue;
                const auto hr=color->Initialize(oriented.Get(),context.Get(),destination.Get(),GUID_WICPixelFormat32bppBGRA);
                if(SUCCEEDED(hr)){source=color.Get();transformed=true;break;}
                veyra::log::warn("image-browser",std::format("color transform failed HRESULT=0x{:08X}; trying next profile",uint32_t(hr)));
            }
            if(transformed)veyra::log::info("image-browser","embedded color profile converted to sRGB");
        }
    }
    if(FAILED(factory->CreateFormatConverter(&convert))||FAILED(convert->Initialize(source,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))||FAILED(convert->GetSize(&result->width,&result->height)))return result;
    result->pixels.resize(size_t(result->width)*result->height);
    if(FAILED(convert->CopyPixels(nullptr,result->width*4,UINT(result->pixels.size()*4),reinterpret_cast<BYTE*>(result->pixels.data()))))result->pixels.clear();return result;
}
struct Browser{
    HWND hwnd;std::vector<std::wstring> files;size_t selected=0;int row=0,wheel=0,columns=1,rowsVisible=1;bool pressed=false;
    std::mutex mutex;std::condition_variable wake;bool quit=false;uint64_t generation=0;std::deque<size_t> jobs;
    std::map<size_t,std::shared_ptr<Thumb>> thumbs;std::map<size_t,uint64_t> used;uint64_t clock=0;size_t visibleFirst=0,visibleLast=0;std::thread worker;
    explicit Browser(HWND h):hwnd(h),worker([this]{run();}){}
    ~Browser(){{std::lock_guard lock(mutex);quit=true;}wake.notify_one();worker.join();}
    void run(){const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);for(;;){size_t index;uint64_t epoch;std::wstring path;
        {std::unique_lock lock(mutex);wake.wait(lock,[&]{return quit||!jobs.empty();});if(quit)break;index=jobs.front();jobs.pop_front();if(thumbs.contains(index))continue;epoch=generation;path=files[index];}
        std::shared_ptr<Thumb> thumb;try{thumb=SUCCEEDED(com)?decode(path):std::make_shared<Thumb>();}catch(const std::exception&){thumb=std::make_shared<Thumb>();}
        {std::lock_guard lock(mutex);if(quit)break;if(epoch!=generation||index<visibleFirst||index>=visibleLast)continue;if(thumbs.size()>=512&&!thumbs.contains(index)){auto victim=thumbs.end();uint64_t oldest=UINT64_MAX;for(auto it=thumbs.begin();it!=thumbs.end();++it)if((it->first<visibleFirst||it->first>=visibleLast)&&used[it->first]<oldest){victim=it;oldest=used[it->first];}if(victim!=thumbs.end()){used.erase(victim->first);thumbs.erase(victim);}else continue;}used[index]=++clock;veyra::log::info("image-browser",std::format("thumbnail index={} ready={} size={}x{} original-only",index,!thumb->pixels.empty(),thumb->width,thumb->height));thumbs[index]=std::move(thumb);}PostMessageW(hwnd,Ready,0,0);
    }if(SUCCEEDED(com))CoUninitialize();}
    void refresh(){RECT r{};GetClientRect(hwnd,&r);columns=std::max(1,int(r.right)/dip(hwnd,180));rowsVisible=std::max(1,(int(r.bottom)-dip(hwnd,42))/dip(hwnd,178));int total=(int(files.size())+columns-1)/columns;row=std::clamp(row,0,std::max(0,total-rowsVisible));SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),UINT(rowsVisible),row};SetScrollInfo(hwnd,SB_VERT,&si,TRUE);
        {std::lock_guard lock(mutex);jobs.clear();const size_t first=size_t(row)*columns,last=std::min(files.size(),first+size_t(rowsVisible+1)*columns);visibleFirst=first;visibleLast=last;for(size_t i=first;i<last;++i){if(!thumbs.contains(i))jobs.push_back(i);else used[i]=++clock;}}wake.notify_one();InvalidateRect(hwnd,nullptr,FALSE);
    }
    void open(size_t index){if(index>=files.size())return;selected=index;PostMessageW(GetParent(hwnd),ImageBrowserPick,WPARAM(index),0);}
};
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){auto b=reinterpret_cast<Browser*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){b=new Browser(hwnd);SetWindowLongPtrW(hwnd,GWLP_USERDATA,LONG_PTR(b));return 0;}if(!b)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){case WM_ERASEBKGND:return 1;case WM_DPICHANGED_AFTERPARENT:case WM_SIZE:b->refresh();return 0;case Ready:InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_MOUSEWHEEL:b->wheel+=GET_WHEEL_DELTA_WPARAM(wp);if(abs(b->wheel)>=WHEEL_DELTA){b->row-=b->wheel/WHEEL_DELTA;b->wheel%=WHEEL_DELTA;b->refresh();}return 0;
    case WM_VSCROLL:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_VERT,&si);switch(LOWORD(wp)){case SB_LINEUP:--b->row;break;case SB_LINEDOWN:++b->row;break;case SB_PAGEUP:b->row-=b->rowsVisible;break;case SB_PAGEDOWN:b->row+=b->rowsVisible;break;case SB_TOP:b->row=0;break;case SB_BOTTOM:b->row=INT_MAX;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:b->row=si.nTrackPos;break;}b->refresh();return 0;}
    case WM_LBUTTONDOWN:b->pressed=true;SetFocus(hwnd);SetCapture(hwnd);return 0;
    case WM_CAPTURECHANGED:b->pressed=false;return 0;
    case WM_LBUTTONUP:{const bool clicked=b->pressed;b->pressed=false;if(GetCapture()==hwnd)ReleaseCapture();if(!clicked)return 0;const int x=GET_X_LPARAM(lp)/dip(hwnd,180),y=(GET_Y_LPARAM(lp)-dip(hwnd,42))/dip(hwnd,178);if(GET_Y_LPARAM(lp)>=dip(hwnd,42)&&x>=0&&x<b->columns&&y>=0)b->open(size_t(b->row+y)*b->columns+x);return 0;}
    case WM_GETDLGCODE:return DLGC_WANTARROWS|DLGC_WANTCHARS;
    case WM_KEYDOWN:{if(wp==VK_RETURN){b->open(b->selected);return 0;}int delta=wp==VK_RIGHT?1:wp==VK_LEFT?-1:wp==VK_DOWN?b->columns:wp==VK_UP?-b->columns:0;if(delta&&!b->files.empty()){b->selected=size_t(std::clamp(int(b->selected)+delta,0,int(b->files.size()-1)));int selectedRow=int(b->selected)/b->columns;if(selectedRow<b->row)b->row=selectedRow;else if(selectedRow>=b->row+b->rowsVisible)b->row=selectedRow-b->rowsVisible+1;b->refresh();}return 0;}
    case WM_PRINTCLIENT:case WM_PAINT:{PaintBuffer paint(hwnd,msg==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):nullptr);auto dc=paint.dc;FillRect(dc,&paint.rect,bgBrush());auto font=makeFont(hwnd,12);auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,textColor);RECT heading{dip(hwnd,10),0,paint.rect.right,dip(hwnd,38)};auto title=L"原图浏览器 · "+std::to_wstring(b->files.size())+L" 张 · 点击图片开始增强";DrawTextW(dc,title.c_str(),-1,&heading,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        const size_t first=size_t(b->row)*b->columns,last=std::min(b->files.size(),first+size_t(b->rowsVisible+1)*b->columns);
        for(size_t i=first;i<last;++i){int x=int(i% b->columns)*dip(hwnd,180),y=dip(hwnd,42)+int(i/b->columns-b->row)*dip(hwnd,178);RECT box{x+dip(hwnd,4),y+dip(hwnd,2),x+dip(hwnd,176),y+dip(hwnd,172)};auto brush=CreateSolidBrush(i==b->selected?RGB(40,75,85):RGB(30,32,36));FillRect(dc,&box,brush);DeleteObject(brush);
            std::shared_ptr<Thumb> t;{std::lock_guard lock(b->mutex);auto it=b->thumbs.find(i);if(it!=b->thumbs.end())t=it->second;}
            if(t&&!t->pixels.empty()){double fit=std::min(double(dip(hwnd,160))/t->width,double(dip(hwnd,136))/t->height);int w=int(t->width*fit),h=int(t->height*fit);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=t->width;info.bmiHeader.biHeight=-LONG(t->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);StretchDIBits(dc,x+(dip(hwnd,180)-w)/2,y+dip(hwnd,6)+(dip(hwnd,136)-h)/2,w,h,0,0,t->width,t->height,t->pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);}
            else{RECT placeholder{x,y+dip(hwnd,40),x+dip(hwnd,180),y+dip(hwnd,90)};DrawTextW(dc,t?L"无法读取":L"加载缩略图…",-1,&placeholder,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
            RECT name{x+dip(hwnd,8),y+dip(hwnd,145),x+dip(hwnd,172),y+dip(hwnd,169)};auto label=std::filesystem::path(b->files[i]).filename().wstring();DrawTextW(dc,label.c_str(),-1,&name,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        }SelectObject(dc,old);DeleteObject(font);paint.makeOpaque();return 0;}
    case WM_DESTROY:SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete b;return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
HWND createImageBrowser(HWND owner){WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraImageBrowser";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(0,wc.lpszClassName,L"原图缩略图浏览器",WS_CHILD|WS_TABSTOP|WS_VSCROLL,0,0,1,1,owner,nullptr,wc.hInstance,nullptr);}
void loadImageBrowser(HWND hwnd,const std::vector<std::wstring>& files,size_t selected){auto b=reinterpret_cast<Browser*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(!b)return;{std::lock_guard lock(b->mutex);++b->generation;b->files=files;b->thumbs.clear();b->used.clear();b->jobs.clear();}b->selected=std::min(selected,files.empty()?0:files.size()-1);b->row=int(b->selected)/b->columns;b->refresh();}
void selectImageBrowser(HWND hwnd,size_t index){auto b=reinterpret_cast<Browser*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(!b||index>=b->files.size())return;b->selected=index;int row=int(index)/b->columns;if(row<b->row||row>=b->row+b->rowsVisible)b->row=row;b->refresh();}
}
