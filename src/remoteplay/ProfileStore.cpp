#include "veyra/remoteplay/ProfileStore.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
namespace veyra::remoteplay {
namespace {
struct SecretBytes {std::vector<uint8_t> data;~SecretBytes(){SecureZeroMemory(data.data(),data.size());}};
struct ProtectedBlob {DATA_BLOB value{};~ProtectedBlob(){if(value.pbData){SecureZeroMemory(value.pbData,value.cbData);LocalFree(value.pbData);}}};
void append32(std::vector<uint8_t>& b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(8*i)));}
uint32_t read32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
}
bool saveProfile(const std::filesystem::path& path,const NativeConnectRequest& request){
    if(!validHost(request.host)||request.video.validate())return false;
    SecretBytes plain;
    auto& b=plain.data;b.insert(b.end(),{'V','R','P',1});
    append32(b,uint32_t(request.host.size()));append32(b,request.video.width);append32(b,request.video.height);
    append32(b,request.video.fps);append32(b,request.video.bitrateKbps);append32(b,uint32_t(request.video.codec));
    b.insert(b.end(),request.credentials.accountId.begin(),request.credentials.accountId.end());
    b.insert(b.end(),request.credentials.registrationKey.begin(),request.credentials.registrationKey.end());
    b.insert(b.end(),request.credentials.sessionKey.begin(),request.credentials.sessionKey.end());
    b.insert(b.end(),request.host.begin(),request.host.end());
    DATA_BLOB input{DWORD(b.size()),b.data()};ProtectedBlob encrypted;
    if(!CryptProtectData(&input,L"Veyra PS5 pairing",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.value))return false;
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
    auto temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".tmp";
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,encrypted.value.pbData,encrypted.value.cbData,&written,nullptr)&&written==encrypted.value.cbData;
    if(ok)ok=FlushFileBuffers(file)!=FALSE;CloseHandle(file);
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temp.c_str());return ok;
}
std::optional<NativeConnectRequest> loadProfile(const std::filesystem::path& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return {};
    const auto size=f.tellg();if(size<=0||size>8192)return {};
    std::vector<uint8_t> encrypted(static_cast<size_t>(size));f.seekg(0);if(!f.read(reinterpret_cast<char*>(encrypted.data()),size))return {};
    DATA_BLOB input{DWORD(encrypted.size()),encrypted.data()};ProtectedBlob plain;
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.value))return {};
    const auto* p=plain.value.pbData;const auto n=plain.value.cbData;
    constexpr size_t header=28,credentials=40;
    if(n<header+credentials||std::memcmp(p,"VRP\1",4)!=0)return {};
    const auto hostSize=read32(p+4);
    if(hostSize>253||n!=header+credentials+hostSize||read32(p+24)>1)return {};
    NativeConnectRequest r;r.video.width=read32(p+8);r.video.height=read32(p+12);r.video.fps=read32(p+16);
    r.video.bitrateKbps=read32(p+20);r.video.codec=Codec(read32(p+24));
    std::memcpy(r.credentials.accountId.data(),p+header,8);std::memcpy(r.credentials.registrationKey.data(),p+header+8,16);
    std::memcpy(r.credentials.sessionKey.data(),p+header+24,16);
    r.host.assign(reinterpret_cast<const char*>(p+header+credentials),hostSize);
    if(!validHost(r.host)||r.video.validate())return {};
    return r;
}
}
