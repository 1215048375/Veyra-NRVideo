# Veyra DLSS Video Player

**面向 Windows 的本地视频增强播放器：视频观看、采集卡输入、图片与视频导出，共用一套 D3D12 增强管线。**

Veyra 使用 C++20 / Win32 原生界面，直接调用 NVIDIA NGX。日常模式把空间留给画面，专业模式展开精细参数、对比、性能信息与导出工具。控制区使用真实桌面毛玻璃，视频区域保持不透明，未打开媒体时为纯黑。

> **当前为实验开发版，状态：Phase 7 / needs_review。** 此仓库提供源码存档，不包含可直接运行的安装包、NVIDIA 专有运行时、SDK、个人媒体或本机测试日志。部分 UI 与 GPU 路径已经本机实测；整体交付门禁、当前版本实卡体验和长期稳定性仍有待完成。

## 主要功能

| 方向 | 当前实现 |
| --- | --- |
| 日常观看 | 默认日常模式；底栏直接提供打开、采集、最近文件、增强、SR、播放、音量、字幕与全屏等常用操作，窄窗口保留图标入口。 |
| 专业工作台 | 240 毫秒展开 / 收起动画；保留同一播放会话；集中管理增强、补帧、预设、原图对比与导出。 |
| 原生界面 | Windows Desktop Acrylic 透出窗口下方的模糊颜色；统一圆角选择器、Lucide 图标、视频全屏与自动隐藏控制条。系统不支持材质时回退。 |
| 媒体输入 | H.264 / HEVC 视频、PNG / JPEG 图片；DirectShow / UVC 采集设备；当前范围为最高 3840×2160 的 SDR。 |
| 图像增强 | DLSS SR、实验 Feature 18 NR、NVOF 光流与置信度门控；实时 NR 与原生 NR 档位；参数应用失败时回滚。 |
| 补帧 | DLSSG 2X 路径；另有实验性 3X / 4X 设置。倍率不等于实际显示帧率，也不代表所有组合均能实时运行。 |
| 对比与诊断 | 同一源帧原图 / 增强对比、分屏拖动、实际处理尺寸与阶段耗时、可预览的脱敏诊断。 |
| 图片导出 | 增强 PNG / JPEG，支持保存当前真实源帧。 |
| 视频导出 | 独立后台作业，冻结启动时的设置；D3D12 NVENC 编码 H.264 / HEVC MP4，支持暂停、取消和优先观看。 |
| 音频与字幕 | 应用独立音量、静音、外部 SRT；导出保留第一条兼容 MP4 的音轨，不兼容时明确拒绝。 |

操作说明见 [使用指南](docs/USER_GUIDE.md)。最新界面变更见 [底栏与选择器交付报告](docs/UI_CONTROLS_V4_DELIVERY_2026-09-08.md)。

## 分辨率与性能

- **实时 NR 档**：保留源画面 / SR 底图，NR 在约 1080p 内部尺寸处理，再由 GPU 将增强变化回填到底图。它不是原生 4K NR。
- **原生 NR 档**：按工作画面尺寸执行 NR，成本更高；不承诺本机原生 4K NR 达到 60 fps。
- **SR 4K**：按原比例放大到 3840×2160 范围，尺寸取偶数。例如 16:9 为 3840×2160，4:3 为 2880×2160；达到边界时跳过 SR。
- **视频导出**：不沿用实时档的内部降分辨率策略；4K 输入使用原生 4K NR。后台观看与导出共享 GPU，可能争用资源。

补帧需要等待后续源帧，不能消除采集卡固有延迟。历史短测中 SR 4K + 4X 组合仍出现明显落后，不能把功能调用成功写成 4K60 实时通过。

## 架构

```text
视频 / 图片 / 采集卡
         │
     FrameSource
         │
     EnhanceGraph
  统一颜色处理 → SR（可选）→ NR（可选）→ DLSSG（可选）
                 NVOF 光流 + 置信度门控
         │
      FrameSink
    ┌────┼────────────┐
 屏幕呈现  PNG / JPEG   D3D12 NVENC → MP4
```

播放器、采集与导出复用产品库。字幕与播放器叠加层在补帧之后合成。产品不依赖 ReShade，也不加载 RenoDX add-on；当前 NR 适配器为本机实验 Feature 18 路径，不代表获得通用官方 DLSS 5 接口或分发授权。

## 构建与运行

### 环境与本地依赖

- Windows x64，Visual Studio 2022 C++ 工具链，CMake 3.24+、Ninja，以及带 `dxc.exe` 的 Windows SDK。
- 兼容所用 NGX / NVOF / NVENC 路径的 NVIDIA GPU 与驱动。已有本机测试使用 RTX 5070；没有据此验证所有 RTX 型号。桌面毛玻璃使用 Windows 11 Desktop Acrylic。
- 下列依赖须按各自来源与许可自行准备，源码仓库不会自动下载实验运行时。

| 依赖 | 当前构建约定 |
| --- | --- |
| NVIDIA DLSS SDK 310.7.0 | `third_party_local/nvidia/DLSS_SDK_310.7.0/`，包含头文件与 x64 NGX 库。 |
| NVIDIA Optical Flow SDK 5.0.7 | `third_party_local/nvidia/Optical_Flow_SDK_5.0.7/`。 |
| NVENC API 头文件 | `third_party_local/nvidia/nv-codec-headers/include/`；固定来源见 [第三方说明](THIRD_PARTY_NOTICES.md)。 |
| NVIDIA 运行时 | `runtime_local/nvidia/`；实验 NR 仅使用 [项目规则](AGENTS.md) 中固定身份的本地文件。仓库不提供或代寻该文件。 |
| FFmpeg 开发依赖 | 构建脚本当前自动检测 `C:/veyra-deps/installed/x64-windows/`，含头文件、库及 `share/ffmpeg/FindFFMPEG.cmake`。 |
| 测试工具 | 部分检查需要 PATH 中的 `ffmpeg` / `ffprobe`，以及本地合成媒体；测试工具依赖与产品依赖分开。 |

没有完整依赖时，部分 CMake 目标不会生成；仅克隆源码或编译基础测试不等于得到完整播放器。`build.ps1` 仍包含本机依赖目录约定，尚未提供一键依赖安装器。运行时 / shader 路径在构建时绑定，移动目录后应重新配置与构建。

### 构建

在 PowerShell 中执行：

```powershell
git clone https://github.com/Likely7/Veyra-DLSS-Video-Player.git
Set-Location Veyra-DLSS-Video-Player

# 先按上表准备本地依赖，再构建。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release

# 构建成功且运行依赖就绪后启动。
.\Veyra.cmd
```

输出程序为 `out/build/x64-release/veyra.exe`。如需自定义依赖根目录，可在 x64 开发者环境中直接使用 CMake 的 `VEYRA_DLSS_SDK_ROOT`、`VEYRA_NVOF_SDK_ROOT`、`VEYRA_FFMPEG_ROOT` 等缓存项；具体目标条件见 [CMakeLists.txt](CMakeLists.txt)。

## 验证状态

**截至 2026-09-08：UI v4 已完成限定范围验证，整体仍需验收。** 最新报告记录了实际构建、EXE 身份、NVIDIA Create / Evaluate 返回值、失败与修复，不以旧阶段通过记录代替当前产品状态。

- 最终 UI v4 程序：布局 / 状态与 14 场景弹层检查通过；40 次模式切换检查通过；NR / SR 开关等原生运行检查通过。
- 最后一次 12 DIP 排版微调前：完整 UI 15 命令与图片 / 视频 / 导出 11 命令检查通过。报告明确区分这两个构建，未把上一构建的全套结果标成最终构建结果。
- 既有统一 `delivery` 门禁仍有失败：旧断言要求 23 帧，当前 CFR 尾部保持策略输出 24 帧。尚未解决该门禁差异；专项通过不等于 Phase 7 通过。
- 当前版本实卡体验、长期稳定性、多屏物理 DPI 与部分真实 IME 场景尚未完成验收。历史实卡吞吐测试不替代当前版本体验确认。

在本地依赖与相应测试素材就绪后，可分别运行：

```powershell
# 底栏、布局、图标身份与原生弹层专项。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\acceptance\ui-controls-v4.ps1 -Root .

# 完整双模式专项；需要脚本指定的本地合成媒体。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\acceptance\ui-dual-mode.ps1 -Root . -Case all

# 统一软件交付门禁；当前已知失败如上，不标记为全绿。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\gates\delivery.ps1 -Root .
```

**每次测试调用最多 300 秒，累计测试时间不设“一轮五分钟”限制。** 原始日志、截图和测试媒体保留在本机，不随源码上传；文档中的历史 `logs/` 路径不是 GitHub 可下载附件。此次 README / 源码存档不新增 GPU 性能结论，也未验证全新机器的一键构建。

## 已知边界

- 当前面向 SDR；HDR、AV1 / ProRes 导出、VFR 原样输出与厂商私有采集 SDK 不在已交付范围。
- Depth provider 尚未实现，当前使用 NVOF motion + confidence；估算输入不等同游戏引擎原生 depth / motion。
- 部分 NR 参数属于实验项，不能保证对所有素材有效；画质没有被证明等同或优于官方游戏实现及其他产品。
- 外部 SRT 可显示；内嵌字幕解码、多音轨选择、音频转码和字幕烧录导出尚未提供。
- 3X / 4X 为实验性扩展；有界短测不证明完整组合的长期稳定或实际屏幕扫描帧率。
- NVIDIA 运行时、SDK、FFmpeg 配置与成品分发仍需独立审计，目前没有可公开捆绑分发的安装包。

## 项目导航

| 路径 | 内容 |
| --- | --- |
| [apps/veyra](apps/veyra/) | Win32 应用与双模式界面。 |
| [include/veyra](include/veyra/) / [src](src/) | 共享引擎、媒体源、NGX、光流与输出实现。 |
| [shaders](shaders/) | 颜色、缩放、光流与增强合成 shader。 |
| [tests](tests/) / [scripts](scripts/) | 单元 / 集成检查、构建与验收入口。 |
| [使用指南](docs/USER_GUIDE.md) | 播放、采集、增强、字幕与导出操作。 |
| [最新 UI 交付](docs/UI_CONTROLS_V4_DELIVERY_2026-09-08.md) / [复核记录](docs/REVIEW_UI_CONTROLS_V4_2026-09-08.md) | 最新限定范围变更、真实验证与剩余问题。 |
| [当前交付计划](docs/ACTIVE_DELIVERY_PLAN.md) / [工作日志](docs/WORKLOG.md) | 当前合同与按时间保留的开发记录；旧条目不代表最新状态。 |
| [施工手册](VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md) / [产品规范](VEYRA_PRODUCT_SPEC_V1.md) / [项目规则](AGENTS.md) | 技术边界与开发约束。 |
| [竞品审计](docs/COMPETITOR_AUDIT_2026-09-03.md) / [状态](loop/STATE.json) / [证据](loop/EVIDENCE.md) / [施工记录](loop/JOURNAL.md) | 可追溯的历史事实与恢复资料；历史阶段标签不覆盖本文的当前验证状态。 |

## 许可证与存档范围

保留此 GitHub 仓库初始化时已有的 [GNU GPL v3 许可证](LICENSE)。Lucide / Feather 图标的 ISC / MIT 通知随素材保留，来源和其他依赖说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。仓库许可证不改变外部 NVIDIA 组件或其他依赖自身的许可条件。

本次公开存档包含当前源码、shader、许可允许保留的图标、构建 / 测试脚本与开发文档。完整本地开发历史另行保留；含旧测试视频或本地依赖记录的历史提交不推送。`runtime_local/`、`third_party_local/`、构建产物、个人配置、原始诊断与测试媒体均不包含在本次上传中。
