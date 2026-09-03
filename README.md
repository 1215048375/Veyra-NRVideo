# Veyra

Veyra V1 是一个 Windows x64 / C++20 / D3D12 的本地首发产品候选，不以“最小 MVP”作为完成标准。首版必须同时交付三个入口、4K SDR 全链路和可恢复的产品行为，三者只共享一套增强引擎：

1. **采集卡实时模式**：读取 Windows DirectShow/UVC 采集设备，支持设备真实提供的 1080p/4K SDR 30/60 fps，提供低延迟与有界后帧缓冲两种模式，执行 DLSS SR（可选）→ Feature 18 → DLSSG 2X（可选）并播放 HDMI 音频。
2. **播放器模式**：打开最高 3840×2160 的本地 H.264/HEVC 视频，保持正确 PTS、字幕和 A/V 同步，执行同一条增强链后播放。
3. **导出模式**：对 PNG/JPEG 图片或最高 3840×2160 的 H.264/HEVC 视频应用增强；视频可选 2X 补帧，以原生 D3D12 NVENC 输出 H.264/HEVC，并保留或明确转换音频与基础元数据。

这里的“DLSS 5”指项目根目录中用户提供的实验性 `nvngx_dlssnr.dll` 所暴露的 Feature 18，不代表 NVIDIA 已公开发布或授权 Veyra 分发该运行时。

## 当前真实状态

- Phase 0–4 的 D3D12、Feature 18、颜色 parity、FFmpeg 解码与 DLSS SR harness 已有实测证据。
- 还没有可用的播放器 UI、采集卡输入、导出器、NVOF、深度估计或 DLSSG 产品闭环。
- 旧路线中“采集卡、Depth Anything 和导出不属于 V1”的决定已于 2026-09-03 被用户撤销；不要再按旧边界施工。
- 旧路线中“只做 1080p 薄闭环、离线导出允许 raw pipe”的决定也已于 2026-09-03 被用户撤销；首发 gate 必须覆盖 4K SDR、原生硬件编码和产品级恢复/诊断。
- 当前工作从 Phase 5 的统一 Guidance/质量核心继续，Phase 7 的三条端到端 gate 全通过才算 V1 完成。

## 开工顺序

必须依次完整阅读：

1. `AGENTS.md`
2. `VEYRA_PRODUCT_SPEC_V1.md`
3. `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`
4. `docs/COMPETITOR_AUDIT_2026-09-03.md`
5. `docs/WORKLOG.md`
6. Goal 模式再读 `loop/LOOP_ENGINE.md`、`loop/STATE.json`、`loop/BACKLOG.md`、`loop/INBOX.md`

然后运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
```

## 二进制边界

- `nvngx_dlssnr.dll`：用户提供、NVIDIA 签名的本地实验 runtime。只能复制到已忽略的 `runtime_local/nvidia/`，不得修改、提交、上传或随安装包分发。
- `renodx-dlss5-1.addon64`：未签名的 ReShade/RenoDX add-on，不是配置文件。只用于隔离对照；Veyra 主程序不得加载、注入、链接或分发它。
- Veyra 直接调用 NGX，不以 Magpie/ReShade 为运行依赖。

Magpie 已证明窗口/视频路径能调用这些能力，但没有替 Veyra 验证参数、资源状态、颜色、时序、延迟或画质。每一个“更好”结论仍需同源 A/B 证据。
