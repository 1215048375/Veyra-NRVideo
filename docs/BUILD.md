# Build / 构建

普通用户下载Release免安装包即可。以下用于开发者构建，不需要把SDK提交到Git。

## Requirements

- Windows 11 x64, Visual Studio 2022 C++ tools, Windows SDK, CMake 3.24+, Ninja.
- Local FFmpeg development libraries matching avcodec63 / avformat63 / avutil61 / swresample7 / swscale10. Release 1.0.0 uses the LGPL vcpkg FFmpeg 9.0.1#1 build with the Veyra H.264 slice-capacity patch.
- The PS5 H.264 repair included in 1.0.0 requires the additional [slice-capacity patch and matching rebuild](../scripts/ffmpeg/README.md). Stock 9.0.1 D3D12 H.264 has a 32-slice limit; the tested PS5 stream uses 68. Preserve `veyra-local-build.json` alongside the original vcpkg provenance in corresponding-source packages. Older 0.0.5 assets remain unchanged.
- NVIDIA DLSS SDK 310.7.0, Optical Flow SDK 5.0.7, RTX Video SDK 1.1.0, and nv-codec-headers. Prepare these under their respective licenses in ignored local directories.
- Intel XeSS SDK 3.0.2 for the XeSS presenter; AMD FidelityFX SDK 1.1.4 optical-flow/backend static libraries for the AMD flow option.
- Local NR runtime and NGX project configuration for experimental NR. The source repository intentionally does not contain them.

## Local Build

```powershell
git clone https://github.com/Likely7/Veyra-NRVideo.git
cd Veyra-NRVideo
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
```

`build.ps1` resolves Visual Studio and CMake, stages the five FFmpeg DLLs, and builds `out/build/x64-release/veyra.exe`. Its default FFmpeg location is `C:/veyra-deps/installed/x64-windows`. Use the CMake cache variables below for other dependency layouts. `-BuildDirectory` selects a separate output directory while an existing build is running.

| CMake variable | Local dependency |
| --- | --- |
| `VEYRA_FFMPEG_ROOT` | Prefix with FFmpeg include/lib/bin/share directories |
| `VEYRA_DLSS_SDK_ROOT` | NVIDIA DLSS SDK, including NGX static shim |
| `VEYRA_NVOF_SDK_ROOT` | Optical Flow SDK |
| `VEYRA_XESS_ROOT` | XeSS SDK `inc` directory parent |
| `VEYRA_FIDELITYFX_ROOT` | FidelityFX SDK `sdk` directory with built static libraries |
| `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR` | Enable the experimental NGX application targets |

The full player needs the NGX/NVOF and FFmpeg targets; a build with those dependencies absent is not the portable application's equivalent. CMake may disable optional XeSS/AMD flow when their local SDKs are missing. Inspect the configuration and test the actual backends before packaging.

The NGX configuration lives in `runtime_local/config/ngx-local.json` for development. A release places NVIDIA DLLs in `runtime/experimental` and the configuration in `runtime/config`. XeSS DLLs use `runtime_local/intel/experimental` in both layouts. Shader binaries live beside the executable in `shaders`.

## Dependency Sources

Release 1.0.0 includes `Veyra-1.0.0-FFmpeg-source.zip` separately: the patched FFmpeg source, SPDX-verified vcpkg port and patches, notices, and configuration queried from the shipped DLL. It is not needed to run Veyra.

- FFmpeg: https://github.com/FFmpeg/FFmpeg/tree/n9.0.1 ; vcpkg port source recorded in the distributed `licenses/FFMPEG-SPDX.json`.
- vcpkg FFmpeg port: https://github.com/microsoft/vcpkg/tree/55cd8b8a4f19d8e6ba2ad114c8acacc4af5915a0/ports/ffmpeg . Its patches and the LGPL notices are part of the corresponding-source material.
- FidelityFX SDK: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55 ; MIT license.
- XeSS SDK: https://github.com/intel/xess ; Intel Simplified Software License.
- DLSS SDK: https://github.com/NVIDIA/DLSS ; NVIDIA RTX SDK license.
- NVIDIA video SDK documentation: https://developer.nvidia.com/rtx-video-sdk . System NVENC/NVOF driver DLLs are not copied into the package.

Veyra and FidelityFX code are compiled into the executable; FFmpeg is dynamically linked. The executable uses the static MSVC runtime; FFmpeg and XeSS use the shipped redistributables `vcruntime140.dll`, `vcruntime140_1.dll`, and `msvcp140.dll`. UCRT, DirectX, and GPU driver components come from Windows and the installed driver.

## Verification and Packaging

Run targeted tests and `scripts/gates/delivery.ps1`; each individual test must stay below300seconds. Physical capture and screen scanout require separate hardware verification. The legacy Loop gate is not part of the current process.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 1.0.0 -OutputDirectory out/releases/1.0.0
```

The packager accepts `-BuildDirectory` for an isolated build, checks the publisher's fixed input files, copies an explicit payload, and emits a ZIP, checksums, and component manifests. It refuses to overwrite an existing candidate. This build-time audit does not restrict user DLL replacement. Do not upload SDK headers, samples, libraries, private media, logs, or development archives with the source.

## PS5 in 1.0.0

The release enables Remote Play; the default non-RemotePlay command above is a reduced build. Follow [REMOTEPLAY_BUILD_1.0.0.md](REMOTEPLAY_BUILD_1.0.0.md) for the full build, dependency source, patches and licensing.

## GPU DIS (1.0.0 experimental option)

The open-source subset in `third_party/gpu-dis` compiles with the existing Windows
SDK DXC; no additional GPU vendor SDK is required for DIS. `cmake/VeyraGpuDis.cmake`
retains the shader variants and compiler flags from `shader-recipes.json`.
Keep `shaders/dis/*.dxil`, `GpuDisLuma.dxil`, and `GpuDisValidate.dxil` when moving
an executable. Runtime requires D3D12 double-precision shader operations.
See `docs/GPU_DIS_INTEGRATION_PLAN_2026-09-13.md` for tests and limitations.
