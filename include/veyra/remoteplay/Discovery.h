#pragma once
#include "ChiakiBackend.h"
#include <vector>
namespace veyra::remoteplay {
struct DiscoveredConsole {std::string host;bool standby=false;std::string consoleId;};
struct DiscoveryReport {
    std::vector<DiscoveredConsole> hosts;
    std::vector<std::string> diagnostics;
    int error=0;
};
DiscoveryReport discoverLocalPs5(std::stop_token);
BackendResult wakeLocalPs5(const NativeConnectRequest&);
}
