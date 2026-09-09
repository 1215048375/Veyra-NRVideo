# Veyra

Windows 本地视频播放器与画面增强工作台。它把视频播放、采集卡预览、图片增强和视频导出放进同一个原生应用：日常模式专注看画面，专业模式集中放增强、补帧、对比、诊断和导出工具。

Veyra is a native Windows video player and image-enhancement workspace. It combines local playback, capture-card preview, image enhancement, and video export in one application. Daily mode is built for watching; Professional mode exposes enhancement, frame generation, comparison, diagnostics, and export tools.

> 这是 `0.0.1` 的早期实验版本。便携包包含经哈希和签名校验的 Runtime Pack，让兼容 RTX 环境可直接启用对应增强；其中 NR、FRUC 和 DLSSG 属于社区实验运行时，不代表 NVIDIA 官方合作、认证或支持。This is an early experimental build. The portable package includes a hash- and signature-verified Runtime Pack so compatible RTX systems can enable its available enhancements directly. NR, FRUC, and DLSSG are community-experimental runtimes and do not imply NVIDIA endorsement, certification, or support.

## 功能 / Features

- 默认日常模式：大画面、底部播放控制、打开媒体、采集卡、音量、字幕和全屏。
- 专业模式：带展开动画的参数工作台；同一播放会话不中断。
- H.264 / HEVC 视频、PNG / JPEG 图片、DirectShow / UVC 采集设备。
- 可选 DLSS SR、RTX Video SR、实验性 NR、NVOF 运动置信度，以及 DLSS / FRUC 补帧后端。
- 原图与增强结果的即时对比、分屏拖动、专业模式内的画面缩放和性能诊断。
- PNG / JPEG 图片导出，D3D12 NVENC H.264 / HEVC 视频导出。
- Windows 11 Desktop Acrylic 控制面板；视频区域保持不透明，未打开媒体时为纯黑。

- Daily mode by default: a large picture area with playback, media opening, capture, volume, subtitles, and fullscreen controls.
- Professional mode: an expanding settings workspace without interrupting the current session.
- H.264 / HEVC video, PNG / JPEG images, and DirectShow / UVC capture devices.
- Optional DLSS SR, RTX Video SR, experimental NR, NVOF motion confidence, plus DLSS and FRUC frame-generation backends.
- Same-frame comparison, split view, preview zoom in Professional mode, and performance diagnostics.
- PNG / JPEG image export and D3D12 NVENC H.264 / HEVC video export.
- Windows 11 Desktop Acrylic for controls; the video area stays opaque and is pure black with no media open.

## 使用方法 / How to use

### 免安装包 / Portable package

1. 下载并解压 `Veyra-0.0.1-win64-portable.zip`。
2. 双击 `Veyra.exe`。
3. 在日常模式点击“打开视频 / 图片”或“采集”，然后播放、暂停、调节音量或进入全屏。
4. 点击“切换专业模式”展开完整面板。这里可以选择增强、超分、补帧、预设、对比和导出。

压缩包中的 `runtime_local/nvidia` 包含版本固定的 Runtime Pack。软件会读取 `release-runtime-manifest.json`，校验 DLL 哈希和签名；校验、驱动或硬件不兼容时自动关闭对应增强，基础播放器、采集与导出仍可启动。不要自行替换其中的 DLL，也不要从游戏目录、驱动缓存或未知来源复制文件。

1. Download and extract `Veyra-0.0.1-win64-portable.zip`.
2. Run `Veyra.exe`.
3. In Daily mode, choose Open Video / Image or Capture, then play, pause, adjust volume, or enter fullscreen.
4. Select Switch to Professional Mode to expand the complete workspace. This is where enhancement, super resolution, frame generation, presets, comparison, and export are configured.

The package includes a version-pinned Runtime Pack in `runtime_local/nvidia`. The release builder generates `release-runtime-manifest.json` and verifies DLL hashes and signatures; NR and FRUC also validate their pinned identities at startup. If driver, hardware, or initialization compatibility fails, Veyra disables only the affected enhancement while the player, capture, and export paths remain available. Do not replace these DLLs or copy files from games, driver caches, or unknown sources.

### 从源码构建 / Build from source

准备 Windows x64、Visual Studio 2022 C++ 工具链、CMake 3.24+、Ninja 和 Windows SDK。NVIDIA 与 FFmpeg 依赖请按各自许可准备在本地，仓库不会自动下载它们。

```powershell
git clone https://github.com/Likely7/Veyra-DLSS-Video-Player.git
Set-Location Veyra-DLSS-Video-Player
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release
.\Veyra.cmd
```

Prepare Windows x64, the Visual Studio 2022 C++ toolchain, CMake 3.24+, Ninja, and the Windows SDK. Supply NVIDIA and FFmpeg dependencies locally under their own licenses; this repository does not download them automatically.

## 技术路线 / Technical approach

三个输入入口共享同一条 GPU 图，不复制三套增强逻辑：

```text
视频 / 图片 / 采集卡
        → 颜色与时间戳解析
        → SR（可选）
        → NR（可选）
        → 帧生成（可选）
        → 呈现、图片导出或 NVENC 视频导出
```

The three input modes use one shared GPU graph rather than three separate enhancement implementations:

```text
Video / image / capture card
        → color and timestamp handling
        → optional SR
        → optional NR
        → optional frame generation
        → presentation, image export, or NVENC video export
```

- Veyra uses C++20, Win32, D3D12, FFmpeg, and Windows media APIs.
- SR is performed before NR; frame generation is performed after NR. Player subtitles and OSD are composed after frame generation.
- Capture uses a latest-frame mailbox to limit latency. Frame generation requires the next real source frame; it cannot remove a capture card's inherent delay.
- Realtime NR can use an explicitly labelled 1080p-class internal working size for 4K input. Native 4K NR remains available but costs substantially more GPU time. Export retains its native-size path.
- HDMI and ordinary video do not contain game-engine depth, motion vectors, exposure, or HUD-free color. Veyra estimates only what can be derived from pixels and does not claim game-native equivalence.

## 当前边界 / Current limits

- SDR is the supported delivery path. HDR, AV1 / ProRes export, and VFR-preserving export are not part of this version.
- Depth is not a shipped default provider. Current motion guidance uses NVOF confidence handling where available.
- 3X and 4X frame generation are experimental. A selected multiplier does not guarantee that every source-frame pair produces a valid generated frame or that every configuration runs in real time.
- Actual scanout latency, long-term stability, capture audio behavior, and every GPU / driver combination require further real-device validation.
- No NVIDIA runtime, SDK archive, headers, libraries, models, or samples are in this Git repository. The user-authorized Release Runtime Pack is a separate Release-only payload and remains experimental.

## 项目结构 / Project layout

| Path | Purpose |
| --- | --- |
| `apps/veyra` | Win32 application and the two-mode interface |
| `include/veyra`, `src` | Shared engine, media, GPU, enhancement, and export code |
| `shaders` | GPU shader sources and build rules |
| `tests`, `scripts` | Tests, build commands, and local acceptance helpers |
| `docs` | User guide, architecture notes, work log, and implementation plans |

## 许可 / License

The repository keeps its existing [GNU GPL v3 license](LICENSE). Third-party notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This repository license does not grant rights to redistribute NVIDIA SDKs, runtimes, drivers, or other external components.
