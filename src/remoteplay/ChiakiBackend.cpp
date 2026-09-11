// SPDX-License-Identifier: GPL-3.0-only
// Uses chiaki-ng's AGPL-3.0-only + OpenSSL-exception APIs; retain both licenses.
// NOT compiled/tested in the Linux delivery environment: see the native gate.
#include "veyra/remoteplay/ChiakiBackend.h"
#include <chiaki/session.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/controller.h>
#include <chiaki/log.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
namespace veyra::remoteplay {
static_assert(CHIAKI_PSN_ACCOUNT_ID_SIZE == 8);
static_assert(CHIAKI_SESSION_AUTH_SIZE == 16);
namespace {
void wipe(void* ptr,std::size_t n) noexcept {
    auto* p=static_cast<volatile unsigned char*>(ptr);
    while(n--) *p++=0;
}
BackendResult result(ChiakiErrorCode code,const char* operation) {
    return {code==CHIAKI_ERR_SUCCESS,static_cast<int>(code),operation};
}
}
PairingCredentials::PairingCredentials(PairingCredentials&& other) noexcept { *this=std::move(other); }
PairingCredentials& PairingCredentials::operator=(PairingCredentials&& other) noexcept {
    if(this==&other) return *this;
    registrationKey=other.registrationKey;sessionKey=other.sessionKey;accountId=other.accountId;
    wipe(other.registrationKey.data(),other.registrationKey.size());
    wipe(other.sessionKey.data(),other.sessionKey.size());wipe(other.accountId.data(),other.accountId.size());
    return *this;
}
PairingCredentials::~PairingCredentials(){wipe(registrationKey.data(),registrationKey.size());wipe(sessionKey.data(),sessionKey.size());wipe(accountId.data(),accountId.size());}
BackendResult initializeChiaki(){
    static std::once_flag flag;
    static ChiakiErrorCode code=CHIAKI_ERR_UNKNOWN;
    std::call_once(flag,[]{code=chiaki_lib_init();});
    return result(code,"chiaki_lib_init");
}
struct ChiakiBackend::Impl {
    explicit Impl(SessionInbox::Token t):token(std::move(t)),owner(std::this_thread::get_id()){}
    SessionInbox::Token token;
    std::thread::id owner;
    ChiakiSession session{};
    ChiakiOpusDecoder opus{};
    ChiakiLog log{};
    VideoProfile profile;
    std::string host;
    bool initialized=false,opusInitialized=false;
    std::atomic<bool> active=false,started=false,connected=false;
    std::atomic<std::uint64_t> warnings=0,errors=0,videoCallbacks=0,audioCallbacks=0;
    std::atomic<int> quitReason=0,apiError=0;
    // Only the upstream audio/Opus callback thread reads/writes these fields.
    std::uint32_t channels=0,rate=0;
    std::uint64_t sampleIndex=0;
    bool audioDiscontinuity=true;
    static void logCallback(ChiakiLogLevel level,const char*,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        // Raw upstream log strings/hexdumps may contain keys. Do not forward them.
        if(level==CHIAKI_LOG_ERROR)++self.errors;
        if(level==CHIAKI_LOG_WARNING)++self.warnings;
    }
    static void eventCallback(ChiakiEvent* event,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(!event||!self.active.load())return;
        switch(event->type){
        case CHIAKI_EVENT_CONNECTED:self.connected=true;self.token.connected();break;
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST:self.token.loginPinRequired();break;
        case CHIAKI_EVENT_QUIT:
            self.connected=false;self.quitReason=static_cast<int>(event->quit.reason);
            // A remote termination is not a successful decoded EOF.
            self.token.failed(2000+static_cast<int>(event->quit.reason));break;
        default:break; // No unsolicited mic, standby, haptics or keyboard side effects.
        }
    }
    static bool videoCallback(std::uint8_t* data,std::size_t size,
        std::int32_t framesLost,bool frameRecovered,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(!self.active.load()||!data||!size)return false;
        try{
            const auto nal=inspectAnnexB({data,size},self.profile.codec);
            if(!nal.valid || (!nal.hasConfig&&!nal.hasPicture))return false;
            VideoSample sample;
            sample.generation=self.token.generation();sample.arrival100ns=monotonic100ns();
            sample.codec=self.profile.codec;
            sample.kind=nal.hasPicture?SampleKind::AccessUnit:SampleKind::CodecConfig;
            sample.width=self.profile.width;sample.height=self.profile.height;
            sample.framesLost=framesLost;sample.referenceRecovered=frameRecovered;
            sample.payload=PaddedBytes::copy({data,size});++self.videoCallbacks;
            return self.token.video(std::move(sample));
        }catch(...){self.apiError=-1001;self.token.failed(-1001);return false;}
    }
    static void opusSettings(std::uint32_t channels,std::uint32_t rate,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        // Sample position stays monotonic across repeated Opus headers within
        // one session; restarting at zero makes AudioIngress reject new PCM.
        self.channels=channels;self.rate=rate;self.audioDiscontinuity=true;
        if((channels!=1&&channels!=2)||rate!=48000){self.channels=0;self.token.failed(-1003);}
    }
    static void opusFrame(std::int16_t* data,std::size_t count,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(!self.active.load()||!self.channels||!data)return;
        if(count==0||count>self.rate/5u||count>(std::numeric_limits<std::uint64_t>::max)()-self.sampleIndex){self.token.failed(-1004);return;}
        try{
            PcmBlock block;block.generation=self.token.generation();block.channels=self.channels;block.rate=self.rate;
            block.firstSample=self.sampleIndex;block.arrival100ns=monotonic100ns();block.discontinuity=self.audioDiscontinuity;
            // Opus count is samples PER CHANNEL, not bytes or interleaved elements.
            block.samples.assign(data,data+count*self.channels);self.sampleIndex+=count;++self.audioCallbacks;
            if(self.token.audio(std::move(block))) self.audioDiscontinuity=false;
        }catch(...){self.apiError=-1002;self.token.failed(-1002);}
    }
    BackendResult record(ChiakiErrorCode code,const char* name){apiError=static_cast<int>(code);return result(code,name);}
    bool onOwner()const noexcept{return owner==std::this_thread::get_id();}
};
ChiakiBackend::ChiakiBackend()=default;
ChiakiBackend::~ChiakiBackend(){
    if(!p_)return;
    try{
        const auto stopped=stop();
        if(!stopped.ok && p_){
            // A failed join cannot be repaired by freeing memory still used by callbacks.
            // Quarantine/leak this one session rather than create a use-after-free.
            // The owner must treat stop failure as fatal and not reconnect this object.
            p_->active=false;(void)p_.release();
        }
    }catch(...){if(p_){p_->active=false;(void)p_.release();}}
}
BackendResult ChiakiBackend::start(const NativeConnectRequest& request,SessionInbox::Token token){
    if(p_)return {false,-1100,"already_initialized"};
    if(!validHost(request.host)||request.video.validate()||token.generation()==0)return {false,-1101,"validate_connect"};
    auto init=initializeChiaki();if(!init.ok)return init;
    p_=std::make_unique<Impl>(std::move(token));auto& s=*p_;s.host=request.host;s.profile=request.video;
    chiaki_log_init(&s.log,CHIAKI_LOG_WARNING|CHIAKI_LOG_ERROR,Impl::logCallback,&s);
    ChiakiConnectInfo info{};info.ps5=true;info.host=s.host.c_str();
    std::memcpy(info.regist_key,request.credentials.registrationKey.data(),request.credentials.registrationKey.size());
    std::memcpy(info.morning,request.credentials.sessionKey.data(),request.credentials.sessionKey.size());
    std::memcpy(info.psn_account_id,request.credentials.accountId.data(),request.credentials.accountId.size());
    chiaki_connect_video_profile_preset(&info.video_profile,
        request.video.height==720?CHIAKI_VIDEO_RESOLUTION_PRESET_720p:CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
        request.video.fps==30?CHIAKI_VIDEO_FPS_PRESET_30:CHIAKI_VIDEO_FPS_PRESET_60);
    info.video_profile.codec=request.video.codec==Codec::H264?CHIAKI_CODEC_H264:CHIAKI_CODEC_H265;
    info.video_profile.bitrate=request.video.bitrateKbps;
    info.video_profile_auto_downgrade=false;info.enable_keyboard=false;info.enable_dualsense=false;
    info.auto_regist=false;info.packet_loss_max=0.05;info.enable_idr_on_fec_failure=true;
    const auto code=chiaki_session_init(&s.session,&info,&s.log);
    wipe(info.regist_key,sizeof(info.regist_key));wipe(info.morning,sizeof(info.morning));wipe(info.psn_account_id,sizeof(info.psn_account_id));
    if(code!=CHIAKI_ERR_SUCCESS){auto out=s.record(code,"chiaki_session_init");s.token.failed(out.code);p_.reset();return out;}
    s.initialized=true;
    chiaki_opus_decoder_init(&s.opus,&s.log);s.opusInitialized=true;
    chiaki_opus_decoder_set_cb(&s.opus,Impl::opusSettings,Impl::opusFrame,&s);
    ChiakiAudioSink sink{};chiaki_opus_decoder_get_sink(&s.opus,&sink);chiaki_session_set_audio_sink(&s.session,&sink);
    chiaki_session_set_event_cb(&s.session,Impl::eventCallback,&s);
    chiaki_session_set_video_sample_cb(&s.session,Impl::videoCallback,&s);
    s.active=true;
    const auto startCode=chiaki_session_start(&s.session);
    if(startCode!=CHIAKI_ERR_SUCCESS){auto out=s.record(startCode,"chiaki_session_start");s.active=false;s.token.failed(out.code);(void)stop();return out;}
    s.started=true;return s.record(startCode,"chiaki_session_start");
}
BackendResult ChiakiBackend::stop(){
    if(!p_)return {true,0,"already_stopped"};
    auto& s=*p_;
    if(!s.onOwner())return {false,-1102,"wrong_owner_thread"};
    s.active=false;
    ChiakiErrorCode stopResult=CHIAKI_ERR_SUCCESS;
    if(s.started){
        ChiakiControllerState idle;chiaki_controller_state_set_idle(&idle);
        (void)s.record(chiaki_session_set_controller_state(&s.session,&idle),"release_controller");
        const auto stopCode=chiaki_session_stop(&s.session);
        stopResult=stopCode;
        // Join even if stop reported an error. Never fini while a callback is alive.
        const auto joinCode=chiaki_session_join(&s.session);
        if(joinCode!=CHIAKI_ERR_SUCCESS)return s.record(joinCode,"chiaki_session_join");
        s.started=false;s.connected=false;
        if(stopCode!=CHIAKI_ERR_SUCCESS)s.apiError=static_cast<int>(stopCode);
    }
    if(s.opusInitialized){chiaki_opus_decoder_fini(&s.opus);s.opusInitialized=false;}
    if(s.initialized){chiaki_session_fini(&s.session);s.initialized=false;}
    wipe(&s.session,sizeof(s.session));
    p_.reset();return result(stopResult,"stop_join_fini");
}
BackendResult ChiakiBackend::requestIdr(){
    if(!p_||!p_->onOwner()||!p_->started)return {false,-1103,"idr_without_session"};
    return p_->record(chiaki_session_request_idr(&p_->session),"chiaki_session_request_idr");
}
BackendResult ChiakiBackend::submitLoginPin(std::string_view pin){
    if(!p_||!p_->onOwner()||!p_->started)return {false,-1104,"pin_without_session"};
    if(pin.empty()||pin.size()>8||!std::all_of(pin.begin(),pin.end(),[](char c){return c>='0'&&c<='9';}))return {false,-1105,"invalid_login_pin"};
    return p_->record(chiaki_session_set_login_pin(&p_->session,reinterpret_cast<const std::uint8_t*>(pin.data()),pin.size()),"chiaki_session_set_login_pin");
}
BackendResult ChiakiBackend::submitController(const ControllerState& state){
    if(!p_||!p_->onOwner()||!p_->started)return {false,-1106,"controller_without_session"};
    ChiakiControllerState c;chiaki_controller_state_set_idle(&c);
    struct Mapping{std::uint32_t semantic;ChiakiControllerButton target;};
    static constexpr Mapping buttons[]={
        {ControllerState::Cross,CHIAKI_CONTROLLER_BUTTON_CROSS},{ControllerState::Circle,CHIAKI_CONTROLLER_BUTTON_MOON},
        {ControllerState::Square,CHIAKI_CONTROLLER_BUTTON_BOX},{ControllerState::Triangle,CHIAKI_CONTROLLER_BUTTON_PYRAMID},
        {ControllerState::Left,CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT},{ControllerState::Right,CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT},
        {ControllerState::Up,CHIAKI_CONTROLLER_BUTTON_DPAD_UP},{ControllerState::Down,CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN},
        {ControllerState::L1,CHIAKI_CONTROLLER_BUTTON_L1},{ControllerState::R1,CHIAKI_CONTROLLER_BUTTON_R1},
        {ControllerState::L3,CHIAKI_CONTROLLER_BUTTON_L3},{ControllerState::R3,CHIAKI_CONTROLLER_BUTTON_R3},
        {ControllerState::Options,CHIAKI_CONTROLLER_BUTTON_OPTIONS},{ControllerState::Share,CHIAKI_CONTROLLER_BUTTON_SHARE},
        {ControllerState::Touchpad,CHIAKI_CONTROLLER_BUTTON_TOUCHPAD},{ControllerState::PS,CHIAKI_CONTROLLER_BUTTON_PS}};
    for(auto b:buttons)if(state.buttons&b.semantic)c.buttons|=static_cast<std::uint32_t>(b.target);
    c.l2_state=state.l2;c.r2_state=state.r2;c.left_x=state.leftX;c.left_y=state.leftY;c.right_x=state.rightX;c.right_y=state.rightY;
    return p_->record(chiaki_session_set_controller_state(&p_->session,&c),"chiaki_session_set_controller_state");
}
NativeSnapshot ChiakiBackend::snapshot()const{
    // Like start/stop, pointer ownership requires the owner; the atomic counters
    // themselves may be updated by callbacks. Do not call concurrently with reset.
    if(!p_)return {};
    const auto& s=*p_;
    return {s.started.load(),s.connected.load(),s.warnings.load(),s.errors.load(),s.videoCallbacks.load(),s.audioCallbacks.load(),s.quitReason.load(),s.apiError.load()};
}
} // namespace veyra::remoteplay
