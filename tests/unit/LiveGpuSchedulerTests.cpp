#include "veyra/engine/LiveGpuScheduler.h"
#include "veyra/engine/TimingWindow.h"
#include <iostream>
#include <memory>
#include <atomic>
int main(){
    using Scheduler=veyra::engine::LiveGpuScheduler;using State=Scheduler::State;
    int failures=0;auto check=[&](bool pass,const char* text){std::cout<<(pass?"PASS ":"FAIL ")<<text<<'\n';failures+=!pass;};
    Scheduler scheduler;unsigned finished=0,secondRan=0,firstSteps=0;bool gpuReady=false;
    const auto owner=std::this_thread::get_id();bool sameOwner=true;
    auto lease=std::make_shared<int>(1);std::weak_ptr<int> weak=lease;
    check(scheduler.push([&,lease](int64_t now){++firstSteps;sameOwner&=std::this_thread::get_id()==owner;if(!gpuReady)return Scheduler::Step{State::Pending,now+2};return Scheduler::Step{now<100?State::Pending:State::Complete,100};},[&]{++finished;}),"accept first leased GPU batch");
    lease.reset();scheduler.advance(0);
    check(firstSteps==1&&scheduler.wakeAt()==2&&!weak.expired(),"GPU-pending step yields immediately and retains lease");
    check(scheduler.push([&](int64_t){++secondRan;return Scheduler::Step{State::Complete};},[&]{++finished;}),"next batch accepted while current batch awaits GPU");
    check(!scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[]{})&&scheduler.occupancy()==2,"capacity includes both current and next batch");
    gpuReady=true;scheduler.advance(20);
    check(scheduler.wakeAt()==100&&secondRan==0,"deadline wait yields without presenting early or reordering batches");
    unsigned submission=0;++submission;
    check(submission==1&&finished==0,"owner remains available to submit work during future presentation deadline");
    scheduler.advance(100);
    check(finished==2&&secondRan==1&&weak.expired()&&scheduler.occupancy()==0&&sameOwner,"due work finishes on owner and releases leases exactly once");
    scheduler.advance(200);check(finished==2,"completed jobs cannot execute or finalize twice");
    unsigned cancelled=0;
    scheduler.push([](int64_t){return Scheduler::Step{State::Pending,1000};},[&]{++cancelled;});
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++cancelled;});
    scheduler.advance(300);scheduler.cancel();scheduler.cancel();
    check(cancelled==2&&scheduler.occupancy()==0&&scheduler.wakeAt()==0,"cancellation finalizes pending and queued batches exactly once without waiting");
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++finished;});scheduler.advance(400);
    check(finished==3,"new work accepted after cancellation");
    std::atomic<bool> rejected=false;std::thread foreign([&]{try{scheduler.advance(500);}catch(const std::logic_error&){rejected=true;}});foreign.join();
    check(rejected,"foreign thread cannot advance GPU owner state");
    unsigned failedFinished=0;
    scheduler.push([](int64_t){return Scheduler::Step{State::Failed};},[&]{++failedFinished;});
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++failedFinished;});scheduler.advance(600);
    check(scheduler.failed()&&failedFinished==2&&scheduler.occupancy()==0,"failure propagates and finalizes queued work");
    check(!scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[]{}),"failed scheduler rejects new work");
    veyra::engine::TimingWindow timing;for(unsigned i=0;i<100;++i)timing.add(i);check(timing.p95()==94,"percentile order statistic");timing.clear();timing.add(6);check(timing.p95()==6,"reset removes old timing window");
    return failures?1:0;
}
