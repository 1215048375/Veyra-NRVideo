# Veyra DLSS Video Player

## Agent 施工手册 V1

文档日期：2026-09-01  
适用目录：`C:\Users\123\Desktop\Veyra DLSS Video Player`  
产品方案基线：`VEYRA_PRODUCT_SPEC_V1.md`

这不是概念方案，而是交给下一位 Agent 的执行合同。目标是让能力一般的模型也能按固定顺序搭建、调用、验收，并且在失败时知道停在哪里。除非用户明确改需求，不准擅自换技术栈、扩大 V1 或跳阶段。

---

# 0. 先说结论

Veyra V1 采用下面这条唯一主线：

~~~text
FFmpeg demux/decode
  ↓
D3D12VA surface（早期 harness 可临时 CPU upload）
  ↓
YUV → linear working RGB
  ↓
可选 DLSS Super Resolution
  ↓
保留 Original FP16
  ↓
RenoDX-equivalent Proxy Encode
  ↓
动态加载 nvngx_dlssnr.dll
  ↓
NGX Feature 18 Create/Evaluate
  ↓
RenoDX-equivalent Parity Decode
  ↓
可选 DLSS Frame Generation 2X
  ↓
最后叠加播放器 UI
  ↓
DXGI Present
~~~

关键工程决定：

- Windows x64、C++20、Win32、D3D12。
- 所有 GPU 阶段共用一个 DXGI adapter、一个 `ID3D12Device` 和主 direct queue。
- V1 直接用 NGX SDK。不要同时集成 Streamline；那会增加 swapchain、resource tagging、plugin lifecycle 和本地 Feature 18 adapter 之间的变量。
- `nvngx_dlssnr.dll` 不是链接库，必须用 `LoadLibraryExW` 动态加载并解析导出。
- `renodx-dlss5-1.addon64` 不是配置文件，更不是播放器依赖。它只用于确认参数名、shader 数学和参考输出。
- “类似 ReShade”不是实现通用 ReShade shader 加载器；只复现当前 RenoDX DLSS5 add-on 的颜色代理编码、Feature 18 参数和输出恢复。
- 第一件事不是完整播放器，而是固定帧的 `veyra_nr_harness`。连续 300 帧成功之前，不做 FFmpeg/UI/音频/SR/FG。
- 已接受 Magpie Experimental 对“Feature 18 能调用、DLSSG 能执行”的可行性证明。这里仍要做接口和时序回归测试，因为“别人跑通过”不等于本工程不会写错参数。

---

# 1. 当前项目里有什么

当前目录还不是代码仓库，也没有应用源码。有效控制面、方案和本地二进制是：

~~~text
README.md
AGENTS.md
VEYRA_PRODUCT_SPEC_V1.md
VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md
loop/
scripts/
docs/WORKLOG.md
nvngx_dlssnr.dll
renodx-dlss5-1.addon64
~~~

不存在仍有效的历史方案；不要联网找旧副本，也不要从旧文档恢复被当前边界否决的功能。

## 1.1 DLSSNR 文件

~~~text
File:        nvngx_dlssnr.dll
Size:        165840496 bytes
SHA256:      E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
Version:     310.8.0.0
Description: NVIDIA DLSSNR - DVS PRODUCTION
Signature:   Valid NVIDIA Authenticode signature
Timestamp:   2026-08-12 04:29:33
Min driver:  615.00（来自文件 version resource）
GPU target:  Blackwell2（来自文件 version resource）
NGX API:     0x0000013（来自文件 version resource）
~~~

已确认的 D3D12 导出：

~~~text
NVSDK_NGX_D3D12_Init_Ext
NVSDK_NGX_D3D12_CreateFeature
NVSDK_NGX_D3D12_EvaluateFeature
NVSDK_NGX_D3D12_ReleaseFeature
NVSDK_NGX_D3D12_Shutdown1
~~~

注意：文件也含 D3D11/Vulkan/CUDA 通用符号，但内部文本明确显示当前 D3D11 路径未完成。V1 不碰 D3D11 Feature 18。

## 1.2 RenoDX 文件

~~~text
File:        renodx-dlss5-1.addon64
Size:        359424 bytes
SHA256:      837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F
Version:     0.2026.0827.2036
Signature:   NotSigned
Internal:    RenoDX.DLSS5 / DLSS 5 Neural Rendering
~~~

它是 ReShade binary add-on，通过 ReShade add-on API 保存配置和 hook NGX。最终 Veyra 不加载它。它已经给出三个重要事实：

1. 插入点是游戏 DLSS 输出之后、UI 之前；
2. Feature 18 的参数字符串和资源合同；
3. Control-compatible soft-clip/sRGB/UpgradeToneMap codec 的 shader 数学。

## 1.3 当前机器

~~~text
OS:          Windows 11 Pro for Workstations, build 26200
GPU:         NVIDIA GeForce RTX 5070
Driver:      616.56
VRAM:        12227 MiB
Compute:     12.0
nvofapi64:   32.0.16.1656, signed, present in System32
~~~

这台机器满足当前 DLL 标注的 driver/GPU 条件。不要把这个事实扩张成“所有 NVIDIA GPU 都支持”。V1 对修改过的 Ada runtime 不提供支持。

## 1.4 开发工具，已安装但多数不在普通 PATH

~~~text
Visual Studio Build Tools:
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools

MSVC:
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207

CMake 3.31.6:
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

Ninja 1.12.1:
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

MSBuild:
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe

DXC:
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe

Git:
C:\Program Files\Git\cmd\git.exe
~~~

普通终端找不到 `cmake` 不代表没装。优先用以上绝对路径或从 VS Developer PowerShell 启动。

---

# 2. 真源、参考源和不能抄的东西

按下面优先级判断事实：

1. 本目录两个二进制的 hash、导出、内嵌参数/数学；
2. 本文与 Product Spec 的已锁定产品边界；
3. NVIDIA 官方 SDK 头文件、sample 和文档；
4. Magpie Experimental 的可观察调用顺序；
5. 其他博客、论坛和一键整合包一律不作为实现真源。

## 2.1 Magpie 只作行为参考

固定参考：

~~~text
Repo:    https://github.com/SAOG0721/Magpie
Branch:  experimental
Commit:  e15394a9e15b996b52ad92c6bf22747b2e7ae46d
License: GPLv3
~~~

重点阅读文件：

~~~text
src/Magpie.Core/NgxD3D12Core.cpp
src/Magpie.Core/NgxD3D12Core.h
src/Magpie.Core/DLSSNRFilter.cpp
src/Magpie.Core/DLSSNRFilter.h
src/Magpie.Core/DLSSFrameGenerator.cpp
src/Magpie.Core/DLSSFrameGenerator.h
src/Magpie.Core/FrameGuidanceTypes.h
src/Magpie.Core/NvidiaOpticalFlowProvider.cpp
~~~

闭源 Veyra 不得复制这些文件或机械改名。应依据公开 NGX/NVOF 接口和本文列出的行为独立实现。如果用户决定整个项目接受 GPLv3，再单独评估复用。

不要复用 Magpie 的 NGX Project ID。Veyra 必须使用自己的 Project ID；本地实验可生成一个持久 GUID，发布前必须换成 NVIDIA 分配/允许的身份。

## 2.2 不从非官方来源拿 DLL

不要去 Discord、网盘、论坛或所谓“一键包”下载别的 `nvngx_dlssnr.dll`、`nvngx_dlss.dll`、patched Ada DLL。只使用当前根目录已给出的 DLSSNR 文件和 NVIDIA 官方 DLSS SDK 里的公开 SR/FG runtime。

---

# 3. 依赖从哪里拿、拿什么、放哪里

## 3.1 目录约定

创建以下三个本地目录，并全部加入 `.gitignore`：

~~~text
runtime_local/       # 本机 proprietary feature DLL 和 manifest
third_party_local/   # 下载/解压的 SDK 与 vcpkg
reference_local/     # Magpie 源码快照、ReShade 对照材料
~~~

公开的本项目源码放在 `src/`、`shaders/`、`tools/`、`tests/`；第三方 SDK 不复制进这些目录。

## 3.2 NVIDIA DLSS SDK 310.7.0

来源：

~~~text
Release: https://github.com/NVIDIA/DLSS/releases/tag/v310.7.0
Repo:    https://github.com/NVIDIA/DLSS
Tag:     v310.7.0
~~~

下载 release ZIP 或 clone 后 checkout tag，解压到：

~~~text
third_party_local/nvidia/DLSS_SDK_310.7.0/
~~~

至少确认这些文件存在：

~~~text
include/nvsdk_ngx.h
include/nvsdk_ngx_defs.h
include/nvsdk_ngx_helpers.h
include/nvsdk_ngx_helpers_dlssg.h
include/nvsdk_ngx_params.h
include/nvsdk_ngx_params_dlssg.h

lib/Windows_x86_64/x64/nvsdk_ngx_s.lib
lib/Windows_x86_64/rel/nvngx_dlss.dll
lib/Windows_x86_64/rel/nvngx_dlssg.dll
~~~

用途：

- `nvsdk_ngx_s.lib`：NGX D3D12 core 入口；
- `nvsdk_ngx_helpers.h`：DLSS Super Resolution helper；
- `nvsdk_ngx_helpers_dlssg.h`：DLSS Frame Generation helper；
- `nvngx_dlss.dll` / `nvngx_dlssg.dll`：Phase 4/6 的公开 feature runtime。

不要用 SDK 的 `dev` DLL 做验收；正常使用 `rel`。不要用它覆盖根目录 `nvngx_dlssnr.dll`，这不是同一个 feature。

## 3.3 NVIDIA Optical Flow SDK 5.0

来源：

~~~text
Download: https://developer.nvidia.com/opticalflow/download
Docs:     https://docs.nvidia.com/video-technologies/optical-flow-sdk/index.html
Guide:    https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html
~~~

下载需要 NVIDIA Developer Program 登录并接受 EULA，Agent 不得绕过。让用户在官方页面下载，然后解压到：

~~~text
third_party_local/nvidia/Optical_Flow_SDK_5.0/
~~~

解压后用 `rg --files` 找并确认：

~~~text
nvOpticalFlowCommon.h
nvOpticalFlowD3D12.h
NvOFUtils...D3D12...（名称以 SDK 实际 sample 为准）
D3D12 optical-flow sample
~~~

运行时 `nvofapi64.dll` 来自 NVIDIA 驱动的 System32，不从 SDK 随包复制。程序用：

~~~text
LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)
GetProcAddress(..., "NvOFAPICreateInstanceD3D12")
GetProcAddress(..., "NvOFGetMaxSupportedApiVersion")
~~~

不要链接或分发一个来路不明的 `nvofapi64.dll`。

## 3.4 FFmpeg 9.0.1 与 Dear ImGui 1.92.8

V1 用 vcpkg manifest 锁版本。固定 registry baseline：

~~~text
vcpkg commit:
30ef65cad98f08e7197c9a1656fbd871bcb72f2d

该 baseline 的 port：
ffmpeg 9.0.1#1
imgui 1.92.8#1
~~~

将 vcpkg clone 到：

~~~powershell
git clone https://github.com/microsoft/vcpkg.git third_party_local/vcpkg
git -C third_party_local/vcpkg checkout 30ef65cad98f08e7197c9a1656fbd871bcb72f2d
& .\third_party_local\vcpkg\bootstrap-vcpkg.bat -disableMetrics
~~~

项目根目录的 `vcpkg.json` 使用：

~~~json
{
  "name": "veyra-dlss-video-player",
  "version-string": "0.1.0",
  "builtin-baseline": "30ef65cad98f08e7197c9a1656fbd871bcb72f2d",
  "dependencies": [
    {
      "name": "ffmpeg",
      "default-features": false,
      "features": [
        "avcodec",
        "avformat",
        "swresample",
        "swscale"
      ]
    },
    {
      "name": "imgui",
      "features": [
        "dx12-binding",
        "win32-binding"
      ]
    }
  ]
}
~~~

不要启用 `gpl`、`nonfree`、`x264`、`x265` 或 `fdk-aac`。播放器只需要解码、封装、重采样和早期软件转换 fallback。

FFmpeg CMake 接法：

~~~cmake
find_package(FFMPEG REQUIRED)
target_include_directories(veyra_media PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(veyra_media PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(veyra_media PRIVATE ${FFMPEG_LIBRARIES})
~~~

ImGui 接法：

~~~cmake
find_package(imgui CONFIG REQUIRED)
target_link_libraries(veyra_ui PRIVATE imgui::imgui)
~~~

用 `x64-windows` 动态 triplet，避免把 FFmpeg 静态链接义务藏进可执行文件。发布阶段仍需附 LGPL notice 和对应动态库许可证。

## 3.5 Streamline

参考版本：

~~~text
https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.12.0
~~~

V1 不下载、不链接、不初始化 Streamline。原因不是它不能用，而是当前主线已经用直接 NGX，混用会把 swapchain proxy、resource tagging、plugin lifecycle 和本地 Feature 18 adapter 搅在一起。等直接 NGX 的 SR/NR/FG 全部稳定后，才允许另开分支评估 Streamline。

---

# 4. 第一次开工的精确步骤

## 4.1 先保护二进制，再初始化 Git

当前目录不是 Git 仓库。必须先创建 `.gitignore`，至少包含：

~~~gitignore
# Proprietary / local-only
/nvngx_dlssnr.dll
/renodx-dlss5-1.addon64
/runtime_local/
/third_party_local/
/reference_local/

# Build
/out/
/build/
/.vs/
/vcpkg_installed/
CMakeUserPresets.json

# Diagnostics and captures
/captures/
/logs/
*.pdb
*.ilk
*.dmp
*.etl
~~~

确认忽略规则后才执行。Goal 模式必须同时遵守 `loop/LOOP_ENGINE.md` 的 P0.2 baseline/branch 流程：

~~~powershell
git init -b main
git status --short --ignored
git check-ignore -v --no-index -- nvngx_dlssnr.dll renodx-dlss5-1.addon64 runtime_local/.probe third_party_local/.probe reference_local/.probe captures/.probe logs/.probe loop/STOP
~~~

如果上述任一敏感路径没有显示为 ignored，禁止 staging。初始化 baseline 时只显式 `git add` 已审查的源码/文档路径并先检查 `git diff --cached --name-only`；不要使用 `git add .`。若仓库已存在，不重新 init、不重写历史。

## 4.2 验证本地输入

从根目录运行：

~~~powershell
Get-Item -LiteralPath '.\nvngx_dlssnr.dll','.\renodx-dlss5-1.addon64' | Select-Object Name,Length
Get-FileHash -Algorithm SHA256 -LiteralPath '.\nvngx_dlssnr.dll','.\renodx-dlss5-1.addon64'
Get-AuthenticodeSignature -LiteralPath '.\nvngx_dlssnr.dll','.\renodx-dlss5-1.addon64' | Select-Object Path,Status,StatusMessage
nvidia-smi --query-gpu=name,driver_version,memory.total,compute_cap --format=csv,noheader
~~~

必须得到第 1 节列出的 size/hash。DLSSNR 必须 `Valid`；addon 预期 `NotSigned`。任何变化都写入 `docs/WORKLOG.md` 并停工。

## 4.3 建立 runtime_local

只复制，不移动根目录原件：

~~~text
runtime_local/
└─ nvidia/
   ├─ nvngx_dlssnr.dll       # 从项目根目录复制
   ├─ nvngx_dlss.dll         # Phase 4 才从官方 DLSS 310.7 rel 复制
   ├─ nvngx_dlssg.dll        # Phase 6 才从官方 DLSS 310.7 rel 复制
   └─ runtime-manifest.json
~~~

Phase 1 只放 `nvngx_dlssnr.dll`。不要为了“看起来齐全”提前塞入论坛里的 310.8 SR/FG DLL。

`runtime-manifest.json` 初始内容：

~~~json
{
  "schema": 1,
  "mode": "local-experimental-only",
  "files": [
    {
      "name": "nvngx_dlssnr.dll",
      "size": 165840496,
      "sha256": "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E",
      "fileVersion": "310.8.0.0",
      "authenticode": "Valid",
      "source": "user-provided workspace file",
      "redistributable": false
    }
  ]
}
~~~

Phase 4/6 加 DLL 时，Agent 必须计算各自 hash 并追加 manifest，不能写“latest”。

## 4.4 创建本地 NGX 身份

不要复用 Magpie 的 Project ID。执行一次：

~~~powershell
New-Item -ItemType Directory -Force '.\runtime_local\config'
$id = [guid]::NewGuid().ToString()
@{
  ngxProjectId = $id
  engineType = 'custom'
  engineVersion = 'Veyra-Experimental-0.1.0'
} | ConvertTo-Json | Set-Content '.\runtime_local\config\ngx-local.json' -Encoding utf8
~~~

这是本机研发身份，不代表 NVIDIA 授权的发行身份。准备发布前，必须替换为合规 Project ID。

## 4.5 建立工程骨架

最终目录：

~~~text
Veyra DLSS Video Player/
├─ README.md
├─ AGENTS.md
├─ VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md
├─ VEYRA_PRODUCT_SPEC_V1.md
├─ loop/
├─ scripts/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ vcpkg.json
├─ .gitignore
├─ cmake/
│  ├─ VeyraWarnings.cmake
│  ├─ VeyraShaders.cmake
│  └─ VeyraRuntime.cmake
├─ config/
│  ├─ nr-default.json
│  ├─ parity-default.json
│  └─ player-default.json
├─ docs/
│  ├─ WORKLOG.md
│  ├─ DECISIONS.md
│  └─ RUNTIME_REPORT.md
├─ include/veyra/
│  ├─ Result.h
│  ├─ Log.h
│  └─ FrameTypes.h
├─ src/
│  ├─ app/
│  ├─ gfx/
│  ├─ media/
│  ├─ ngx/
│  ├─ parity/
│  ├─ guidance/
│  ├─ player/
│  └─ ui/
├─ shaders/
│  ├─ GenerateTestPattern.hlsl
│  ├─ YuvToLinearRgb.hlsl
│  ├─ ParityEncode.hlsl
│  ├─ ParityDecode.hlsl
│  ├─ NvofVectorConvert.hlsl
│  └─ Present.hlsl
├─ tools/
│  ├─ runtime_probe/
│  ├─ nr_harness/
│  └─ parity_capture/
├─ tests/
│  ├─ unit/
│  └─ integration/
├─ validation/
│  ├─ manifests/
│  ├─ fixed_frames/
│  └─ fixed_clips/
├─ scripts/
│  ├─ configure.ps1
│  ├─ build.ps1
│  ├─ stage-runtime.ps1
│  └─ verify-runtime.ps1
├─ runtime_local/             # ignored
├─ third_party_local/         # ignored
├─ reference_local/           # ignored
└─ out/                       # ignored
~~~

不要一开始创建几十个空类。Phase 0 只建立实际要编译的 `gfx`、`ngx`、`tools/runtime_probe` 和 `tools/nr_harness`。

---

# 5. CMake 和构建方式

## 5.1 CMake cache 变量

根 `CMakeLists.txt` 必须定义：

~~~cmake
set(VEYRA_DLSS_SDK_ROOT "" CACHE PATH "NVIDIA DLSS SDK root")
set(VEYRA_NVOF_SDK_ROOT "" CACHE PATH "NVIDIA Optical Flow SDK root")
set(VEYRA_RUNTIME_ROOT "" CACHE PATH "Local NVIDIA runtime root")

option(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR
  "Enable local-only experimental Feature 18 adapter" OFF)
option(VEYRA_ENABLE_DLSS_SR "Enable public DLSS SR backend" OFF)
option(VEYRA_ENABLE_DLSS_FG "Enable public DLSSG backend" OFF)
option(VEYRA_ENABLE_NVOF "Enable NVIDIA Optical Flow guidance" OFF)
option(VEYRA_ENABLE_D3D12_DEBUG "Enable D3D12 debug layer" ON)
~~~

如果启用 NR 而 SDK/runtime 路径缺失，配置阶段直接 `message(FATAL_ERROR)`。不要拖到运行时才报头文件或 DLL 找不到。

## 5.2 编译目标

按阶段创建：

~~~text
veyra_base              logging/result/config/hash/signature
veyra_gfx               D3D12 device/queue/resources/barriers/profiler
veyra_ngx               NGX core + NR runtime adapter + later SR/FG
veyra_parity            parity shaders/profile/capture
veyra_guidance          zero guidance + later NVOF
veyra_media             FFmpeg demux/decode/audio
veyra_player_core       scheduler/pipeline
veyra_ui                Win32/ImGui

veyra_runtime_probe.exe
veyra_nr_harness.exe
veyra_parity_tests.exe
veyra_player.exe
~~~

不要让 `veyra_player.exe` 直接拥有 Feature 18 调用细节。它只面向 `IDlssNrBackend`。

## 5.3 系统库

基础链接：

~~~cmake
target_link_libraries(veyra_gfx PUBLIC
  d3d12
  dxgi
  dxguid
)

target_link_libraries(veyra_base PUBLIC
  bcrypt
  wintrust
  crypt32
  version
  shlwapi
)
~~~

全项目：

~~~cmake
target_compile_features(veyra_base PUBLIC cxx_std_20)
target_compile_definitions(veyra_base PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
~~~

MSVC 警告至少 `/W4 /permissive- /Zc:__cplusplus`。不要全局 `/WX` 阻塞第三方头；只对本项目 target 开启。

NGX：

~~~cmake
target_include_directories(veyra_ngx PRIVATE
  "${VEYRA_DLSS_SDK_ROOT}/include")
target_link_libraries(veyra_ngx PRIVATE
  "${VEYRA_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
~~~

## 5.4 Shader 编译

使用已安装 DXC：

~~~text
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe
~~~

目标：

~~~text
Entry: main
Target: cs_6_0
Debug:  -Zi -Qembed_debug -Od
Release:-O3 -Qstrip_debug -Qstrip_reflect
~~~

CMake custom command 将每个 HLSL 编译为 `out/<preset>/shaders/*.dxil`。运行时不调用 D3DCompile 编译字符串；这样 shader 错误在构建阶段暴露。

## 5.5 Preset

至少提供：

~~~text
x64-debug
x64-release
~~~

生成命令示例：

~~~powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$vcpkgToolchain = (Resolve-Path '.\third_party_local\vcpkg\scripts\buildsystems\vcpkg.cmake')
$dlssRoot = (Resolve-Path '.\third_party_local\nvidia\DLSS_SDK_310.7.0')
$runtimeRoot = (Resolve-Path '.\runtime_local\nvidia')
& $cmake --preset x64-debug -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" -DVCPKG_TARGET_TRIPLET=x64-windows -DVEYRA_DLSS_SDK_ROOT="$dlssRoot" -DVEYRA_RUNTIME_ROOT="$runtimeRoot" -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON
& $cmake --build --preset x64-debug
& $cmake --build --preset x64-debug --target veyra_nr_harness
~~~

不要把这些机器绝对路径提交进 `CMakePresets.json`。共享 preset 用 cache variable 占位；本机覆盖放 ignored 的 `CMakeUserPresets.json` 或让 `scripts/configure.ps1` 解析。

---

# 6. GPU 基础层，必须先写对

## 6.1 Adapter 和 device

`D3D12DeviceContext` 初始化顺序：

1. 可选启用 `ID3D12Debug`；
2. `CreateDXGIFactory2`；
3. `IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)`；
4. 只选 `VendorId == 0x10DE` 且非 software adapter；
5. `D3D12CreateDevice`，最低 feature level 12_0；
6. 创建 direct command queue；
7. 创建 fence/event；
8. 记录 adapter description、LUID、dedicated memory、driver/runtime report。

不要偷偷 fallback 到 WARP 后继续报告“DLSS 已启用”。runtime probe 可显示 WARP，但 NR backend 必须返回 unsupported。

## 6.2 Command slots

创建 4 个 slot，每个拥有：

~~~text
ID3D12CommandAllocator
fenceValue
2 个 timestamp query index
临时 descriptor range
保持本帧 AVFrame/texture 存活的引用
~~~

每帧只等待“将要复用的 slot 的 fence”，不是等待刚提交的当前帧。正常队列深度最多 1 个待处理 real frame。Phase 1 创建 Feature 后允许同步等待一次。

## 6.3 Barrier 原则

建立统一 `ResourceStateTracker`，不要在各 backend 猜状态。NR Evaluate 前：

~~~text
Color / MVec / Depth:
  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE

Output:
  D3D12_RESOURCE_STATE_UNORDERED_ACCESS
~~~

Evaluate 后对 Output 发 UAV barrier，再转为后续 parity shader 的 `NON_PIXEL_SHADER_RESOURCE`。如果官方对应 SDK 版本明确要求不同状态，以官方头/sample 为准，同时更新本表和 WORKLOG；禁止一个资源在代码里同时被两套 state tracker 管。

所有 NR/parity pass 放在同一 direct command list/queue，避免跨 queue fence 增加延迟。

## 6.4 纹理合同

Phase 1 只要求 Proxy/Neural/Zero Guidance；Phase 2 再加入 Original/Final：

~~~text
Proxy:      R8G8B8A8_UNORM,     1920x1080, ALLOW_UNORDERED_ACCESS，Phase 1 直接生成
Neural:     R8G8B8A8_UNORM,     1920x1080, ALLOW_UNORDERED_ACCESS
ZeroMotion: R16G16_FLOAT,        1920x1080, 全 0
ZeroDepth:  R32_FLOAT,           1920x1080, 全 0
Confidence: R8_UNORM,            1920x1080, 全 0，仅 guidance 内部

Original:   R16G16B16A16_FLOAT, 1920x1080, ALLOW_UNORDERED_ACCESS，Phase 2 加入
Final:      R16G16B16A16_FLOAT, 1920x1080, ALLOW_UNORDERED_ACCESS，Phase 2 加入
~~~

不允许把 motion/depth 设为 null。即使不用真实 guidance，也传入尺寸正确、格式正确、全零的纹理。

---

# 7. NGX Core Host

文件：

~~~text
src/ngx/NgxCoreHost.h
src/ngx/NgxCoreHost.cpp
src/ngx/NgxResult.cpp
src/ngx/NgxParameters.h
~~~

## 7.1 一进程一次

`NgxCoreHost` 对一个 D3D12 device 只初始化一次。SR、NR、FG 是 consumer，不得各自调用一套 core Init/Shutdown。

初始化伪代码：

~~~cpp
const wchar_t* featurePaths[] = { runtimeDir.c_str() };
NVSDK_NGX_FeatureCommonInfo common{};
common.PathListInfo.Path = featurePaths;
common.PathListInfo.Length = 1;

auto result = NVSDK_NGX_D3D12_Init_with_ProjectID(
    veyraProjectId.c_str(),
    NVSDK_NGX_ENGINE_TYPE_CUSTOM,
    "Veyra-Experimental-0.1.0",
    runtimeDir.c_str(),
    device,
    &common,
    NVSDK_NGX_Version_API);
~~~

所有参数块统一由 core 创建/销毁：

~~~text
NVSDK_NGX_D3D12_AllocateParameters
NVSDK_NGX_D3D12_GetCapabilityParameters
NVSDK_NGX_D3D12_DestroyParameters
~~~

最终 consumer 和 parameter block 都释放后，才调用：

~~~text
NVSDK_NGX_D3D12_Shutdown1(device)
~~~

## 7.2 外部调用保护

当前 runtime 是实验构建。每个 NGX 外部入口包在 MSVC SEH 边界：

~~~cpp
__try {
  result = external_call(...);
} __except(EXCEPTION_EXECUTE_HANDLER) {
  sehCode = GetExceptionCode();
  result = NVSDK_NGX_Result_FAIL_PlatformError;
}
~~~

SEH 不是正常控制流。每次触发都视为失败，记录 operation、feature、result hex、SEH code，停止当前 backend，不能吞掉后继续 present 垃圾纹理。

---

# 8. Feature 18 Runtime Adapter：精确调用协议

文件：

~~~text
src/ngx/DlssNrBackend.h
src/ngx/DlssNrBackend.cpp
src/ngx/DlssNrRuntimeAdapter.h
src/ngx/DlssNrRuntimeAdapter.cpp
src/ngx/DlssNrParameters.h
~~~

`DlssNrRuntimeAdapter.cpp` 是项目里唯一允许知道以下内容的文件：

- Feature ID 18；
- signed snippet application ID；
- DLL 导出函数签名；
- caller-name compatibility；
- 本地 runtime 路径和实验开关。

这样未来 NVIDIA 正式 SDK 到来时，只替换 adapter，不拆整个播放器。

## 8.1 编译门

只有 `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON` 才编译 adapter。Release/发行 preset 默认 OFF。本地启用时启动日志必须明确显示：

~~~text
EXPERIMENTAL LOCAL-ONLY DLSSNR ADAPTER ENABLED
~~~

## 8.2 加载方式

绝对路径：

~~~cpp
runtimeDir / L"nvngx_dlssnr.dll"
~~~

先验证 size/hash/signature，再：

~~~cpp
LoadLibraryExW(
  absoluteDllPath.c_str(),
  nullptr,
  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
  LOAD_LIBRARY_SEARCH_SYSTEM32);
~~~

`absoluteDllPath` 必须先经 `GetFullPathNameW`/`std::filesystem::canonical` 解析并确认仍位于配置的 `runtime_local/nvidia`。`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 只给同目录依赖，`LOAD_LIBRARY_SEARCH_SYSTEM32` 只给系统依赖；不要用 `LOAD_WITH_ALTERED_SEARCH_PATH`、裸 `LoadLibraryW` 或进程当前目录。

解析并逐项非空检查：

~~~cpp
using InitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long,
    const wchar_t*,
    ID3D12Device*,
    NVSDK_NGX_Version,
    const NVSDK_NGX_Parameter*);

using CreateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*,
    NVSDK_NGX_Feature,
    NVSDK_NGX_Parameter*,
    NVSDK_NGX_Handle**);

using EvaluateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*,
    const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);

using ReleaseFeatureFn =
    NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);

using ShutdownFn =
    NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
~~~

导出名必须逐字一致：

~~~text
NVSDK_NGX_D3D12_Init_Ext
NVSDK_NGX_D3D12_CreateFeature
NVSDK_NGX_D3D12_EvaluateFeature
NVSDK_NGX_D3D12_ReleaseFeature
NVSDK_NGX_D3D12_Shutdown1
~~~

不要静态 link `nvngx_dlssnr.dll`，不要依赖当前目录搜索，不要改名为 `nvngx.dll`。

## 8.3 caller-name compatibility 隔离层

当前 signed snippet 会检查 caller module name。Magpie 的已验证路径在 DLL 的 `GetModuleFileNameW` import slot 上装一个最小兼容 shim：只有当查询的是 Veyra caller module 时返回 `nvngx.dll`，其他调用全部转发原始 `GetModuleFileNameW`。

实现要求：

1. 解析已加载 signed snippet 的 PE import table；
2. 只寻找 KERNEL32/API-set 下名为 `GetModuleFileNameW` 的 IAT slot；
3. 全局只允许一个 adapter session 持有该 slot；
4. 保存原函数指针和旧页面保护；
5. `VirtualProtect` 只覆盖一个指针宽度；
6. `InterlockedExchangePointer` 安装 shim；
7. 恢复页面保护并 `FlushInstructionCache`；
8. shim 只对本模块 handle 返回字面量 `nvngx.dll`，并严格模拟 `GetModuleFileNameW` 的 buffer/return/error 语义；
9. 所有其他 module handle 调原函数；
10. Release/Shutdown 后必须逆序恢复原 IAT，再 `FreeLibrary`。

Veyra caller module handle 用 shim 函数地址解析，不按 exe 文件名猜：

~~~cpp
GetModuleHandleExW(
  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>(shimFunctionAddress),
  &callerModule);
~~~

shim 的边界测试必须覆盖：`hModule == callerModule`、`nullptr`、`nSize == 0`、刚好容纳 10 个 wchar（9 字符加 NUL）、过小 buffer 和非 caller module。`nSize == 0` 时返回 0、不得访问 buffer、不得改变 last-error；过小 buffer 必须 NUL 截断、返回 `nSize` 并设置 `ERROR_INSUFFICIENT_BUFFER`；正常返回 9（不含 NUL）。测试要先写入 sentinel last-error，分别验证成功、截断和零长度路径。禁止 `wcscpy` 和越界写。所有非 caller 情况原样转发保存的真实函数指针。

不要 patch DLL 文件，不要全局 hook Kernel32，不要影响进程内其他模块。这个兼容层是未公开、脆弱、只限本机实验的东西，必须能通过一个编译开关完全移除。

如果 IAT 中没有目标 import、已有其他 owner、`VirtualProtect` 失败或不能恢复，立即报错并停止 NR；不允许退化成更宽泛的 hook。

独立实现时只参考 Magpie 对外行为，不复制 GPL 源码。参考位置：

~~~text
Magpie experimental:
src/Magpie.Core/DLSSNRFilter.cpp
  SnippetGetModuleFileNameW
  FindImportedFunctionSlot
  InstallSnippetCallerCompatibility
  RestoreSnippetCallerCompatibility
~~~

## 8.4 signed snippet 初始化

常量：

~~~cpp
constexpr auto kDlssNrFeature =
    static_cast<NVSDK_NGX_Feature>(18);
constexpr unsigned long long kSignedSnippetAppId = 0x0876232Cull;
~~~

这个 App ID 来自当前 signed snippet 的已验证调用合同；它不是 Veyra NGX Project ID，二者不能混用。

调用：

~~~cpp
snippetInitExt(
  0x0876232Cull,
  runtimeDir.c_str(),
  device,
  NVSDK_NGX_Version_API,
  nullptr);
~~~

顺序必须是：

~~~text
NgxCoreHost Init
→ Load signed snippet
→ install narrow caller compatibility
→ signed snippet Init_Ext
→ core AllocateParameters
→ set create parameters
→ signed snippet CreateFeature(feature 18)
→ close/execute init command list
→ wait once for creation
~~~

不要默认走 core 的 `NVSDK_NGX_D3D12_CreateFeature(18)`。在 Magpie 当前实现中，core Feature 18 路径只保留为显式 diagnostic，正常路径是 signed snippet export。

## 8.5 Create 参数，名字和类型

所有字符串集中在 `DlssNrParameters.h`，禁止散落：

| 参数 | 类型 | Phase 1 值 |
|---|---:|---:|
| `DLSSNR.Width` | uint32 | width |
| `DLSSNR.Height` | uint32 | height |
| `DLSSNR.InputWidth` | uint32 | width |
| `DLSSNR.InputHeight` | uint32 | height |
| `DLSSNR.OutputWidth` | uint32 | width |
| `DLSSNR.OutputHeight` | uint32 | height |
| `DLSSNR.Output.Width` | uint32 | width |
| `DLSSNR.Output.Height` | uint32 | height |
| `DLSSNR.Upscaling` | uint32 | 0 |
| `DLSSNR.Scale` | float | 1.0 |
| `DLSSNR.ScalingRatio` | float | 1.0 |
| `DLSSNRComputeScalingRatioCallback` | void* callback | callback sets ratio 1.0 |
| `DLSSNR.Hint.Render.Preset` | int | profile preset |
| `Width` / NGX standard width | uint32 | width |
| `Height` / NGX standard height | uint32 | height |
| `PerfQualityValue` | int | Balanced |
| `CreationNodeMask` | uint32 | 1 |
| `VisibilityNodeMask` | uint32 | 1 |

不要省略那些看似重复的 width/height。当前实验 runtime 同时观察 DLSSNR 自定义名和 NGX 标准名。

`DLSSNRComputeScalingRatioCallback` 签名接受 `NVSDK_NGX_Parameter*`，内部只设置：

~~~cpp
params->Set("DLSSNR.ScalingRatio", 1.0f);
return NVSDK_NGX_Result_Success;
~~~

## 8.6 Evaluate 参数，名字和类型

资源：

| 参数 | 类型 | 值 |
|---|---|---|
| `DLSSNR.Color` | ID3D12Resource* | Proxy |
| `DLSSNR.Output` | ID3D12Resource* | Neural |
| `DLSSNR.MVec` | ID3D12Resource* | Motion 或 ZeroMotion |
| `DLSSNR.Depth` | ID3D12Resource* | Depth 或 ZeroDepth |

每个资源都设置完整 subrect，类型为 uint32：

~~~text
DLSSNR.ColorSubrectBaseX/Y/Width/Height
DLSSNR.OutputSubrectBaseX/Y/Width/Height
DLSSNR.MVecSubrectBaseX/Y/Width/Height
DLSSNR.DepthSubrectBaseX/Y/Width/Height
~~~

Phase 1 均为 `0, 0, width, height`。

其他参数：

| 参数 | 类型 | 默认 |
|---|---:|---:|
| `DLSSNR.MVecScaleX` | float | 1.0 |
| `DLSSNR.MVecScaleY` | float | 1.0 |
| `DLSSNR.DepthInverted` | int | 1 |
| `DLSS.Indicator.Invert.X.Axis` | int | 0 |
| `DLSS.Indicator.Invert.Y.Axis` | int | 0 |
| `DLSSNR.Enabled` | int | 1 |
| `DLSSNR.Reset` | int | 首帧/seek/resize/scene cut 为 1，否则 0 |
| `DLSSNR.Style` | int | 0 |
| `DLSSNR.Intensity` | float | 1.0 |
| `DLSSNR.LocalToneStrength` | float | 1.0 |
| `DLSSNR.LocalStructureStrength` | float | 1.0 |
| `DLSSNR.SkinStructureStrength` | float | -1.0 |
| `DLSSNR.UseAutoMask` | int | 0 |
| `DLSSNR.UICorrection` | int | 0 |

这些默认值与当前 Magpie Experimental 的 baseline 一致。不要先发明 slider 范围。Phase 1 用 JSON profile 直接写值；等通过 reference capture 确认 UI 合法范围后再加 clamp。

## 8.7 每帧执行

Phase 1 一个 frame 的 command list：

~~~text
reset reusable slot
→ GenerateTestPattern 直接写 Proxy RGBA8
→ barriers
→ set every Evaluate parameter
→ signed snippet EvaluateFeature(cmd, handle, params, nullptr)
→ UAV barrier on Neural
→ timestamp resolves
→ close and submit once
→ signal slot fence
~~~

Phase 2 再把前半段换成“生成/加载 Original → ParityEncode 写 Proxy”，并在 Neural 后接 ParityDecode 写 Final。Phase 1 不得为了提前追求最终观感而把 parity bug 混进 Feature 18 调用验证。

Evaluate 返回成功只表示命令记录成功，不表示像素一定正确。Phase 1 同时检查：

- output 非全黑/非全常数；
- 无 NaN/Inf；
- 修改 Style/Intensity 时 output hash 会变化；
- 300 次 evaluateSuccessCount == 300；
- D3D12 debug layer 无 resource-state error；
- GPU timestamp 非 0。

## 8.8 释放顺序

~~~text
drain GPU slots
→ signed snippet ReleaseFeature
→ core DestroyParameters
→ signed snippet Shutdown1(device)
→ restore caller IAT
→ FreeLibrary(snippet)
→ release NR textures
→ release NR consumer from NgxCoreHost
→ all NGX consumers gone后 core Shutdown1(device)
~~~

每一步即使前一步失败也要尝试安全清理剩余已初始化对象。用显式 init-state bitmask/RAII，不用一个 `initialized=true` 糊住部分初始化。

---

# 9. RenoDX Parity Codec：如何做到“类似 ReShade”

这部分才是“类似 ReShade”的核心。Feature 18 只输出 neural proxy；RenoDX 的观感还来自前后的颜色传递。直接把播放器线性 FP16 喂给 NR 或把 Raw Neural 直接 present，都会发生 gamma、亮度和高光错误。

文件：

~~~text
shaders/ParityEncode.hlsl
shaders/ParityDecode.hlsl
src/parity/RenoDxParityCodec.*
src/parity/RenoDxParityProfile.*
tests/unit/ParityCpuReference.*
~~~

## 9.1 固定资源

~~~text
Original = 线性 BT.709 working RGB，R16G16B16A16_FLOAT
Proxy    = encode 后，R8G8B8A8_UNORM
Neural   = Feature 18 raw output，R8G8B8A8_UNORM
Final    = decode/reconstruct 后，R16G16B16A16_FLOAT
~~~

四个阶段必须可抓：

~~~text
00_original
01_proxy_input
02_raw_dlssnr
03_final_parity_output
~~~

## 9.2 Parity Encode 的确定数学

对每个 RGB 分量：

~~~text
linear = max(Original.rgb, 0) / PaperWhiteScale

shoulder(x) =
  0.75 + 0.25 * (1 - exp(-5.7780 * (x - 0.75)))

proxyLinear =
  x <= 0.75 ? x : shoulder(x)

Proxy.rgb = sRGBEncode(proxyLinear)
Proxy.a   = Original.a
~~~

`5.7780` 是当前 addon binary 中可观察到的 codec 常数。不要随意换成 filmic curve。

标准 sRGB：

~~~text
Encode(c):
  c <= 0.0031308
    ? 12.92 * c
    : 1.055 * pow(c, 1/2.4) - 0.055

Decode(c):
  c <= 0.04045
    ? c / 12.92
    : pow((c + 0.055) / 1.055, 2.4)
~~~

dispatch：

~~~text
[numthreads(16,16,1)]
groupsX = ceil(width / 16)
groupsY = ceil(height / 16)
越界线程必须 return
~~~

## 9.3 Parity Decode 的确定数学

亮度：

~~~text
Y = dot(rgb, (0.212639, 0.715169, 0.072192))
~~~

读取：

~~~text
original = max(Original.rgb, 0) / PaperWhiteScale
proxy    = sRGBDecode(Proxy.rgb)
neural   = sRGBDecode(Neural.rgb)
~~~

OkLab 矩阵必须按下面数值、以 `mul(matrix, vector)` 的方向使用，别转置：

~~~text
RGB → LMS
0.4122214708  0.5363325363  0.0514459929
0.2119034982  0.6806995451  0.1073969566
0.0883024619  0.2817188376  0.6299787005

cuberoot(LMS) → OkLab
0.2104542553  0.7936177850 -0.0040720468
1.9779984951 -2.4285922050  0.4505937099
0.0259040371  0.7827717662 -0.8086757660

OkLab → LMS'
1.0  0.3963377774  0.2158037573
1.0 -0.1055613458 -0.0638541728
1.0 -0.0894841775 -1.2914855480

LMS³ → RGB
 4.0767416621 -3.3077115913  0.2309699292
-1.2684380046  2.6097574011 -0.3413193965
-0.0041960863 -0.7034186147  1.7076147010
~~~

cuberoot 必须是 signed：

~~~text
sign(x) * pow(abs(x), 1/3)
~~~

AP1 gamut clamp：

~~~text
BT.709 → AP1
0.613097  0.339523  0.047379
0.070194  0.916354  0.013452
0.020616  0.109570  0.869815

AP1 → BT.709
 1.705051 -0.621792 -0.083259
-0.130256  1.140805 -0.010548
-0.024003 -0.128969  1.152972

ClampAp1(c) = AP1To709(max(0, BT709ToAP1(c)))
~~~

HueOkLab：

~~~text
incorrectLab = ToOkLab(incorrect)
correctLab   = ToOkLab(correct)
incorrectChroma = length(incorrectLab.ab)
correctChroma   = length(correctLab.ab)
incorrectLab.ab =
  correctLab.ab *
  (correctChroma == 0 ? 1 : incorrectChroma / correctChroma)
return ClampAp1(FromOkLab(incorrectLab))
~~~

UpgradeToneMap：

~~~text
originalY = Luminance(original)
proxyY    = Luminance(proxy)
neuralY   = Luminance(neural)

if originalY < proxyY:
  ratio = originalY / proxyY
else:
  newY  = neuralY + max(0, originalY - proxyY)
  ratio = neuralY > 0 ? newY / neuralY : 0

scaled   = HueOkLab(neural * ratio, neural)
upgraded = lerp(original, scaled, TransferStrength)
~~~

最终：

~~~text
originalY = Luminance(original)
upgradedY = Luminance(upgraded)
ratio = originalY == 0 ? 1 : upgradedY / originalY
luminanceOnly = original * ratio
result = lerp(luminanceOnly, upgraded, ColorStrength)
Final = float4(result * PaperWhiteScale, Original.a)
~~~

## 9.4 Parity 配置

三个 codec 参数和 NR 参数分开：

~~~json
{
  "schema": 1,
  "paperWhiteScale": 1.0,
  "transferStrength": 1.0,
  "colorStrength": 1.0,
  "nr": {
    "preset": 0,
    "style": 0,
    "intensity": 1.0,
    "localToneStrength": 1.0,
    "localStructureStrength": 1.0,
    "skinStructureStrength": -1.0,
    "useAutoMask": false,
    "uiCorrection": false
  }
}
~~~

`paperWhiteScale/transferStrength/colorStrength = 1.0` 是 V1 的规范化中性工程 baseline，不得谎称它就是 addon 所有环境下的 UI 默认。当前项目没有 ReShade/RenoDX preset；`renodx-dlss5-1.addon64` 是二进制 add-on，不是 profile。Phase 2 必须把这三个 baseline 值、addon hash、输入/输出分辨率和 capture hash 一起记录。若用户以后提供真实 preset，只把它导入为单独命名的可选 reference profile；preset 缺失不阻塞 V1，也禁止通过加载 addon 取值。

## 9.5 单元测试

写一份独立 CPU float reference，不调用 GPU：

- 纯黑、纯白、18% gray；
- 0.75 阈值左右 `0.7499/0.75/0.7501`；
- 高光 `1/2/4/8`；
- RGB primary、肤色近似值；
- 负分量和 alpha；
- 64×64 gradient/checker/edge pattern。

测试：

~~~text
CPU encode → quantize RGBA8 → CPU decode
GPU encode → readback RGBA8
GPU decode → readback FP16
~~~

容差：

~~~text
Proxy RGBA8: 每通道最多 1 code value
Final FP16:  abs error <= 0.002，且无 NaN/Inf
~~~

只有 diagnostic capture 工具允许同步 GPU readback。正常播放器路径不允许。

---

# 10. Frame Guidance

统一数据合同：

~~~cpp
struct GuidanceFrame {
  uint64_t frameId;
  uint32_t width;
  uint32_t height;
  ID3D12Resource* motion;     // R16G16_FLOAT
  ID3D12Resource* depth;      // R32_FLOAT
  ID3D12Resource* confidence; // R8_UNORM
  bool motionIsZero;
  bool depthIsZero;
  bool requiresHistoryReset;
};
~~~

固定语义：

~~~text
Motion direction: Current → Previous
Motion unit:      source pixels
Motion scale:     1.0, 1.0 after conversion
Depth:            relative inverse if real
Depth inverted:   true
~~~

## 10.1 Zero Guidance

Phase 1 就实现。每个 frameId 发布尺寸正确的全零 motion/depth/confidence；metadata 不能复用旧 frameId。第一帧、seek、resize、scene cut、device recreate 设置 reset。

Zero Guidance 是诚实的 fallback，不得在日志/UI 显示为 NVOF。

## 10.2 NVOF D3D12

Phase 5 再做。调用路径：

~~~text
Load System32 nvofapi64.dll
→ NvOFGetMaxSupportedApiVersion
→ NvOFAPICreateInstanceD3D12
→ nvCreateOpticalFlowD3D12(device)
→ nvOFGetCaps
→ nvOFInit
→ register current/reference/output/cost resources
→ queue waits on producer fence
→ nvOFExecuteD3D12
→ graphics queue waits on NVOF output fence
→ NvofVectorConvert compute
~~~

初始配置：

~~~text
mode:                NV_OF_MODE_OPTICALFLOW
perfLevel:           NV_OF_PERF_LEVEL_MEDIUM
external hints:      false
output cost:         true, UINT8
prediction:          forward
grid:                首选 1x1；不支持则 2x2/4x4 后 densify
input/reference:     current frame → previous frame
~~~

NVOF 不直接接受 Veyra 的 `Original/Final R16G16B16A16_FLOAT`。Phase 5 baseline 在 optional SR 之后、NR 之前，从同一 pre-NR linear frame 用 compute shader 生成专用 8-bit `NvofInput`，通过 SDK D3D12 buffer/register 路径声明为 `NV_OF_BUFFER_FORMAT_ABGR8`；全程 GPU-resident，不做 CPU 像素回读。实际 DXGI resource format、row pitch 和 RGBA/ABGR 通道映射必须以 SDK 5.0 D3D12 sample/返回的 resource desc 为准，并用 RGB bars 验证，禁止凭枚举名字猜 swizzle。

至少保留 current/previous 两组独立注册 buffer 并用 fence 轮转；第一帧没有 previous 时发布 Zero Guidance，不调用 NVOF。seek/resize/scene-cut/device recreate 后，下一次 execute 设置 `disableTemporalHints = 1`，不得让 NVOF 内部 temporal hint 跨历史边界。

NVOF raw vector 是 signed 10.5 fixed point，X/Y 各 16 bit。转 source-pixel float 时除以 32。若 grid 大于 1，按 SDK sample 的坐标定义 densify 到完整尺寸，不能简单把 raw texture 当 `R16G16_FLOAT`。

NVOF 的 UINT8 cost 越高表示越不可信；内部字段既然命名为 confidence，就统一转换为 `1.0 - cost / 255.0` 后写 `R8_UNORM`。禁止把 raw cost 原样写入却标成 confidence。

NVOF API context 非线程安全。一个 context 只在 guidance worker/受 mutex 保护的单一调用点使用。

选择 current 为 input、previous 为 reference，是为了得到 Current→Previous。用一个平移测试片验证符号：画面向右平移时，当前像素回查 previous 的方向必须与 contract 一致；符号错误就修 producer，禁止在 NR 和 FG 两边各自乘一次 -1。

## 10.3 Guidance 尺寸

V1 让 NVOF 在 DLSSNR 的 working extent 上工作：

- 无 SR：video output extent；
- 有 SR：SR 输出/NR 输入 extent。

这样 NR/FG 都消费同一份 full-resolution motion，`MVecScaleX/Y=1`。不要在一个 backend 用 render resolution、另一个用 output resolution。

---

# 11. DLSS Super Resolution

Phase 4 实现，独立 backend：

~~~text
src/ngx/DlssSrBackend.*
~~~

使用官方 `nvsdk_ngx_helpers.h`：

~~~text
NGX_D3D12_CREATE_DLSS_EXT
NGX_D3D12_EVALUATE_DLSS_EXT
~~~

资源和参数以 310.7.0 header 为真源，不手写一套私有字符串。

策略：

- input extent == output extent：整个 SR backend bypass；
- input extent < output extent：SR 先执行，NR 后执行；
- 第一轮用 Zero Guidance、jitter 0、preExposure 1 跑通；`reset=1` 只用于首帧和真实 history invalidation，正常后续帧必须为 0；
- V1 的 SR 始终使用 Zero Guidance；full-resolution NVOF 在 SR 输出后生成，只供 NR/FG，禁止让 SR 反向依赖自己的输出；
- SDR V1 不设置 HDR feature flag；
- seek/resize/quality mode change 时 Release/Create 并 reset；
- 不允许让 DLSSNR 自己兼任放大。

Phase 4 必须单独抓：

~~~text
decoded_linear
sr_output
nr_proxy
nr_raw
final
~~~

如果 SR output gamma 错，先查输入是不是 linear、pre-exposure、format 和 range，不调 NR 参数掩盖。

---

# 12. DLSS Frame Generation 2X

Phase 6 实现：

~~~text
src/ngx/DlssFgBackend.*
src/player/GeneratedFrameScheduler.*
~~~

使用官方：

~~~text
nvsdk_ngx_helpers_dlssg.h
NGX_D3D12_CREATE_DLSSG_EXT
NGX_D3D12_EVALUATE_DLSSG_EXT
~~~

先从 capability parameters 读取：

~~~text
NVSDK_NGX_Parameter_FrameGeneration_Available
NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult
NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax
~~~

V1 固定 2X，即每相邻 real frame 只生成 1 帧：

~~~text
multiFrameCount = 1
multiFrameIndex = 1
~~~

Create：

~~~text
Width/Height:             最终 backbuffer extent
RenderWidth/RenderHeight: guidance extent
NativeBackbufferFormat:   Final NR color format
~~~

如果永远不提供以下资源，按官方 header/Magpie 已验证路径设置 `ResourceNeverProvided_Flags`：

~~~text
HUDLess
UI
UIAlpha
BidirectionalDistortionField
OutputReal
~~~

Evaluate 基线：

~~~text
pBackbuffer                 = 当前 Final NR real frame
pMVecs                      = NVOF motion 或 ZeroMotion
pDepth                      = ZeroDepth（V1 不估深度）
pOutputInterpFrame          = generated output
pOutputDisableInterpolation = 4-byte/required output buffer
BackbufferFrameID           = 单调 frameId
mvecScale                   = 1/width,1/height（GuidanceFrame 是 source-pixel 单位）
reset                       = history reset flag
cameraMotionIncluded        = NVOF dense motion 时 true，Zero Motion 时 false
depthInverted               = false when using zero depth
camera matrices             = identity baseline
~~~

`mvecScale` 是 DLSSG 的归一化倍率，不是 Feature 18 的 `DLSSNR.MVecScaleX/Y`。
官方 310.7 helper 要求 scale 后的 motion 落入 `[-1,1]`；因此 full-resolution
source-pixel motion 使用 `{1/width, 1/height}`，平移测试同时验证方向和幅值。
所有 backbuffer/motion/depth/output subrect 都显式填满各自 extent，不能依赖
结构体的零尺寸默认值。

已锁定的 Magpie experimental commit 在这一字段使用 `{1,1}`，与官方 header
注释矛盾。实现 `DlssgMvecScaleMode::{OfficialNormalized, MagpieUnitDiagnostic}`：
V1 默认且可发布路径只能是 `OfficialNormalized`；后者只允许 harness 显式选择，
用于复现上游行为和定位 runtime 差异。Phase 6 的固定平移片必须把 mode、scale、
输入 motion 的 min/max、generated hash/counter 和几何方向/幅值一起写入日志；
不能因为两个模式都返回 success 就声称二者等价，也不能自动把诊断模式保存成默认。

UI 必须在 FG 之后叠加。若把 UI 放进 `pBackbuffer`，FG 会把字幕、按钮和鼠标一起插值，结果不等价于 RenoDX 的“UI downstream”。

## 12.1 时序

要生成 F0 与 F1 中间的 G0.5，必须先拿到 F1。因此 2X FG 有至少一个 source-frame 的时间依赖：

~~~text
decode/process F0, hold
decode/process F1
evaluate FG using history/current
present F0
present G0.5
present F1
~~~

实际 NVIDIA helper 的发布顺序以 runtime contract 为准，但 scheduler 必须明确 real/generated PTS，不能重复帧冒充。

对源帧间隔 Δ：

~~~text
PTS(G between A,B) = (PTS(A) + PTS(B)) / 2
~~~

变帧率视频按相邻真实 PTS 插值，不按 nominal FPS 猜。

## 12.2 证明真的插帧

同时满足：

- capability available；
- Create/Evaluate success；
- `pOutputDisableInterpolation` 表明 interpolation 没被 runtime 禁用；
- generated frame hash 不等于前后 real frame；
- 实际 Present 计数接近 real×2；
- debug overlay 分开显示 real/generated；
- seek/pause/resize 后旧 generated frame 不再 present。

理论设置 2X 或把上一帧重复 present 两次都不算成功。

---

# 13. FFmpeg 媒体管线

Phase 3 先 video-only。分两小步：

## 13.1 Phase 3A：功能基线

先用软件 decode + upload 验证 demux、PTS、seek/reset 和连续 NR。这个路径只用于 bring-up，不是最终性能路径。

~~~text
avformat_open_input
→ avformat_find_stream_info
→ av_find_best_stream(video)
→ avcodec_alloc_context3 / parameters_to_context
→ avcodec_open2
→ packet send / frame receive
→ swscale to BGRA/NV12 if needed
→ upload to D3D12
~~~

完成后立刻进入 3B，不要把 CPU upload 当最终“零拷贝播放器”。

## 13.2 Phase 3B：D3D12VA，共用 Veyra device

使用 FFmpeg `AV_HWDEVICE_TYPE_D3D12VA`，不要让 FFmpeg 另建 adapter/device：

~~~cpp
AVBufferRef* ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D12VA);
auto* hw = reinterpret_cast<AVHWDeviceContext*>(ref->data);
auto* d3d = reinterpret_cast<AVD3D12VADeviceContext*>(hw->hwctx);

d3d->device = veyraDevice;
veyraDevice->AddRef(); // FFmpeg context owns/release this interface

av_hwdevice_ctx_init(ref);
codec->hw_device_ctx = av_buffer_ref(ref);
~~~

`get_format` 只在 decoder 提供时选择 `AV_PIX_FMT_D3D12`；否则记录 codec/profile 并 fallback software，不要声称硬解。

每个硬解 AVFrame：

~~~cpp
auto* d3dFrame =
  reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);

ID3D12Resource* texture = d3dFrame->texture;
int subresource = d3dFrame->subresource_index;
auto& sync = d3dFrame->sync_ctx;
~~~

不要把 `frame->data[0]` 直接 cast 为 `ID3D12Resource*`。

固定 FFmpeg 9.0.1 的 `AVD3D12VAFrame::subresource_index` 在 texture-array 模式表示 array slice。创建 NV12/P010 plane SRV 时：

- 非 array texture 使用 `Texture2D.PlaneSlice = 0/1`；
- array texture 使用 `Texture2DArray.FirstArraySlice = subresource_index`、`ArraySize = 1`、`PlaneSlice = 0/1`；
- 不得把 `subresource_index` 当 plane index，也不得永远读取 array slice 0；
- 日志记录 `d3dFrame->flags`、resource desc、array slice 和两个 plane SRV 描述。

等待 `sync_ctx.fence/value` 后，再对实际 slice/plane 做正确的 state transition；对应 `AVFrame` 必须继续存活到 Veyra graphics fence 完成。

GPU 同步：

~~~text
graphicsQueue->Wait(sync.fence, sync.fence_value)
~~~

这是 GPU queue wait，不是 CPU `WaitForSingleObject`。必须保持对应 `AVFrame`/`AVBufferRef` 存活，直到消费该 surface 的 graphics fence 完成。

## 13.3 YUV → linear RGB

`YuvToLinearRgb.hlsl` 读取：

- NV12：plane 0 `R8_UNORM`，plane 1 `R8G8_UNORM`；
- P010：plane 0 `R16_UNORM`，plane 1 `R16G16_UNORM`。

每帧根据 FFmpeg metadata 选择：

~~~text
color_range
colorspace
color_primaries
color_trc
chroma_location
~~~

V1 至少正确支持：

~~~text
BT.709 limited/full SDR
BT.601 limited/full SDR
NV12 8-bit
P010 SDR fallback
~~~

HDR/PQ/HLG 在 V1 中明确报 unsupported/bypass NR，不可把 PQ 数值当线性 RGB。

优化顺序：

1. 先独立 YUV→Original shader，保证颜色正确；
2. 再把 Proxy Encode 融合进同一 dispatch，同时写 Original FP16 和 Proxy RGBA8；
3. 融合前后用固定帧测试保证 1 code value 内一致。

## 13.4 Seek/reset

seek 流程必须完整：

~~~text
pause scheduler
→ stop accepting decoded frames
→ drain/retire GPU slots
→ avcodec_flush_buffers
→ av_seek_frame / avformat_seek_file
→ clear packet/frame queues
→ reset playback clock
→ reset SR/NR/FG/NVOF history
→ first new frame carries Reset=1
→ resume
~~~

只调 `av_seek_frame` 而不清历史会把 seek 前内容混进 Neural/FG 输出。

---

# 14. 音频、UI 和播放时钟

Phase 7：

~~~text
FFmpeg audio decode
→ swresample to 48 kHz float stereo
→ event-driven WASAPI shared mode
→ audio clock is master
~~~

视频 scheduler 按 PTS 对齐音频：

- 视频早：等待；
- 视频轻微晚：尽快 present；
- 严重晚：只丢尚未进入 temporal chain 的 real frame，并立即 history reset；
- FG 不改变音频时长；
- pause 时音频和视频同时冻结；
- seek 后清 WASAPI padding/重新基准。

UI：

- 正常底栏只显示播放/暂停、seek、音量、SR/NR/FG 开关；
- 调试面板显示 runtime/hash、参数、result、GPU ms、queue depth、real/generated present counts、guidance type、reset reason；
- ImGui 在 Final/Generated frame 之后绘制；
- UI 开关改变 NR 配置时，标记下一 real frame reset，不能在一对 FG 中间切参数。

---

# 15. 延迟预算和不能犯的错误

Parity Encode/Decode 是 compute shader，本身不需要帧缓存，只增加 GPU 执行时间。DLSSNR/NVOF 也主要增加 GPU time。真正不可避免的整帧等待主要来自 2X FG：生成中间帧前需要下一张 real frame。

理论额外等待：

~~~text
24 fps source: one frame ≈ 41.7 ms
30 fps source: one frame ≈ 33.3 ms
60 fps source: one frame ≈ 16.7 ms
~~~

这是 temporal availability，不包括 decode、NR、FG GPU time 和显示扫描。最终必须实测，不得承诺固定值。

低延迟规则：

- decoded-ready queue 上限 1；
- GPU in-flight slots 3–4，但不做多帧 lookahead；
- 单 direct queue 串起 color/SR/NR/parity；
- normal path 无 GPU→CPU pixel readback；
- 不在每个 pass/每帧 CPU wait；
- D3D12VA fence 用 queue wait；
- Present 用 flip model，支持时用 waitable swapchain；
- capture 模式和正常模式完全分开；
- 关闭 FG 时不保留下一 real frame，立即 present；
- 统计 `decode→present`、`NR GPU`、`FG GPU`、`queue depth` 和 `present cadence`。

禁止：

~~~text
decode 4–8 帧排队再处理
每帧 WaitForSingleObject
每 pass ExecuteCommandLists
用 CPU memcpy 在 FP16/RGBA8 间来回
为了 debug 一直开启同步 readback
FG 开启后仍按源 FPS sleep
~~~

---

# 16. 分阶段施工与硬门槛

## Phase 0 — Runtime probe + D3D12 skeleton

实现：

~~~text
veyra_runtime_probe
D3D12DeviceContext
CommandSlotRing
ResourceStateTracker
GpuProfiler
RuntimeManifest
Win32 test window
~~~

`veyra_runtime_probe --runtime-dir <absolute>` 输出：

~~~text
OS build
adapter name/vendor/LUID/VRAM
driver version
D3D12 feature level
DLL path/size/SHA256/file version/signature
required export present/missing
nvofapi64 path/version
experimental flag
~~~

完成门槛：

- Debug/Release 均可构建；
- D3D12 窗口循环 5 分钟无 device removed；
- runtime hash/signature 与 manifest 一致；
- proprietary files 都是 ignored；
- WORKLOG 记录真实命令和输出。

## Phase 1 — Feature 18 Native Harness

CLI 必须支持：

~~~text
veyra_nr_harness
  --runtime-dir <absolute>
  --width 1920
  --height 1080
  --frames 300
  --guidance zero
  --profile config/nr-default.json
  --capture-frame 0
  --capture-dir captures/phase1
~~~

输入由 `GenerateTestPattern.hlsl` 直接生成 RGBA8 Proxy，包含 gradient、checker、硬边和 RGB bars。Phase 1 先不依赖 Original、Parity Shader 或外部图片。

完成门槛：

- signed snippet Init_Ext success；
- Feature 18 handle 非空；
- 300/300 Evaluate success；
- raw output 非黑、无 NaN；
- 至少两组 Style/Intensity 产生不同 output hash；
- Release/Shutdown 后无 crash/device removed；
- D3D12 debug layer 无 state/descriptor lifetime error；
- 保存 `logs/phase1.log` 与 capture manifest。

任何一项不满足，不进入 Phase 2/3。

## Phase 2 — Parity Codec

实现 CPU reference、两个 shader、四阶段 capture、profile。

完成门槛：

- `ParityCpuReference` 全过；
- GPU/CPU encode 误差 ≤1 code；
- GPU/CPU decode FP16 误差 ≤0.002；
- Original/Proxy/Raw/Final 四份真实抓帧存在；
- 没有系统性洗白、压黑、截高光、整体偏色；
- V1 中性 baseline 的三个 codec 参数、来源说明和 capture hash 已记录；
- Raw/Final 可即时切换，证明 parity 确实执行。

## Phase 3 — Minimal Video Pipeline

3A 软件 decode，3B D3D12VA。

完成门槛：

- H.264/HEVC 的合法测试片可播放；
- PTS 单调处理正确，VFR 不按固定 FPS 猜；
- 10 次 seek 后无旧历史影像；
- NR 每个 real frame 执行；
- D3D12VA 模式日志显示共享 device 和 `AV_PIX_FMT_D3D12`；
- normal path 无 pixel readback；
- 30 分钟 video-only 无内存持续增长。

## Phase 4 — DLSS SR

完成门槛：

- 1:1 自动 bypass；
- 1080p→4K 走 SR→NR；
- SR/NR resource extent/subrect 对齐；
- resize/quality change 正确重建；
- SR runtime 来自官方 310.7 manifest；
- Phase 4 以 Zero Guidance 完成 SR 门禁，日志明确标为 Zero；NVOF 属于 Phase 5，尚未实现时必须报告 not enabled，不能用零运动冒充 NVOF。

## Phase 5 — NVOF Guidance

完成门槛：

- System32 runtime/version/capability query 成功；
- current→previous 方向由平移片验证；
- fixed 10.5 除以 32 的转换测试通过；
- 1x1 或 densified full-res motion；
- NR/FG 消费同一个 GuidanceFrame；
- NVOF 不可用时明确 fallback Zero；
- API context 无多线程并发调用。

## Phase 6 — DLSSG 2X

完成门槛：

- capability available、Create/Evaluate success；
- 插值未被 disable；
- 每对 real frame 产生一帧不同内容；
- present counter 实际接近 2X；
- generated PTS 位于相邻 real PTS 中点；
- UI 在 FG 后；
- seek/pause/resize 无 stale generated frame；
- 报告新增 latency。

## Phase 7 — Audio + minimum UI

完成门槛：

- WASAPI event-driven playback；
- audio master clock；
- 60 分钟 A/V drift 无持续增长；
- FG on/off 不改变音频时长；
- UI 改参数在 real-frame 边界 reset；
- debug panel 数据来自真实 counter/timestamp。

---

# 17. 日志、抓帧和验收证据

## 17.1 日志格式

每行至少：

~~~text
timestamp
thread
frameId
component
operation
resultHex/HRESULT
SEH code if any
width/height/format
reset flag/reason
GPU ms if available
~~~

必须出现的计数：

~~~text
nrCreateCount
nrEvaluateAttempt/Success/Failure
nrResetCount
nvofSuccess/Fallback
srEvaluateSuccess
fgRealFrameCount
fgGeneratedFrameCount
presentReal/Generated/Dropped
decodeQueueDepth
gpuSlotWaitMs
~~~

## 17.2 Capture 格式

RGBA8 阶段可保存无损 PNG。FP16 阶段不要强行转 8 bit 后声称是原始数据：

~~~text
00_original.rgba16f.bin
00_original.json
01_proxy.png
02_raw_dlssnr.png
03_final.rgba16f.bin
03_final-preview.png
03_final.json
~~~

JSON 写 width、height、DXGI format、row pitch、color space、profile、runtime hash、frameId、PTS。

## 17.3 WORKLOG 模板

每阶段追加：

~~~markdown
## YYYY-MM-DD Phase N

Goal:

Changed:

Commands actually run:

Results:

Artifacts/logs:

Failures and exact codes:

Decision:

Next single task:
~~~

不准写“测试应该能过”。没在 RTX 5070 上跑就写“未执行”。

---

# 18. 常见失败定位表

## DLL hash 不一致

动作：停工。记录实际 path/size/hash/signature，询问用户。不要自动下载替换。

## `LoadLibraryExW` 失败

检查：

1. 是否绝对路径；
2. runtime 是否 staged；
3. Win32 `GetLastError`；
4. 进程是否 x64；
5. DLL signature/hash；
6. 依赖搜索 flags。

不要把 search path 扩到整个磁盘。

## 导出缺失

动作：报告缺失导出和 file version，停。不要尝试 ordinal 猜函数。

## signed snippet Init_Ext 失败

按顺序查：

1. NGX core 是否先成功；
2. adapter/device 是否同一个；
3. runtimeDir 是否正确；
4. caller compatibility 是否只对正确 module handle 生效；
5. driver ≥615；
6. GPU 是否当前支持；
7. result hex/SEH。

不要先改 App ID 或复用 Magpie Project ID。

## CreateFeature 返回 `0xBAD00005` / InvalidParameter

逐项 dump 参数“名字、类型、值”，重点：

- 重复的 input/output/standard width/height 是否全设；
- Upscaling=0、Scale=1、ScalingRatio=1；
- callback 是否有效；
- Preset 是 int；
- node mask 是 uint32；
- command list/device 是否同源；
- feature ID 精确为 18；
- create resources extent 是否稳定。

不要通过删参数随机试。

## Evaluate InvalidParameter

重点：

- 四个 resource 都非 null；
- resource 属于同一个 D3D12 device；
- Proxy/Neural/Motion/Depth format；
- subrect 全部在资源内部且非 0；
- parameter type；
- motion/depth extent 与 output 相同；
- output 有 UAV flag/state；
- first frame Reset=1。

## 返回 success 但黑图

检查：

1. Proxy 是否非黑；
2. Neural UAV barrier；
3. capture 是否读对 subresource/row pitch；
4. output 是否被下一 pass 清零；
5. sRGB encode 是否写到 UNORM；
6. resource state tracker；
7. parameter block 是否在 GPU 完成前被复用/销毁。

success 不是画面正确的证据。

## 画面洗白/压黑/高光断层

按顺序查：

1. YUV range；
2. BT.601/709 matrix；
3. transfer 是否 decode 到 linear；
4. PaperWhiteScale；
5. Proxy 是否 sRGB encode；
6. Neural/Proxy 是否 sRGB decode；
7. FP16/UNORM format；
8. shoulder 常数/阈值；
9. matrix 是否转置。

不要先调 Intensity。

## 时间越播越慢/延迟越来越高

检查：

- packet/frame queue 是否有上限；
- slot 是否每帧同步等待；
- capture readback 是否误开；
- scheduler 是否按 nominal FPS 而非 PTS；
- FG 是否缓存超过下一 real frame；
- audio clock/present clock 是否双重 sleep；
- NVOF/NGX 是否每帧重建 feature。

## device removed

记录：

~~~text
GetDeviceRemovedReason
DRED breadcrumbs
DRED page-fault data
last frameId
last resource states
last NGX operation
~~~

不要立刻自动循环重建导致日志被冲掉。先写 crash report，再做一次受控 device recreation。

## FG “成功”但画面重复

检查 interpolation-disable output、generated hash、multiFrameCount/index、BackbufferFrameID、history reset、真实 motion。重复 present 不是 DLSSG。

---

# 19. 对抗式审查：最可能把项目做废的十件事

1. 一上来搭完整播放器，Feature 18 错误被 FFmpeg/UI/时钟问题淹没。
2. 把 addon 当配置或 DLL 加载，最终仍依赖 ReShade。
3. Raw Neural 直接 present，误判为 DLSS 画质差，实际是缺 parity。
4. 把 sRGB Proxy 当 linear 或把 FP16 当 UNORM。
5. 参数名正确但类型错误，得到 InvalidParameter。
6. 每帧 CPU wait，功能能跑但延迟和吞吐彻底坏掉。
7. 用理论倍率冒充真实 DLSSG 帧。
8. NVOF 符号、单位或 grid 转换错误，却在 consumer 端乱乘 scale。
9. 复制 GPL Magpie 源码后还想闭源发布。
10. 把 leaked/local runtime 放进 Git、安装包或云端 artifact。

每个 PR/阶段完成前，对照这十条逐项打勾。

---

# 20. 唯一执行入口

无人值守施工只使用根目录 `loop/GOAL_PROMPT.md`，并严格执行
`loop/LOOP_ENGINE.md`。不要从本手册摘一段另写启动 Prompt；那会丢失循环上限、Reviewer、STOP、控制面 hash 和恢复规则。

机器可读进度在 `loop/STATE.json`，唯一任务顺序在 `loop/BACKLOG.md`，
实际门禁证据在 `loop/EVIDENCE.md`。每个 Phase 的 gate 和只读 Reviewer
都通过后才自动进入下一 Phase。

若不是 Goal 模式，只执行 STATE 指向的当前 Phase，完成或阻塞后停下报告，
不得自行跨 Phase。任何状态文字都不能覆盖真实编译、运行、日志、counter、
capture 和 present 结果。

---

# 21. 最终 V1 完成定义

只有下面全部真实成立，才能说 V1 完成：

~~~text
Windows x64 player
+ FFmpeg D3D12VA decode on shared device
+ correct SDR YUV/range/transfer handling
+ optional DLSS SR
+ native signed-snippet DLSSNR Feature 18
+ RenoDX-equivalent proxy encode/decode
+ Zero/NVOF guidance with explicit identity
+ native DLSSG 2X with real generated frames
+ audio-master A/V sync
+ UI after FG
+ bounded low-latency queues
+ runtime/hash/result/GPU timing diagnostics
+ no ReShade/addon dependency in final executable
~~~

并且：

- Feature 18 连续执行、重建、seek/reset 稳定；
- CPU/GPU parity 数值测试与固定灰阶/色卡/高光门禁通过；若用户提供合格 RenoDX reference capture，再追加同输入/参数的管线级偏差对照；
- runtime/local SDK 未进入 Git 或发行包；
- 没有用 Magpie GPL 源码污染预定的闭源边界；
- 所有“成功”都有日志、counter、capture 或实际 present 证据。

这份手册不评价 DLSS5 对日常视频是否好看。我们的职责是正确调用、正确复现 parity、正确控制时序；模型本身的审美效果不在工程优化范围内。
