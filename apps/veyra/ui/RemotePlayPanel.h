#pragma once
#include <windows.h>
#include <functional>
#include "veyra/source/RemotePlaySource.h"
namespace veyra::ui {
void showRemotePlayPanel(HWND,
    std::function<void(source::RemotePlayConnectDesc)>,std::function<void(std::string)>);
}
