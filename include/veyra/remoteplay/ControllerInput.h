#pragma once
#include "Types.h"
struct SDL_Gamepad;
namespace veyra::remoteplay {
// SDL calls stay on the Win32 UI owner, independently of GPU processing.
class ControllerInput {
public:
    ~ControllerInput(){stop();}
    bool start();
    void stop();
    ControllerState poll(bool focused);
    bool connected()const{return gamepad_!=nullptr;}
private:
    SDL_Gamepad* gamepad_=nullptr;
    bool initialized_=false;
    HostTime enumerateAt_=0;
};
}
