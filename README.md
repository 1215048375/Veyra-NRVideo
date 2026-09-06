# Veyra

Windows x64 / C++20 / Win32 / D3D12 的本机视频增强软件，三个入口共用 `FrameSource -> EnhanceGraph -> FrameSink`。直接 NGX 实验 Feature 18，不依赖 ReShade 注入。

**运行：双击 `Veyra.cmd`。** 使用说明见 [docs/USER_GUIDE.md](docs/USER_GUIDE.md)，最新实测与未完成项见 [docs/DELIVERY_STATUS.md](docs/DELIVERY_STATUS.md)。不再用旧 Phase 数量或百分比表示产品交付。

## 当前交付合同

用户授权接管全部实现，自动联合测试不超过五分钟；当前采集卡没接入，由用户亲自验收。用户随后明确接受默认实时档、保留原生 4K 可选。

- 播放/采集默认 **Live 1080**：4K 解码、1080 内部 NR/FG、按显示窗口缩放。状态显示真实输入/处理尺寸，不称原生 4K NR。
- 原生 4K 与 SR 4K 可选；本机 RTX 5070 实测原生 4K NR 约 22–23ms/帧，无法承诺 4K60 实时。实时档的短测另行记录。
- 视频导出不采用实时档降分辨率：4K 输入原生 4K NR，可选 FG2X，D3D12 NVENC H.264/HEVC MP4，兼容音轨保留；PNG/JPEG 图片增强保存。
- DirectShow 实卡功能已接入，硬件测试状态始终为 `awaiting_user_capture_test`，不能冒充测试通过。
- Depth provider 未实现，明确 Motion Only；HDR / 3X4X / AV1ProRes / 私有采集SDK不在本次承诺。
- 仅本机研发交付，专有 DLL 分发未授权：`distribution_blocked`，不制作安装包或上传二进制。

## 文档唯一入口

1. `AGENTS.md`：安全、许可、实现纪律。
2. `docs/ACTIVE_DELIVERY_PLAN.md`：当前 F0–F6 推进与用户授权的短测合同，覆盖旧长测/严格串行施工规则。
3. `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` / `VEYRA_PRODUCT_SPEC_V1.md`：技术参数与原始规格；旧状态文字仅作历史，最新状态看 DELIVERY_STATUS/STATE。
4. `docs/COMPETITOR_AUDIT_2026-09-03.md`：竞品与许可事实。
5. `docs/WORKLOG.md`、`loop/STATE.json`、`loop/EVIDENCE.md`、`loop/JOURNAL.md`：实际证据与恢复点。

## 构建与短测

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\gates\delivery.ps1 -Root .
```

应用：`out/build/x64-release/veyra.exe`。依赖现有 `runtime_local/nvidia`、`third_party_local/nvidia` 与本机 FFmpeg；不把 SDK/runtime 提交 Git。NGX DLL 使用固定身份和绝对路径。Video Codec API 头文件来源/许可证见 `THIRD_PARTY_NOTICES.md`。

历史 Phase 0–4 仅证明当时基础 harness。旧 Phase 5 的错误输入、假指标和未接 Guidance 结论已撤回并修复；本次独立 review 前不宣称新的阶段放行。不得再跑旧 30 分钟 gate 作为本次默认任务，也不得把短测写成耐久证明。
