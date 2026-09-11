#include "veyra/RuntimePaths.h"

#include <windows.h>

namespace veyra::runtime {
namespace {
std::filesystem::path executableDirectory()
{
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) return std::filesystem::current_path();
    return std::filesystem::path(path).parent_path();
}
}

std::filesystem::path applicationRoot()
{
    auto candidate = executableDirectory();
    for (unsigned depth = 0; depth != 6; ++depth) {
        if (std::filesystem::exists(candidate / "runtime_local")) return candidate;
        const auto parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) break;
        candidate = parent;
    }
    return executableDirectory();
}

std::filesystem::path shaderDirectory()
{
    const auto besideExecutable = executableDirectory() / "shaders";
    if (std::filesystem::exists(besideExecutable)) return besideExecutable;
    return applicationRoot() / "shaders";
}

std::filesystem::path localRuntimeDirectory() {
    const auto root=applicationRoot();
    return std::filesystem::exists(root/"runtime")?root/"runtime"/"experimental":root/"runtime_local"/"nvidia";
}
std::filesystem::path localDataDirectory() { return applicationRoot() / "runtime_local"; }
std::filesystem::path logsDirectory() { return applicationRoot() / "logs"; }

} // namespace veyra::runtime
