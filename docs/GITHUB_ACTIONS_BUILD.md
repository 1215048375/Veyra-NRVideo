# GitHub Actions Windows 构建

工作流：`.github/workflows/windows-build.yml`，名称 **Windows build**。

## 一次性设置

不需要上传 220 多 MB 的 `Optical_Flow_SDK_5.0.7.zip`，也不需要制作私有依赖整包或准备下载服务器。公开依赖由脚本下载固定版本并编译；唯一非公开输入是用户已取得 SDK 内的两个原始 NVOF 头文件。

1. 本机运行 `python scripts/ci/dependencies.py export-nvof-secret`。当前已生成 `out/ci-dependencies/nvof-secret.txt`，约 22 KB。该文件只在本机忽略目录中，内容不要提交 Git、贴到日志或作为 artifact 上传。
2. 打开 GitHub 仓库 **Settings → Secrets and variables → Actions → New repository secret**。
3. 名称填 `VEYRA_NVOF_HEADERS_B64`，值填上面文本文件的全部内容。只需这一个 Secret；旧的 `VEYRA_CI_DEPS_URL` 和 `VEYRA_CI_DEPS_SHA256` 不再使用。
4. 将工作流和脚本推送到默认分支 `main`，或打开 **Actions → Windows build → Run workflow**，在默认分支勾选 `full_build`。
5. 完整任务成功后，在本次运行页面底部 **Artifacts** 下载 `Veyra-win64-build-<commit>`。

完整任务使用 `windows-build` environment。也可把 Secret 配置在同名 environment 内；若环境设有审批或分支限制，应按仓库设置放行默认分支。未设置 Secret 时完整任务明确失败，源码检查仍可运行。

工作流只读仓库；不会 push、创建 Release 或上传 Runtime Pack。PR 和 `codex/**` 分支仅执行源码检查，完整构建只在默认分支 push 或显式手动运行时执行，不使用 `pull_request_target`。

## 自动准备的依赖

`scripts/ci/dependencies.lock.json` 记录固定提交和归档 SHA-256；`dependencies.py` 负责取得源码、校验、构建，生成本机 `third_party_local/ci-build.json`。SDK 和开发库均留在忽略目录；不缓存或上传开发依赖。

| 依赖 | 来源与处理 |
| --- | --- |
| DLSS 310.7.0 | NVIDIA 官方仓库固定提交，稀疏取得头文件和 x64 NGX shim |
| XeSS 3.0.2 | Intel 官方仓库固定提交，只取接口目录 |
| NVENC 声明 | FFmpeg/nv-codec-headers 固定提交 |
| NVOF 5.0.7 | Secret 内两份原始头文件，解码后逐文件核对 SHA-256 |
| FidelityFX 1.1.4 | AMD 官方固定提交，编译 Optical Flow 与 DX12 后端静态库 |
| FFmpeg 9.0.1 | 从 Veyra 1.2.0 对应源码资产恢复原 vcpkg 配方，并应用项目 PS5 H.264 32→256 slices 补丁 |
| Chiaki 与串流库 | 固定 Chiaki 提交和子模块、项目两份补丁；从对应源码资产恢复 SDL3 3.4.14 等配方，编译匹配静态 CRT 的开发库 |

FFmpeg 构建记录同时绑定项目补丁 SHA-256 和新编译 avcodec DLL 的 SHA-256。重编译产生的新二进制不要求等于旧机器的 DLL 哈希；缺少补丁或二进制与本次记录不匹配会失败。此检查不改变播放器允许用户替换运行组件的行为。

公开来源：[NVIDIA DLSS](https://github.com/NVIDIA/DLSS)、[Intel XeSS](https://github.com/intel/xess)、[AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)、[Veyra 1.2.0 对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.2.0)。归档若被删除或更换，哈希校验会失败，需维护者核对新来源；不会退回未经补丁的 FFmpeg。

## 产物与验证范围

源码检查会编译播放列表窗口、AppShell 两种定义并运行队列/Win32/UI 合同测试，不需要 Secret。只有完整编译成功才产生 Veyra.exe。

完整编译启用 NR 和 Remote Play，保留 XeSS、AMD 后端。artifact 仅包含程序、编译着色器、许可证和构建信息，保留 7 天。**它不是可独立运行的便携包**，需要配套的既有运行组件；不包含 NVIDIA/Intel/FFmpeg 运行 DLL、SDK 或模型。

GitHub 编译不会执行 NR/FG Create/Evaluate、真实视频连播、PS5/采集实卡或 GPU delivery gate。云端运行尚未执行；本地验证状态见 `docs/WORKLOG.md`，不能把脚本存在当成完整编译通过。

本地复现需 Windows x64、Visual Studio 2022 C++ 工具、CMake、Ninja、Python 3.11+、Git，工程路径不能含空格。原始 NVOF 头文件存在本地指定目录时，无需设置 Secret：

```powershell
./scripts/ci/prepare-dependencies.ps1
./scripts/ci/build-windows.ps1
```
