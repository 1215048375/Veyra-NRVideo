#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
namespace veyra::engine {
// Two leased batches including the active batch. Sleeping for presentation
// deadlines must not stop the source/graph thread from submitting the next one.
class PresentationWorker {
public:
    using Cancelled=std::function<bool()>;
    using Job=std::function<bool(const Cancelled&)>;
    explicit PresentationWorker(Cancelled interrupt):interrupt_(std::move(interrupt)),thread_([this]{run();}){}
    ~PresentationWorker(){stopping_=true;++epoch_;wake_.notify_all();if(thread_.joinable())thread_.join();}
    PresentationWorker(const PresentationWorker&)=delete;
    bool waitForSlot(){
        std::unique_lock lock(mutex_);
        while(queue_.size()+unsigned(active_)>=2&&!failed_&&!stopping_&&!interrupt_())wake_.wait_for(lock,std::chrono::milliseconds(1));
        return !failed_&&!stopping_&&!interrupt_();
    }
    bool push(Job job){std::lock_guard lock(mutex_);if(failed_||stopping_||queue_.size()+unsigned(active_)>=2)return false;queue_.push_back({epoch_.load(),std::move(job)});wake_.notify_all();return true;}
    void cancelAndDrain(){
        ++epoch_;std::unique_lock lock(mutex_);queue_.clear();wake_.notify_all();wake_.wait(lock,[&]{return !active_;});
    }
    bool failed()const{std::lock_guard lock(mutex_);return failed_;}
private:
    struct Entry{uint64_t epoch;Job job;};
    Cancelled interrupt_;mutable std::mutex mutex_;std::condition_variable wake_;std::deque<Entry> queue_;
    bool active_=false,failed_=false;std::atomic<bool> stopping_{false};std::atomic<uint64_t> epoch_{0};std::thread thread_;
    void run(){
        for(;;){Entry entry;{
            std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stopping_||!queue_.empty();});if(stopping_)break;
            entry=std::move(queue_.front());queue_.pop_front();active_=true;
        }
        bool ok=true;try{ok=entry.job([&,generation=entry.epoch]{return stopping_||epoch_!=generation||interrupt_();});}catch(...){ok=false;}
        // Drop captured FrameOutputs/leases before advertising a reusable slot.
        entry.job={};{
            std::lock_guard lock(mutex_);active_=false;if(!ok){failed_=true;queue_.clear();}wake_.notify_all();
        }}
    }
};
}
