#include "veyra/engine/ImageRenderCache.h"
#include "veyra/FileIdentity.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <algorithm>
namespace veyra::engine {
namespace {
std::string hash(const std::string& value){return sha256Hex(reinterpret_cast<const uint8_t*>(value.data()),value.size());}

}
std::string ImageRenderCache::sourceKey(const std::wstring& path,const sink::RgbaImage& image){
    auto name=std::filesystem::path(path).filename().u8string();
    std::string data(reinterpret_cast<const char*>(name.data()),name.size());
    data+=' '+std::to_string(image.width)+' '+std::to_string(image.height)+' '+sha256Hex(image.pixels.data(),image.pixels.size());
    return hash(data);
}
std::string ImageRenderCache::settingsKey(const EnhancementSettings&){return "rendered";}
std::filesystem::path ImageRenderCache::entry(const std::wstring& source,const std::string& key,const EnhancementSettings& settings){
    return std::filesystem::path(source).parent_path()/L"_cache"/(key+"-"+settingsKey(settings)+".png");
}
static bool loadExact(const std::filesystem::path& path,sink::RgbaImage& image){
    try{
        auto meta=path;meta+=L".txt";std::ifstream in(meta);std::string magic,digest,extra;uint32_t w=0,h=0;
        if(!(in>>magic>>w>>h>>digest)||magic!="VEYRA_IMAGE_CACHE_1"||(in>>extra))return false;
        sink::RgbaImage result;
        if(!w||!h||uint64_t(w)*h>UINT32_MAX/4||!sink::loadImage(path.wstring(),result,size_t(w)*h*4)||result.width!=w||result.height!=h||sha256Hex(result.pixels.data(),result.pixels.size())!=digest)return false;
        image=std::move(result);log::info("image-disk-cache","hit; displaying saved result without NR/SR evaluation");return true;
    }catch(const std::exception& e){log::warn("image-disk-cache",e.what());return false;}
}
bool ImageRenderCache::load(const std::filesystem::path& path,sink::RgbaImage& image){
    if(loadExact(path,image))return true;
    // Accept valid caches produced by previous parameter-keyed builds, newest first.
    const auto name=path.filename().wstring();if(!name.ends_with(L"-rendered.png")||name.size()!=77)return false;
    try{std::vector<std::filesystem::directory_entry> candidates;std::error_code ec;
        for(std::filesystem::directory_iterator it(path.parent_path(),ec),end;!ec&&it!=end;it.increment(ec)){
            const auto file=it->path().filename().wstring();if(file.starts_with(name.substr(0,65))&&it->path()!=path&&it->path().extension()==L".png"&&it->is_regular_file())candidates.push_back(*it);
        }
        std::sort(candidates.begin(),candidates.end(),[](auto& a,auto& b){return a.last_write_time()>b.last_write_time();});
        for(auto& candidate:candidates)if(loadExact(candidate.path(),image))return true;
    }catch(const std::exception& e){log::warn("image-disk-cache",e.what());}return false;
}
bool ImageRenderCache::save(const std::filesystem::path& path,const sink::RgbaImage& image){
    try{
        std::filesystem::create_directories(path.parent_path());
        const auto suffix=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        auto temp=path;temp+=suffix;auto meta=path;meta+=L".txt";auto tempMeta=meta;tempMeta+=suffix;
        struct Cleanup{std::filesystem::path a,b;~Cleanup(){DeleteFileW(a.c_str());DeleteFileW(b.c_str());}} cleanup{temp,tempMeta};
        if(!sink::saveImage(temp.wstring(),image))return false;
        {std::ofstream out(tempMeta,std::ios::binary);out<<"VEYRA_IMAGE_CACHE_1 "<<image.width<<' '<<image.height<<' '<<sha256Hex(image.pixels.data(),image.pixels.size())<<'\n';out.close();if(!out)return false;}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)||!MoveFileExW(tempMeta.c_str(),meta.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
        log::info("image-disk-cache","saved lossless rendered PNG and integrity record");return true;
    }catch(const std::exception& e){log::warn("image-disk-cache",e.what());return false;}
}
}
