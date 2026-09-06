# Veyra

Veyra V1 是一个 Windows x64 / C++20 / D3D12 的本地首发产品候选，不以“最小 MVP”作为完成标准。首版必须同时交付三个入口、4K SDR 全链路和可恢复的产品行为，三者只共享一套增强引擎：

1. **采集卡实时模式**：读取 Windows DirectShow/UVC 采集设备，支持设备真实提供的 1080p/4K SDR 30/60 fps，提供低延迟与有界后帧缓冲两种模式，执行 DLSS SR（可选）→ Feature 18 → DLSSG 2X（可选）并播放 HDMI 音频。
2. **播放器模式**：打开最高 3840×2160 的本地 H.264/HEVC 视频，保持正确 PTS、字幕和 A/V 同步，执行同一条增强链后播放。
3. **导出模式**：对 PNG/JPEG 图片或最高 3840×2160 的 H.264/HEVC 视频应用增强；视频可选 2X 补帧，以原生 D3D12 NVENC 输出 H.264/HEVC，并保留或明确转换音频与基础元数据。

这里的“DLSS 5”指项目根目录中用户提供的实验性 `nvngx_dlssnr.dll` 所暴露的 Feature 18。NVIDIA 已于 2026-09 正式面向游戏发布 DLSS 5，但截至本次重基线，公开 Developer 页面和公开 Streamline SDK 尚未提供可供本项目直接替换的通用 DLSS 5 开发接口。用户决定继续使用当前实验 runtime 做本机研发；这不构成 NVIDIA 授权，也不允许 Veyra 将该 DLL 打包、上传或分发。

## 当前真实状态

- 2026-09-06 对抗式复核后的 Launch V1 交付进度约为 **40%（误差约 5%）**。Phase 数量不能当百分比；Phase 7 承担 UI、采集和两类导出，工作量远大于早期 harness。
- Phase 0–4 的 D3D12、Feature 18、颜色 parity、FFmpeg 解码与 DLSS SR harness 有可复现实测证据，继续有效。
- 历史 Phase 5 的组件证据保留：Frame/Reset 契约、scene/cadence、NVOF 合成位移与 endurance probe 已运行；但原 gate 把 manifest 当作 depth 完成、把第二次 1080p endurance 放在 4K 检查位置，而且 `src/core/EnhanceGraph.cpp` 只计数，真实 GPU 链仍由 harness 编排。因此 Phase 5 的产品级 `passed` 已撤销，必须重开修复。
- Phase 6 已有未提交的实验资产：真实 DLSSG capability/Create/Evaluate、59/59 非重复/非 blend 中间帧、NVOF、WASAPI 和 PresentSink；但当前播放器 probe 的 A/V drift P95 约 2.8 秒（门槛 50 ms），L2/GBV 有大量 descriptor-uninitialized 错误，Phase 6 gate/Reviewer/checkpoint 均未通过。
- 还没有可发布的共享 EngineController、播放器 UI、字幕、采集卡输入、DAV2 depth provider、图片导出、D3D12 NVENC 视频导出、设置/恢复/日志导出或安装交付闭环。
- 当前工作树包含一组未提交的 Phase 6 改动。新 Agent 必须先审计和保存它们，禁止 reset、checkout 或从旧 checkpoint 覆盖。
- 旧路线中“采集卡、Depth Anything 和导出不属于 V1”的决定已于 2026-09-03 被用户撤销；不要再按旧边界施工。
- 旧路线中“只做 1080p 薄闭环、离线导出允许 raw pipe”的决定也已于 2026-09-03 被用户撤销；首发 gate 必须覆盖 4K SDR、原生硬件编码和产品级恢复/诊断。
- 当前工作从“Phase 5 产品级修复”继续。唯一施工顺序见 `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` 的“2026-09-06 接管恢复计划”和 `loop/BACKLOG.md`；Phase 7 联合 gate 全通过才算功能 release candidate。

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

## 当前接管原则

- 不重写已验证的 NGX/NVOF/FG backend；先把它们从 `tools/player_probe/main.cpp` 迁入真正共享的库。
- 不先做漂亮 UI。先让共享 engine 在 1080p 与 native 4K 上通过正确性、GBV、GPU timing、A/V drift 和 teardown 门槛。
- 不接受“probe 能跑”等于“产品完成”。Capture、Player、Export 必须由同一可执行程序调用同一 graph。
- 泄露 runtime 只能是外置、可关闭、固定 hash 的实验依赖；功能完成但分发权未解决时状态只能是 `distribution_blocked`。
