# 2026-09-08 优化 Goal 启动与 preflight 停点

用户明确开启目标模式，新增解除不合理媒体尺寸限制、专业预览悬停滚轮缩放，并实施现有优化方案；导出完整性检查保持现状。Goal 和 BACKLOG 已建立。

执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`，2.604秒 exit1，唯一失败 README hash。当前 README 与此前用户要求更新并上传的 f79ef95 一致；其他控制与两份二进制身份通过。按 AGENTS 明确停工条款，未自行修改控制清单/gate，已准备仅两处 hash 同步的未应用提案 logs/optimization-goal-20260908/，等待明确授权。

只读定位到 EngineController.cpp:89 对所有来源统一拒绝 3840×2160 以外和奇数尺寸，专业缩放需接 AppShell/VideoPresenter。新行为尚未实现；本轮未构建、未运行 GPU/实卡。STATE/JOURNAL/EVIDENCE/INBOX 同步，整体仍 Phase7 未验收。

---

# 2026-09-08 画质优化调研与 UI 修正像素检查

用户要求保持现有导出完整性检查，核对官方 / GitHub / 其他渠道的 NR、4K SR、FG 路线，以及当前 UI 修正是否有效，并合并旧方案给出优化建议。交付见 [优化方案](QUALITY_OPTIMIZATION_PLAN_2026-09-08.md)；架构审计加注最新决定，保留历史差距事实。本次不实施产品改动、不扩大导出检查，也不自动启动新后端或深度模型接入。

核对 NVIDIA DLSS5 研究说明、公开 DLSS / Streamline 文档、RTX Video SDK、NVOF / FRUC，以及固定提交的 Magpie、AIO、Feeder、video2dlssnr、Odyssey、2600th、Infinity Studio、Visual Enhancer，另参考 mpv / Video2X / RIFE 与教程和社区反馈。重点更正：NR 推理输入与 SR / FG 不能混同；本次 Magpie 最新源码使用 ZeroDepth，旧 DAV2 描述仅适用于历史版本；AIO 作者像素实验给出 UIAlpha / Backbuffer 资源线索，不能当成本机已验证的接口合同。

当前面板 `UI修正 · 未证实` 实际连接 `DLSSNR.UICorrection`，但 NR 未提供 UI / UIAlpha / Backbuffer / ControlMask，FG 也未提供其独立 UI 资源。自写隔离 probe 链接现有产品库，在 RTX 5070 上以静态 / 移动背景和固定文字 HUD 驱动真实 EnhanceGraph。自动遮罩开、关两种配置下，切换 UI 修正的四个取样帧 RGB 差异全部为零；重复基线也为零，而自动遮罩与强度零对照会改变像素。因此不能把当前选项当作有效的自动 UI 剔除；结论限定于所测内容和设置，不是任何场景永远无效的证明。未自动点击原生 UI 控件。

实际执行命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\logs\research\20260908\build-run-ui-probe.ps1`。最新诊断构建 2.684 秒、运行 4.600 秒、exit 0；192 次 NR Evaluate、180 次 NVOF Execute；Feature 18 Create `0x1 / Success`、非空句柄、SEH 0。最新证据位于 `logs/research/20260908/ui-1f853a0238804d8db3b26e3f763d7033/` 的 result JSON / stdout / CSV。诊断 EXE `0B0674016A029EAA81A0C6111EF3F895DFA601377D76A20FD6E204F13C62C405`；源码 `DBEE9D6D215ED37EC741CCE47515B75C154EB42E7877B953194EE6438C62DE35`。

失败与补测：首次程序产生完整输出但旧 Start-Process runner 没拿到 ExitCode，脚本失败，不算通过；改用显式 Process 结果读取后运行 4.230 秒 exit 0。看到 PNG 色块通道异常后增加 NR-off 与源图保存对照，得到上述最终 4.600 秒结果。三次进程均小于 300 秒，失败日志保留。CPU 源 RGB 245/128/38 经当前 PNG 保存后独立解码为 38/128/245；NR-off 内存为 244/128/36、保存后为 36/128/244，定位到保存路径。WIC 实际协商 GUID 尚未测得，因此仅将未处理格式协商列为疑似原因。UI 对照在 PNG 编码之前进行，不受该问题影响。

只新增方案、更新架构审计说明与本 WORKLOG；第三方文本缓存、自写诊断源码 / EXE / 图片与结果均在 Git 忽略目录。应用 EXE SHA256 仍为 `7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514`，指定 NR / add-on 身份一致，add-on 未加载。未修改产品、配置、runtime、SDK 或保护文件，未运行全部 delivery gate / 新 SR-FG 性能 / 实卡测试，未提交或上传。整体仍 `Phase 7 / needs_review`；旧尾帧 gate 差异不由本次专项解决。

文档检查：方案、审计与 WORKLOG 的本地链接 / 代码围栏 / 行尾空格检查通过，`git diff --check` 通过；Git 状态只有这三份文档，EXE 和诊断源码 hash 复核一致。PNG 的独立 System.Drawing 读取结果另存于最终实验目录的 `png-channel-check.json`。

下一条建议任务：先完成帧时长与颜色合同修复，再核验 SR 参数、实现确定有效的 UI 保护与运动可靠性增强；RTX Video SR 和深度先独立测收益。导出完整性检查保持现状。

---

# 2026-09-08 原始方案与当前架构审计

用户要求核对原始方案并对照当前架构。本地基线 `f79ef95`，工作树初始干净。完整对照见 [架构审计](ARCHITECTURE_AUDIT_2026-09-08.md)。确认真实产品复用 `pipeline::EnhanceGraph`，但完整帧 / 颜色 / reset 契约未贯通，深度推理与 C / 离线双向质量分支缺失，confidence 仅 cost 硬门控，播放器 / 导出主动选择软件解码；字幕、导出格式与完整验证也未达到原方案全部范围。

具体静态问题：采集 packet 未填 duration，默认 known zero 被调度器 clamp 到约 8.33ms；source 层与 graph 对缺失颜色标签的默认解释不一致。没有把这两项静态推导说成已测得画质或实卡延迟。报告单独列出后续已批准的分辨率 / 残差、源空间光流、深度暂缓、双模式 UI、独立导出进程和短测规则，避免混同为擅自偏离。

本次仅运行 Git / rg / 文件读取及文档检查；只改审计文档与本记录。未构建，未执行 RTX / NGX 或实卡测试，未改变应用、EXE、保护文件与 runtime，也未提交或上传。整体仍 `Phase 7 / needs_review`。下一条建议任务：贯通真实产品 FramePacket，先修采集 duration 与颜色元数据的唯一解释；本次尚未开始实现。

---

# 2026-09-08 GitHub 源码存档准备与 README

用户明确要求建立 Git 存档并上传到 `Likely7/Veyra-DLSS-Video-Player`。本次只修改 README、保留远端既有 LICENSE，并补充存档记录；应用源码与最终 UI v4 EXE 未改，不重新运行构建、GPU 或实卡测试，也不改变 Phase 7 / needs_review。

初始本地 `c557d5f`，94 次提交、661 个历史 blob、248 个当前文件。只读检查发现历史包含已移除的测试 MP4 和 `third_party_local/depth/manifest.json`，当前树没有这些文件。全历史常见密钥模式扫描未发现匹配，不把模式扫描当作绝对安全证明。指定 NR / add-on 身份匹配，Lucide 素材与许可保留。

远端 `main` 初始为 `8556fc7`，只有 README 和 GPL v3 LICENSE。沿用用户仓库已有许可证；保留本地完整历史，在远端初始提交之后追加当前源码快照，不合并或推送本地旧历史。详细范围与后续同步注意事项见 [源码存档记录](SOURCE_ARCHIVE_2026-09-08.md)。上传后的提交身份以 GitHub 远端及本机 `logs/github-archive/` 核对记录为准。

本轮执行 Git 状态 / 历史对象检查、`gh auth status`、`gh repo view`、`git ls-remote` / `fetch`、README 本地链接与格式检查、源码树 / 许可证 / 图标身份检查；结果记录在 `logs/github-archive/`。原始日志、SDK、runtime、用户配置和媒体不随源码上传。原有统一 delivery 门禁差异、实卡体验与长稳仍未解决；本轮不新增产品通过结论。

---

# 2026-09-08 UI v4：常用操作直出与统一玻璃选择器

日常底栏直接提供打开、采集、最近、总增强、SR、播放、音量、字幕、全屏及窗口控制；720px保留全部入口。采用固定来源的Lucide免费图标（完整ISC/Feather MIT通知），字幕和所有应用内下拉框使用同一Desktop Acrylic弹层。保留真实桌面透明与纯黑视频区域。

基线 `97ccd19`；最终EXE SHA256 `7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514`。最终布局/14场景弹层专项1.312秒PASS；40次切换15.008秒PASS；原生NR/SR/NaN草稿/底栏可见性25.858秒exit0，Create/Evaluate实际成功。紧邻的12DIP排版微调前，全UI15命令141.503秒PASS，图片/视频/导出专项11命令80.914秒PASS。报告明确区分EXE版本，不把旧证据冒充最终抓图。单次测试均小于300秒。

实际检查宽窗口、720px窗口、字幕/专业下拉、键盘确认取消。独立只读复核通过其限定代码范围；最后SR文字宽度微调另经主Agent验证。保护文件和runtime身份不变，整体仍Phase7/needs_review，实卡、长稳、分发和既有delivery门禁失败不由本轮UI验收代替。

详细文件、命令、真实日志、失败与截图：[UI v4交付报告](UI_CONTROLS_V4_DELIVERY_2026-09-08.md)；[只读复核](REVIEW_UI_CONTROLS_V4_2026-09-08.md)。下一条唯一任务：用户验收底栏/选择器，并继续原合同的实卡体验验收。

---

以下为历史记录：

# 2026-09-08 UI修复v3：真实桌面毛玻璃

已删除应用内彩色渐变，控制区采用Windows Desktop Acrylic，透出下方桌面或其他窗口的模糊颜色；视频区域不透明、空闲纯黑。同步修复窗口白边、视频全屏、专业NR/SR开关、数值草稿/回滚、滚动裁剪、日常底部布局与240ms展开动画。独立采集对话框仍为普通深色。

基线`b6b251d`，最终EXE SHA256 `B1892C51E8AFC27D7223E271D48D93107C34CAA96BC4014DFF6F45FDE0D7BF74`。build16成功；专项11命令PASS/81.001秒，UI全套15命令PASS/141.187秒，Repair18项PASS/子进程70.478秒，当前4K合同23项PASS/13.751秒。均为每次300秒以内，历史累计保留。40次切换P95 8.191ms/max9.963，442次提交epoch1，GDI12/handles721稳定。真实红蓝背窗、视频字幕、输入/粘贴、全屏及滚动已检查；独立只读复核通过本轮范围。

全项目仍Phase7/needs_review；本轮保护delivery gate保留FAIL（旧23帧断言，当前12源帧2X含尾部CFR占位输出24帧），不修改保护门禁或用补测冒充阶段通过。实卡、真实IME候选、多屏DPI、长稳及公开分发未验收。没有开启采集流或公开上传，没有再次执行历史的一次性关机请求。

完整文件、命令、RTX Create/Evaluate日志、失败和截图：[UI修复v3报告](UI_REPAIR_V3_DELIVERY_2026-09-08.md)；[只读复核](REVIEW_UI_REPAIR_V3_2026-09-08.md)；[操作指南](USER_GUIDE.md)。下一条唯一任务：用户通过`Veyra.cmd`验收当前UI和原合同中的实卡体验。

---

以下保留历史记录，旧的“当前版本/通过状态”不代表本轮：

# 2026-09-08 双模式UI本机软件交付

已实现批准的UI0–UI9：默认日常影院界面、专业四区工作台、共享视频宿主与无重开切换、完整参数/预设、真实音量和透明字幕、后台冻结设置导出、诊断/全屏/小窗口。最初普通Win32排布在用户反馈后重新实现。

基线本地存档`ddc515d`；当前EXE SHA256 `2DE33230CD3A6556BC8E1399DD93BB8959B8EF37B2BACEF23D1486590AA4D3C6`。UI全套15子检查109.583秒PASS；Repair v2 18项PASS，子进程计时合计70.712秒；4K补充23检查14.178秒PASS。真实4K输入/底图/光流/FG/输出、1080内部NR短测59.55源fps、落后P95 1.77ms。每次调用最多300秒，累计历史保留。

受保护delivery脚本此次34.736秒后在旧23帧断言FAIL；基线已有CFR tail hold，当前12源帧2X正确输出24帧/120fps/0.2秒。保护文件未改，补测不替代Phase gate。全项目仍needs_review；当前UI软件交付不等于实卡、多屏物理DPI、长稳、完整组合性能或公开发行通过。

详情、文件、命令、错误码、截图和复核范围：[双模式交付报告](UI_DUAL_MODE_DELIVERY_2026-09-08.md)。操作：[使用指南](USER_GUIDE.md)。下一条用户验收：从Veyra.cmd启动，在真实采集卡上检查新界面切换、声音和体感延迟。本轮未开启采集流、未更改外部应用或运行时、未上传发布。

---

以下保留历史版本记录，旧的“当前EXE/尚未施工/已通过”不代表本次状态：

# Veyra Worklog

## 2026-09-08 Dual-mode UI execution document only

User approved the two-mode design based on their references: Daily default for video/capture; Professional for fine controls, comparison, telemetry/diagnostics and export. Added docs/UI_DUAL_MODE_EXECUTION_PLAN_2026-09-08.md with verified code map, shared-session invariants, responsive DIP layout, controls/shortcut rules, UI0–UI9 tasks and test matrix. Read-only inspection identified necessary backend work: current startExport replaces the playback worker and audio volume is not exposed. The plan explicitly includes a bounded independent export worker using shared product libraries and real per-application audio controls; these are planned, not claimed implemented. User-rejected design skill was not used.

No app code, runtime, protected gate, user media or EXE changed; no build/GPU/capture test, Goal, commit or upload. EXE remains 1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266. Static checks: document links exist, code fences balanced, UI0–UI9 present, 7 protected hashes unchanged, deleted fixture remains absent. Test limit remains per invocation <=300 seconds. UI status is design_approved / implementation_not_started. Next implementation task, when requested: UI0 inventory and UI1 shared state/stable video HWND.

## 2026-09-08 Repair v2 final known-fix review

User corrected test budget to per invocation <=300s. Implemented timestamp-quantization CFR validation and candidate selection, full Desc rollback, failure-code diagnostic capture, and cached source identity/count preservation. Final build exit0 (3.9694163s); joint joint-a077b89fd39348e4a49f4195c9d4e416 18 cases exit0 in 71.0278864s runtime. Historical cumulative 346.3985650s includes failures; not reset. Read-only fresh reviewer review_known_fixes passed this limited fix scope, verified EXE 1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266. No old Phase pass updated; global needs_review retained. Full evidence, modified files, intermediate failures, performance limitations and user capture next action: docs/REPAIR_V2_DELIVERY_2026-09-08.md. Protected hashes unchanged, runtime identity matches, no capture/commit/upload. SR4K+4X smoke39.75sourcefps/2.17s lateness remains an explicit performance limitation, not a functional transaction failure hidden as realtime pass.


## 2026-09-07 B1–B5 selected; planning/handoff only

User selected B1–B5 for the next implementation scope: same-frame comparison, per-stage performance UI, transactional user presets, shared optical-flow quality profiles, and a local privacy-aware diagnostics center. The repair plan now contains their precise data ownership, state transition, UI, failure and acceptance contracts; the Magpie backlog marks them selected. Added a paste-ready next-conversation handoff that explicitly refuses to treat the historical shared Phase5–7 gate as new release proof and forbids modifying protected hashes/gates to manufacture a pass.

This entry is documentation only. No application code, gate, CONTROL_HASHES, review prompt, SDK/runtime, driver, external app, capture device or user media was changed or executed. No build/test/Goal/checkpoint/publication was performed. Current software remains needs_review with SR4K/FG user-visible defects unresolved.

## 2026-09-07 SR4K / FG / parameters planning only

User requested detailed documents and a Magpie feature shortlist before implementation. Added `C:/Users/123/Desktop/Veyra DLSS Video Player/docs/REPAIR_EXECUTION_PLAN_2026-09-07.md` and `C:/Users/123/Desktop/Veyra DLSS Video Player/docs/MAGPIE_FEATURE_BACKLOG_2026-09-07.md`; updated DELIVERY_STATUS with the unresolved SR4K/FG feedback and planning links. The plan covers resolution separation, real generated-frame ownership/content/pacing, SDK MFG 2/3/4, export timing, typed NR controls, independent residual controls, live settings, Chinese/fullscreen UI, and a shared 300-second future runtime-test budget. Other competitor features remain user-selectable candidates, not automatic implementation tasks.

Evidence this turn: read-only current code inspection plus Magpie 0.6.6/0.6.5 release and parameter/frame-sync documents; no Magpie GPU benchmark. No application code, SDK/runtime, protected control file, control hash, user configuration or existing deleted fixture was changed. No build, GPU test, app/device operation, Goal, checkpoint, driver/remote-software operation or publication. Documentation static checks are recorded with this turn's tool results; prior EXE and needs_review state remain unchanged. Next action: user selection/implementation confirmation, then resolve protected-document scope before any new Goal.

Static verification: `git diff --check` exit 0 (existing LF/CRLF notices only); all absolute local Markdown links in the two new documents and DELIVERY_STATUS resolve; no Unicode replacement characters; all 11 protected manifest file hashes match. No gate was run and no manifest was edited.

## 2026-09-07 capture latency repair

User explicitly requested implementation after the physical-card diagnosis. Owned bounded capture AVFrames + deferred Run + live-specific bounded pacing remove the stale-PTS wait; ring/presenter use one rotating cursor; live metrics no longer masquerade as photon latency. Code/files/commands/results are recorded in `docs/CAPTURE_LATENCY_FIX_2026-09-07.md`. Release build exit0; timing7/7, source8/8, physical 1080p50 off/NR/NR+FG ~49.4–49.8fps and0drops; final NR195/NVOF193/generated193. Visible-player consolidated gate21/21 exit0 in47.309s, run01b72df768524af9ab0aa8d2e3dbb59e, final EXE4A9BA4B321DEEC17C5E3562AF03EF75A316856200A8708FA8E692475A05B59C9. Preflight71/71. No proprietary files, external apps, driver, user settings or deleted user clip changed. Read-only reviewer attempt failed without final verdict; needs_review, no new checkpoint. User perceived latency/audio and actual generated-frame display cadence remain unverified.

## 2026-09-07 direct implementation delivery

See docs/DELIVERY_STATUS.md for current software, tests and limits. Actual app/controller/presenter, DirectShow source, WIC and D3D12 NVENC export now exist; earlier “no UI/export” entries are historical. Fixed NV12/uint shader inputs, real guidance-before-NR, scene resets, audio format/paused seek, GPU timestamp semantics. CMake x64-release exit0; consolidated gate run d28879b01b5c44dd86cad33d6f386d90 exit0 in31.59s. No 30-minute retests. User accepted realtime internal-resolution option after GPU measurement. Independent review next; do not claim phase checkpoint yet.

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

## 2026-09-02 Phase 2 — RenoDX-equivalent parity codec (gate 35/35 + reviewer PASS)

Goal:

Implement the parity codec end to end: CPU golden reference (Playbook §9 exact math), ParityEncode/ParityDecode HLSL compiled at build time, the harness --parity-compare mode (Original FP16 → encode → 16 Feature-18 evaluates → decode → Final FP16), GPU-vs-CPU statistics, four-stage captures, and the phase2 gate.

Changed:

- include/veyra/parity + src/parity: RenoDxParityCodec (shoulder 0.75/5.7780, sRGB, six OkLab/AP1 matrices in mul(matrix,vector) direction, signed cbrt, HueOkLab, UpgradeToneMap two-stage, luminance-only).
- tests/unit/ParityCpuReference.cpp: 12 golden checks (threshold continuity, neutral bypass identity, highlight luminance restoration, quantization bounds).
- shaders/Parity{Encode,Decode}.hlsl + cmake shader targets; tools/nr_harness parity_compare mode + shared harness_util (PNG writer, JSON, stats).
- scripts/gates/phase2.ps1: CPU tests both configs, GPU-vs-CPU tolerances, four-stage captures, neutral baseline + addon hash, raw≠final.

Commands actually run (key evidence):

- veyra_parity_tests: 12/12, 0 failures in both configs.
- --parity-compare: encode maxCodeDelta=1 (≤1), maxAlphaDelta=0; decode beyondOneUlpCount=0, maxAbsError=0.00390625 (= exactly 1 FP16 ulp at [4,8)), nanInf=0; infoqueue stored=0 errors=0 (debug run persisted).
- loop-gate -Gate phase2: 35/35 checks exit 0 (reproduced identically by the reviewer in an independent run).

Reviewer outcome (P2.6):

- First review: FAIL with 1×P1 (an evidence line about the debug parity run had no persisted artifact — same class as the Phase 1 P1) + 6×P2.
- Fixes: unconditional infoqueue drain with counters into the JSON, a real persisted debug run, RNE float→half (matching GPU storage), alpha comparison, stage luma statistics into the JSON, stage JSON enriched with rowPitch/runtimeSha256/pts/source.
- The RNE fix surfaced a real physical effect: highlight-amplified fp32-vs-double intermediate differences cross FP16 bucket boundaries (5038 of 2M pixels, every one exactly 1 ulp; bit-level examples in the log). The absolute 0.002 bound is mathematically unreachable at ≥4.0 for any correct fp32 pipeline, so the gate enforces diff ≤ max(0.002, 1×stored ulp) — the reviewer examined the worst-pixel evidence and accepted this ruling as the same-intent bound (0.002 verbatim below 4.0).
- Final review: VERDICT PASS (three-way reproducible numbers, control plane untouched from the Phase 1 checkpoint). Three one-line P2s fixed immediately; two P2s filed for Phase 3 (--profile parsing, in-flight parameter-block reuse hardening).

Decision:

- All parity math comes from Playbook §9; every tolerance kept falsifiable; every claim backed by a persisted artifact.

Next single task:

Phase 3 P3.1: vcpkg/FFmpeg 310-baseline dependency acquisition + phase3 gate (fail-closed).


## 2026-09-02 Phase 1 — Feature 18 native harness (gate 54/55, one user-action item)

Goal:

Build the Feature 18 harness end to end: NGX core host, isolated caller-name compatibility layer, signed-snippet Create/Evaluate, deterministic test-pattern Proxy, 300-frame runs with statistics/captures/variants, and the phase1 gate.

Changed:

- include/veyra/ngx + src/ngx: NgxCoreHost (single Init/Shutdown, parameter-block lifecycle, SEH), ParameterBlock typed setters, DlssNrParameters constants, DlssNrRuntimeAdapter (restricted load, 5 exports, PE-import IAT shim with single-owner install/restore, SEH-wrapped snippet calls, scaling-ratio callback).
- shaders/GenerateTestPattern.hlsl + cmake/VeyraShaders.cmake: build-time DXC compile (deterministic quadrant pattern with frameId shift).
- tools/nr_harness: --load-only/--shim-test/--create-test and the full frame loop (Proxy->Feature18->Raw, zero guidance via upload-copy, 4-slot execution, PNG captures, statistics, variant segments, GPU timestamps, gate-contract JSON).
- NgxResult: full official 310.7 result table (Success=0x1 — corrected from an earlier wrong assumption).

Commands actually run (key evidence):

- Core Init_with_ProjectID result=0x1; snippet Init_Ext (AppID 0x0876232C) result=0x1; CreateFeature id=18 result=0x1 handle non-null; Release/Shutdown results all 0x1.
- Shim boundary battery: 8/8 PASS (zero-size, truncation with ERROR_INSUFFICIENT_BUFFER, exact 10-wchar, roomy, nullptr/other-module forwarding, restore verified).
- 300/300 Evaluate succeeded in BOTH Debug and Release (0 failures), output meanLuma≈0.494 stddev≈0.327 non-black non-constant; three distinct hashes (baseline / style=1 / intensity=0.5); GPU timestamps non-zero, avg ≈6.3 ms/frame at 1080p.
- Lifecycle: two consecutive full runs + create-test + shim-test 4/4 PASS.
- loop-gate -Gate phase1: **54/55 checks PASS; the single FAIL is json-debug:debug-layer-enabled (debugLayer=False)**.

Artifacts/logs:

- logs/phase1/824a66eca2bd4ebfb23a28950eecbc8f/ (gate runs), logs/tmp/p16*.json/out, captures (gitignored).
- third_party_local/nvidia/DLSS_SDK_310.7.0 staged from the official GitHub repo clone (headers + nvsdk_ngx_s[_dbg].lib + rel DLLs; nvngx_dlss.dll BE6E434A…, nvngx_dlssg.dll 135EAF07…).

Failures and exact codes:

- ClearUnorderedAccessViewFloat crashed (139) during zero-init even after binding heaps; replaced with an upload-buffer copy path (equally deterministic). Root cause unverifiable without the debug layer; noted for re-check after Graphics Tools is installed.
- Compile iterations: SDK header include order (d3d12.h before nvsdk_ngx.h), Init_with_ProjectID casing (capital D), NVIDIA static libs are MT-flavored (switched tools to static CRT via CMP0091 + per-config _dbg lib), DXC argument quoting via generator expressions (switched to CMAKE_BUILD_TYPE branch), union aggregate init.

Decision:

- NGX result table and all signatures come from the staged official 310.7 headers, never memory.
- Zero-init via upload copy; JSON debugLayer reports the actual runtime state.

Next single task (user action required):

~~Install Windows "Graphics Tools"~~ (user installed 2026-09-02; probe verified "d3d12 debug layer enabled").

Reviewer outcome (P1.8, 2026-09-02):

- First review: VERDICT FAIL with 1×P1 — the gate's no-state-errors grep was vacuous because no component captured the debug layer's OutputDebugString stream; plus 5×P2 (literal log line, path containment, SEH on parameter calls, hardcoded nanCount, 10-frame debug matrix).
- Fixes landed (commit ff98e37): real ID3D12InfoQueue capture in debug builds (attach after device creation, drain after the full loop into the log and a debugInfoQueue JSON block), gate now asserts infoqueue-active and no-error-messages (both falsifiable), debug run raised to 30 frames, exact Playbook 8.1 literal, runtime_local/nvidia containment check, SEH wrappers for Allocate/DestroyParameters, nanCount removed.
- Final review: VERDICT PASS (independent fresh-build run: release 300/300, debug 30/30 with infoQueue active and 0 error messages; three parameter-variant hashes identical across four independent runs; anti-stale runId/exeSha256 verified; control plane untouched from the Phase 0 checkpoint). Three non-blocking P2 residuals recorded in the journal (teardown-time infoqueue drain, suffix vs prefix containment, 200-message drain cap).

Phase 1 conclusion: gate 56/56 + reviewer PASS. Phase 2 unlocked.


## 2026-09-02 Phase 0 — Runtime probe + D3D12 skeleton

Goal:

Complete Phase 0 per the Playbook: fail-closed phase0 gate, minimal CMake/C++20 project, veyra_base (logger/result strings/file identity), veyra_gfx (D3D12DeviceContext + 4-slot ring), full veyra_runtime_probe, and the real gate run including the 5-minute window loop.

Changed:

- Initialized local Git per LOOP_ENGINE fixed order (baseline 2086282, branch agent/veyra-v1-loop, loop pointer commit 09abf5d).
- Added scripts/gates/phase0.ps1 (fail-closed, verified failing before the project existed).
- Added CMakeLists.txt/CMakePresets.json/cmake/VeyraWarnings.cmake (Ninja x64 debug/release, /W4 /permissive- /WX).
- Added scripts/build.ps1 (vswhere/vcvars resolution; no machine paths in presets) and scripts/stage-runtime.ps1 (pinned-identity copy + manifest + persistent ngx-local.json).
- Added include/veyra + src/base (Logger, Status/HRESULT/NGX strings, BCrypt SHA-256 + WinVerifyTrust + signer extraction) and src/gfx (D3D12DeviceContext, CommandSlotRing with timestamp heap).
- Implemented tools/runtime_probe/main.cpp: --self-test, --device-info, full mode (restricted LoadLibraryExW, 5 exports, nvofapi64 probe, fixed-size window + flip swapchain + 4-slot loop, JSON summary with runId/exeSha256).

Commands actually run:

- preflight (54 checks then 66 after Git): exit 0 both times.
- Toolchain/GPU probes: RTX 5070 / 616.56 / 12227 MiB / compute 12.0; nvofapi64 32.0.16.1656; MSVC 14.44.35207; CMake 3.31.6; Ninja 1.12.1; DXC 1.8; Git 2.53.
- scripts/build.ps1 -Preset x64-debug / x64-release: exit 0 (multiple times).
- veyra_runtime_probe --self-test / --device-info / full smoke: exit 0 each.
- loop-gate.ps1 -Gate phase0: three honest failures (locale version format; SwitchParameter binding via -File; ignore probe on non-existent dirs), each fixed without lowering thresholds, then **exit 0: VEYRA GATE PASSED: phase0 (70 checks)**.

Results:

- Gate run-id a5fd6348b3084b44857b1f1ffc96a449 (308.3 s): exports 5/5; Debug window 3 s / 303 frames; Release window **300 s / 30002 frames, deviceRemoved=false**; staged runtime identity matches the pinned contract; git ignore 7/7; no sensitive files tracked.
- Driver version resolves via registry nvlddmkm.sys file version (32.0.16.1656); DisplayVersion value absent on this driver.
- D3D12 debug layer unavailable on this machine (0x887A002D, Windows "Graphics Tools" optional feature missing); recorded in loop/INBOX.md for user action before Phase 1.

Artifacts/logs:

- logs/phase0/a5fd6348b3084b44857b1f1ffc96a449/ (probe logs + JSON, gitignored)
- runtime_local/nvidia/{nvngx_dlssnr.dll, runtime-manifest.json}, runtime_local/config/ngx-local.json (gitignored)

Failures and exact codes:

- Gate iterations: runtime:fileversion "310,8,0,0" != "310.8.0.0" (locale) → FileVersionRaw; build exit 1 via ParameterArgumentTransformationError ("-Clean:$false" as string) → omit switch; git check-ignore exit 1 for non-existent trailing-slash dirs → in-directory probe files.
- Compile iterations: C4838 (DXGI literals), WinVerifyTrust const GUID*, namespace log::, wchar→char C4244, IDXGIAdapter1 vs DESC3, ComPtr .Get() for Signal, GetCurrentBackBufferIndex needs IDXGISwapChain3. All fixed; no warnings remain (/WX).

Decision:

- Keep machine-specific paths out of tracked files (build.ps1 resolves them); gate verifies staged state rather than staging itself; probe JSON embeds runId + exe SHA-256 so stale artifacts cannot pass.

Reviewer outcome (P0.9):

- Independent read-only sub-agent reran preflight (exit 0) and phase0 (exit 0, its own run-id a55d5fb41b164abc88fc2760f0b635ec, 300 s / 30003 frames), verified BASE_COMMIT, the full diff (control plane untouched), anti-stale runId/exeSha256 mechanics, and reverse-order cleanup. VERDICT PASS, zero P0/P1.
- Four P2 hardening notes recorded in loop/JOURNAL.md Cycle 009; the fence-timeline ownership item is queued as BACKLOG P1.0a; D3D12 debug-layer absence remains in loop/INBOX.md for the user before Phase 1's debug-layer criterion.

Next single task:

Phase 1 P1.1: phase1 gate + deterministic RGBA8 test frames/output statistics (after P1.0a fence ownership hardening).


## 2026-09-01 Handoff baseline

Goal:

Prepare an implementation contract for the next Agent. No player source has been implemented yet.

Changed:

- Added `AGENTS.md` with project guardrails and phase gates.
- Added `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` with dependency acquisition, runtime layout, exact Feature 18 parameter contract, parity math, media pipeline, latency rules and phase acceptance criteria.
- Added `.gitignore` before repository initialization so local NVIDIA/RenoDX binaries cannot be added accidentally.

Commands actually run:

- Inspected the project file list and product-spec headings.
- Calculated/verified both local binary identities and Authenticode status.
- Inspected exported/runtime strings and the embedded RenoDX parity shader behavior.
- Verified the installed Windows, RTX 5070/616.56 environment and local Visual Studio/CMake/Ninja/DXC tool paths.
- Checked pinned upstream DLSS, Magpie, FFmpeg/vcpkg and NVOF references.

Results:

- Current repository state before handoff: no `.git` directory and no application source.
- Next allowed implementation phase: Phase 0 only.
- No DLSS Feature was invoked and no runtime test was claimed in this handoff task.

Artifacts/logs:

- `AGENTS.md`
- `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`
- `VEYRA_PRODUCT_SPEC_V1.md`

Failures and exact codes:

- None. One documentation patch wrapper parse error occurred before any write; it was corrected and had no workspace effect.

Decision:

Use direct NGX/D3D12 for V1; signed DLSSNR adapter is local-only; RenoDX add-on is reference-only; start with a native fixed-frame harness before the media player.

Next single task:

Execute Phase 0 from the playbook and stop at its acceptance gate.

## 2026-09-01 Unattended Goal Loop handoff

Goal:

Turn the implementation plan into a recoverable Goal-based loop that another Agent can run without phase-by-phase supervision.

Changed:

- Added loop/LOOP_ENGINE.md with single-writer state machine, evidence rules, retry bounds, independent review and stop/complete conditions.
- Added loop/GOAL_PROMPT.md as the copy-paste Goal task and loop/REVIEW_PROMPT.md as the read-only phase review task.
- Added persistent STATE/BACKLOG/JOURNAL/EVIDENCE/INBOX files.
- Added scripts/loop-gate.ps1 and scripts/gates/README.md.
- Updated AGENTS.md, the Playbook and .gitignore for unattended execution.

Commands actually run:

- powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
- powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase0
- PowerShell AST parse of scripts/loop-gate.ps1 and ConvertFrom-Json validation of loop/STATE.json.

Results:

- Initial preflight found a Windows PowerShell 5.1 source-encoding bug in a non-ASCII product-plan filename check (exit 1); the bootstrap check was made encoding-safe. The later audit renamed the canonical product spec to an ASCII path.
- Final re-run at 2026-09-01T17:16:32+08:00 passed 45/45 baseline checks (exit 0), including both local binary identities, STATE ledger/limits, ignore rules and eight protected control-file hashes.
- Negative phase0 test failed closed as intended (underlying gate exit 1): Git is not initialized and scripts/gates/phase0.ps1 does not yet exist.
- No application code, NGX Feature, video pipeline or Phase gate was claimed complete.

Next single task:

Start the Goal with loop/GOAL_PROMPT.md. The Agent must rerun preflight, finish P0.1 toolchain/GPU evidence, then execute Phase 0 in backlog order.

## 2026-09-01 Full project audit and cleanup

Goal:

Re-audit the whole handoff package adversarially, remove superseded documentation, repair contradictory implementation instructions, and leave the unattended loop fail-closed for a weaker Agent.

Changed:

- Added `README.md` as the canonical entry point and renamed the retained product boundary to `VEYRA_PRODUCT_SPEC_V1.md`.
- Deleted the obsolete V3 plan. It prescribed the superseded quality-first/capture/depth route and had no remaining active references.
- Removed stale Phase 8 and old-plan routing. V1 is strictly Phase 0 through Phase 7.
- Corrected the false premise that the RenoDX `.addon64` is a ReShade configuration. No preset exists in this workspace; Phase 2 uses a declared neutral codec baseline and only performs external-reference comparison if a matching preset/capture is later supplied.
- Removed the D3D11VA/D3D11On12 side route and aligned the minimum codec/container matrix with Phase 3/7 gates.
- Fixed the SR/NVOF circular dependency: V1 SR uses Zero Guidance; full-resolution NVOF is generated after SR and is shared only by NR/FG.
- Added the D3D12VA texture-array slice/plane/lifetime contract, the NVOF ABGR8 input/ring/reset contract, and exact `GetModuleFileNameW` shim edge semantics.
- Recorded the official DLSSG motion-normalization rule and isolated Magpie's conflicting `{1,1}` behavior as a diagnostic-only mode with a deterministic Phase 6 translation gate.
- Hardened `scripts/loop-gate.ps1`: exact nine-file control set, stronger STATE phase/evidence/bound checks, Git commit-pointer checks, representative ignore probes, current-phase enforcement, and before/after hashes that prevent a phase gate from mutating non-ignored project files.
- Rebuilt `loop/CONTROL_HASHES.json` for the canonical control plane.

Commands actually run:

- Official-source checks against NVIDIA DLSS SDK 310.7 headers, NVIDIA Optical Flow documentation/sample behavior, FFmpeg D3D12VA headers, the pinned vcpkg ports and Microsoft `GetModuleFileNameW` documentation.
- PowerShell AST parse, JSON parse for every JSON file, Markdown fence-balance scan, stale-reference/TODO scans, and file inventory checks.
- In-memory unit exercise of `Get-ProjectSourceSnapshot` / `Compare-ProjectSourceSnapshot` without changing disk files.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Negative gates for `phase0` and out-of-order `phase1`.
- Reversible negative STATE tests for `cycle.completed=80` with active status and for prematurely unlocking Phase 1; the original state was restored after each test.

Results:

- Audited preflight passed 54/54 checks, including nine protected control hashes and both local binary identities.
- `phase0` failed closed because Git is not initialized and `scripts/gates/phase0.ps1` does not exist; no Phase completion was claimed.
- `phase1` additionally failed the current-phase check.
- The loop-bound and phase-sequence mutations each made preflight exit 1 on the intended check, then the valid STATE was restored.
- Mutation-snapshot unit exercise saw 17 control/state files, reported zero changes for identical snapshots, and detected an in-memory README hash change.
- Current project truth remains: no Git repository, no application source, Phase 0 not started.

Deleted/renamed:

- Deleted the obsolete V3 plan. This workspace has no Git history yet, so that deletion is not recoverable from this directory's repository history.
- Renamed the retained product plan to `VEYRA_PRODUCT_SPEC_V1.md`; its useful content was audited rather than discarded.

Next single task:

Start the Goal using `loop/GOAL_PROMPT.md`. The first Agent action is the 54-check preflight; then it completes P0.1 and proceeds through Phase 0 backlog order without crossing the gate.

## 2026-09-03 Fast-track V1 scope rebaseline and competitor audit

Goal:

Replace the obsolete player-only route with the user-confirmed first-release scope: physical capture-card enhancement, interactive media player, and image/video export, all sharing one DLSS quality graph. Audit Magpie and recent GitHub competitors before changing the plan.

Facts found:

- Magpie commit `289dc0f6d52075f5a06b47a3f70b35d438095bf5` already contains NVOF motion/confidence, optional Depth Anything V2 Small with temporal reprojection, and DLSSG. Adding a depth texture alone is not a competitive advantage.
- `Merserk/dlss5-visual-enhancer`, `DaniilSokolyuk/video2dlssnr`, `Zonnery/dlss5-nr-player`, `SamG-Coder/dlss5-infinity-studio`, and `jlrouzies-fr/DLSS5-Feeder` were inspected at source/README level. Details, commit IDs, limitations and licenses are in `docs/COMPETITOR_AUDIT_2026-09-03.md`.
- The screenshot comment's useful lesson is the complete DLSS render contract, not reverse engineering itself. HDMI/video pixels cannot recover engine-native depth/motion/HUD-less buffers; Veyra will use estimated guidance and label it honestly.

Changed:

- Replaced `VEYRA_PRODUCT_SPEC_V1.md` with the three-workflow product definition and measurable Definition of Done.
- Replaced `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` with explicit module layout, dependencies, API contracts, motion/depth/reset rules, source/sink implementation steps and Phase 5–7 gates.
- Added `docs/COMPETITOR_AUDIT_2026-09-03.md`.
- Updated `README.md`, `AGENTS.md`, Goal/Loop/Reviewer instructions, gate contract, BACKLOG, STATE and INBOX.
- Expanded the protected control set from 9 to 10 files and rebaselined `loop/CONTROL_HASHES.json`; `scripts/loop-gate.ps1` still fails closed on any drift.
- Preserved Phase 0–4 checkpoints. Invalidated only the old Zero-only Phase 5 evidence because it no longer proves the new quality core.
- Adversarial re-read found and fixed one graph-order contradiction: V1 now states everywhere that SR uses Zero Guidance first, then NVOF/depth/confidence are generated at the post-SR `workingExtent` for Feature 18 and FG. This prevents a weak Agent from building an SR↔NVOF circular dependency or mixing resource extents.
- Verified the local official SDK already contains a signed `nvngx_dlssg.dll` 310.7.0.0 and recorded its exact path/size/hash in the Playbook. It is a Phase 6 staging source, not evidence that DLSSG currently works in Veyra.

Commands actually run:

- Cloned/fetched the six upstream repositories into a unique directory under `%LOCALAPPDATA%\Temp` and inspected files with `rg`/`Get-Content`; no upstream source was copied into Veyra.
- `git status --short --branch`, `rg --files`, JSON parsing, suspicious-text scan, `git diff --check`.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight` → exit 0, 68/68.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root <project> -Preset x64-release` → exit 0, Ninja no work to do.

Runtime result:

- This was a control-plane/research task. No new Feature 18, NVOF, DAV2, DLSSG, capture-card or export run was executed. Historical Phase 0–4 runtime evidence was not re-labeled as current product completion.

Known blockers:

- Optical Flow SDK 5.0 headers/sample require the user to accept NVIDIA's EULA and place the SDK under `third_party_local`.
- The final capture gate needs a real DirectShow/UVC device and HDMI test signal.
- Public distribution of NVIDIA runtime/model/FFmpeg assets remains unauthorized.

Next single task:

P5.1: replace the obsolete Zero-only `scripts/gates/phase5.ps1` with the fail-closed Fast-track quality-core gate and prove it fails while unified graph/NVOF/depth implementations are absent.

## 2026-09-03 Launch V1.2 / native-4K rebaseline

User decision:

- Do not ship or accept a minimal MVP. The first release must support native 4K SDR video and retain capture-card, player, image export and video export.

Engineering decisions:

- Native 4K means the Player/Capture/Export graph actually processes 3840x2160; the historical 1080p-to-4K SR harness is not product proof.
- Capture latency is not free lookahead. `NR Low Latency` uses no future frame; `FG Low Latency` needs A/B (`lookaheadFrames=1`); `Buffered Quality` keeps bounded A/B/C (`lookaheadFrames=2`) and uses C only for Veyra consistency/depth/cut/trust, not as a fictional third DLSSG input.
- Replaced the proposed full-frame readback/ffmpeg raw pipe release path with native D3D12 NVENC H.264/HEVC plus libavformat mux. Raw pipe is diagnostic-only and cannot pass Phase 7.
- Added 4K resource pooling, DXGI video-memory budget/headroom, 4K30/60 player gates, real 4K60 capture gate, subtitle layer after FG, settings/dependency/recovery/log-export release behavior.
- Increased the unattended safety limit from 80 to 120 cycles; failure/no-progress limits remain 3/5.
- Added `release_candidate` and `distribution_blocked` state validation. Functional completion cannot be called a public launch while proprietary distribution rights remain unresolved.

Primary references checked:

- NVIDIA NVOFA FRUC programming guide for previous/next frames and forward/backward validation.
- NVIDIA public DLSS-G programming guide for resources and pacing.
- NVIDIA Video Codec SDK 13.1 NVENC guide for D3D12 resources and fence points.
- Elgato official device comparison for the distinction between HDMI passthrough and software preview latency.

Commands actually run:

- JSON parse for STATE/control/config files and PowerShell AST parse for `scripts/loop-gate.ps1`.
- Markdown fence-balance scan.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight` -> exit 0, 70/70.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root <project> -Preset x64-release` -> exit 0, Ninja no work to do.

Runtime boundary:

- No native-4K Feature 18/NVOF/DAV2/DLSSG/Player/Capture/NVENC Export run was executed in this control-plane turn. None of those features is claimed complete.

New external blockers:

- Video Codec SDK 13.1 EULA/header/sample.
- A real DirectShow/UVC 4K60 SDR capture device, HDMI audio and 4K60 source.
- Public distribution rights for the experimental runtime and bundled assets.

Next single task:

P5.1: replace the obsolete phase5 gate with the Launch V1 1080p60 + native-4K60 fail-closed gate, then prove it fails for the currently missing product graph/guidance implementations.

## 2026-09-03 NVIDIA SDK EULA decision handoff

User decision:

- The user accepts the NVIDIA Optical Flow SDK and Video Codec SDK licensing direction and authorizes their use for local Veyra development.
- This chat record is not evidence that NVIDIA Developer Portal acceptance/download has completed. Both expected SDK directories were checked and remain absent.

State update:

- `loop/INBOX.md` and `loop/STATE.json` now distinguish the resolved product decision from the unresolved package acquisition.
- The next Maker must not ask the user to reconsider the EULA. It should continue P5.1 immediately and only treat the missing Optical Flow package as a concrete blocker when P5.5 needs its headers/sample.
- Video Codec SDK absence does not block Phase 5; it becomes a concrete integration blocker at P7.5.
- No SDK, runtime, driver or source code was installed or changed by this documentation handoff.

Next single task:

P5.1: replace the obsolete Phase 5 gate with the Launch V1 fail-closed quality-core gate and prove the current missing implementation produces exit 1.

## Phase 6 session 2026-09-04: DLSSG 2X + realtime engine

### Verified runs (all commands executed, logs in logs/phase6-manual/)

- `veyra_fg_harness.exe --fg-cap`: FG.Available=true (Get ull/i both 0x1), FeatureInitResult=1,
  NeedsUpdatedDriver=false, MinDriver 520.0, MultiFrameCountMax=5, HwSchMode registry absent
  (= system default; runtime confirms availability). GPU RTX 5070, driver 32.0.16.1656.
- `veyra_fg_harness.exe --fg-test` (run-ids manual-fg3, regression-fg): translation
  usable=59/59, dup=0, minBlendResidual=1.058 vs baseline=1.106 (0.6x=0.664 PASS),
  maxTrueResidual=0.171 (0.5x=0.553 PASS), maxMidErr=1.27px, PTS monotonic, direction OK;
  cut phase usable=58, crossCut=false, reset=true. mvec convention winner:
  pixels-scaled-1-over-w (half2(16,0) with scale {1/1920,1/1080}).
- `veyra_fg_harness.exe --audio-test` (audio4.json): eventMode=true, 192000/192000 frames,
  underruns=0, drift=0.015ms (LSQ slope vs QPC over 349 samples; constant offset -10.26ms
  is IAudioClock quantization), pauseFlushWorks=true.
- `veyra_player_probe.exe --input test_av_1080p.mp4` (pp-run10/11): exit=0.
  presents=1306, real=418+, FG=1156, NR=953, SR=953 (1080p->4K), drift=32ms, 10/10 seeks,
  resize OK, NR/FG toggles OK. mvecSource=zero-motion-fallback (see finding below).
- `veyra_player_probe.exe --input test_av_4k.mp4` (pp-4k1): exit=0. All toggles true
  (SR 1:1 bypass), NR=1080, FG=1068, drift=32ms, maxInFlight=7.
- Endurance 15s smoke: 4K30 internal=59.07Hz (fg 505), 4K60 internal=111.87Hz (fg 778).

### System finding: injected D3D12 layer (documented, worked around)

This machine runs third-party software that hooks D3D12 (consistent with screen-capture
injection; VEDetector/nvapi64_impl crashes appear in the System event log from other apps).
Once the first CreateShaderResourceView runs in a process:
1. CreateCommittedResource / Resource::Map / ResizeBuffers / Present fabricate
   DXGI_ERROR_DEVICE_REMOVED while the device actually keeps working (NGX calls, queues,
   shader dispatches all continue; external window capture verified composition).
2. Any CopyTextureRegion/CopyResource recorded afterwards poisons the command list
   (Close returns E_INVALIDARG).
3. NVOF frame-time Execute and the first DLSSG Evaluate fail (NV_OF_ERR_GENERIC /
   0xBAD00002) because their internal allocations/copies hit (1)/(2).

Workarounds (all commented in code):
- Allocate every committed resource and Map upload buffers BEFORE creating any view.
- Warm up NVOF (20 executes) and FG (1 evaluate) before views so internal allocations
  complete in the clean window.
- All per-frame data movement via compute shaders (Nv12Upload.hlsl, ScaleBlit.hlsl);
  SR bypass and NVOF A/B chain use ScaleBlit instead of CopyResource.
- NR snippet evaluate runs on a freshly reset command list (it also refuses lists with a
  bound compute PSO/descriptor heap - independent of the hook).
- Present path is a PRESENT<->RENDER_TARGET pixel-shader blit (flip buffers cannot enter
  UAV; also required by D3D12 rules).
- ResizeBuffers failure falls back to window-only resize (DWM scales the fixed buffers).
- Present's fabricated device-removed is tolerated (counted, logged) for exactly the two
  injected-layer codes.
- NVOF frame-time guidance is blocked by (3) on this system; the player falls back to
  zero-guidance mvec (DLSSG's internal optical-flow engine still interpolates; generation
  truth was proven separately in fg-test with exact synthetic motion). JSON field
  mvecSource reports this honestly; real NVOF at scale was proven in the Phase 5 probe.

### Files

- scripts/gates/phase6.ps1 (fail-closed; proven exit 1 before implementation)
- include/veyra/ngx/DlssFgBackend.h, src/ngx/DlssFgBackend.cpp
- include/veyra/ngx/NvOfSession.h, src/ngx/NvOfSession.cpp
- include/veyra/gfx/PresentSink.h, src/gfx/PresentSink.cpp (+ CommandSlotRing::lastSignaledValue)
- tools/fg_harness/{main,fg_test,audio_test}.cpp
- tools/player_probe/main.cpp
- shaders/{ScaleBlit,Nv12Upload,PresentBlit}.hlsl
- cmake/VeyraShaders.cmake (graphics shader pair support)
- CMakeLists.txt (veyra_nvof lib, fg_harness, player_probe, shader targets)

## Phase 6 session 2, 2026-09-04: 用户指令 8 步执行记录

### 已完成的代码修复(全部构建+实测)

1. **控制面恢复**:.gitignore 从 git 恢复为 LF 原始字节(SHA256 A1DA73CC... 与 CONTROL_HASHES 一致),
   preflight 70/70。测试片迁移 loop/local/fixed_clips/(已忽略目录)。
2. **phase6.ps1 控制字符修复**:第 178/179/182 行 U+000C/U+000B/U+0008 与 "installedd" 损坏以字节级
   编辑修复;新增 gate:self-control-chars 检查(脚本自身含 CR/LF/TAB 以外控制字符即 FAIL),实测 PASS。
3. **AVPacket 泄漏修复(根因确认)**:src/media/FFmpegDemuxer.cpp readVideoPacket 在 av_read_frame 前
   显式 av_packet_unref(packet_)(此前依赖隐式释放,4K 下每帧泄漏 ~150KB 与帧字节成正比)。
   修复后 FFmpeg-only(VEYRA_GRAPH_OFF+PRESENT_OFF+NR/FG/AUDIO off)40 秒实测:
   - 4K+音频: 536MB 稳定; 4K 无音频: 537-538MB 稳定; 1080p: 523MB 稳定(每 10s pace 采样,logs/phase6-manual/leak-*)
   此前 4K 软解 5 分钟增长 4.8GB。D3D12VA 已实现(VEYRA_HW_DECODE=1)但帧内解码仅 ~8fps,不用。
4. **PresentSink 严格化**:删除全部"injected-layer artifact"宽容分支;presentCount 仅计 SUCCEEDED,
   新增 attemptedPresentCount/failedPresentCount;失败时记录 Present HRESULT + GetDeviceRemovedReason +
   DRED breadcrumbs/page fault;DEVICE_REMOVED/RESET 走 Status::DeviceFailure 返回 false;
   vsync=false 且支持撕裂时使用 DXGI_PRESENT_ALLOW_TEARING;present 前 back buffer 处于 PRESENT 状态
   (RT→PRESENT 转换在命令列表内完成)。
5. **SRV staging**:DescriptorStager(SRV 先写入非着色器可见堆再 CopyDescriptorsSimple 到可见堆)。

### Present 失败最小判别矩阵(全部当前环境实测,同一二进制)

ve​rya_player_probe VEYRA_BARE_STAGE=N(隔离模式:窗口+交换链+清屏呈现 600 次,无解码):
- 0(裸): ok=600 failed=0
- 1(NGX core): ok=600
- 2(+NR snippet+IAT shim): ok=600
- 3(+capability): ok=600
- 4(+FG create): ok=600
- 5(SRV 直写可见堆): ok=0 failed=600 ← Present 全部 DEVICE_REMOVED(removedReason=INVALID_CALL,无 DRED)
- 6(SRV 直写非可见堆): ok=600
- 7(UAV 直写可见堆): ok=600
- 8(CBV 直写可见堆): ok=600
- 9(SRV 经 staging 复制到可见堆): ok=600(两次复测)
- 13(与 9 语义相同的探针,仅源码位置不同): ok=0 failed=600(两次复测;vsync=1 也失败)

引擎内交叉验证(VEYRA_SKIP_VIEWS+VEYRA_CLEAR_PRESENT+GRAPH_OFF+NO_FEATURES+NO_AUDIO):
- 无任何视图: 681 次 present 0 失败
- 仅 UAV: 440 次后于 resize+1s 失败;仅 raw-buffer SRV: 439 次同点位失败;任何纹理 SRV(staged): 第 1 次即失败
- 进程模块扫描: 除系统/驱动/本项目外仅 NVIDIA NvTelemetry 两个 DLL;无 GameViewer/OBS 模块在场

结论强度:破坏是确定性的、依赖调用序列/地址布局;同一二进制内两个语义相同的探针一过一败(stage9 vs
stage13)排除了应用层逻辑解释;正确实现的 D3D12 运行时/驱动不应有此行为。**在用户关闭相关软件做 A/B
之前,此根因只能记为"环境相关假设(有模块在场+确定性判别证据)",不能写"已确认"。**

### 等待用户动作(唯一阻塞)

A/B 实验(约 1 分钟):退出/禁用 UU远程、GameViewer、OBS、NVIDIA App 覆盖层(以及任何含捕获/覆盖
功能的软件,必要时重启),然后运行:
  VEYRA_BARE_STAGE=13 VEYRA_BARE13=0 out/build/x64-release/veyra_player_probe.exe --input loop/local/fixed_clips/test_av_1080p.mp4 ...
- 若 ok=600:确认为覆盖/捕获软件钩子;保留 DescriptorStager(无害)或移除,继续 1080p/4K 场景。
- 若仍 ok=0:指向显卡驱动 616.56 的 Present/SRV 缺陷;按"不擅自更新驱动"规则,向用户报告并等待决定。

## Phase 6 session 3, 2026-09-04: 真根因确认与修复(用户指令 s3 全部执行)

### 作废声明
- 上一 session 的 "stage9 证明 staging workaround 有效"、"stage9/stage13 地址相关"、"RTX 5070/616.56
  驱动缺陷"结论全部作废:用户指出并经日志验证(r9-1.log 无 "bare stage9" 标记),stage6-12 被错误嵌套
  在 if (bareStage == 5) 内,stage9 从未执行,其 600/600 是裸 Present。

### 真根因(实锤)
- tools/nr_harness/parity_compare.cpp:543 早有注释:"MipLevels = 1; // 0 is invalid; the debug layer
  removes the device"。全项目 makeSrv(SRV 描述)值初始化后未设置 Texture2D.MipLevels(默认 0=非法),
  CreateShaderResourceView 传入非法描述 → debug layer 下立即移除设备;release 下表现为后续 Present
  返回 DXGI_ERROR_DEVICE_REMOVED(removedReason=INVALID_CALL,无 DRED)。
- 这解释了此前全部矩阵:任何使用 makeSrv 的路径(stage5、bare13、引擎全开、k-experiment)必死;
  无视图(bis8)与全字段描述(dpp)全活;"UAV 活"因 UAV 描述恰好合法;"只有 stage9 活"是嵌套假象。

### 执行记录(命令+结果)
1. 全新 tools/descriptor_present_probe(独立函数+switch、唯一 marker、executedOperation 校验、
   JSON 含 expectedOperation/executedOperation/presentSucceeded/presentFailed/removedReason、
   资源存活到 Present 循环后、debug layer+GBV+同步队列验证+DRED+InfoQueue 全开)。
   7 案例 × 3 轮 = 21/21 PASS(exit 0、600 成功 Present、failed=0、removedReason=S_OK、ERROR/CORRUPTION=0),
   含 case C(直写可见堆 SRV)与 case F(真实采样绘制)。日志 logs/phase6-manual/dpp/。
   (debug layer 的 atexit 会污染进程退出码为 0x87D,已用显式释放+ExitProcess 修复并记录。)
2. SRV 描述全字段修复:player_probe makeSrv/stagedSrv/present SRV/raw buffer(FirstElement/Stride)/
   D3D12VA plane SRV、media_probe plane SRV(parity_compare 原本已正确)。
3. 按决策树第 1 分支:DescriptorStager 从生产路径移除(stagedSrv 改为直接 makeSrv);
   "驱动缺陷/注入层"结论从 STATE blockers 删除。
4. 修复后播放器(1080p 与 4K 场景):Present FAILED = 0(此前必死);真实 NVOF 首次运行
   (1080p: 305 execute/0 失败;4K: 288/0);FG/NR/SR 全部真实执行;mvecSource=nvof(零引导弃用);
   内存增长 206MB(45s)。
5. 遗留(真实性能问题,非正确性):maxAvDriftMs=262ms > 50ms 阈值、presents 低(103 real+96 gen 呈现,
   451 迟到丢弃)。原因:NVOF 4K grid-1(8.3M 向量/帧)+SR+NR 串行使 GPU 每帧超出预算,3 缓冲
   swapchain 背压使 Present 阻塞。下一步唯一任务:把 NVOF 网格/perf 等级或流水线深度工程化
   (或在 gate 前降低到 grid-2 并如实记录分辨率变化),使 A/V drift ≤ 50ms。

## Phase 6 session 4, 2026-09-04: P0.1-P0.6 执行记录(用户指令 s5)

### 修改文件
- tools/player_probe/main.cpp:音频类整体重写;PresentItem 资源绑定;drift 统计;
  NVOF raw SHORT2 + densify 接线;P0.5 诊断计时。
- include/veyra/ngx/NvOfSession.h + src/ngx/NvOfSession.cpp:caps 查询、grid-4、
  SHORT2 契约注释、cost buffer 注册、方向注释。
- shaders/NvofDensify.hlsl(新增):S10.5→float、grid 采样、cost 阈值、negate。
- tools/nvof_probe/main.cpp:grid-4 + R16G16_SINT + S10.5 读回 + 随机点 +8px 测试 + p05/p50/p95。
- src/gfx/PresentSink.cpp:ResizeBuffers 按 DXGI 规范(queue idle fence + 全部
  backbuffer 引用释放后才 Resize;失败为硬错误),删除 DWM 缩放回退与"注入层"措辞。

### P0.1 音频(实测)
- 根因确认:旧 decodeUntil 把 ringMs()(缓冲长度)与绝对媒体时间比较,永不满足→
  解码到溢出(16974 次 overrun)。重写为独立音频线程:水位(250/500/1000ms)、
  prefill 后才 Start、原子 seek(stop/reset→flush→seek→剪枝→prefill→重锚→start)、
  IAudioClock 设备位置映射真实音频 PTS。
- 1080p 场景(p4b-1080):**audioUnderruns=0 audioOverruns=0**,bufferedMsEnd≈1007ms
  (高水位),audioClockPtsMsEnd 与媒体时间一致(55.4s 片尾)。seekCount 含 10 次场景 seek。

### P0.2 drift(实测,阈值污染已消除)
- 每帧在 present 决策前记录 signedLateness;输出 min/p50/p95/p99/max。
- p0-1080(修音频后首测):min=-1049 p50=853 p95=3777 p99=4403 max=4502ms。
  真实状况:引擎吞吐(~21 present/s)远低于 120/s 时间线;262ms 是旧阈值污染,
  已确认用户判断正确。

### P0.3 资源绑定
- PresentItem 现携带 frameSeq/textureSlot/epoch/fenceValue/kind;genFrame[2] 池,
  FG 各写自己的 slot;present 用 item 自己的 slot;seek 递增 resetEpoch 使旧项失效;
  decode 门限 queue<3 = 背压;droppedSourceFrames/droppedLatePresents 计数(gate 必查)。

### P0.4 NVOF 格式(实测)
- caps:nvOFGetCaps 查询(NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES/WIDTH/HEIGHT min/max);
  当前返回 grids 列表为空(mask=0)、min=max=32(异常,已如实记录,init 仍成功)。
- grid-4 初始化成功:flowExtent=960x540(3840/4),不再用 grid-1/全分辨率 RG16F 假象。
- nvof_probe 随机点 +8px 测试(nvof-dots):dx p50=-32.06 p95=-32.06(raw=-1026),
  dy p50=0;真实位移 +8px ⇒ 像素单位 = raw/(32×gridSize)(该 4 倍因子为实测,
  非"凭 grid 猜测");方向 current→previous 为负(与 input=B/ref=A 注释一致)。
- NvofDensify.hlsl 按 /(32×gridSize) 换算 + negate=1(DLSSG truth 约定为 prev→current)
  + cost<32 清零;confTex R8_UNORM。注意:+8px 测试模式为渐变→随机点修正后 dy=0 恢复正常。

### P0.5 逐 pass GPU 计时(VEYRA_GPU_TS=1,串行 fence+QPC,ts-1080)
upload p50=0.01 | yuv 0.00 | sr 0.00 | encode 0.00 | nvof_call 0.18(提交)|
**nr 2.34 / p95 2.75** | **decode_blit 24.68 / p95 26.01(瓶颈)** | **fg 2.37 / p95 2.70**。
decode_blit 段 = parity decode + videoFrame blit + NVOF A/B 链 3 次 4K blit。
串行化测量含同步开销,但 24.7ms 决定性超标(4K60 预算 8.3ms;即使 60fps 真帧 16.7ms)。
下一唯一任务:削减 decode_blit 段(parity decode 着色器成本与 3 次链式 blit 的结构),
再按 P0.5 允许的矩阵比较。

### P0.6
- PresentSink 删除所有无证据归因措辞;ResizeBuffers 前显式 queue-idle fence +
  释放全部 backbuffer 引用;失败为硬错误(不再 DWM 缩放冒充)。
- 文档层:此前"注入层/驱动缺陷"结论已在 session 3 更正,本 session 无新增。

### 当前未通过项(诚实)
- B 测试(30-60s 播放器):p95 drift 仍 2844ms(P0.5 显示 decode_blit 24.7ms 是根因);
  presents≈947/场景,远低于 120/s。
- Phase 6 gate 未跑绿;不进入耐久/Reviewer/checkpoint。

## Phase 6 session 5, 2026-09-04: NVOF 数据契约修复与重证(用户指令 s6,第一+第二部分)

### 一、接线修复(全部 fail-closed,构建通过)
1. 输入格式:NVOF 输入纹理改为 DXGI_FORMAT_B8G8R8A8_UNORM(与申报 NV_OF_BUFFER_FORMAT_ABGR8
   一致,依据本地 SDK NvOFD3DCommon.cpp 映射);格式不符在注册前直接失败。
2. 输出:rawFlowTex R16G16_SINT @ ceil(w/grid)×ceil(h/grid);costTex R8_UINT 同 extent;
   两者作为 initialize 显式参数传入;分配失败立即失败;cost 注册失败也失败(cost 为 V1 必需)。
3. caps:两次调用协议(先 nullptr 查元素数,再填数组;**不除以 sizeof(uint32_t)**)。
   实测:elemCount=3,列表 [1 2 4]。grid=4 在列表中;不在列表即失败。
4. densify 契约:S10.5 换算固定 float2(raw)/32.0(**删除 /gridSize——位移矩阵证明其为错误**);
   方向 current→previous,negate 为单一显式翻转点(由符号矩阵证明);cost 阈值门控。
5. confidence:改为 (255-cost)/255(NVIDIA cost 越高越不可靠→confidence 下降);
   cost≥阈值区域 motion 清零、confidence 置 0。
6. shutdown 逆序:unregister cost→flow→inputB→inputA(每步状态日志)→ nvOFDestroy →
   释放函数表/DLL/资源。实测全部 st=0。

### 二、独立证明(tools/nvof_probe 重写,exit 0)
- 诊断:debug layer + GBV + 同步队列验证 + DRED 全开。
- 测试图案:噪声+彩色块(2D 结构);位移矩阵 dx∈{±4,±8}、dy∈{±4,±8}、2D(+6,+3)/(-5,+7) 共 10 例。
- interior(8% 边距)中位数;raw/32.0;方向 current→previous(负号)。
- 结果(nvof-proof.json):**10/10 例 sign 正确、median endpoint error = 0.00px(≤1px)**;
  flowWritten=true;costWritten=true(哨兵 0xAA 预填充法:完美平移 cost 全 0 是合法输出);
  confidence 反相关证明:低 cost 四分位 |err|=0.0000px ≤ 高 cost 四分位 0.0011px;
  debug ERROR=0、CORRUPTION=0。**exit=0**。
- 关键修正:先前 session 的 "/(32×gridSize)" 结论错误——本次矩阵(±4/±8 双轴)证明 /32.0 即像素单位。
- GBV 注意事项:验证层开启时,进程退出前的资源释放会段错误(debug layer teardown);
  NVOF 对象已逆序 unregister+destroy(有日志)后直接 ExitProcess。JSON/verdict 先于退出写出。

### 附带修正
- tools/player_probe 的 NVOF 接线同步到新契约(B8G8R8A8 输入、raw/cost 显式、/32.0 densify、
  inverse confidence),但播放器整体验证尚未重跑——按指令,先证明数据契约,再谈质量/性能。

## Phase 6 session 6, 2026-09-04: NVOF 契约移植入 NvOfSession + 播放器集成证明(用户指令 s7)

### NvOfSession 修复(全部构建通过)
1. caps 真两次调用:nullptr→elemCount=3→分配→读取;scalar caps 用元素数 1;
   查询失败/列表空/grid 不支持全部 fail closed(无回退)。日志显示 grids=[1 2 4]。
2. initialize 入口要求 costOut!=nullptr(V1 confidence 契约的一部分)。
3. GetDesc 校验四资源:输入 B8G8R8A8_UNORM(0x57) 3840×2160;flow R16G16_SINT(0x26)
   960×540;cost R8_UINT(0x3E) 960×540。不符立即失败并打印实际/期望。
4. costOut 注册失败:逆序回滚 flow/inputB/inputA(带日志)并返回 false。
5. 播放器中 20 次未初始化 A/B 的 NVOF warm-up 已删除(历史 workaround,注释注明)。
6. NvOfSession.h 旧注释(RGBA8/full-size float/grid1)清除,更新为 SHORT2/grid-extent 契约。

### 播放器集成证明(integ-4k2,4K 片,exit=12[drift,预期],子任务证据全绿)
- caps: elemCount=3 grids=[1 2 4] width=[32,8192] height=[32,8192]
- contract-check: inputs A=0x57/3840x2160 B=0x57/3840x2160 (want B8G8R8A8/3840x2160)
  | flow 0x26/960x540 (want 0x26=R16G16_SINT/960x540) | cost 0x3E/960x540
  (want 0x3E=R8_UINT/960x540) -> inputsOk=true flowOk=true costOk=true
- nvOFInit status=0 (3840x2160 grid4 fwd ABGR8 flowExtent=960x540)
- register inputA/inputB/flowOut/costOut 全部 status=0
- nvofExecuteCount=451、nvofFrameFailures=0、mvecSource=nvof(真实 NVOF)
- 逆序 unregister costOut/flowOut/inputB/inputA 全部 status=0;nvOFDestroy status=0 executes=451
- 全程 0 条 [ERROR] 日志(含无 DRED/无 Present FAILED)
- 注意:0 ERROR/0 CORRUPTION 是日志级证明(播放器未开 debug layer;独立 nvof_probe
  已在 GBV 下给出 0/0)。若验收要求播放器内验证层开启,为下一轮任务。

### cost 分布与置信度门控的诚实声明
独立证明中 cost 分布近乎全 0(完美平移),low/high quartile 0.000 vs 0.0011 不能作为
强经验相关性证明。当前只能声称:**confidence 公式已修正为 (255-cost)/255(NVIDIA 语义),
门控阈值已接线**,真实置信度门控的经验证明需要遮挡/无纹理/噪声区域使 cost 分布非退化
——已列为后续任务,不在此轮声称已证明。

### STATE
- blockers 已删"decode_blit 24.7ms 根因"旧结论;Phase 6 保持 not_started;
  nextAction = 播放器 NVOF 集成验证(本轮已完成,等待验收)。

## Phase 6 session 7, 2026-09-04: NVOF 契约最终收尾(用户指令 s8 全部 7 项)

### s8-1..4 NvOfSession 收尾(构建通过)
1. 入口:null 检查覆盖 device/A/B/flow/costOut/inFence/outFence;costOut==nullptr
   单独先行拒绝(注明"never optional")。
2. capability 完整 fail closed:scalar caps 每次查询前元素数重置为 1;WIDTH/HEIGHT
   MIN/MAX 任一失败立即 false;验证 min<=3840<=8192、min<=2160<=8192,全部打日志。
3. 统一注册回滚:inputA/inputB/flowOut/costOut 任意一步注册失败,已注册资源按
   逆序 unregister(逐条日志)后返回 false,不依赖析构。
4. 头文件:删除 R8G8B8A8 旧注释;明确 inputA=previous、inputB=current、Execute
   输出 current→previous;删除 Desc.costOut(唯一入口为 initialize 参数);cost
   注明 REQUIRED 非 optional。

### s8-5 GBV 下播放器 4K 集成(VEYRA_D3D_DIAG=1,diag-4k4)
- 诊断在设备创建前开启(debug layer + GBV + 同步队列验证 + DRED)。
- JSON(diag-4k4.json):d3dDiagEnabled=true **d3dDiagErrors=0 d3dDiagCorruption=0**;
  nvofExecuteCount=406、nvofFrameFailures=0、mvecSource=nvof;presents=845;
  audioUnderruns=0 audioOverruns=0;normalPathReadbackCount=0。
- 日志:grids=[1 2 4] width/heightOk=true;四资源 contract-check 全 true;
  InfoQueue errors=0 corruption=0 (scanned 1024);逆序 unregister 4×status=0;
  nvOFDestroy status=0;全程 0 [ERROR]、0 Present FAILED。
- 工程:证据 JSON 改为在 D3D12 teardown 之前写(GBV 下 debug-layer 在设备关闭/
  atexit 阶段崩溃会吃掉 post-teardown 证据;exit 0x7D 仍会出现在进程码,但所有
  验收数据已落盘并验证)。

### s8-6 cost 非退化测试(nvof-proof,exit 0)
- 三区域内容:60% 纹理区 / 20% 纯色无纹理区 / 20% 高频噪声区(dx+8 用例)。
- 结果:**textured costP50=0、textureless costP50=2(p95=4,max=11)、noise
  costP50=0(max=4)** ——无纹理区 cost 显著高于纹理区,方向符合"cost 高=不可靠"。
  全图 quartile:lowCost(|err|)=0.000px < highCost=0.151px(inverse=OK)。
- 诚实结论:数据已非退化且方向正确,但幅度仍小(误差都≈0,gated=0);真实内容
  的置信度门控阈值仍待标定,本轮只声称"公式符合 NVIDIA 语义+非退化方向性验证"。

### s8-7 STATE
- 集成 blocker 已删除(GBV 集成证据落地);Phase 6 保持 not_started;
  未写 gate passed/Reviewer/checkpoint;nextAction=query-heap GPU timestamp。

## Phase 6 session 8, 2026-09-04/05: s9 入口校验/诊断假绿/teardown 崩溃(用户指令 s9)

### 更正声明(s9-A4)
session 6/7 的 WORKLOG 声称"入口已检查 costOut"是**错误记录**:s8 重写把该检查丢失,
costOut==nullptr 会走到 GetDesc 崩溃。本轮已在 initialize 入口恢复全参数检查(副作用
之前:不 LoadLibrary/不建会话/不 GetDesc),并以 7 例表驱动 fault-injection 证明
(veyra_nvof_fault_inject,7/7 REJECTED-CLEAN,exit 0)。

### s9-B 诊断假绿修复
- 顺序修正:InfoQueue 扫描现在发生在 g_d3dDiag* 统计复制与 overall 判定**之前**;
  overall 追加 `!diagRequested || (diagActive && retrievalComplete && err==0 && corr==0)`。
- 三阶段(startup clear / runtime 扫描+清空 / teardown 扫描)分别计数并写 JSON。
- 饱和检测:发现默认队列容量 1024 且曾饱和(旧"扫 1024 条全绿"不可靠);现已
  SetMessageCountLimit(无限),L1 实测 stored=3178 retrieved=3178 failures=0。
- 检索修复:两段式(先查长度)在 GBV 下全失败(failures==stored);改为单次固定
  缓冲调用后 L1 全部检索成功。
- 字段:storedMessageCount/retrievedMessageCount/retrievalFailureCount/capacity/
  saturated/err/corr/warn/info + message-ID 直方图 + 每类样本。

### s9-C 0x87D 根因(staged teardown 全标记 + 子步标记 + refcount 探针)
- 崩溃点精确定位:PresentSink::shutdown 内 **IDXGISwapChain3::Release()**(子步标记
  "swapchain-release"后无输出;refcount 探针=1,无外部引用泄漏)。
- 隔离矩阵(均开 debug layer + GBV + DRED):
  | 配置 | 交换链 | NVOF | NGX | 结果 |
  |---|---|---|---|---|
  | dpp/nvof_probe | 无 | 有/无 | 无 | exit 0(干净) |
  | iso3/iso6 full | 有 | 有 | 有 | 崩在 swapChain_.Release(exit 0x87D) |
  | iso3 nvofonly(FG/NR 特性在) | 有 | 有 | 有(FG) | 同上 |
  | iso3 nongx | 有 | 无 | 无 | exit 12 干净(全部 teardown 标记) |
  | iso9/10/11 full@L2/L1 | 有 | 有 | 有 | 同崩;L1 检索 3178/0err/0corr 后仍崩 |
  | **iso12 full@L0(无诊断层)** | 有 | 有 | 有 | **exit 12,teardown-complete,450 execute/0 失败** |
  | iso13 nvof-pure(无 NGX core)@L0 | 有 | 有 | 无 | 崩在 resize 后路径(独立缺陷,非产品路径) |
- 结论(矩阵证明,不归因任何一方):崩溃需要 **NVOF 会话 + D3D12 debug layer + 交换链**
  三者同时存在;去掉任一即干净。debug layer 与 NVOF 的设备包装在交换链销毁路径上的
  交互缺陷在用户态无法进一步归因(需要 NVIDIA/驱动级确认),如实记录,不指责驱动/GBV/
  远程软件。已试 6 种释放顺序(session 先/后、DLL 卸载先/后、窗口先销毁、out-fence 排空)
  均不改变结果。
- 工程处置:4K 集成验收在 L0 运行(进程正常析构,exit 12=drift gate);诊断层+GBV 在
  无交换链 harness(descriptor_present_probe 21/21、nvof_probe 含 10 用例矩阵)全绿。
  播放器内 InfoQueue 扫描已实现且在崩溃前正确报告(0 err/0 corr)。

### 播放器诊断分级
VEYRA_D3D_DIAG: 0=off, 1=layer+DRED, 2=+GBV+sync(默认 0)。

### 原始证据
logs/phase6-manual/{nvof-fault-inject.json, iso3..iso14-*.log/json, diag-4k*}

## Phase 6 session s10, 2026-09-05: teardown 所有权重构 + 0x87D 真根因修复(用户指令 s10 全部执行)

### 修正后的精确释放顺序(player_probe,已实现)
1. in-scope(资源 ComPtr 全部存活):保存 `lastNvofSignal = nvof.nextOutValue()-1` →
   INCOMPLETE stub JSON(processCompleted=false/teardownCompleted=false/verdict=INCOMPLETE)→
   sws-free → audio-thread-stop → wasapi-shutdown → ring-wait-idle →
   **nvof-out-fence-drain**(SetEventOnCompletion HRESULT + WaitForSingleObject 返回值检查,
   timeout/WAIT_FAILED=硬失败,日志打印 expected/completedBefore/completedAfter/waitResult)→
   ring-wait-idle-2 → **queue-final-drain(新)** → NR/FG/SR feature release →
   **nvof-unregister(纹理存活时逆序注销 cost/flow/B/A)** → **release-nvof-resources
   (四纹理 Reset,DLL 仍加载)** → **nvof-shutdown(destroy+FreeLibrary)** → nvof-event-close →
   ngx-params-destroy → iat-shim-restore → ngx-core-shutdown → staged 显式释放
   rtvHeap/presentPass/computePasses/guidance/frame/working/upload 全部 GPU 资源(逐组标记)。
2. 作用域结束:资源自然析构(显式释放后已无残余;device 仍存活)。
3. post-scope:**sink-shutdown(交换链,先于队列;内部 backbuffers→swapchain→window→factory)**
   → ring-shutdown(队列)→ teardown scan → ReportLiveDeviceObjects → final scan →
   InfoQueue.Reset → context-shutdown → demuxer/decoder close →
   final JSON(processCompleted=true 仅在 context.shutdown 完成后、自然 return 前写入)。

### 三个真根因(全部矩阵/日志证明,均修复)
1. **NVOF 纹理在 FreeLibrary(nvofapi64.dll) 之后 Release → SEGV**(t10-L0-r2:全部 staged
   标记完成后作用域析构崩溃;显式分阶段释放精确定位到 release-nvof-resources 组)。
   修复:NvOfSession 拆为 `unregisterAll()`(纹理存活时逆序注销)→ 调用方释放四纹理 →
   `shutdown()`(nvOFDestroy+unload)。头文件写明所有权规则。
2. **0x87D 真根因**(推翻 s9 "NVOF+layer+swapchain 三方交互" 结论):最后一次 Present 提交在
   最终 fence signal **之后**,`waitIdle()` 只等已 signal 值 → 交换链销毁时该 Present 操作
   仍标记 in-flight → D3D12 调试层报 ERROR id=921(ID3D12Resource final-release with GPU
   operations in-flight)并经 KERNELBASE `RaiseException(0x87D)` 未处理 → 进程死
   (WER event 1000:exception code 0x0000087D,faulting KERNELBASE.dll;真实退出码
   0x87D=2173 由 PowerShell Start-Process 证实)。SEH 证据捕获 wrapper 记录 code/addr/module
   并在异常后立即扫 InfoQueue 拿到触发消息原文。修复:`CommandSlotRing::drainQueue()`
   (Present 之后入队新 Signal 并等待)+ sink 内部顺序改 backbuffers→swapchain→window→factory
   (窗口后于交换链销毁)+ sink 先于 ring(交换链先于队列销毁)。修复后 L1/L2
   三阶段扫描全部 0 ERROR/0 CORRUPTION,异常不再触发(修复非抑制)。
3. **NF 控制 run 空句柄**:nrEnabled toggle 在 nrHandle==nullptr(VEYRA_NO_FEATURES)时仍调用
   NR evaluate(adapter SEH 捕获 seh=0xC0000005)→ runPlayback false → break 跳过 in-scope
   teardown → 析构顺序颠倒(nrAdapter 先于 coreHost)→ return 时 SEGV。修复:toggle 按
   `nrHandle != nullptr` 门控(与 fgBackend.created() 门控一致)。

### s10-V 矩阵(同条件隔离,全部自然 return,禁 ExitProcess)
| 配置 | r1 | r2 | 三阶段 diag(err/corr) |
|---|---|---|---|
| L0(无诊断层) | exit 12 | exit 12 | n/a(diag off) |
| L1(layer+DRED) | exit 12 | exit 12 | runtime 0/0, teardown 0/0, final 0/0 |
| L2(+GBV+sync) | exit 12 | exit 12 | runtime 1764-1772/0, teardown 0/0, final 0/0 |
| NF(NO_FEATURES) | exit 12 | exit 12 | n/a(diag off) |
- 全部 8 轮 `teardown-complete; process will return naturally` 后自然 return;无 0x87D、
  无 0xC0000005。L1-r3.json 保留了一个修复前的 INCOMPLETE stub 崩溃样本(证明 stub 机制)。
- nvof-out-fence-drain 每轮:expected==completedAfter==lastSignal,waitResult=0。
- mvecSource=nvof,NVOF 450/0(L0/L1)、427-430/0(L2)、0/0(NF)。

### 新发现 blocker(非 teardown,引擎运行期)
L2(GBV)runtime 扫描 1764+ ERROR,id=938 `GPU_BASED_VALIDATION_DESCRIPTOR_UNINITIALIZED`
(Dispatch 访问未初始化描述符槽,样本已入 JSON diagErrorSamples)。fail-closed 正确生效
(verdict=FAIL)。待后续任务修复(描述符表覆盖槽位需全部初始化)。

### 构建/命令记录
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release`
  → exitCode=0(每次修改后)。
- 矩阵命令:`VEYRA_D3D_DIAG={0,1,2}` / `VEYRA_NO_FEATURES=1`
  `out/build/x64-release/veyra_player_probe.exe --input loop/local/fixed_clips/test_av_1080p.mp4
  --run-id t10-{L0,L1,L2,NF}-{r1,r2} --log-file ... --json-file ...`
- 附带修复:FFmpeg DLL(avcodec-63 等)缺失导致 MSYS 127/0xC0000135 → 从
  C:/veyra-deps/installed/x64-windows/bin 复制到 exe 旁;`--runtime-dir` 默认值
  runtime_local/nvidia 才是正确层级。
- 修改文件:tools/player_probe/main.cpp、src/ngx/NvOfSession.cpp、include/veyra/ngx/NvOfSession.h、
  src/gfx/PresentSink.cpp、include/veyra/gfx/PresentSink.h、src/gfx/CommandSlotRing.cpp、
  include/veyra/gfx/CommandSlotRing.h。

### 原始证据
logs/phase6-manual/t10/{L0,L1,L2,NF}-{r1,r2}.{log,json}

## 2026-09-06 Goal session 4（Cycle 038-039）：R3.3 质量运行器 + 毒源隔离

### 执行摘要

- **R3.3a**: bare-stage 诊断矩阵（~584 行，调查已由 MipLevels=0 根因关闭，常设诊断工具 descriptor_present_probe 保留）从 player_probe 移除，1971→1392 行，行为对等验证（exit 12/计数/mvecSource 不变）。
- **R3.3b**: `veyra_quality_probe` headless 运行器（~440 行）建成：链 veyra_pipeline+veyra_sources 产品库，corpus/单输入循环双模式，R1.1 gate JSON 契约全字段输出。**完整 corpus 验证：6000/6000 帧、nr=6000/sr=3000（1080p SR+4K bypass 正确分流）/nvof=5990、nonZeroMotion=115200、confidence=0.9765、10 个顺序 graph 生命周期零崩溃、0 设备移除**。
- **系统发现（两个注入层毒源精确隔离）**:
  1. **RAW-buffer-SRV 创建**（持久映射上传缓冲的 R32_TYPELESS SRV）是设备移除+NVOF 阻断的唯一触发器；TEX/UAV 纹理视图全量初始化完全安全。→ 纹理描述符默认全量初始化。
  2. **帧内 Copy\*（CopyTextureRegion）仍被毒化**（NGX evaluate 内 SEGV）——Nv12Upload compute dispatch 是唯一可行的帧路径上载方式（session-1 结论再确认）。
- **GBV id=938 降 73%**: 1764+ → 467（残留=uploadPass RAW SRV 两槽有意不初始化，R6.1 精确指向）。0 Present FAILED；player 行为对等保持（presents 957/mvecSource=nvof/exit 12）。
- **Gate 矩阵结果**（全量复跑中）: quality:run-*×5 全 PASS；extent-matrix/nr-per-frame/**nvof-nonzero-motion**/confidence-stats/gpu-timing/**hash-binding**/depth:provider-from-run 全 PASS；reset-contract 红（sceneCut=0，R4.5 场景分析器集成范围）。

### 调试战记（诚实记录）

- 采样器：ring 外临时命令列表竞态移除设备 → ring slot 3；conf 终态 COMMON 屏障；footprint RowPitch 数学。
- corpus 模式 0xC0000409 两轮假线索（陈旧二进制 127 / fprintf 字面量断裂）→ 真因：manifest 扫描中 `(base+"/"+rel).begin()/end()` 跨临时对象迭代器 UB（堆越界 fail-fast）。
- 上传纹理 64KB 限制 → 回滚缓冲+dispatch。

### 下一步

后台 gate 耐久（2×30 分钟）完成后按结果修补；R4.1（NVOF flow 接入 NR MVec——当前 NR 仍消费 zero motion）、R4.5（sceneCut 计数）是 reset-contract 转绿的路径。

## 2026-09-06 Goal session 3（Cycle 037）：R3.2 真实 GPU 链迁入 EnhanceGraph

### 执行摘要

撤销 Phase 5 的核心 P0 缺陷已修复——"EnhanceGraph 只计数、真实 GPU 链在 harness"不复存在：

- **R3.2a**: GPU 辅助设施（ComputePass/GraphicsPass/DescriptorStager/StateTracker/makeTexture 等 357 行）迁入 veyra_pipeline 的 GpuPassUtils.h；probe 改用产品库版本，行为零回归（r32a 冒烟验证）。
- **R3.2b**: `src/pipeline/EnhanceGraph.cpp`（1020 行）承载完整真实链：资源/零初始化（直接 ExecuteCommandLists）/NVOF/NGX core+NR Create+SR+FG+warmup（snippetEvaluateFeature/evaluate）/五 compute pass/静态 views/逐帧 process（NV12→YUV→SR/bypass→parity→NR evaluate→parity decode→videoFrame+NVOF A/B→NVOF execute+densify→FG evaluate→genFrame）/s10 顺序 shutdown。createViews 独立阶段（swapchain 分配之后）。
- **R3.2c**: player_probe 3641→**1971 行**：引擎初始化→graph 构造；processOneFrame 500 行→薄包装（保持 previous→generated→current 呈现序）；teardown 按所有权拆分；JSON 计数全局桥接。

### 系统发现（重要，供 R6.1）

初始化的描述符 + dispatch + Present 组合在本机触发注入层假报 DEVICE_REMOVED（0x887A0005/DRIVER_INTERNAL_ERROR，无 DRED、debug layer 0 错误；stager 路径同样触发）。**基线 r0/t10 的全部证据运行在 views 默认未创建状态**（dispatch 消费未写槽位=GBV id=938 的来源）。裁定：graph createViews 复刻原始 env 门控（默认 OFF）保持行为一致；描述符初始化与注入层交互是 R6.1 既定范围。

### 最终验证

- 双配置构建 exit 0。
- 默认 env 冒烟（r32final-185450）：**exit 12（已知 drift FAIL 不变）**、driftP95=2827ms、presents=958、nr=432/sr=497/fg=461/nvof=461、**mvecSource=nvof**、underruns=0、自然 teardown——与迁移前完全对等。
- phase5 gate 26/45：**product:enhance-graph-submits-gpu + product:player-links-pipeline 双绿**。

### 下一条唯一任务

R3.3：probe 继续去重（bare-stage 诊断矩阵移出）+ headless `veyra_quality_probe` 骨架（链 veyra_pipeline+veyra_sources，无窗口跑 corpus，产出 gate JSON 契约），使 quality:runner-exe 具备通过条件。

## 2026-09-06 Goal session 2（Cycle 034-036）：产品库 R3.1 全部建立

### 执行摘要

接 session 1，完成 Playbook R3.1（四个产品库全部以真实成员建立并被 gate 检查放行）：

- **Cycle 034 R3.1a veyra_sinks**: WASAPI 音频（AudioPipeline 水位环形缓冲 + AudioRenderer 事件驱动 PTS 锚定主时钟 + 原子 seek）从 player_probe **逐字迁移**到 `veyra_sinks`（include/veyra/sink/WasapiAudioSink.h）。player_probe 3641→3128 行。行为验证无回归：underruns=0 overruns=0 seekCount=11、exit 12（已知 drift FAIL）不变。gate 新增 product:sinks/sources/player-links-sinks 检查并修复 player-links-pipeline 的跨 target 假阳性。
- **Cycle 035 R3.1b veyra_guidance**: IGuidanceProvider 接口 + **ZeroGuidanceProvider 真 GPU 实现**（三纹理 upload-copy 零初始化、GpuTextureHandle 完整生命周期字段、epoch 边界 requiresReset、provenance=Zero 诚实上报）。GPU 集成测试 13/13 双配置（真 RTX 5070，诊断 readback 验证全零）。测试自身曾有一个 staging 溢出 bug（分配 8 行复制 1080 行）——provider 本身正确，box 限定后全绿。
- **Cycle 036 R3.1c veyra_sources**: MediaFileSource 组合 veyra_media（无第二份解码实现）：Rational PTS 用真实流时基（实测 1/15360 单调）、Open/Seek/Discontinuity flags、单调 epoch、ColorDescription 解析 + assumed 默认（1080p→BT709 Limited 全 assumed）、原子 seek（demuxer+flush+flag）、EOS drain。corpus 驱动 23/23 双配置。修 3 轮：TRC 常量名、std::format 参数数（运行时 abort）、EOS 期望值。

### Gate 状态

phase5 gate 28/45 失败（exit 1 保持）。**产品库检查全绿**：pipeline/guidance/sinks/sources 四 target + player-links-sinks。剩余红项：quality-runner-target、player-links-pipeline、enhance-graph-submits-gpu（全部 R3.2 范围）+ 矩阵/depth/耐久（R3.2-R5 范围）。

### R3.2 迁移地图（供下个上下文）

- `processOneFrame` 位于 player_probe main.cpp:1982-~2470，~500 行 lambda，深度捕获 main() 作用域。
- 链路：ring.acquire(slot) → NV12 源（D3D12VA 纹理 fence-wait+双 plane SRV / 软件 sws→Nv12Upload dispatch）→ YuvToRgb dispatch 到 srcRgba → SR evaluate 或 ScaleBlit bypass 到 workRgba → ParityEncode → **submitAndSignal+新 list**（snippet 约束）→ NR evaluate（全参数块）→ ParityDecode 到 finalRgba → videoFrame[parity] blit + NVOF A/B 链（blit 传递）→ [后续未读：NVOF execute/densify/FG evaluate/presentQueue]。
- 迁移目标：src/pipeline/EnhanceGraph.cpp（gate 检查 ExecuteCommandLists+Evaluate 必须在此文件）；NvofGuidanceProvider 同批出生；player_probe 最终 <800 行（R3.3）。

### 下一条唯一任务

R3.2 EnhanceGraph 真实 GPU 链迁移（精确指针已写入 STATE.nextAction）。

## 2026-09-06 Goal session 1（Cycle 030-033）：接管、gate 重建、corpus、契约

### 执行摘要

新 Maker 按 GOAL_PROMPT 接管，完成 4 个原子 cycle，全部本地 checkpoint，无 push：

- **Cycle 030 R0 接管**: preflight 70/70；Release build exit 0；接管指纹 `61feb89b38e32f589b5c2fb6750526e5ad90dc3c`（44 entry）；四类窄 probe 新 run-id 复现基线（NVOF PASS / FG 59/59 / audio PASS / player FAIL driftP95=2858ms 复现已知缺陷）；44 项分类 keep/repair/hold 入 JOURNAL；保护性存档 `bb9c5361`（不含 MP4 删除，不写 lastGoodCommit）。
- **Cycle 031 R1.1 gate 重建并先红**: phase5.ps1 重写为 42 项 fail-closed 契约（删除 manifest-depth/第二次 1080p 冒充 4K/5 分钟冒充 30 分钟三个假通过口；新增产品库执行证明、本次 run extent/hash/timing/VRAM/reset JSON 契约、主路径纪律）。当前实现 exit 1，34/42 命名失败与 Playbook R1 逐项对应。存档 `7ec1e0c`。
- **Cycle 032 R1.2 确定性 corpus**: veyra_clip_gen 五场景（translation/occlusion/cut-flash-duplicate/particles/ui-text）× 1080p60/4K60 共 10 片 + SHA256 manifest。DLL 遮蔽问题（最小版 avcodec 遮蔽含 openh264 的 tools 版）用隔离运行目录 `out/build/x64-release/clipgen/` 解决。gate corpus:* 四项转绿，其余保持红（30/42）。存档 `c7d6414`。
- **Cycle 033 R2 契约族**: include/veyra/pipeline（Rational PTS 负值/未知、ColorDescription+assumed 标志+P010 fail-closed 路径、10 位 FrameFlags+breaksHistory、GpuTextureHandle ownerSlot/expectedState/readyFence、FrameWindow 固定 prev/current/next+lookaheadFrames≤2 无 vector、GuidanceFrame provenance/age/sourceSequence、ResetCoordinator 帧边界消费）。PipelineContractTests 50/50 Debug+Release；旧 unified 51/51 无回归。附带修复 descriptor_present_probe:617 debug C4702。存档 `07eb66d`。

### 实际命令（关键）

- `loop-gate.ps1 -Gate preflight` → 70/70 exit 0（session 首尾各一次）
- `build.ps1 -Preset x64-release` → exit 0（多轮）
- `veyra_nvof_probe` / `veyra_fg_harness --fg-test|--audio-test` / `veyra_player_probe --duration-seconds 20` → 0/0/0/12（logs/takeover-20260906/）
- `phase5.ps1 -Root .` → exit 1（34/42 → 30/42 两轮，失败清单见 JOURNAL 031/032）
- `veyra_clip_gen --make-corpus` → exit 0（隔离目录）
- `veyra_pipeline_tests` / `veyra_unified_tests` → 50/50、51/51（Debug+Release）

### 未执行/未通过

- Phase 5 gate 仍 exit 1（产品库/runner/真实 EnhanceGraph 未实现——这是 R3 的任务）；无 Phase 通过、无 Reviewer、无 lastGoodCommit 变更。
- player probe drift 2.8s 与 L2 GBV id=938 维持已知 FAIL（Phase 6 范围，未动）。
- 未运行 30 分钟耐久（runner 不存在，gate 正确拒绝）。

### 下一条唯一任务

R3.1：从 player_probe 抽取真实成员建立 veyra_sinks（WasapiAudioSink，~194-690 行）/veyra_guidance（NvofGuidanceProvider 包 NvOfSession）/veyra_sources（MediaFileSource），随后 R3.2 把 GPU 链移入 EnhanceGraph 使 `product:enhance-graph-submits-gpu` 检查具备通过条件。STATE.nextAction 已写入精确指针。

## 2026-09-06 强 Agent 接管审计与 Launch V1.3 重基线

### 用户决定

- 继续使用固定 hash 的实验 `nvngx_dlssnr.dll`/Feature 18 做本机研发，不等待尚未公开的通用 DLSS 5 SDK。
- 该决定不等于“效果与官方/Magpie 相同”已被证明，也不允许提交、打包或分发 runtime。
- 旧 Agent 错误过多；要求重写详细执行计划并交给更强 Agent。

### 对抗式审查结论

- Phase 0–4 的真实 checkpoint/日志保留。
- 历史 Phase 5 产品级 pass 撤销：`src/core/EnhanceGraph.cpp` 只复制 packet/增加 counter，注释写明实际 GPU pipeline 在 harness；旧 `phase5.ps1` 只因 depth manifest 存在就放行，并把第二次 1080p endurance 放在“4K60”检查位置。
- Phase 6 组件代码与证据保留：DLSSG 59/59 truth、NVOF、WASAPI、Present/teardown 修复均有价值；但 t10 所有 player JSON 仍为 FAIL，drift P95 约 2.8 秒，L2/GBV runtime 有 1700+ id=938 descriptor-uninitialized。
- `player_probe/main.cpp` 约 3641 行，真实 graph 尚未抽成共享产品库。
- 无 `apps/veyra` UI、CaptureCardSource、ImageExportSink、VideoExportSink 或真正 DAV2 provider。按完整 Launch V1 交付物估算进度约 40%±5%。
- 控制面修改前工作树约 31 个 status entry；本次文档重基线完成后为 44 个（增加的是计划/状态文件），且 tracked `validation/fixed_clips/test_h264_1080p.mp4` 仍处于删除状态；本轮未 reset/restore/删除任何旧 Agent 代码。
- NVOF SDK 实际已存在于 `third_party_local/nvidia/Optical_Flow_SDK_5.0.7`；旧 INBOX 缺失记录已作废。Video Codec SDK 13.1 与真实 4K60 采集硬件仍是外部阻塞。

### 文档/状态更新

- README、AGENTS、Product Spec、Playbook、Competitor Audit、Loop Engine、Goal/Review Prompt、gate contract 全部加入 2026-09-06 恢复口径。
- Playbook 新增唯一 R0→R12 施工顺序，精确规定工作树保护、phase5 gate 修复、共享 graph、guidance/depth、GBV/timing/drift、Player、Capture、Image/NVENC Export、UI/recovery 和最终 gate。
- BACKLOG 重新拆成可执行原子项；STATE 回到 Phase 5 `in_progress`，Phase 6/7 locked，`lastGoodCommit` 回到有效 Phase 4 checkpoint。
- GOAL_PROMPT 改为强 Agent 接管提示词，禁止从 UI 开始、禁止相信旧 pass、禁止清理未提交成果。

### 本轮实际命令

- `git status --short` / `git diff --stat` / `git diff --check` / `git log --oneline`：完成；发现上述 dirty tree，无 whitespace error。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`：控制面改动前 70/70，exit 0。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release`：exit 0，Ninja no work to do。
- 检查 `logs/phase6-manual/t10/*.json`：当前矩阵 verdict 全 FAIL；L0-r2 driftP95=2828.229ms、NVOF 450/0、FG 450、readback=0、自然 teardown。

### 未执行

- 本轮是计划/控制面审计，没有重新执行 RTX Feature 18/NVOF/DLSSG/player runtime；历史结果未冒充本轮结果。
- 未运行新的 phase5/phase6 gate、Reviewer 或 checkpoint；控制面 rehash/preflight 在文档修改完成后单独记录。

### 下一条唯一任务

新 Maker 执行 Playbook R0.1：重新取得工作树指纹并分类约 44 个未提交项，然后执行 R1.1，重写 phase5 gate 并在当前 metrics-only graph/假 4K/depth 缺口上证明 exit 1。

### 控制面收尾

- 重新计算 10 个受保护文件 SHA256，更新 `loop/CONTROL_HASHES.json`，并同步基础 `scripts/loop-gate.ps1` 的 manifest hash。
- 修改后再次运行 preflight：**70/70，exit 0**；STATE 当前 Phase 5 `in_progress`、Phase 6/7 locked、3 个 open P0/P1、5 个 blocker，状态机与 Git 指针检查全部通过。

## 2026-09-08 optimization blocked audit 3 — Goal blocked

Previous and current continuation classified no progress, not verified process waits. Read-only revalidation confirms unchanged README F226D0A7 / manifest expectation 781FAFD8, manifest hash 26559334; no explicit authorization for the exact two-hash synchronization. Same blocker for three consecutive Goal turns including startup. Under the explicit control-plane stop rule there is no permitted independent implementation remaining. Mark Goal blocked, retain full Q0–Q8 scope and unapplied proposal; no product/control edits, build or GPU test. Resume requires explicit authorization recorded in INBOX, then exact synchronization and fresh preflight. Not complete.

## 2026-09-08 Q1a RGB / odd dimensions and static NR isolation

Phase 7 optimization remains in progress. Q1b large-image tiling, Q2 zoom and Q3-Q7 quality work are not passed. Real capture remains unexecuted.

Changes: ResolutionPlan/EnhanceGraph accept odd extents up to actual single-texture 16384; WIC arbitrary 8192 guard removed with checked UINT byte bounds; RGB upload and RgbToLinear avoid 4:2:0 conversion; PNG negotiated BGRA packing fixed; ScaleBlit exact 1:1 load avoids long-image floating-point interpolation error. Static images skip NVOF and FG capability/Create/warmup, reject FG enable, normalize image settings to 1X. Product per-export integrity checks unchanged.

Build command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release (exit0; logs/optimization-goal-20260908/build-static-final.log).
Image command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File logs/optimization-goal-20260908/run-image-tests.ps1 (latest image-c17dd1e5813b494e9d25344ebd0c93e1, exit0,4.4185s; EXE 60EEF44E1E36B97182D0EE53A09A782444D7AA01864CD1B617D5677703CF9008).
Five NR-off cases 1x1,257x513,97x9001,4097x257,257x4097 have max RGB error0 and exact PNG readback. 257x513 and97x9001 real NR: Feature18 Create0x1 Success,handle non-null,SEH0,Evaluate1,nonblack output. FG capability/Create absent; direct enable and 2X apply rejected. This proves execution/dimensions, not NR quality equivalence.

Historical failures retained: image-ece8... missing shader dependency (fixed CMake); image-fe18... long1:1 error6 (fixed ScaleBlit). phase7-rgb failed old 23-frame assertion: actual 12source+11generated+1hold=24,120Hz,0.2sec,audio preserved. Developer delivery gate now checks all those identities/duration/rate for H264/HEVC; it does not change export behavior or add product scans.

Gate command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7. Q1a run4c37adc29e6e406c9f7e398fd2a286cd passed; independent reviewer run3af42697442443a6b79e3aab2e78c04b passed41.461s. Static-FG fix runb3c3df19f8b94d3eae6bcfde6aa68679 exit0 preceded final controller normalization/test assertions, so do not claim identical final executable coverage. Independent review_rgb_foundation scoped PASS with P2 static-FG finding subsequently fixed; this subsequent fix awaits review.

Next: Q1b bounded tiles using the same EnhanceGraph, full-size image export and viewport rendering. Inputs beyond single-texture limit still fail explicitly; memory/model/codec limits are real and arbitrary input support is not complete. No runtime replacement, push, artifact upload, packaging or shutdown.

## 2026-09-08 Q1b tiles and Q2 preview candidate (cycles 53–54)

Previous turn classified progress, not wait/no-progress. Fresh preflight passed; STOP absent. Q1a static FG isolation integrated into Q1b. Added TiledImageProcessor using shared EnhanceGraph (1280 core,128 context halo,64 overlap feather; spatial tiles reset independently). Real full-sized CPU result retained for image save. EngineControllerImage presents bounded viewport sampled from full result, reprocesses on settings only, supports original comparison, cancel and rollback. Normal presentation uses PreviewView UVs; professional wheel anchors cursor, middle pan/right reset; new media/daily reset fit. Product video export integrity unchanged.

Release build: scripts/build.ps1 -Root . -Preset x64-release, exit0; logs/optimization-goal-20260908/build-q1b-final.log. Current app SHA256 0409DACD716950F3B674D0B105AAC9972B1B85A8AAC8362EECAA27314166F0E6. Shader identities recorded separately; EXE hash alone does not prove shader identity.

Tests: image-55da6ef7d941450cbbb8d934852191a0, exit0,6.400s (run-image-tests.ps1,275s watchdog). NR-off single texture five sizes exact; 17001x17 and17x17001 tiled14 each, maxError0 including independent half-transparent white->188/transparent->black fixtures; cancellation after first tile returns no output. Real NR97x17001:14 tiles,14 Evaluates, full dimensions,PNG exact readback (pixel quality/equivalence not asserted). Earlier single257x513 and97x9001 real NR remain covered. UiContractTests pass includes pointer anchoring/inverse wheel/pan math.

Application --smoke-zoom --smoke-seconds 9/10 tests: zoom-large-5ae8f78c7c5442ca982871c5638d7c82 (17x17001 NRoff) saved same PNG SHA3E3331C3FF73E636F4F37467F8F0175EE2F1158772E24A5EB0AC1F1A69F525C1 despite zoom; zoom-nr-7918200c1f554761b4af22d7ffd68db4 normal257x513 NR1 unchanged; final app zoom-large-nr-0b7a2fdd8cef4cc1855094d3d3cedbe8 NR14 unchanged,97x17001 full save. Session/revision/output extent preserved; zoom/pan/reset/daily flags pass. App processes bounded25s. Runtime output/dimensions covered; screenshot pixel comparison and real pointer hover routing still need stronger evidence.

Final phase7: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7 exit0; logs/delivery/bb35fca4c4e64b269caa7a5eb4243a2d/result.json 41.140s, same app0409..., all software checks pass. Prior run0ff376...41.203s is older app1B943... . Software gate is not full Goal approval. Q1 video model/codec edge limits and tiled visual seams/quality still need review; Q3–Q7 remain. No capture hardware claim, publishing or shutdown.

### Cycle55 reviewer P2 recovery fix — 2026-09-08

review_tiles_zoom independently passed software gates (3637158bbc434cb3aa6af32f9d62935c40.906s; image-bb9100790cf54d5c85237c8afc1da9d0 6.465s) but scoped verdict FAIL for unhandled save allocation exception and presenter-before-drain unwind. Finding accepted and repaired, not dismissed by green gates.

All-exit cleanup guard now drains shared queue before presenter/graph destruction. Large-image save and settings exceptions retain last successful result; ordinary image saves also preserve playback. WIC channel packing is bounded to row bands instead of another full-size image; existing decode-back extent verification remains unchanged. Partial file is CREATE_NEW-owned and removed after WIC closes on failure, or disarmed after successful rename. No new integrity scans or video-export changes.

Fault injection is opt-in VEYRA_TEST_LARGE_IMAGE_SAVE_THROW=1, once after actual WIC WritePixels (partial exists), not actual system memory exhaustion. App save-recovery-15610c7429934b15803b3e0a5cbc6751 9s smoke/25s watchdog: exception caught; session/output retained; same-path retry success; no partial remains; saved SHA equals source3E3331...525C1. Zoom session/revision/output assertions still pass.

Build scripts/build.ps1 -Root . -Preset x64-release exit0 (build-save-recovery-final.log/build-save-tests.log). Final app SHA5E7BC723B647903851C4DD0265F6CDC1A16BBDCE9AEEF07D6E213808F49BE95E. Image integration image-98845305ab6045a79311ef503ecfee9d exit0,6.515s: prior cases, cancellation, new PNG/JPEG multiple-band1024x3073 solid RGB fixtures pass. Final phase7 via scripts/loop-gate.ps1 exit0; logs/delivery/04815e94852a435a83840eaa45ba0ac1/result.json41.246s matches final app. Reviewer recheck pending.

Read-only Q3 diagnosis: CaptureCardSource packet duration is still default known0; downstream clamp selects83333 ticks incorrectly. Source colorInfo defaults BT709/SD601 but graph reads unresolved AVFrame fields and uses different transfer/matrix defaults. Capture RGB32 also leaves transfer/matrix unspecified and alpha is not guaranteed meaningful. Next Q3 must resolve once, carry actual sample/nominal duration, and retain explicit fallback logging; no Q3 code changes yet.

Independent recheck PASS for implemented Q1a/Q1b/Q2 and cycle55 save recovery; see docs/REVIEW_IMAGES_PREVIEW_2026-09-08.md. Current Phase7/Goal remains in_progress. Prepare local checkpoint only; next close remaining input/preview evidence, then Q3–Q7.

## 2026-09-08 cycles56–61: input/preview evidence, duration and color

Prior turn progress checkpoint3b2806b; clean startup and preflight passed, no STOP. Phase7 optimization remains active.

Two-dimensional2561x2561 image tests:3x3 tiles,NRoff maxError0 including intersections;NRon9 real Evaluates. Final combined image matrix image-97a039da3acf457b91046ba4e93831e1 exit0,9.532s. Earlier image4a730...9.300s before color changes. This verifies coverage/execution; natural-image neural seam/context equivalence still not asserted.

Actual VideoPresenter GPU framebuffer geometry: preview-pixels-96eb8b4f9f9b44668dcfb3cb7a935ede,exit0,<1s/25s watchdog. Fit/zoom/pan/reference/reset pixel assertions passed; fit.png/zoom.png viewed. readPresentedFrameForTest is explicit diagnostics only and has no production callers. Normal playback/video export do not read pixels back. Root-window queued hover/focus test --smoke-hover: hover-0579d93ffdf14b3db425da4da6dfc553 exit0,9s,actual WindowFromPoint route with focus on ModeSwitch button;zoom/pan/reset/session/revision pass. Initial hover-fe036...failed because smoke checked in same timer before posted message could run;log shows actual route immediately afterwards. Fixed smoke's150ms post-message wait, not product logic.

Actual H264 YUV444 video import: video-dimensions-4790082c088444288795325b778e400a,257x513 and1280x2561 each6frames,NR6/NVOF5,app exit0,6s smoke/25s watchdog. Old even and2160-height import guards no longer block these inputs. Does not promise unlimited model/codec dimensions or extend export codec contract.

Q3 duration: CaptureTiming derives duration from complete IMediaSample GetTime;missing stop uses negotiated nominal;unknown stays unknown. FramePacket default duration is unknown. Scheduler rejects zero/invalid duration as measured interval and uses explicit nominal fallback;clamps before integer conversion. Unit14 checks pass:30/60fps,knownzero,missing/invalidsample,huge duration. Initial build-duration.log failed Windows max macro;parenthesized numeric_limits(max) fixed. No physical capture test in this run.

Q3 color: shared ColorMetadata resolver combines declared AVFrame fields with source fallback (per-frame explicit wins,assumed values adapt to decoded format),SD601/HD709,YUV709/RGBsRGB defaults with assumed logs. Source metadata and graph swscale/shader share it;player and video export pass packet colors. Captured RGB32 source reports same resolved metadata. Explicit BT2020 conversion rejected instead of misinterpreted as709;HDR policy unchanged. GPU YUV neutral128 gives142(BT709),130(sRGB),189(linear);SD601red254,0,0 within golden tolerance. File parser defaults now use resolver. Capture RGB still traverses NV12 and remains a Q3 optimization task.

Commands: scripts/build.ps1 -Root . -Preset x64-release exit0 (build-color-capture.log); veyra_live_timing_tests.exe14 PASS (duration-unit.log); run-image-tests.ps1 (275s bound) and explicit25s-bounded PreviewGeometry/app tests. Latest phase7 via scripts/loop-gate.ps1 -Gate phase7 exit0; logs/delivery/f3fbdc84e9c54e718e03a94066df28bc/result.json41.453s appCDD4A16CC66A462A907EFA9EB82C10D22AA58A7B057BAEFDFAF4FB0EC055DC4F. Earlier duration gate e2fee...41.489s was previous app9A5C... . Latest small metadata-format changes covered by gate; GPU color goldens precede those small changes and await independent rerun. No new export integrity checks, publish, runtime change or shutdown. Independent review pending; Q4–Q7 open.

Independent scoped review PASS: docs/REVIEW_COLOR_TIMING_2026-09-08.md; actual GPU tests and phase7 rerun. Q3 direct RGB capture and Q4-Q7 remain.

## Cycle62 — direct RGB capture (review pending)
- CaptureCardSource now labels DirectShow RGB32 as BGR0 (unused alpha); EngineController selects direct RGB for capture. EnhanceGraph accepts BGR0/RGB0 with opaque alpha and preserves alpha only for RGBA/BGRA. Removes an application RGB→NV12 conversion, not upstream device compression.
- Shared 64×36 scene/cadence analysis serves CPU YUV and live RGB. Static images remain single frame. No normal pixel readback added.
- Build: scripts/build.ps1 -Root project -Preset x64-release, build-capture-rgb2.log exit0. Initial image test 1df8045c failed because fixture omitted enableNvofStandalone; production sets it. Fixed fixture to match product, no production bypass.
- GPU image test image-1660346e66504f8bac949030915d7398 exit0, 11.706s (275s watchdog): alternating red/blue BGR0 alpha0 exact maxError8=0, opaque output, one detected cut; NR-on4 actual Evaluates and2 NVOF executes. Prior images/colors/tiles remain passed. This is synthetic source, not physical capture validation.
- powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7: 75 checks PASS, logs/delivery/8211c4bf969647d099f62cd3cd11d96a/result.json. App SHA256 410EE473BDBCD8948D24589D8047F4A54FA6C30BA4E243F2B946D44EAF03B06A.
- Q4–Q7 and natural image tile quality remain; full Phase7/Goal stays in_progress. Existing export integrity unchanged.

Cycle62 independent PASS: docs/REVIEW_CAPTURE_RGB_2026-09-08.md. phase7 afe022a9 41.407s, image9ac2822f11.487s. NR-on fixture error0 is unmeasured, not a pixel quality assertion. Next Q4: official DLSS guide31March2026 PDF pages20/35/37 verifies linear input IsHDR and exposure contract; local SR evaluate/isBypass still width-only (Create checks both), fix and GPU-test next.

## Cycle63 — SR linear input and extent contract
- DlssSrBackend now declares linear input via IsHDR|AutoExposure (0x41); preExposure/exposureScale1. Official NVIDIA DLSS Programming Guide31March2026, local DLSS_repo/doc PDF pages20,35,37 and installed310.7 SDK helpers confirm contract. Reference https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf . SDR product does not become HDR display output. No fabricated sampling jitter, MVLowRes flag or runtime replacement.
- Evaluate/isBypass now compare both dimensions, like Create; graph avoids creating SR for identical extents. Output-space motion pixels retained; zero-depth fallback remains explicitly unproven geometry.
- Actual GPU baseline image-576edf2d1fa145bfbfa7573cffb4e29213.081s unflagged256→512 gray interiors0 error. Fixed image-87b550aa803042169c9a4458e564b59216.462s: ordinary upscale gray error1, height-only256x128→256x256 error0, same-size bypass error0/evaluate0. Three real SR Evaluates for each upscale. Baseline/fixed lastframe whole RGB MAE0.1771/max62, mainly edges; not a general quality improvement claim. Height-only PNG visually inspected.
- Build scripts/build.ps1 -Root project -Preset x64-release logs build-sr-baseline/flags/diagnostic.log exit0. phase7-sr.log PASS75 checks logs/delivery/57facb26ad2d4776a9d5a37a8a814b80/result.json (before diagnostic-only fix).
- 1080→4K24frames SR/NR passed, but did not trigger60frame stats. Follow-up sr-motion60-bd4905c3be0b4b559dec8925030df785 FAILED76 GPU diagnostic errors: sampler used output4K extent on source1920x1080 flow/conf textures. Fixed tools/quality_probe/main.cpp sampler to read/validate actual resource dimensions/formats; no normal pipeline/gate relaxation.
- Final sr-motion60-c1220641df6b46d6afef93054dda5bf5/result.json exit0,5.8s,60sec watchdog: SR60,NR60,NVOF59,nonzeroMotion2292,GBV errors0,failures0,normal readback0. Source1920x1080/output3840x2160. Current app37361B9C23AEE741C11EA626DCF6C3D587DA73ACB848D2C1D1C1599EDE2A2445; qualityprobe57B89BC4A9CDB542A317E0060FD8ED1C477BECE5F203136D228EA4F5C94422FC.
- Review pending. Natural/known4K reconstruction, UI protection, motion/FG quality and candidate ROI remain. Export integrity unchanged; physical capture not executed.

Cycle63 independent scoped PASS: docs/REVIEW_SR_CONTRACT_2026-09-08.md. phase7 1168a76f41.678s/image0d5938b316.225s/SR60a70819e65.925s. Actual4K colorbars output visually inspected; not natural content. Next atomic Q5 GPU protected regions within residual composition, source-normalized coordinates, then professional rectangle selection/preset persistence; do not label private UI correction effective or forget SR/FG distinction.
