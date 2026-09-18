#pragma once
#include "veyra/sink/ImageExportSink.h"
#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
namespace veyra::engine {
// One background decoder; at most twenty retained images and a bounded RGBA budget.
class ImageDecodeCache {
public:
    using Image=std::shared_ptr<const sink::RgbaImage>;
    using Loader=std::function<bool(const std::wstring&,sink::RgbaImage&,size_t)>;
    explicit ImageDecodeCache(Loader loader,size_t budget=512ull*1024*1024):loader_(std::move(loader)),budget_(budget),worker_([this]{run();}){}
    ~ImageDecodeCache(){{std::lock_guard lock(mutex_);stopping_=true;}wake_.notify_one();worker_.join();}
    void prefetch(std::vector<std::wstring> paths){
        if(paths.size()>20)paths.resize(20);
        std::lock_guard lock(mutex_);if(paths==wanted_)return;wanted_=std::move(paths);++epoch_;attempted_.clear();
        for(auto it=entries_.begin();it!=entries_.end();)if(std::find(wanted_.begin(),wanted_.end(),it->first)==wanted_.end()){bytes_-=it->second.image->pixels.size();it=entries_.erase(it);}else ++it;
        wake_.notify_one();
    }
    Image get(const std::wstring& path){
        const auto stamp=identity(path);std::lock_guard lock(mutex_);auto it=entries_.find(path);
        if(it==entries_.end())return {};
        if(!stamp.valid||it->second.stamp!=stamp){bytes_-=it->second.image->pixels.size();entries_.erase(it);return {};}
        return it->second.image;
    }
    std::pair<size_t,size_t> progress()const{std::lock_guard lock(mutex_);return {entries_.size(),wanted_.size()};}
    size_t bytes()const{std::lock_guard lock(mutex_);return bytes_;}
private:
    struct Stamp {uintmax_t size=0;std::filesystem::file_time_type time{};bool valid=false;bool operator==(const Stamp&)const=default;};
    static Stamp identity(const std::wstring& path){std::error_code ec;Stamp s;s.size=std::filesystem::file_size(path,ec);if(ec)return {};s.time=std::filesystem::last_write_time(path,ec);s.valid=!ec;return s;}
    struct Entry{Stamp stamp;Image image;};
    void run(){
        for(;;){
            std::wstring path;size_t available;
            {std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stopping_||std::any_of(wanted_.begin(),wanted_.end(),[&](const auto& p){return !entries_.contains(p)&&std::find(attempted_.begin(),attempted_.end(),p)==attempted_.end();});});
                if(stopping_)return;
                auto it=std::find_if(wanted_.begin(),wanted_.end(),[&](const auto& p){return !entries_.contains(p)&&std::find(attempted_.begin(),attempted_.end(),p)==attempted_.end();});
                path=*it;attempted_.push_back(path);available=budget_-bytes_;
            }
            auto before=identity(path);std::shared_ptr<sink::RgbaImage> image;
            try{if(before.valid&&available){auto decoded=std::make_shared<sink::RgbaImage>();if(loader_(path,*decoded,available)&&!decoded->pixels.empty()&&decoded->pixels.size()<=available)image=std::move(decoded);}}catch(const std::exception&){/* A failed speculative decode must not stop browsing. */}
            auto after=identity(path);
            {std::lock_guard lock(mutex_);if(!stopping_&&std::find(wanted_.begin(),wanted_.end(),path)!=wanted_.end()&&!entries_.contains(path)&&image&&before==after&&image->pixels.size()<=budget_-bytes_){bytes_+=image->pixels.size();entries_[path]={after,std::move(image)};}}
        }
    }
    Loader loader_;size_t budget_;mutable std::mutex mutex_;std::condition_variable wake_;
    bool stopping_=false;uint64_t epoch_=0;size_t bytes_=0;
    std::vector<std::wstring> wanted_,attempted_;std::map<std::wstring,Entry> entries_;std::thread worker_;
};
}
