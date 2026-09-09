#pragma once

#include <filesystem>

namespace veyra::runtime {

std::filesystem::path applicationRoot();
std::filesystem::path shaderDirectory();
std::filesystem::path localRuntimeDirectory();
std::filesystem::path localDataDirectory();
std::filesystem::path logsDirectory();

} // namespace veyra::runtime
