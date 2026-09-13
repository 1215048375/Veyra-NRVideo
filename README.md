# Veyra

<p align="center"><img src="assets/veyra-logo.png" alt="Veyra" width="200"></p>

[English](README_EN.md) | 简体中文

<p align="center">
  <a href="https://github.com/Likely7/Veyra-NRVideo/blob/main/REAMDE%20MP4.mp4">
    <img src="assets/readme-demo.gif" alt="Veyra 演示视频" width="960">
  </a>
</p>


Windows 视频播放器与采集卡增强工具。支持视频、图片、采集卡实时预览和 PS5 局域网串流，可组合使用超分辨率、NR 画面增强与补帧。

[下载 1.0.1 免安装版](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.1) · [更新记录](docs/RELEASE_NOTES_1.0.1.md) · [反馈问题](https://github.com/Likely7/Veyra-NRVideo/issues)

## 功能

| 功能 | 支持内容 |
| --- | --- |
| 播放与采集 | H.264 / HEVC 视频、PNG / JPEG 图片、DirectShow / UVC 采集卡 |
| 超分辨率 | DLSS SR、RTX Video SR；2K / 4K / 8K 目标，保持画面比例 |
| NR 增强 | 实验性 NVIDIA NR；实时档与原生档，风格、强度和局部保护调整 |
| 帧生成 | DLSS 2X / 3X / 4X，实验性 XeSS 2X 预览补帧 |
| 调整与对比 | 设置实时生效、还原默认、原画对比、分屏、画面缩放 |
| PS5 串流 | 局域网搜索 / 手动 IP、配对保存、唤醒、手柄、软硬解选择、实验 PSN 登录与 HDR |
| 导出 | PNG / JPEG 图片，NVENC H.264 / HEVC 视频 |
| 实时状态 | 源帧处理、有效补帧、呈现提交、链路耗时和软件延迟 |

日常模式以观看为主；专业模式展开增强参数、诊断和导出工具。切换模式不需要重新打开视频。

1.0.1 修复采集音频未协商低延迟缓冲的问题，增加音频输入与队列诊断；保留 1.0.0 的功能与修复。

## 下载与运行

1. 在 [Releases](https://github.com/Likely7/Veyra-NRVideo/releases) 下载 **Veyra-1.0.1-win64-portable.zip**，不要下载 Source code。
2. 完整解压到一个可写文件夹，双击 **Veyra.exe**。无需安装 SDK、Python 或开发工具。
3. 使用当前显卡驱动。要使用 NVIDIA NR、DLSS、RTX Video SR 和 NVENC，需兼容的 NVIDIA RTX 显卡；本版本主要在 RTX 5070 上验证。

系统要求：Windows 11 x64、DirectX 12。便携包含当前功能所需运行组件，显卡驱动和采集卡驱动由系统提供。8K 超分和原生高分辨率增强需要更多显存，不保证每个组合都能实时运行。

升级时先退出旧版，解压到新目录。需要保留设置时，复制旧目录下的 `runtime_local/*.v1` 与 `veyra.ini`；不要用旧目录整体覆盖新版运行组件。

## 使用教程
如果这个项目对你有所帮助，欢迎请作者喝杯咖啡！你的支持是持续维护的最大动力

<img width="276" height="374" alt="7baf2084b2310a7e685c5355bf9307d3" src="https://github.com/user-attachments/assets/1438c8f7-08b4-46fd-ad5e-6d83c46cdec2" />

### 视频与图片

专业模式顶部点击 **截图**，保存处理后的完整画面到系统“图片”文件夹下的 `Veyra Screenshots`，格式为 PNG。原生 HDR 截图暂不支持。

点击底栏“打开”选择文件。底栏提供播放、进度、音量、字幕和全屏；切换到专业模式后，鼠标位于画面上时可用滚轮缩放。

### 采集卡

1. 连接设备，关闭其他软件对同一采集卡的占用。
2. 点击“采集”，选择设备、分辨率、帧率、像素格式及音频设备。
3. 先关闭增强确认基础画面，再按需打开 NR、超分和补帧。同一模式下可优先试 YUY2 / NV12。
4. 若主机游戏为30fps、采集卡输出60fps，在专业模式的内容节奏中选择60转30；真正60fps内容使用原始采集节奏。

### PS5 串流

1. PS5 和电脑连接同一局域网，优先网线。在 PS5 **设置 → 系统 → 远程游玩**中启用远程游玩。
2. 点击软件 **PS5** 并搜索主机。搜不到时，在 PS5 **设置 → 网络 → 连接状态 → 查看连接状态**找到 IPv4 地址，手动填写。
3. 填入 **PSN Account ID**，不是昵称或 Online ID。实验性“登录 PSN”可打开索尼网页；完成登录后复制最终跳转网址，再点击“从剪贴板提交登录结果”获取 ID。
4. 首次配对仍需 PS5 **远程游玩 → 关联设备**的八位配对码。成功后选已保存主机直接连接，不需每次重新配对。
5. 手柄连接 **电脑**，DualSense 优先 USB。兼容设备支持触摸板、陀螺仪、震动及自适应扳机，效果取决于设备、连接方式和游戏。“仅观看”不转发 PC 手柄，也不保证同账号下 PS5 直连手柄与串流会话共存。

输入最高可选 1080p；2K / 4K / 8K 是本地超分目标，不是主机原生串流分辨率。H.264 / H.265 可选，码率请求范围 5–100Mbps，不保证主机实际达到请求值。自动解码优先硬解、失败回退软解，也可手动指定。

主机和 PSN 凭据加密保存在 **%LOCALAPPDATA%/Veyra/remoteplay**，绑定当前 Windows 用户，升级不需搬运；不要分享这个目录。退出 PSN 不删除主机配对。首次免配对码注册、外网串流未实现。

**实验 HDR**：H.265 HDR 在 Windows HDR 开启且所有增强关闭时可原生输出；开启增强或使用 SDR 显示器时先映射为 SDR，不是原生 HDR NR。真实 PS5 HDR 与 Sony 登录仍待进一步验收。

### 增强与补帧

光流可选 NVIDIA NVOF、AMD FidelityFX 和 **GPU DIS · FAST 实验**，默认 NVOF。GPU DIS 供效果对比，本机 1080p 合成测试明显慢于 NVOF，不保证提升性能。

专业模式中分别开启 NR、超分和补帧。超分下方选择算法及目标尺寸；补帧页选择 DLSS 或 XeSS 及可用倍率。建议从实时 NR、较低 RTX Video SR 档位和2X补帧开始，结合对比画面与实时状态调整。

处理跟不上时降低画质、倍率或目标尺寸。四个阶段卡显示光流、NR、超分、补帧的 GPU 耗时；曲线显示光流、NR、超分、残差合成、补帧的总增强处理耗时；展开后的第一项另列画面额外延迟估计。两者口径不同，都不是按键到屏幕的实测延迟。

音画同步自动跟随软件处理链路，采集卡音视频共同的输入延迟不会重复补偿。轻微耗时波动不会逐帧停放声音；持续欠速时实时预览跳过过期的补帧或源帧增强机会，保持媒体时间前进。导出仍完整处理，不采用预览跳帧策略。

鼠标在设置上停留约半秒即可查看说明。默认超分→NR→补帧；“低延迟模式”改为 NR→超分→补帧，默认关闭，仅影响预览。可能降低耗时，也可能增加拖影或边缘瑕疵。

### 导出与运行组件

在专业模式选择图片保存或视频导出、格式与输出位置。视频导出会在结尾核对完整性，可能需要额外时间，可取消。内嵌字幕暂不保留；不要让多个实例同时导出到同一个目标文件。XeSS 目前仅用于预览；视频导出使用已支持的 DLSS 路径。

允许自行替换 DLL：退出软件后，NVIDIA 文件放在 `runtime/experimental/`，XeSS / XeLL 放在 `runtime_local/intel/experimental/`，保留文件名。软件不锁定哈希或签名；清单仅记录发布包原件，替换版的接口与硬件兼容性不作保证。卸载整个软件只需退出后删除解压目录。

### NR 运行版本

专业模式 → 增强 → NR 运行版本，可选 NVIDIA 原版或 RTX 40/50 社区兼容版。切换会短暂停顿，失败恢复上一套设置。社区版已随包放在 `runtime/experimental/nr-community/`，无需手动覆盖原版。它是社区修改文件，签名状态为 `HashMismatch`，不是有效 NVIDIA 签名原件；两版目前均在 RTX 5070 验证。

### OBS 直播与录制

添加“窗口采集”，选择 Veyra，并将“捕获方式”手动设为 **Windows 10（1903及以上）**。不要依赖“自动”：它可能选中 BitBlt，导致只捕获 UI、视频区域没有画面。本机已通过切换到该 Windows 捕获方式恢复视频。

软件里的“直播兼容 · 实验”只切换显示交换链，不能解决 BitBlt 捕获问题，默认关闭即可。其他录屏软件优先选 Windows Graphics Capture / WGC；未验证所有录屏软件及补帧输出的捕获节奏。

## 技术路线与边界

<img width="332" height="576" alt="123" src="https://github.com/user-attachments/assets/3cd48fca-2f7b-418a-8b0e-0848f6bde556" />


C++20、Win32、D3D12；文件由 FFmpeg 处理，采集使用 DirectShow，视频导出使用 NVENC。各入口共享增强管线，采集保留最新帧，光流提供估算的运动信息。

NR 与 DLSS 帧生成属于 **community experimental / 社区实验集成**，不是 NVIDIA 官方认证或完整游戏原生集成。采集画面没有游戏引擎的原生深度和运动数据，效果可能存在拖影或细节变化。AMD NR 暂未提供；FRUC 已移除；仅 PS5 有上述实验 HDR 路径，文件 / 采集 HDR、HDR 导出、AV1 / ProRes 导出尚不支持。实卡兼容性与长期稳定性仍需持续验证。

## 开发与许可

[构建说明](docs/BUILD.md) · [组件清单](docs/RUNTIME_COMPONENTS_1.0.1.md) · [第三方许可](THIRD_PARTY_NOTICES.md)

Veyra 原有源码采用 [GPLv3](LICENSE)；含串流的组合程序同时适用 [AGPLv3 与上游 OpenSSL 例外](licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt)。应用源码对应版本标签，Release 另附串流依赖与 FFmpeg 对应源码包，普通用户无需下载。SDK、模型和运行时不进入源码仓库；Release 组件按各自许可与实验发布范围单独提供。


<details>
<summary>已搁置的 AMD NR 方向：调研参考</summary>

[DLSS-NR-on-AMD](https://github.com/danielblnc/DLSS-NR-on-AMD)、[dlss5-on-amd-9070xt-porting](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting)、[dlss5-image-enhancer-zluda](https://github.com/RedDukeDev/dlss5-image-enhancer-zluda) 及其 [ZLUDA 分支](https://github.com/RedDukeDev/ZLUDA)、[dlss5-neural-amd](https://github.com/zmodelerlover/dlss5-neural-amd)、[DLSS5-AMD-Video](https://github.com/eikkapine/DLSS5-AMD-Video)。这些项目用于可行性与性能路线研究，当前版本不提供 AMD NR。

</details>
