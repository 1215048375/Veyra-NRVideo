#pragma once
#include <windows.h>
#include <algorithm>
#include "veyra/Log.h"
namespace veyra::engine {
class DeadlineWait {
public:
    DeadlineWait(){
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer_){const auto code=GetLastError();timer_=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);log::warn("scheduler","high resolution wait timer unavailable; error="+std::to_string(code));}
        else log::info("scheduler","high resolution waitable timer active; no global timer resolution changes");
    }
    ~DeadlineWait(){if(timer_)CloseHandle(timer_);}
    DeadlineWait(const DeadlineWait&)=delete;
    DeadlineWait& operator=(const DeadlineWait&)=delete;
    void slice(double milliseconds=1){
        if(!timer_){Sleep(1);return;}
        LARGE_INTEGER due;due.QuadPart=-std::max<LONGLONG>(1,static_cast<LONGLONG>(std::clamp(milliseconds,0.05,2.0)*10000));
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)){log::error("scheduler","SetWaitableTimer error="+std::to_string(GetLastError()));return;}
        const auto result=WaitForSingleObject(timer_,10);
        if(result!=WAIT_OBJECT_0)log::warn("scheduler","wait timer result="+std::to_string(result));
    }
private:HANDLE timer_=nullptr;
};
}
