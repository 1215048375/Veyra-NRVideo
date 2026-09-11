#include "veyra/remoteplay/ControllerInput.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <algorithm>
namespace veyra::remoteplay {
bool ControllerInput::start(){
    if(initialized_)return true;
    // No SDL window is created. Our explicit foreground gate is authoritative.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    initialized_=SDL_InitSubSystem(SDL_INIT_GAMEPAD);return initialized_;
}
void ControllerInput::stop(){
    if(gamepad_){SDL_CloseGamepad(gamepad_);gamepad_=nullptr;}
    if(initialized_){SDL_QuitSubSystem(SDL_INIT_GAMEPAD);initialized_=false;}
}
ControllerState ControllerInput::poll(bool focused){
    ControllerState state;if(!initialized_)return state;
    SDL_PumpEvents();SDL_UpdateGamepads();
    // Only this module initializes SDL; drain its input events rather than
    // accumulating an unconsumed second application event queue.
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    if(gamepad_&&!SDL_GamepadConnected(gamepad_)){SDL_CloseGamepad(gamepad_);gamepad_=nullptr;enumerateAt_=0;}
    const auto now=monotonic100ns();
    if(!gamepad_&&now>=enumerateAt_){
        int count=0;auto* ids=SDL_GetGamepads(&count);
        for(int i=0;i<count&&!gamepad_;++i)gamepad_=SDL_OpenGamepad(ids[i]);
        SDL_free(ids);enumerateAt_=now+10000000;
    }
    if(!focused||!gamepad_)return state;
    struct Button{SDL_GamepadButton input;ControllerState::Button output;};
    constexpr Button buttons[]={
        {SDL_GAMEPAD_BUTTON_SOUTH,ControllerState::Cross},{SDL_GAMEPAD_BUTTON_EAST,ControllerState::Circle},
        {SDL_GAMEPAD_BUTTON_WEST,ControllerState::Square},{SDL_GAMEPAD_BUTTON_NORTH,ControllerState::Triangle},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,ControllerState::Left},{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,ControllerState::Right},
        {SDL_GAMEPAD_BUTTON_DPAD_UP,ControllerState::Up},{SDL_GAMEPAD_BUTTON_DPAD_DOWN,ControllerState::Down},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,ControllerState::L1},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,ControllerState::R1},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,ControllerState::L3},{SDL_GAMEPAD_BUTTON_RIGHT_STICK,ControllerState::R3},
        {SDL_GAMEPAD_BUTTON_START,ControllerState::Options},{SDL_GAMEPAD_BUTTON_BACK,ControllerState::Share},
        {SDL_GAMEPAD_BUTTON_GUIDE,ControllerState::PS},{SDL_GAMEPAD_BUTTON_TOUCHPAD,ControllerState::Touchpad}};
    for(auto b:buttons)if(SDL_GetGamepadButton(gamepad_,b.input))state.buttons|=b.output;
    state.leftX=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFTX);state.leftY=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFTY);
    state.rightX=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHTX);state.rightY=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHTY);
    state.l2=uint8_t(std::max(0,int(SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)))*255/32767);
    state.r2=uint8_t(std::max(0,int(SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)))*255/32767);
    return state;
}
}
