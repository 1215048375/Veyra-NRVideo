// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/ChiakiBackend.h"
#include <chiaki/session.h>
#include <chiaki/controller.h>
#include <iostream>
int main(){
    auto init=veyra::remoteplay::initializeChiaki();
    if(!init.ok){std::cerr<<init.operation<<" failed code="<<init.code<<'\n';return 1;}
    ChiakiConnectVideoProfile p{};
    chiaki_connect_video_profile_preset(&p,CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,CHIAKI_VIDEO_FPS_PRESET_60);
    ChiakiControllerState c{};chiaki_controller_state_set_idle(&c);
    if(p.width!=1920||p.height!=1080||p.max_fps!=60||c.buttons!=0){std::cerr<<"Native profile/controller contract failed\n";return 1;}
    std::cout<<"REAL_CHIAKI_CORE_INITIALIZED upstream_video_callback=1\n";
    std::cout<<"PS5_CONNECTION_NOT_TESTED VIDEO_DECODE_NOT_TESTED WINDOWS_PLAYER_NOT_TESTED\n";
    return 0;
}
