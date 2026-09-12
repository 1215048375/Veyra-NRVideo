#include "veyra/remoteplay/Discovery.h"
#include <chiaki/discoveryservice.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <format>
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <mutex>
namespace veyra::remoteplay {
namespace {
struct DiscoveryResult {
    std::mutex mutex;DiscoveryReport report;
    static void logging(ChiakiLogLevel level,const char* message,void* user) noexcept {
        try {auto& out=*static_cast<DiscoveryResult*>(user);std::lock_guard lock(out.mutex);
            if(level==CHIAKI_LOG_ERROR)out.report.error=-1402;
            if(out.report.diagnostics.size()<256)out.report.diagnostics.emplace_back(message?message:"");
        }catch(...){}
    }
    static void receive(ChiakiDiscoveryHost* hosts,size_t count,void* user) noexcept {
        auto& out=*static_cast<DiscoveryResult*>(user);
        try{std::lock_guard lock(out.mutex);out.report.hosts.clear();
            for(size_t i=0;i<std::min(count,size_t(16));++i)
                if(chiaki_discovery_host_is_ps5(&hosts[i])&&hosts[i].host_addr&&validHost(hosts[i].host_addr))
                    out.report.hosts.push_back({hosts[i].host_addr,hosts[i].state==CHIAKI_DISCOVERY_HOST_STATE_STANDBY,hosts[i].host_id?hosts[i].host_id:""});
        }catch(...){/* Keep callback exceptions out of C. */}
    }
};
void quiet(ChiakiLogLevel,const char*,void*){}
}
DiscoveryReport discoverLocalPs5(std::stop_token stop){
    if(stop.stop_requested())return {};
    auto initialized=initializeChiaki();
    if(!initialized.ok)return {{},{"chiaki_lib_init failed"},initialized.code};
    DiscoveryResult result;ChiakiLog log{};
    chiaki_log_init(&log,CHIAKI_LOG_ERROR|CHIAKI_LOG_WARNING|CHIAKI_LOG_INFO,DiscoveryResult::logging,&result);
    std::vector<sockaddr_storage> broadcasts;
    ULONG bytes=16384;std::vector<unsigned char> buffer;
    ULONG code=ERROR_BUFFER_OVERFLOW;
    for(int attempt=0;attempt<3&&code==ERROR_BUFFER_OVERFLOW;++attempt){
        if(bytes>4*1024*1024)break;
        buffer.resize(bytes);
        code=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&bytes);
    }
    if(code!=NO_ERROR)return {{},{std::format("GetAdaptersAddresses failed code={}",code)},int(code)};
    for(auto* adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());adapter;adapter=adapter->Next){
        if(adapter->OperStatus!=IfOperStatusUp||adapter->IfType==IF_TYPE_SOFTWARE_LOOPBACK||adapter->IfType==IF_TYPE_TUNNEL)continue;
        for(auto* entry=adapter->FirstUnicastAddress;entry;entry=entry->Next){
            if(!entry->Address.lpSockaddr||entry->Address.lpSockaddr->sa_family!=AF_INET||entry->OnLinkPrefixLength==0||entry->OnLinkPrefixLength>30)continue;
            const auto address=ntohl(reinterpret_cast<sockaddr_in*>(entry->Address.lpSockaddr)->sin_addr.s_addr);
            if((address>>24)==127||(address>>16)==0xa9fe||address==0)continue;
            const auto mask=0xffffffffu<<(32-entry->OnLinkPrefixLength);
            sockaddr_storage target{};auto* ipv4=reinterpret_cast<sockaddr_in*>(&target);
            ipv4->sin_family=AF_INET;ipv4->sin_addr.s_addr=htonl(address|~mask);
            if(std::any_of(broadcasts.begin(),broadcasts.end(),[&](const auto& old){return reinterpret_cast<const sockaddr_in*>(&old)->sin_addr.s_addr==ipv4->sin_addr.s_addr;}))continue;
            char printable[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&ipv4->sin_addr,printable,sizeof(printable));
            result.report.diagnostics.push_back(std::format("interface={} directed_broadcast={}",adapter->IfIndex,printable));
            broadcasts.push_back(target);
        }
    }
    if(broadcasts.empty())return {{},{"No active IPv4 LAN broadcast targets"},-1401};
    sockaddr_storage address{};auto* ipv4=reinterpret_cast<sockaddr_in*>(&address);
    ipv4->sin_family=AF_INET;ipv4->sin_addr.s_addr=INADDR_BROADCAST;ipv4->sin_port=htons(CHIAKI_DISCOVERY_PORT_PS5);
    ChiakiDiscoveryServiceOptions options{};options.hosts_max=16;options.host_drop_pings=20;options.ping_ms=500;options.ping_initial_ms=0;
    options.send_addr=&address;options.send_addr_size=sizeof(sockaddr_in);options.cb=DiscoveryResult::receive;options.cb_user=&result;
    options.broadcast_addrs=broadcasts.data();options.broadcast_num=broadcasts.size();
    ChiakiDiscoveryService service{};
    const auto started=chiaki_discovery_service_init(&service,&options,&log);
    if(started!=CHIAKI_ERR_SUCCESS){result.report.error=int(started);result.report.diagnostics.push_back(std::format("discovery_init failed code={}",int(started)));return std::move(result.report);}
    std::mutex mutex;std::condition_variable_any wait;std::unique_lock lock(mutex);
    wait.wait_for(lock,stop,std::chrono::seconds(6),[]{return false;});
    chiaki_discovery_service_fini(&service);
    result.report.diagnostics.push_back(std::format("discovery finished hosts={} cancelled={}",result.report.hosts.size(),stop.stop_requested()));
    return std::move(result.report);
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
