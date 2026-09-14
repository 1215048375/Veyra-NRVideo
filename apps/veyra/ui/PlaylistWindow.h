#pragma once
#include <windows.h>
#include <functional>
#include "veyra/engine/Playlist.h"

namespace veyra::ui {
std::vector<std::wstring> choosePlaylistFiles(HWND owner);
std::vector<std::wstring> droppedPlaylistFiles(WPARAM drop);
HWND showPlaylistWindow(HWND owner, engine::Playlist&, std::function<void(size_t)> play,
                        std::function<void()> recent);
void refreshPlaylistWindow();
bool playlistWindowMessage(MSG&);
}
