#pragma once
#include "Types.h"
struct SDL_Gamepad;
struct SDL_AudioStream;
namespace veyra::remoteplay {
// SDL calls stay on the Win32 UI owner, independently of GPU processing.
class ControllerInput {
public:
    ~ControllerInput(){stop();}
    bool start(uint32_t preferredDevice=0);
    void stop();
    ControllerState poll(bool focused);
    void feedback(ControllerFeedback,bool focused);
    void releaseEffects();
    bool connected()const{return gamepad_!=nullptr;}
private:
    SDL_Gamepad* gamepad_=nullptr;
    bool initialized_=false;
    HostTime enumerateAt_=0;uint32_t preferredDevice_=0;
    std::array<int8_t,2> touchIds_{-1,-1};uint8_t nextTouchId_=0;
    bool gyro_=false,accel_=false,effectsActive_=false,audioInitialized_=false,hapticAttempted_=false;
    SDL_AudioStream* hapticStream_=nullptr;

};
}
