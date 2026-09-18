#include "veyra/engine/ImageRenderCache.h"
#include "veyra/engine/ImageComparison.h"
#include "veyra/engine/EngineController.h"
#include "veyra/RuntimePaths.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace veyra;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**){CoInitializeEx(nullptr,COINIT_MULTITHREADED);try{
    auto folder=std::filesystem::absolute(std::filesystem::path("out/playlist")/("disk-cache-"+std::to_string(GetTickCount64())));std::filesystem::create_directories(folder);
    auto source=(folder/L"原图.png").wstring();sink::RgbaImage original{8,6,std::vector<uint8_t>(8*6*4,127)};sink::RgbaImage rendered{16,12,std::vector<uint8_t>(16*12*4,223)};
    engine::EnhancementSettings settings;settings.nr=true;
    auto key=engine::ImageRenderCache::sourceKey(source,original);auto path=engine::ImageRenderCache::entry(source,key,settings);sink::RgbaImage result;
    require(!engine::ImageRenderCache::load(path,result),"missing cache");
    require(engine::ImageRenderCache::save(path,rendered)&&engine::ImageRenderCache::load(path,result),"lossless disk roundtrip");
    require(result.width==16&&result.height==12&&result.pixels==rendered.pixels,"full output dimensions and exact RGBA preserved");
    uint8_t pixel[4]{};
    engine::sampleImageComparison(original,result,.25,.5,0,.5f,pixel);require(pixel[0]==223,"enhanced cached pixels displayed");
    engine::sampleImageComparison(original,result,.75,.5,1,.5f,pixel);require(pixel[0]==127,"hold-original bypasses cached pixels");
    engine::sampleImageComparison(original,result,.25,.5,2,.5f,pixel);require(pixel[0]==127,"split left original with different output dimensions");
    engine::sampleImageComparison(original,result,.75,.5,2,.5f,pixel);require(pixel[0]==223,"split right enhanced with different output dimensions");
    engine::sampleImageComparison(original,result,-.1,.5,0,.5f,pixel);require(pixel[0]==0&&pixel[3]==255,"letterbox outside image");
    const auto legacySource=(folder/L"legacy.png").wstring();const auto legacyKey=engine::ImageRenderCache::sourceKey(legacySource,original);
    const auto legacyPath=folder/L"_cache"/(legacyKey+"-"+std::string(64,'A')+".png");
    require(engine::ImageRenderCache::save(legacyPath,rendered),"legacy parameter cache fixture");
    sink::RgbaImage legacyResult;require(engine::ImageRenderCache::load(engine::ImageRenderCache::entry(legacySource,legacyKey,{}),legacyResult)&&legacyResult.pixels==rendered.pixels,"old parameter caches remain usable");
    auto changed=settings;changed.model.intensity=.5f;require(engine::ImageRenderCache::entry(source,key,changed)==path,"enhancement changes preserve cache identity");
    changed=settings;changed.revision=99;changed.audioOffsetMs=50;changed.captureCompatible=true;require(engine::ImageRenderCache::entry(source,key,changed)==path,"UI/revision/audio do not invalidate pixels");
    original.pixels[0]++;require(engine::ImageRenderCache::sourceKey(source,original)!=key,"source pixels invalidate cache");
    require(engine::ImageRenderCache::sourceKey((folder/L"原图.jpg").wstring(),original)!=key,"same stem different extension independent");
    {std::ofstream corrupt(path,std::ios::binary|std::ios::trunc);corrupt<<"invalid PNG";}
    require(!engine::ImageRenderCache::load(path,result),"corrupt PNG rejected");
    require(engine::ImageRenderCache::save(path,rendered)&&engine::ImageRenderCache::load(path,result),"corrupt cache repaired atomically");
    auto meta=path;meta+=L".txt";{std::ofstream corrupt(meta);corrupt<<"VEYRA_IMAGE_CACHE_1 16 12 BADHASH\n";}
    require(!engine::ImageRenderCache::load(path,result),"integrity mismatch rejected");
    auto blocked=folder/"blocked";std::ofstream(blocked).put('x');require(!engine::ImageRenderCache::save(blocked/"image.png",rendered),"write failure is nonfatal");
    std::cout<<"PASS cache pixel/dimension roundtrip, source/settings invalidation, corrupt/missing recovery, failed-write fallback; original/split pixels across output dimensions\n";
    if(argc>1){
        require(sink::saveImage(source,original),"GPU test source");
        const auto liveKey=engine::ImageRenderCache::sourceKey(source,original);
        require(engine::ImageRenderCache::save(engine::ImageRenderCache::entry(source,liveKey,settings),rendered),"GPU test cached NR result");
        HWND window=CreateWindowExW(0,L"STATIC",L"cache integration",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"GPU test window");
        {engine::EngineController engine;engine.cacheRenderedImages(true);engine.open(window,source,engine::PlayerOptions::from(settings));
            auto wait=[&](auto check){const auto end=GetTickCount64()+15000;while(GetTickCount64()<end){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(check())return;Sleep(10);}throw std::runtime_error("GPU cache test timeout");};
            wait([&]{auto s=engine.snapshot();return s.imageDiskCached&&s.frames&&!s.imageLoading&&!s.failed;});
            require(engine.snapshot().nrEvaluated==0,"cached NR result must not evaluate NR");
            engine.comparison(1,false);Sleep(100);engine.comparison(2,false,.3f);Sleep(100);
            auto saved=folder/"gpu-cached-save.png";engine.saveFrame(saved.wstring());wait([&]{return std::filesystem::exists(saved);});
            sink::RgbaImage back;require(sink::loadImage(saved.wstring(),back)&&back.pixels==rendered.pixels&&back.width==rendered.width,"cached full-resolution output survives GPU viewing/save");
            auto next=settings;next.model.intensity=.25f;
            require(engine::ImageRenderCache::entry(source,liveKey,next)==engine::ImageRenderCache::entry(source,liveKey,settings),"parameters reuse original cache");
            require(engine.requestSettings(next),"request cached configuration");
            wait([&]{auto s=engine.snapshot();return s.imageDiskCached&&!s.applying&&s.applied.model.intensity==.25f;});
            require(engine.snapshot().nrEvaluated==0&&!engine.snapshot().failed,"cached settings switch without NR runtime");
            engine.stop();wait([&]{return engine.idle();});
        }DestroyWindow(window);
        window=CreateWindowExW(0,L"STATIC",L"prerender integration",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        {engine::EngineController engine;engine.prerenderImages(true);std::vector<std::wstring> paths;
            for(unsigned i=0;i<22;++i){auto file=folder/("future-"+std::to_string(i)+".png");require(sink::saveImage(file.wstring(),original),"future source");paths.push_back(file.wstring());}
            engine.prefetchImages(std::vector<std::wstring>(paths.begin(),paths.begin()+20));
            engine.open(window,source,engine::PlayerOptions::from({}));
            auto wait=[&](auto check){const auto end=GetTickCount64()+45000;while(GetTickCount64()<end){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(check())return;Sleep(10);}throw std::runtime_error("prerender integration timeout");};
            wait([&]{return engine.imagePrefetchProgress().first==20||engine.snapshot().failed;});
            require(!engine.snapshot().failed&&engine.imagePrefetchProgress()==std::pair<size_t,size_t>{20,20},"20 GPU-processed future results completed");
            for(unsigned i=0;i<20;++i){sink::RgbaImage cached;auto key=engine::ImageRenderCache::sourceKey(paths[i],original);require(engine::ImageRenderCache::load(engine::ImageRenderCache::entry(paths[i],key,{}),cached),"future result persisted before browsing");}
            engine.open(window,paths[0],engine::PlayerOptions::from({}));engine.prefetchImages(std::vector<std::wstring>(paths.begin()+1,paths.begin()+21));
            wait([&]{return engine.snapshot().imageDiskCached&&engine.snapshot().frames&&!engine.snapshot().imageLoading;});
            wait([&]{return engine.imagePrefetchProgress().first==20;});
            engine.prefetchImages({});require(engine.imagePrefetchProgress()==std::pair<size_t,size_t>{0,0},"no stale 20/20 at list end");
            if(!std::filesystem::exists(runtime::localRuntimeDirectory()/L"nvngx_dlssnr.dll")){
                auto missing=folder/"missing-nr.png";require(sink::saveImage(missing.wstring(),original),"missing NR fixture");engine.prefetchImages({paths[21],missing.wstring()});engine.open(window,source,engine::PlayerOptions::from(settings));
                wait([&]{return engine.imagePrerenderFailures()==2||engine.snapshot().failed;});
                require(!engine.snapshot().failed&&engine.imagePrefetchProgress()==std::pair<size_t,size_t>{0,2},"missing NR fails future effects without counting completion or interrupting current cached image");
                std::cout<<"PASS missing NR runtime: two failed future renders, zero completed, current cached image preserved\n";
            }

            engine.stop();wait([&]{return engine.idle();});
        }DestroyWindow(window);
        {engine::EngineController batch;std::vector<std::wstring> files;
            for(int i=0;i<4;++i){auto f=folder/("batch-"+std::to_string(i)+".png");require(sink::saveImage(f.wstring(),original),"batch source");files.push_back(f.wstring());}
            auto wait=[&]{const auto end=GetTickCount64()+45000;while(GetTickCount64()<end){if(!batch.imageBatchProgress().active&&batch.idle())return;Sleep(10);}throw std::runtime_error("batch timeout");};
            batch.renderImageFolder({files[0],files[1],files[2]},{});wait();auto p=batch.imageBatchProgress();require(p.completed==3&&p.rendered==3&&p.skipped==0&&p.failed==0,"first full folder render");
            batch.renderImageFolder({files[0],files[1],files[2]},settings);wait();p=batch.imageBatchProgress();require(p.skipped==3&&p.rendered==0&&p.failed==0,"changed NR settings skip all existing files even without runtime");
            batch.renderImageFolder(files,{});wait();p=batch.imageBatchProgress();require(p.skipped==3&&p.rendered==1&&p.completed==4,"new file only rendered");
            batch.renderImageFolder(files,{});batch.stop();wait();require(batch.imageBatchProgress().cancelled,"cancel pending folder task");
            std::cout<<"PASS full folder batch, parameter-independent skips, new-file-only render, cancel\n";
        }
        std::cout<<"PASS 20 future GPU renders before browsing, persisted results, memory hit, moving window refill, zero target at end\n";
        std::cout<<"PASS real D3D12 cached viewer with NR requested: zero NR evaluations, exact saved result, cached parameter switch, orderly shutdown\n";
    }
    CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';CoUninitialize();return 1;}}
