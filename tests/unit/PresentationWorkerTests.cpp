#include "veyra/engine/PresentationWorker.h"
#include "veyra/engine/TimingWindow.h"
#include <future>
#include <iostream>
using namespace std::chrono_literals;
int main(){
    int failures=0;auto check=[&](bool pass,const char* s){std::cout<<(pass?"PASS ":"FAIL ")<<s<<'\n';if(!pass)++failures;};
    std::promise<void> entered;auto enteredFuture=entered.get_future();std::atomic<unsigned> ran{0};std::atomic<bool> released{false};
    veyra::engine::PresentationWorker worker([]{return false;});
    check(worker.push([&](const auto& cancelled){entered.set_value();while(!cancelled())std::this_thread::sleep_for(1ms);released=true;return true;}),"queue active presentation");
    check(enteredFuture.wait_for(1s)==std::future_status::ready,"worker starts independently of producer");
    std::atomic<unsigned> discarded=0;
    check(worker.push([&](const auto&){++ran;return true;},[&]{++discarded;}),"next batch can queue while active job sleeps");
    check(!worker.push([](const auto&){return true;}),"two-batch capacity includes active lease");
    worker.cancelAndDrain();check(released&&ran==0,"cancel waits for active callback and drops queued job");
    check(discarded==1&&worker.occupancy()==0,"queued cancellation counted exactly once and occupancy drains");
    std::promise<void> next;auto nextFuture=next.get_future();check(worker.waitForSlot()&&worker.push([&](const auto&){next.set_value();return true;}),"producer resumes after cancellation");
    check(nextFuture.wait_for(1s)==std::future_status::ready,"new epoch job executes");worker.cancelAndDrain();
    std::atomic<bool> observed=false;std::promise<void> waiting;auto waitingFuture=waiting.get_future();
    check(worker.push([&](const auto& cancelled){waiting.set_value();while(!observed&&!cancelled())std::this_thread::sleep_for(1ms);return true;}),"hold presenter behind independently completed work");
    check(waitingFuture.wait_for(1s)==std::future_status::ready&&worker.push([](const auto&){return true;}),"fill bounded queue during presentation deadline");
    check(worker.waitForSlot([&]{check(worker.occupancy()==2,"completion polling runs outside queue mutex");observed=true;})&&observed,"backpressure observes completion before presenter releases slot");
    worker.cancelAndDrain();
    check(worker.push([](const auto&){return false;}),"inject presentation failure");
    auto deadline=std::chrono::steady_clock::now()+1s;while(!worker.failed()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
    check(worker.failed()&&!worker.waitForSlot(),"failure propagates without blocking producer");
    veyra::engine::TimingWindow timing;for(unsigned i=0;i<100;++i)timing.add(i);check(timing.p95()==94,"percentile order statistic");timing.clear();timing.add(6);check(timing.p95()==6,"reset removes prior 94ms window immediately");
    return failures?1:0;
}
