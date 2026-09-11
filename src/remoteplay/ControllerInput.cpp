#include "veyra/remoteplay/ControllerInput.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_audio.h>
#include <cstring>
#include "veyra/Log.h"
namespace veyra::remoteplay {
bool ControllerInput::start(uint32_t preferredDevice){
    preferredDevice_=preferredDevice;
    if(initialized_)return true;
    // No SDL window is created. Our explicit foreground gate is authoritative.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    initialized_=SDL_InitSubSystem(SDL_INIT_GAMEPAD);return initialized_;
}
void ControllerInput::stop(){
    releaseEffects();if(hapticStream_){SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;}
    if(audioInitialized_){SDL_QuitSubSystem(SDL_INIT_AUDIO);audioInitialized_=false;}hapticAttempted_=false;
    if(gamepad_){SDL_CloseGamepad(gamepad_);gamepad_=nullptr;}
    if(initialized_){SDL_QuitSubSystem(SDL_INIT_GAMEPAD);initialized_=false;}
}
ControllerState ControllerInput::poll(bool focused){
    ControllerState state;if(!initialized_)return state;
    SDL_PumpEvents();SDL_UpdateGamepads();
    // Only this module initializes SDL; drain its input events rather than
    // accumulating an unconsumed second application event queue.
    SDL_Event event;while(SDL_PollEvent(&event)){} // state queries below consume current sensor/touch state
    if(gamepad_&&!SDL_GamepadConnected(gamepad_)){releaseEffects();if(hapticStream_){SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;}hapticAttempted_=false;SDL_CloseGamepad(gamepad_);gamepad_=nullptr;enumerateAt_=0;}
    const auto now=monotonic100ns();
    if(!gamepad_&&now>=enumerateAt_){
        int count=0;auto* ids=SDL_GetGamepads(&count);
        for(int i=0;i<count&&!gamepad_;++i)if(!preferredDevice_||ids[i]==preferredDevice_)gamepad_=SDL_OpenGamepad(ids[i]);
        if(gamepad_){
            touchIds_={-1,-1};gyro_=SDL_GamepadHasSensor(gamepad_,SDL_SENSOR_GYRO)&&SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_GYRO,true);
            accel_=SDL_GamepadHasSensor(gamepad_,SDL_SENSOR_ACCEL)&&SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_ACCEL,true);
            veyra::log::info("remoteplay-input",std::string("device opened gyro=")+(gyro_?"1":"0")+" accel="+(accel_?"1":"0")+" touchpads="+std::to_string(SDL_GetNumGamepadTouchpads(gamepad_)));
        }
        SDL_free(ids);enumerateAt_=now+10000000;
    }
    if(!focused||!gamepad_){touchIds_={-1,-1};releaseEffects();return state;}
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
    for(int finger=0;finger<2;++finger){
        bool down=false;float x=0,y=0,pressure=0;
        if(SDL_GetNumGamepadTouchpads(gamepad_)>0&&SDL_GetGamepadTouchpadFinger(gamepad_,0,finger,&down,&x,&y,&pressure)&&down&&std::isfinite(x)&&std::isfinite(y)){
            if(touchIds_[finger]<0)touchIds_[finger]=int8_t(nextTouchId_++&0x7f);
            state.touches[finger]={touchIds_[finger],uint16_t(std::clamp(x,0.f,1.f)*1919),uint16_t(std::clamp(y,0.f,1.f)*1079)};
        }else touchIds_[finger]=-1;
    }
    if(gyro_&&accel_&&SDL_GetGamepadSensorData(gamepad_,SDL_SENSOR_GYRO,state.gyro.data(),3)&&SDL_GetGamepadSensorData(gamepad_,SDL_SENSOR_ACCEL,state.accel.data(),3)){
        for(auto& value:state.accel)value/=SDL_STANDARD_GRAVITY;
        state.motionValid=std::all_of(state.gyro.begin(),state.gyro.end(),[](float v){return std::isfinite(v);})&&std::all_of(state.accel.begin(),state.accel.end(),[](float v){return std::isfinite(v);});
        state.motionTimestampUs=SDL_GetTicksNS()/1000;
    }
    return state;
}
void ControllerInput::releaseEffects(){
    if(!effectsActive_)return;
    if(gamepad_){
        if(!SDL_RumbleGamepad(gamepad_,0,0,0))veyra::log::warn("remoteplay-feedback",SDL_GetError());
        if(SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5){
            std::array<uint8_t,47> effect{};effect[0]=0x0c;effect[10]=5;effect[21]=5;
            if(!SDL_SendGamepadEffect(gamepad_,effect.data(),int(effect.size())))veyra::log::warn("remoteplay-feedback",SDL_GetError());
        }
    }
    if(hapticStream_)SDL_ClearAudioStream(hapticStream_);
    effectsActive_=false;
}
void ControllerInput::feedback(ControllerFeedback f,bool focused){
    if(!focused||!gamepad_){releaseEffects();return;}
    if(f.rumble){effectsActive_=true;if(!SDL_RumbleGamepad(gamepad_,uint16_t(f.left)*257,uint16_t(f.right)*257,5000))veyra::log::warn("remoteplay-feedback",SDL_GetError());}
    if(f.triggers&&SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5){
        // SDL DualSense effects report payload (47 bytes): right at10, left at21.
        std::array<uint8_t,47> effect{};effect[0]=0x0c;
        std::copy(f.rightTrigger.begin(),f.rightTrigger.end(),effect.begin()+10);
        std::copy(f.leftTrigger.begin(),f.leftTrigger.end(),effect.begin()+21);
        effectsActive_=true;
        if(!SDL_SendGamepadEffect(gamepad_,effect.data(),int(effect.size())))veyra::log::warn("remoteplay-feedback",SDL_GetError());
    }
    if(f.haptics.empty()||SDL_GetGamepadType(gamepad_)!=SDL_GAMEPAD_TYPE_PS5)return;
    if(!hapticAttempted_){
        hapticAttempted_=true;
        audioInitialized_=SDL_InitSubSystem(SDL_INIT_AUDIO);
        if(!audioInitialized_){veyra::log::error("remoteplay-haptics",SDL_GetError());return;}
        // Never use the default speakers or a stereo endpoint. Ambiguity fails closed.
        int count=0;auto* devices=SDL_GetAudioPlaybackDevices(&count);SDL_AudioDeviceID selected=0;int matches=0;
        for(int i=0;i<count;++i){const auto* name=SDL_GetAudioDeviceName(devices[i]);SDL_AudioSpec spec{};
            if(name&&(std::strstr(name,"Wireless Controller")||std::strstr(name,"DualSense"))&&SDL_GetAudioDeviceFormat(devices[i],&spec,nullptr)&&spec.channels==4){selected=devices[i];++matches;}
        }
        SDL_free(devices);
        int gamepadCount=0;auto* gamepads=SDL_GetGamepads(&gamepadCount);SDL_free(gamepads);
        if(matches!=1||gamepadCount!=1){veyra::log::warn("remoteplay-haptics","No unique four-channel DualSense endpoint/controller pair; haptics not routed");return;}
        SDL_AudioSpec input{SDL_AUDIO_S16,4,3000};
        hapticStream_=SDL_OpenAudioDeviceStream(selected,&input,nullptr,nullptr);
        if(!hapticStream_||!SDL_ResumeAudioStreamDevice(hapticStream_)){veyra::log::error("remoteplay-haptics",SDL_GetError());if(hapticStream_)SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;return;}
        veyra::log::info("remoteplay-haptics","Dedicated four-channel endpoint opened; speaker channels silent");
    }
    if(!hapticStream_)return;
    std::vector<int16_t> quad;quad.reserve(f.haptics.size()*2);
    for(size_t i=0;i+1<f.haptics.size();i+=2){quad.push_back(0);quad.push_back(0);quad.push_back(f.haptics[i]);quad.push_back(f.haptics[i+1]);}
    if(SDL_GetAudioStreamQueued(hapticStream_)>2400)SDL_ClearAudioStream(hapticStream_);
    effectsActive_=true;
    if(!SDL_PutAudioStreamData(hapticStream_,quad.data(),int(quad.size()*sizeof(int16_t))))veyra::log::warn("remoteplay-haptics",SDL_GetError());
}

}
