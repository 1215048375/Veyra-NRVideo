# GitHub Actions Windows 构建

工作流：`.github/workflows/windows-build.yml`，名称 **Windows build**。

## 没有现成依赖包时（2026-09-14 来源核对）

下面的私有整包方式适用于已有完整开发环境的维护者，不应要求新开发者先自行收集、编译所有依赖。当前用户没有该环境，后续应改造为公开依赖自动准备，仅保留无法匿名取得的输入。**现有工作流尚未实现这一自动准备过程**，以下来源核对不代表完整云端编译通过。

| 依赖 | 已核对来源 | 准备方式 |
| --- | --- | --- |
| DLSS SDK 310.7.0 | [NVIDIA 官方版本](https://github.com/NVIDIA/DLSS/releases/tag/v310.7.0) | 固定版本获取开发头文件/静态shim，避免把SDK内运行DLL打包到CI产物 |
| XeSS SDK 3.0.2 | [Intel 官方版本](https://github.com/intel/xess/releases/tag/v3.0.2) | 固定版本获取开发接口 |
| FidelityFX SDK 1.1.4 | [AMD 官方发布](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases) | 用项目已记录提交编译需要的静态库 |
| FFmpeg、Chiaki及串流依赖 | [Veyra 1.2.0 对应源码入口](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.2.0)，另见本仓库BUILD/REMOTEPLAY_BUILD文档 | 由脚本按固定源码、配套版本和补丁重建；源码ZIP不等于已编译开发库 |
| Optical Flow SDK 5.0.7 | [NVIDIA 官方下载页](https://developer.nvidia.com/opticalflow/download) | 需要NVIDIA开发者账号登录，下载前接受网站许可；页面“Windows / Linux Accept & Download”实际指向 `optical_flow_sdk_5.0.7.zip` |

已核对的5.0.7下载入口为 [官方受登录保护的ZIP](https://developer.nvidia.com/downloads/designworks/optical-flow-sdk/secure/5.0/optical_flow_sdk_5.0.7.zip/)，未登录访问会跳转登录页。当前产品需要 `nvOpticalFlowD3D12.h`，不能仅因为另一个公开头文件仓库叫OpticalFlow就断言可以替代完整固定版本。

用户下一步只需取得该官方Optical Flow SDK ZIP并提供本地位置，后续开发库整理和自动化由Agent处理。现阶段不要求用户自行制作下面的整套私有ZIP。当前没有下载/接受此许可、取得完整SDK或重写成功的无Secret完整构建；也没有替换任何已批准运行DLL。

## 开箱可执行的源码检查

推送 `main` / `codex/**`、Pull Request 或手动运行都会使用 GitHub 的 `windows-2022` runner，执行现有 `scripts/acceptance/playlist.ps1`：

- 编译播放列表窗口和 AppShell（包含与不包含 Remote Play 两种定义）。
- 运行队列、Win32列表窗口和已有UI合同测试。
- 上传本次日志，保留7天；不上传测试目录里的配置、OBJ、SDK或运行组件。

这部分不需要Secret，但**不会产生完整Veyra.exe**。它验证源码和列表交互，不代替真实视频播放、NR/FG或显卡验收。

## 完整应用编译：需要一次性准备依赖

仓库目前没有完整开发库。GitHub提供编译工具，但不会自动拥有本项目固定版本SDK与patched FFmpeg。完整入口不是通过关闭PS5/XeSS/AMD功能来绕过依赖。

1. 将已有、已接受许可的**开发依赖**制作成私有ZIP。不要提交Git或上传为Actions artifact，不包含NR、DLSS运行DLL、RenoDX、个人配置、凭据、测试媒体。不要直接压缩完整Release或原始SDK下载包。
2. 在仓库 **Settings → Environments** 创建 `windows-build`；将部署分支限制为默认分支，可按团队需要添加审批者。
3. 在该Environment添加两个Secrets：
   - `VEYRA_CI_DEPS_URL`：可供runner下载ZIP的私有HTTPS直链，例如有时效的签名URL。URL不写入源码。
   - `VEYRA_CI_DEPS_SHA256`：ZIP的完整SHA256，可由 `Get-FileHash -Algorithm SHA256 dependency.zip` 获取。
4. 工作流文件进入默认分支后，打开 **Actions → Windows build → Run workflow**，选择默认分支，勾选 `full_build`。
5. 成功后下载 `Veyra-win64-build-<commit>` artifact。只有成功的完整编译才有该产物；缺少Secret、库、固定patched FFmpeg或构建失败都会报错。

完整任务只允许默认分支的手动运行；PR自动任务没有私有依赖Secret，不使用 `pull_request_target`。工作流只有 `contents: read` 权限，不创建Release、不push、不打包实验Runtime Pack。

### ZIP目录合同

ZIP根目录直接是 `third_party_local` **里面的内容**，不要额外套一层目录。解压只写入runner新建的 `third_party_local`，先核对ZIP哈希和路径，拒绝路径越界及指定增强运行DLL。

根目录包含 `ci-build.json`，如下（所有路径相对ZIP根目录）：

```json
{
  "schema": 1,
  "dlss": "nvidia/DLSS_SDK_310.7.0",
  "nvof": "nvidia/Optical_Flow_SDK_5.0.7",
  "ffmpeg": "ffmpeg/x64-windows",
  "xess": "intel/xess-3.0.2",
  "fidelityfx": "amd/FidelityFX-SDK/sdk",
  "chiakiClean": "remoteplay/chiaki-clean",
  "chiakiStage": "remoteplay/chiaki-stage",
  "remotePlayPrefix": "remoteplay/prefix",
  "protoc": "tools/protobuf/protoc.exe",
  "pkgConfig": "tools/pkgconf/pkgconf.exe"
}
```

还必须在固定位置保留 `nvidia/nv-codec-headers/include/ffnvcodec/nvEncodeAPI.h` 及其相关头文件，因为当前CMake使用这个路径。

具体要求：

- DLSS 310.7.0开发头文件、x64 Release NGX静态shim；NVOF 5.0.7头文件。不需要NR运行DLL参与编译。
- FFmpeg prefix包含对应的 `include`、`lib`、`share/ffmpeg` 和 `bin`。`avcodec-63.dll` 必须为已批准patched构建：`0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`；其余库必须配套。保留构建来源记录、补丁及对应源码供审计。这里只校验该已批准二进制的身份，不修改播放器运行时加载规则。
- XeSS 3.0.2的 `inc`；FidelityFX 1.1.4源树的所需头文件及现有x64光流/后端静态库。缺少时完整任务失败，不静默省略后端。
- Chiaki固定提交 `0e16950165f06e5c3291537c2eeba6e852be7120` 的干净checkout和打过项目两补丁的stage，**都保留.git及完整固定子模块**，继续由 `verify-chiaki-stage.py`验证。可分别clone后准备stage，避免ZIP里的.git文件指向原电脑的绝对worktree路径。
- Remote Play prefix为配套x64静态依赖（含SDL3 3.4.14、Opus、OpenSSL、json-c、libevent、miniupnpc及上游需要的库），其CMake包和pkg-config文件必须能在新路径使用；同时保留可运行的protoc/pkgconf及其所需工具依赖。参考 `docs/REMOTEPLAY_BUILD_1.2.0.md`。CMake不会自动把原电脑的绝对路径变成可迁移路径。
- 开发库必须匹配Windows MSVC x64、Release和项目静态CRT合同；仅复制头文件不足以完成链接。

依赖包只用于短暂的私有CI工作目录，不缓存、不上传。当前尚未制作此包，也没有替用户设置Secret。

## 产物与验证范围

完整任务配置 `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON`、`VEYRA_ENABLE_REMOTEPLAY=ON`，直接构建项目 `veyra` 目标。上传仅含Veyra.exe、编译着色器、许可证/第三方说明、构建提交与哈希信息。

**这不是免安装便携包**：没有FFmpeg/XeSS/NVIDIA等运行DLL，不能独立启动使用。需匹配的既有批准运行包；不要把CI编译成功描述为运行组件身份或GPU验收通过。GitHub构建不执行NR/FG Create/Evaluate、实际媒体连播、PS5/采集实卡或delivery GPU gate。正式发布依旧需要本地完整运行验证和单独授权。

## 本次检查记录

已对YAML结构、触发器/默认权限/私有任务条件，以及两份PowerShell脚本的语法做本地检查；缺少Secret和GitHub上下文时明确失败。默认任务调用上一轮已通过的同一验收脚本。云端工作流和完整依赖构建**未执行**，尚无GitHub run结果，不把配置文件当作云端构建成功证据。

官方参考：[工作流语法](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)、[Windows 2022 runner软件](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md)、[Artifacts](https://docs.github.com/en/actions/tutorials/store-and-share-data)。
