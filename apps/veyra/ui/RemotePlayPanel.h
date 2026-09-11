#pragma once
#include <windows.h>
#include <functional>
#include "veyra/source/RemotePlaySource.h"
namespace veyra::ui {
struct RemotePlayPanelStatus {std::wstring message;bool active=false;};
void showRemotePlayPanel(HWND,
    std::function<void(source::RemotePlayConnectDesc)>,std::function<void(std::string)>,
    std::function<RemotePlayPanelStatus()>,std::function<void()>);
}
