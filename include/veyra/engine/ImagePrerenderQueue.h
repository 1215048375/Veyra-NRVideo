#pragma once
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <mutex>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
namespace veyra::engine {
// Scheduling only. All GPU work remains on EngineController's owner thread.
class ImagePrerenderQueue {
    struct Entry {std::shared_ptr<const sink::RgbaImage> image;uintmax_t size;std::filesystem::file_time_type time;};
    mutable std::mutex mutex_;std::vector<std::wstring> paths_;std::string key_;
    std::set<std::wstring> attempted_,ready_,failed_;std::map<std::wstring,Entry> images_;
    size_t bytes_=0;static constexpr size_t budget=512ull*1024*1024;
public:
    void paths(std::vector<std::wstring> paths){if(paths.size()>20)paths.resize(20);std::lock_guard lock(mutex_);paths_=std::move(paths);
        auto wanted=[&](const auto& p){return std::find(paths_.begin(),paths_.end(),p)!=paths_.end();};
        std::erase_if(attempted_,[&](auto& p){return !wanted(p);});std::erase_if(ready_,[&](auto& p){return !wanted(p);});std::erase_if(failed_,[&](auto& p){return !wanted(p);});
        std::erase_if(images_,[&](auto& p){if(wanted(p.first))return false;bytes_-=p.second.image->pixels.size();return true;});
    }
    void invalidate(){std::lock_guard lock(mutex_);key_.clear();attempted_.clear();ready_.clear();failed_.clear();images_.clear();bytes_=0;}
    bool pending()const{std::lock_guard lock(mutex_);return std::any_of(paths_.begin(),paths_.end(),[&](auto& p){return !attempted_.contains(p);});}
    std::wstring next(const std::string& key){std::lock_guard lock(mutex_);if(key!=key_){key_=key;attempted_.clear();ready_.clear();failed_.clear();images_.clear();bytes_=0;}
        for(auto& path:paths_)if(!attempted_.contains(path)){attempted_.insert(path);return path;}return {};
    }
    void retry(const std::wstring& path){std::lock_guard lock(mutex_);attempted_.erase(path);}
    bool wanted(const std::wstring& path,const std::string& key)const{std::lock_guard lock(mutex_);return key==key_&&std::find(paths_.begin(),paths_.end(),path)!=paths_.end();}
    void finish(const std::wstring& path,const std::string& key,std::shared_ptr<const sink::RgbaImage> image,bool saved){
        std::lock_guard lock(mutex_);if(key!=key_||std::find(paths_.begin(),paths_.end(),path)==paths_.end())return;
        if(!saved){failed_.insert(path);return;}ready_.insert(path);
        if(!image||image->pixels.size()>budget-bytes_)return;
        std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec)return;auto time=std::filesystem::last_write_time(path,ec);if(ec)return;
        auto [it,inserted]=images_.emplace(path,Entry{image,size,time});if(inserted)bytes_+=image->pixels.size();
    }
    std::shared_ptr<const sink::RgbaImage> get(const std::wstring& path,const std::string& key){
        std::lock_guard lock(mutex_);if(key!=key_)return {};auto it=images_.find(path);if(it==images_.end())return {};
        std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size!=it->second.size)return {};auto time=std::filesystem::last_write_time(path,ec);
        return !ec&&time==it->second.time?it->second.image:nullptr;
    }
    std::pair<size_t,size_t> progress()const{std::lock_guard lock(mutex_);return {ready_.size(),paths_.size()};}
    size_t failures()const{std::lock_guard lock(mutex_);return failed_.size();}
};
}
