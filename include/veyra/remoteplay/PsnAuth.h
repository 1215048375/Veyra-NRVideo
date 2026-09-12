#pragma once
#include "Types.h"
#include <filesystem>
#include <stop_token>
namespace veyra::remoteplay {
struct PsnAuthorization {
    std::string accessToken,refreshToken;
    AccountId accountId{};
    int64_t expiresAt=0;
    ~PsnAuthorization();
};
struct PsnResult {bool ok=false;unsigned error=0;std::optional<AccountId> account;};
std::wstring psnLoginUrl();
bool validPsnCallback(std::wstring_view);
PsnResult authorizePsn(std::wstring callback,std::stop_token);
PsnResult refreshPsn(std::stop_token);
std::optional<PsnAuthorization> loadPsnAuthorization();
bool forgetPsnAuthorization();
}
