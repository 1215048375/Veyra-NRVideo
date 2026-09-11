#include "veyra/remoteplay/Discovery.h"
#include <chiaki/discoveryservice.h>
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <mutex>
namespace veyra::remoteplay {
namespace {
struct DiscoveryResult {
    std::mutex mutex;std::vector<DiscoveredConsole> hosts;
    static void receive(ChiakiDiscoveryHost* hosts,size_t count,void* user) noexcept {
        auto& out=*static_cast<DiscoveryResult*>(user);
        try{std::lock_guard lock(out.mutex);out.hosts.clear();
            for(size_t i=0;i<std::min(count,size_t(16));++i)
                if(chiaki_discovery_host_is_ps5(&hosts[i])&&hosts[i].host_addr&&validHost(hosts[i].host_addr))
                    out.hosts.push_back({hosts[i].host_addr,hosts[i].state==CHIAKI_DISCOVERY_HOST_STATE_STANDBY});
        }catch(...){/* Keep callback exceptions out of C. */}
    }
};
void quiet(ChiakiLogLevel,const char*,void*){}
}
std::vector<DiscoveredConsole> discoverLocalPs5(std::stop_token stop){
    if(stop.stop_requested()||!initializeChiaki().ok)return {};
    DiscoveryResult result;ChiakiLog log{};chiaki_log_init(&log,0,quiet,nullptr);
    sockaddr_storage address{};auto* ipv4=reinterpret_cast<sockaddr_in*>(&address);
    ipv4->sin_family=AF_INET;ipv4->sin_addr.s_addr=INADDR_BROADCAST;ipv4->sin_port=htons(CHIAKI_DISCOVERY_PORT_PS5);
    ChiakiDiscoveryServiceOptions options{};options.hosts_max=16;options.host_drop_pings=4;options.ping_ms=500;options.ping_initial_ms=500;
    options.send_addr=&address;options.send_addr_size=sizeof(sockaddr_in);options.cb=DiscoveryResult::receive;options.cb_user=&result;
    ChiakiDiscoveryService service{};
    if(chiaki_discovery_service_init(&service,&options,&log)!=CHIAKI_ERR_SUCCESS)return {};
    std::mutex mutex;std::condition_variable_any wait;std::unique_lock lock(mutex);
    wait.wait_for(lock,stop,std::chrono::seconds(3),[]{return false;});
    chiaki_discovery_service_fini(&service);
    return std::move(result.hosts);
}
BackendResult wakeLocalPs5(const NativeConnectRequest& request){
    if(!validHost(request.host))return {false,-1300,"invalid_wakeup_host"};
    auto init=initializeChiaki();if(!init.ok)return init;
    const auto& key=request.credentials.registrationKey;
    const auto end=std::find(key.begin(),key.end(),'\0');uint64_t credential=0;
    const auto parsed=std::from_chars(key.data(),key.data()+(end-key.begin()),credential,16);
    if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+(end-key.begin()))return {false,-1301,"invalid_wakeup_credential"};
    ChiakiLog log{};chiaki_log_init(&log,0,quiet,nullptr);
    const auto code=chiaki_discovery_wakeup(&log,nullptr,request.host.c_str(),credential,true);
    return {code==CHIAKI_ERR_SUCCESS,int(code),"chiaki_discovery_wakeup"};
}
}
