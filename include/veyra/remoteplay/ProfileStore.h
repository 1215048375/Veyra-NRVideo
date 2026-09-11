#pragma once
#include "ChiakiBackend.h"
#include <filesystem>
#include <optional>
namespace veyra::remoteplay {
// Entire profile is protected for the current Windows user. Never serialize
// credentials to INI/JSON, command lines or diagnostic output.
bool saveProfile(const std::filesystem::path&, const NativeConnectRequest&);
std::optional<NativeConnectRequest> loadProfile(const std::filesystem::path&);
}
