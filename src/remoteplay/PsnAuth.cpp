// OAuth request contract adapted from chiaki-ng (AGPL-3.0-only + OpenSSL exception),
// gui/src/psnaccountid.cpp and psntoken.cpp, pinned 0e16950165f06e5c3291537c2eeba6e852be7120.
#include "veyra/remoteplay/PsnAuth.h"
#include "veyra/remoteplay/ProfileStore.h"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <json-c/json.h>
#include <chrono>
#include <fstream>
#include <memory>
namespace veyra::remoteplay {
namespace {
constexpr auto scope="psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update";
constexpr auto redirect="https://remoteplay.dl.playstation.net/remoteplay/redirect";
constexpr auto client="ba495a24-818c-472b-b12d-ff231c1b5745";
// Public upstream client identifier pair, not a user's credential.
constexpr auto clientPair="ba495a24-818c-472b-b12d-ff231c1b5745:mvaiZkRsAsI1IBkY";
void wipe(std::string& s){if(!s.empty())SecureZeroMemory(s.data(),s.size());}
struct Secret {std::string value;~Secret(){wipe(value);}};
struct Handle {HINTERNET h=nullptr;~Handle(){if(h)WinHttpCloseHandle(h);}};
struct Blob {DATA_BLOB b{};~Blob(){if(b.pbData){SecureZeroMemory(b.pbData,b.cbData);LocalFree(b.pbData);}}};
struct AuthorizationLock {
    HANDLE h=CreateMutexW(nullptr,FALSE,L"Local\\Veyra.PsnAuthorization");bool acquired=false;
    AuthorizationLock(){if(h){auto result=WaitForSingleObject(h,0);acquired=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;}}
    ~AuthorizationLock(){if(h){if(acquired)ReleaseMutex(h);CloseHandle(h);}}
};
using Json=std::unique_ptr<json_object,decltype(&json_object_put)>;
std::string field(json_object* o,const char* name){json_object* v=nullptr;return json_object_object_get_ex(o,name,&v)&&json_object_is_type(v,json_type_string)?json_object_get_string(v):"";}
std::string encode(std::string_view value){std::string out;constexpr char hex[]="0123456789ABCDEF";for(unsigned char c:value){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=c;else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
std::wstring wide(std::string_view s){return {s.begin(),s.end()};}
int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::optional<std::string> callbackCode(std::wstring_view input){
    const std::wstring prefix=wide(redirect)+L"?";
    if(input.size()>8192||!input.starts_with(prefix)||input.find(L'#')!=input.npos)return {};
    std::string code;unsigned codes=0;
    for(auto query=input.substr(prefix.size());!query.empty();){
        auto end=query.find(L'&');auto item=query.substr(0,end);auto equals=item.find(L'=');
        if(item.substr(0,equals)==L"code"&&equals!=item.npos){
            if(++codes!=1)return {};auto value=item.substr(equals+1);
            for(size_t i=0;i<value.size();++i){auto c=value[i];if(c==L'%'){
                if(i+2>=value.size())return {};auto hex=[](wchar_t v){return v>=L'0'&&v<=L'9'?int(v-L'0'):v>=L'a'&&v<=L'f'?int(v-L'a'+10):v>=L'A'&&v<=L'F'?int(v-L'A'+10):-1;};
                auto a=hex(value[++i]),b=hex(value[++i]);if(a<0||b<0)return {};c=wchar_t(a*16+b);
            }if(c<33||c>126)return {};code+=char(c);}
        }
        if(end==query.npos)break;query.remove_prefix(end+1);
    }
    if(codes!=1||code.empty()||code.size()>2048)return {};return code;
}
unsigned request(std::wstring path,std::string_view body,bool post,Secret& output,std::stop_token stop){
    if(stop.stop_requested())return ERROR_CANCELLED;
    Handle session{WinHttpOpen(L"Veyra Remote Play",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h)return GetLastError();WinHttpSetTimeouts(session.h,5000,5000,10000,10000);
    Handle connection{WinHttpConnect(session.h,L"auth.api.sonyentertainmentnetwork.com",INTERNET_DEFAULT_HTTPS_PORT,0)};
    if(!connection.h)return GetLastError();
    Handle req{WinHttpOpenRequest(connection.h,post?L"POST":L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    if(!req.h)return GetLastError();DWORD disabled=WINHTTP_DISABLE_REDIRECTS;WinHttpSetOption(req.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled));
    DWORD size=0;CryptBinaryToStringA(reinterpret_cast<const BYTE*>(clientPair),DWORD(strlen(clientPair)),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&size);
    std::string basic(size,'\0');CryptBinaryToStringA(reinterpret_cast<const BYTE*>(clientPair),DWORD(strlen(clientPair)),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,basic.data(),&size);basic.resize(strlen(basic.c_str()));
    auto headers=L"Content-Type: application/x-www-form-urlencoded\r\nAuthorization: Basic "+wide(basic)+L"\r\n";
    if(!WinHttpSendRequest(req.h,headers.c_str(),DWORD(-1),post?const_cast<char*>(body.data()):nullptr,post?DWORD(body.size()):0,post?DWORD(body.size()):0,0)||!WinHttpReceiveResponse(req.h,nullptr))return GetLastError();
    DWORD status=0,len=sizeof(status);if(!WinHttpQueryHeaders(req.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,WINHTTP_NO_HEADER_INDEX))return GetLastError();
    if(status!=200)return status;
    char bytes[4096];DWORD read=0;
    do{if(stop.stop_requested())return ERROR_CANCELLED;if(!WinHttpReadData(req.h,bytes,sizeof(bytes),&read))return GetLastError();output.value.append(bytes,read);SecureZeroMemory(bytes,sizeof(bytes));if(output.value.size()>65536)return ERROR_BUFFER_OVERFLOW;}while(read);
    return 0;
}
bool save(const PsnAuthorization& auth){
    Secret plain;plain.value=std::to_string(auth.expiresAt)+"\n"+accountIdToBase64(auth.accountId)+"\n"+auth.accessToken+"\n"+auth.refreshToken;
    DATA_BLOB input{DWORD(plain.value.size()),reinterpret_cast<BYTE*>(plain.value.data())};Blob encrypted;
    if(!CryptProtectData(&input,L"Veyra PSN authorization",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.b))return false;
    auto path=profileDirectory()/L"psn.auth",tmp=path;tmp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".tmp";
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
    std::ofstream out(tmp,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(encrypted.b.pbData),encrypted.b.cbData);out.close();
    if(!out||!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());return false;}return true;
}
PsnResult exchange(std::string grant,std::stop_token stop,std::optional<AccountId> knownAccount={}){
    AuthorizationLock lock;if(!lock.acquired)return {false,ERROR_BUSY};
    Secret body;body.value=std::move(grant)+"&scope="+encode(scope)+"&redirect_uri="+encode(redirect);
    Secret response;auto error=request(L"/2.0/oauth/token",body.value,true,response,stop);if(error)return {false,error};
    Json json(json_tokener_parse(response.value.c_str()),json_object_put);if(!json)return {false,ERROR_INVALID_DATA};
    PsnAuthorization auth;auth.accessToken=field(json.get(),"access_token");auth.refreshToken=field(json.get(),"refresh_token");
    json_object* expires=nullptr;if(!json_object_object_get_ex(json.get(),"expires_in",&expires)||!json_object_is_type(expires,json_type_int))return {false,ERROR_INVALID_DATA};
    const auto duration=json_object_get_int64(expires);
    if(auth.accessToken.empty()||auth.refreshToken.empty()||auth.accessToken.size()>8192||auth.refreshToken.size()>8192||duration<1||duration>31536000)return {false,ERROR_INVALID_DATA};
    if(auth.accessToken.find_first_of("\r\n")!=auth.accessToken.npos||auth.refreshToken.find_first_of("\r\n")!=auth.refreshToken.npos)return {false,ERROR_INVALID_DATA};
    auth.expiresAt=now()+duration;
    if(knownAccount){auth.accountId=*knownAccount;if(stop.stop_requested())return {false,ERROR_CANCELLED};return save(auth)?PsnResult{true,0,knownAccount}:PsnResult{false,ERROR_WRITE_FAULT};}
    Secret account;error=request(L"/2.0/oauth/token/"+wide(encode(auth.accessToken)),{},false,account,stop);if(error)return {false,error};
    Json details(json_tokener_parse(account.value.c_str()),json_object_put);if(!details)return {false,ERROR_INVALID_DATA};
    auto id=accountIdFromDecimal(field(details.get(),"user_id"));if(!id)return {false,ERROR_INVALID_DATA};auth.accountId=*id;
    if(stop.stop_requested())return {false,ERROR_CANCELLED};if(!save(auth))return {false,ERROR_WRITE_FAULT};return {true,0,id};
}
}
PsnAuthorization::~PsnAuthorization(){wipe(accessToken);wipe(refreshToken);}
std::wstring psnLoginUrl(){return L"https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn&response_type=code&client_id="+wide(client)+L"&redirect_uri="+wide(encode(redirect))+L"&scope="+wide(encode(scope))+L"&request_locale=zh_CN&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal";}
bool validPsnCallback(std::wstring_view callback){return callbackCode(callback).has_value();}
PsnResult authorizePsn(std::wstring callback,std::stop_token stop){auto code=callbackCode(callback);SecureZeroMemory(callback.data(),callback.size()*sizeof(wchar_t));if(!code)return {false,ERROR_INVALID_PARAMETER};Secret grant;grant.value="grant_type=authorization_code&code="+encode(*code);wipe(*code);return exchange(std::move(grant.value),stop);}
std::optional<PsnAuthorization> loadPsnAuthorization(){
    std::ifstream in(profileDirectory()/L"psn.auth",std::ios::binary|std::ios::ate);if(!in)return {};auto size=in.tellg();if(size<=0||size>65536)return {};std::string bytes(size_t(size),'\0');in.seekg(0);if(!in.read(bytes.data(),size))return {};
    DATA_BLOB input{DWORD(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())};Blob plain;if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.b))return {};
    Secret value;value.value.assign(reinterpret_cast<const char*>(plain.b.pbData),plain.b.cbData);size_t a=value.value.find('\n'),b=value.value.find('\n',a+1),c=value.value.find('\n',b+1);
    if(a==value.value.npos||b==value.value.npos||c==value.value.npos)return {};
    PsnAuthorization auth;try{auth.expiresAt=std::stoll(value.value.substr(0,a));}catch(...){return {};}
    auto id=accountIdFromBase64(value.value.substr(a+1,b-a-1));if(!id)return {};auth.accountId=*id;auth.accessToken=value.value.substr(b+1,c-b-1);auth.refreshToken=value.value.substr(c+1);if(auth.accessToken.empty()||auth.refreshToken.empty())return {};return auth;
}
PsnResult refreshPsn(std::stop_token stop){auto saved=loadPsnAuthorization();if(!saved)return {false,ERROR_NOT_FOUND};if(saved->expiresAt>now()+120)return {true,0,saved->accountId};return exchange("grant_type=refresh_token&refresh_token="+encode(saved->refreshToken),stop,saved->accountId);}
bool forgetPsnAuthorization(){AuthorizationLock lock;if(!lock.acquired)return false;std::error_code ec;auto path=profileDirectory()/L"psn.auth";return !std::filesystem::exists(path,ec)||std::filesystem::remove(path,ec);}
}
