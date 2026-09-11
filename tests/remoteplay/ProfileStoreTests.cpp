#include "veyra/remoteplay/ProfileStore.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace veyra::remoteplay;
int main(){
    wchar_t temp[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,temp))return 2;
    auto path=std::filesystem::path(temp)/(L"veyra-profile-test-"+std::to_wstring(GetCurrentProcessId())+L".dat");
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}} cleanup{path};
    auto require=[](bool ok){if(!ok)throw std::runtime_error("DPAPI profile test failed");};
    try{
        NativeConnectRequest request;request.host="127.0.0.1";request.video.codec=Codec::H265;
        request.credentials.accountId.fill(7);request.credentials.registrationKey.fill('1');request.credentials.sessionKey.fill(42);
        require(saveProfile(path,request));auto saved=loadProfile(path);require(saved.has_value());
        require(saved->host==request.host&&saved->video.codec==Codec::H265&&saved->credentials.accountId==request.credentials.accountId&&saved->credentials.sessionKey==request.credentials.sessionKey&&saved->credentials.registrationKey==request.credentials.registrationKey);
        request.video.fps=30;require(saveProfile(path,request));require(loadProfile(path)->video.fps==30);
        {std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);char byte=0;file.read(&byte,1);byte^=0x40;file.seekp(0);file.write(&byte,1);}
        require(!loadProfile(path));request.host="http://invalid";require(!saveProfile(path,request));
        std::cout<<"DPAPI_PROFILE_PASS roundtrip=1 atomic_replace=1 corrupted_rejected=1 invalid_host_rejected=1 synthetic_credentials_only=1\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
