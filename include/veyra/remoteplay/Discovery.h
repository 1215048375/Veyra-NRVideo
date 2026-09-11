#pragma once
#include "ChiakiBackend.h"
#include <vector>
namespace veyra::remoteplay {
struct DiscoveredConsole {std::string host;bool standby=false;};
std::vector<DiscoveredConsole> discoverLocalPs5(std::stop_token);
BackendResult wakeLocalPs5(const NativeConnectRequest&);
}
