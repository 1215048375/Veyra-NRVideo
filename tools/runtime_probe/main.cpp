// veyra_runtime_probe - Phase 0 skeleton stub.
// Real probe behavior (logger, file identity, D3D12 device context, runtime
// loading, window loop) lands in P0.5-P0.7; this stub only proves that the
// CMake/C++20 x64 skeleton configures, builds and runs.
#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    std::wprintf(L"veyra_runtime_probe skeleton (phase 0 stub)\n");
    std::wprintf(L"argc=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
        std::wprintf(L"argv[%d]=%ls\n", i, argv[i]);
    }
    return 0;
}
