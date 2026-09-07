# Veyra Gate Evidence

## Current capture fix candidate — 2026-09-07 (not independently approved)

EXE4A9BA4B321DEEC17C5E3562AF03EF75A316856200A8708FA8E692475A05B59C9. Actual real-card source FPS~49.4–49.8 atMJPEG1080p50,0capture drops; final NR195/NVOF193/FG193. Logs `logs/capture-fix-20260907/`. Visible file/image/export gate `logs/delivery/01b72df768524af9ab0aa8d2e3dbb59e/result.json`,47.309s,21 checks. Full evidence/caveats `docs/CAPTURE_LATENCY_FIX_2026-09-07.md`. Reviewer attempt ended without verdict, needs_review; old PASS below only applies to its old binary. Do not mark physical latency/audio/FG display cadence verified.

## Historical delivered binary / independent PASS — 2026-09-07

Current EXE SHA256 70DD23C44337004FC734FBAE8BB6139E185C52370AE97CCE6C3432A3C6F30DE1. New-context Reviewer preflight/phase5 (consolidated delivery) exit0/0, PASS, no reviewer source mutations. logs/delivery/9be0614da5d642e394a35c71d80f6207/result.json,40.31s. Initial3 P1 fixed and independently reproduced correction: image4K dimensions, cancel callback lifetime43encodes exit3/partial only, audio fail-closed code review. Review persisted docs/REVIEW_F6.md. Local contract only; awaiting_user_capture_test and distribution_blocked remain.

## F6 consolidated local delivery — 2026-09-07

Build x64-release exit0. `scripts/gates/delivery.ps1 -Root .` exit0, run d28879b01b5c44dd86cad33d6f386d90, 31.59s, 16 checks. Full current evidence in logs/delivery/{run}/result.json and per-command stdout/stderr/JSON/media. Actual 1080 NR60 + NVOF motion + GBV0, native4K NR12, 4K-input realtime FG source264/generated263/absolute-latenessP95 14.88ms, paused seek/resume/JPEG, PNG application run, native4K H264 and HEVC23 frames each + audio fully decoded by ffprobe. EXE900DD752D749036EC482B3B9557AD23D12C60D58A58E37B44C1C960D5DC1B747. New read-only reviewer pending. No capture hardware or endurance PASS.

Raw GPU segmentation logs/f6-profile-split.* proves NR22–23ms versus decode/output0.28ms. User explicitly accepted default realtime processing profile with optional native4K. New controls/doc hash rebaseline records this subsequent user authorization, not a hidden lowering of native4K quality/output correctness.

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

这里只登记真实存在的门禁证据。格式固定：

## Phase 3 / phase3 / 2026-09-03T01:20:00+08:00

- Commit: 验证内容 checkpoint 待登记于 STATE.phases[3].commit
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase3`
- Exit code: 0（外层 70；内部 41 checks，run-id a122c9f792e14ceeb117d07a0e08ff91）
- Build configuration: x64-debug + x64-release 均真实构建；FFmpeg 9.0.1 (vcpkg 30ef65ca 基线, C:\veyra-deps)
- Hardware/runtime identity: RTX 5070 / D3D12VA shared Veyra device / AV_PIX_FMT_D3D12 / Feature 18 via signed snippet
- Logs/captures: logs/phase3/a122c9f792e14ceeb117d07a0e08ff91/（software+d3d12va+seek-storm+debug）；logs/phase3/endurance-30min.{json,log}（60 循环×900 帧）
- Quantitative result: **software 300 帧 PTS 全单调零回读；d3d12va 300 帧 nrEvaluateCount=300/300 + shaderDispatches=300 + AV_PIX_FMT_D3D12 + sharedVeyraDevice=true + gpuReadbackCount=0；seek-storm 10/10 无 stale；debug infoqueue active 0 errors；endurance 60 loops=54000 帧 nrEvaluate=54000/54000 workingSetGrowth=94.4MB<256 commitGrowth=-46.5MB<256**
- Reviewer: 首轮 FAIL（5×P1+8×P2）→ 修复（GPU queue wait/AVFrame lifetime waitIdle/实测 maxInFlight/NR per frame 54000/30min endurance+memory）→ 复核 **PASS**（独立 run 768dd095…全过；endurance JSON↔log 交叉一致；8+1 P2 均维持非阻塞）
- Open P0/P1: none
- Conclusion: Playbook §16 Phase 3 全部门槛机器验证通过且经独立 Reviewer 复核；Phase 3 关账，进入 Phase 4

## Phase 2 / phase2 / 2026-09-02T23:05:00+08:00

- Commit: 验证内容 checkpoint 待 Reviewer 后登记；gate 运行于 220d6ba+f477948 之后的工作树
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase2`
- Exit code: 0（外层 70；内部 phase2.ps1 35/35，run-id 32fb351bb11b49d2b718027bbfdb33f4，5.0s）
- Build configuration: x64-debug + x64-release 均真实构建；CPU 黄金测试双配置 12/12 0 failures；Release parity run 真实执行 16 帧 Evaluate
- Hardware/runtime identity: RTX 5070 / Feature 18 via signed snippet（Init_Ext/Create 0x1）；debug run 附带 ID3D12InfoQueue 零错误
- Logs/captures: logs/phase2/85da38334fd0488b816bccb14635f782/（修复后 gate run）；logs/phase2/debug-parity-run.{log,json}（持久化 debug parity run）；captures/phase2/<runId>/（七文件）
- Quantitative result: **GPU vs CPU encode maxCodeDelta=1（≤1）maxAlphaDelta=0**；**GPU vs CPU decode：beyondOneUlpCount=0（每通道 ≤max(0.002, 1×FP16存储ulp)）、maxAbsError=0.00390625（恰 1 ulp@[4,8)）、nanInf=0**；debug parity run infoqueue active=true stored=0 errors=0（持久化）；中性 baseline 1.0/1.0/1.0 + addon SHA-256 实测匹配（addon 从未加载）；rawDlssnr≠final；stageLuma（JSON）：proxy/raw/final 机器统计替代手抄值
- Reviewer: not run yet（gate 首次通过后立即安排）
- Open P0/P1: none known
- Conclusion: Playbook §16 Phase 2 全部门槛（CPU golden、≤1 code、≤0.002、四阶段真实捕获、中性 baseline+addon hash 记录、raw/final 可区分）机器验证通过

调试记录：5 个缺陷由 debug layer infoQueue 定位——SRV MipLevels=0 触发 RemoveDevice；UAV range 双重偏移越界；NGX 后共享资源 DATA_STATIC bind 冲突（DESCRIPTORS_VOLATILE+前置 barrier）；decode 容差含 FP16 量化（期望量化后比较）。CPU 黄金数学零改动。

容差裁定记录（Reviewer 首轮 P1/P2 修复，2026-09-02）：①P1——"debug parity run infoQueue 零错误"原先无持久化产物；已改为无条件 drain+计数入 JSON 并真实持久化 debug run（logs/phase2/debug-parity-run.log：stored=0 errors=0）。②量化器改 RNE（与 GPU 存储一致）后实测揭示：高光放大区（UpgradeToneMap ratio≈4.26×）fp32 与 double 的固有中间差（~0.05%）会跨越 FP16 桶边界（例证已留档：cpu=4.00763→RNE 4.0078125，GPU fp32≈4.0055→存 4.00390625；5038/2M 像素呈 ±1 ulp 分布，全部恰 1 ulp）。裁定：可表示区（<4.0）与原 0.002 界逐字等同；≥4.0 区因绝对 0.002 对正确 fp32 管线数学不可达，裁定为恰 1 存储 ulp。经独立 Reviewer 终审接受（PASS）。

## Phase 1 / phase1 / 2026-09-02T22:20:00+08:00

- Commit: 验证内容 checkpoint 在 Reviewer 通过后登记于 STATE.phases[1].commit；gate 运行时工作树为 a8feb21 + 用户安装 Graphics Tools 的环境变化
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase1`
- Exit code: 0（外层 70 checks；内部 phase1.ps1 55/55，run-id 7a535572145b4d73b25da192adf847ee，9.1s）
- Build configuration: x64-debug（带 D3D12 debug layer，实跑 10 帧零 state error）+ x64-release（300 帧）均经 build.ps1 真实构建
- Hardware/runtime identity: RTX 5070 / 0x10DE / driver 32.0.16.1656 / FL 12_2；staged nvngx_dlssnr.dll 165840496 / E16B…FC8E / Valid / NVIDIA；NGX SDK 310.7 官方 GitHub clone
- Logs/captures: logs/phase1/7a535572145b4d73b25da192adf847ee/（harness-release.{log,json}、harness-debug.{log,json}、captures/）
- Quantitative result: **Release 300/300 Evaluate 成功 0 失败**；output meanLuma=0.49425 min=0.05098 max=0.94397 stddev≈0.327 allZero=false constant=false；baseline=1BD84180… style1=B5294CB6… intensity05=8A621726…（三 hash 互异）；GPU timestamps nonZero=true avgMs=6.32；teardown release=0x1 snippetShutdown=0x1；deviceRemoved=false；**Debug run debugLayer=true 且日志 0 条 state/descriptor 错误**
- Reviewer: 首轮 FAIL（1×P1：no-state-errors 为空转 grep，debug layer 消息无捕获通道；5×P2）→ 修复（ID3D12InfoQueue 真实接入 + gate 两项可失败断言 + 字面量/containment/SEH/nanCount/debug30帧）后 **PASS**（2026-09-02；独立 run-id 0ede6908f036462f99ced2ccfe48f169 双配置 fresh build；release 300/300、debug 30/30 infoqueue active 0 error；三 hash 与此前三个 run 完全一致；runId/exeSha256 与独立 Get-FileHash 一致；控制面 1fb7afa..HEAD 零改动；Reviewer 工作树零变异）
- Open P0/P1: none（P2 残留三条已记录 JOURNAL Cycle 017：teardown 段 infoqueue drain、containment 后缀 vs 前缀、200 条检索上限——均不阻塞，留待后续 Phase 顺带处理）
- Conclusion: Playbook §16 Phase 1 全部门槛（Init_Ext/Create 成功、300/300、非黑非恒定、参数可变 hash、GPU 计时、干净释放、debug layer 无 state error【经 ID3D12InfoQueue 真实断言】、capture 存在）机器验证通过且经独立 Reviewer 复核；Phase 1 关账，进入 Phase 2

迭代记录：2026-09-02T21:56 首次运行 54/55（唯一红项 debug-layer-enabled，本机未装 Graphics Tools）；用户安装 Windows 图形工具后本 run 55/55。前期过程证据见 JOURNAL Cycle 016 与 WORKLOG Phase 1 条目。## Phase N / Gate name / timestamp

- Commit:
- Command:
- Exit code:
- Build configuration:
- Hardware/runtime identity:
- Logs/captures:
- Quantitative result:
- Reviewer:
- Open P0/P1:
- Conclusion:

在命令未执行、artifact 不存在或 Reviewer 未运行时，字段必须明确写 not run / not available，禁止使用 assumed、should pass 或理论结果代替。

## Bootstrap / preflight / 2026-09-01T17:16:32+08:00

- Commit: not available（本地 Git 尚未初始化）
- Command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
- Exit code: 0
- Build configuration: not applicable；应用工程尚未建立
- Hardware/runtime identity: nvngx_dlssnr.dll size/hash/signature/signer 通过；renodx add-on size/hash/NotSigned 身份通过
- Logs/captures: 命令标准输出；无 Phase capture
- Quantitative result: 45/45 baseline checks 通过，0 个失败；包含 8 个受保护控制面文件 SHA-256
- Reviewer: not run；bootstrap 不是 Phase gate
- Open P0/P1: none for Loop bootstrap
- Conclusion: Loop preflight 可用；Phase 0 仍未通过

反向检查：同一入口以 -Gate phase0 运行，exit 1，准确报告 Git 未初始化和 scripts/gates/phase0.ps1 缺失。

## Audit / preflight / 2026-09-01T21:48:25+08:00

- Commit: not available（本地 Git 尚未初始化）
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build configuration: not applicable；应用工程尚未建立
- Hardware/runtime identity: 两个本地文件的 size/SHA-256/signature 校验通过；本次文档审查未冒充 P0.1 的 GPU/driver probe
- Logs/captures: 命令标准输出；无 Phase capture
- Quantitative result: 54/54 baseline checks；九个受保护控制文件 hash、STATE sequence/evidence/bounds、ignore rules 和 binary identity 全部通过；source-snapshot 内存单测为 17 files / 0 false changes / README mutation detected
- Reviewer: not run；这是交接控制面审查，不是 Phase gate
- Open P0/P1: none for the control-plane audit；应用 Phase 0 尚未开始
- Conclusion: 经过清理后的 Loop preflight 可用，仍没有任何 Phase 被标为完成

反向证据：`phase0` exit 1（Git 与 phase0 gate 缺失）；`phase1` exit 1（并命中 requested=1/current=0）；`cycle.completed=80,status=ready` exit 1（state-loop-bounds）；提前解锁 Phase 1 exit 1（state-phase-sequence）。两次临时 STATE 修改均已恢复，随后再次执行正向 preflight。

## Phase 0 / phase0 / 2026-09-02T21:09:09+08:00

- Commit: gate 运行于 639fd48d 工作树之上（含 gate 修复，未提交时运行）；验证内容 checkpoint 在 Reviewer 通过后登记于 STATE.phases[0].commit
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase0`
- Exit code: 0（外层 70 checks；内部 phase0.ps1 61 checks 全过，run-id a5fd6348b3084b44857b1f1ffc96a449，总 308.3s）
- Build configuration: x64-debug（VEYRA_ENABLE_D3D12_DEBUG=ON）与 x64-release（OFF）均通过 scripts/build.ps1 真实 configure+build；probe exe sha256 debug=E75D22D04A9DCAADC31451FD6CA14A90CC86B5D1C2C736016BD2A47FE9EA683F，release=0C1AB463E94588998D8BD1E4E11CAC5DBE6C44A9B97CD05308D6146401C9ED2C（JSON 自报与 gate 独立计算一致）
- Hardware/runtime identity: NVIDIA GeForce RTX 5070 / vendor 0x10DE / LUID 0x00000000:0x000000005D5F2320 / dedicatedVideoMiB 11943 / driver 32.0.16.1656（注册表 nvlddmkm.sys）/ featureLevel 12_2 / OS 10.0.26200；staged nvngx_dlssnr.dll 165840496 / E16B…FC8E / 310.8.0.0 / Valid / NVIDIA Corporation；System32 nvofapi64.dll 32.0.16.1656
- Logs/captures: logs/phase0/a5fd6348b3084b44857b1f1ffc96a449/（probe-debug.log/json、probe-release.log/json）
- Quantitative result: exports 5/5；Debug 窗口 3s/303 帧；**Release 窗口 300s/30002 帧，deviceRemoved=false，reason=0x00000000**；git ignore 7/7 探针通过；git-sensitive-files none tracked；D3D12 debug layer 在本机不可用（0x887A002D，INBOX 已记录，非 Phase 0 门禁项）
- Reviewer: PASS（2026-09-02，新上下文只读子 Agent；独立重跑 preflight exit 0 / phase0 exit 0，自建 run-id a55d5fb41b164abc88fc2760f0b635ec，300s/30003 帧；BASE_COMMIT 2086282 经 cat-file 验证；控制面九文件 diff 为零；runId/exeSha256 防陈旧机制验证有效；4 条 P2 加固建议、无 P0/P1；工作树指纹前后一致 e69de29b）
- Open P0/P1: none
- Conclusion: Playbook §16 Phase 0 全部门槛机器验证通过且经独立 Reviewer 复核；Phase 0 关闭，进入 Phase 1

迭代记录：第 1 次运行 exit 1（runtime:fileversion 区域逗号格式）；第 2 次 exit 1（-Clean:$false 的 SwitchParameter 字符串转换）；第 3 次 exit 1（不存在目录的 ignore 匹配）；第 4 次 exit 0。三次修复均未降低任何阈值。

## Goal Cycle 001 / preflight / 2026-09-02T20:23:27+08:00

- Commit: not available（本地 Git 尚未初始化，属 P0.2 范围）
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build configuration: not applicable；应用工程尚未建立（P0.4 范围）
- Hardware/runtime identity: nvngx_dlssnr.dll size/SHA256/Valid NVIDIA 签名通过；renodx add-on size/SHA256/NotSigned 通过；`nvidia-smi` 实测 NVIDIA GeForce RTX 5070 / 616.56 / 12227 MiB / compute 12.0；System32 nvofapi64.dll 32.0.16.1656；Windows 10.0.26200.9168
- Logs/captures: 命令标准输出（记录于 loop/JOURNAL.md Cycle 001）；无 Phase capture
- Quantitative result: 54/54 checks 通过，0 失败；工具链实测 MSVC 14.44.35207（cl 19.44.35226）、CMake 3.31.6-msvc6、Ninja 1.12.1、DXC 1.8-1.8.2502.11、Git 2.53.0.windows.2，全部与 Playbook §1.4 记录一致
- Reviewer: not run；preflight 不是 Phase gate
- Open P0/P1: none
- Conclusion: P0.1 验收成立——Goal Agent 亲自采集的 preflight + 工具链/GPU/driver 证据取代交接基线；Phase 0 仍未通过（P0.2–P0.9 未完成）

## Fast-track control rebaseline / preflight / 2026-09-03T15:59:14+08:00

- Commit: not yet checkpointed; working tree based on `7b85752`
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build configuration: x64-release configure/build also executed, exit 0 (`ninja: no work to do`)
- Hardware/runtime identity: root `nvngx_dlssnr.dll` size/hash/Valid NVIDIA signature all rechecked by preflight; RenoDX add-on size/hash/NotSigned identity rechecked; no addon loaded
- Logs/captures: command output only; this is control-plane evidence, not a Phase 5 runtime gate
- Quantitative result: 68/68 preflight checks; protected control set is exactly 10 files; final manifest hash `332DB6AA1D5F109F8D6CA729985F2110FAB5AD57C086B155D5BBFDE0015D7A75`; STATE ledger 0–7 valid; sensitive Git paths none tracked
- Adversarial consistency pass: fixed the sole SR/Guidance graph-order contradiction; Product Spec, Playbook and competitor audit now all require `ingress -> optional SR(Zero) -> post-SR Guidance -> Feature 18 -> FG`
- Local DLSSG source identity recorded: SDK 310.7.0 `nvngx_dlssg.dll`, 7,519,856 bytes, SHA256 `135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F`, valid NVIDIA signature; not yet staged or executed
- Reviewer: not run; P5.0 is a user-authorized control rebaseline, not the Phase 5 completion gate
- Open P0/P1: old `scripts/gates/phase5.ps1` is obsolete and must be replaced before Phase 5 can pass; recorded as next backlog task
- Conclusion: Fast-track control plane is internally consistent and fail-closed; no new Feature 18/NVOF/DAV2/DLSSG/Capture/Export success is claimed

## Launch V1.2 4K control rebaseline / preflight / 2026-09-03T16:53:03+08:00

- Commit: not checkpointed; working tree based on `7b85752`
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build: x64-release configure/build exit 0 (`ninja: no work to do`)
- Quantitative result: 70/70 preflight checks; protected control set exactly 10 files; manifest SHA256 `77A9414588B0EF09156CB97EDF3618AF36D63DF12C5E2E3F8E15743292B96921`; cycle limit 24/120; four external/distribution blockers recorded
- New invariants: native 4K Player/Capture/Export; A/B and A/B/C windows bounded at 2/3; lookaheadFrames is not fabricated latency; D3D12 NVENC H.264+HEVC; release-candidate/distribution-blocked consistency checks
- Runtime scope: control-plane only. Native-4K Feature 18/NVOF/DAV2/DLSSG/Capture/NVENC were not executed and are not marked passed
- Reviewer: not run; P5.0b is a user-authorized scope/control rebaseline, not Phase 5 completion
- Conclusion: Launch V1.2 control plane is fail-closed and ready for P5.1

## Phase 6 — DLSSG 2X 与实时 engine (2026-09-04, session partial)

### fg-cap (manual-cap1)
- FG.Available=true (Get(ull)=0x1 Get(i)=0x1), FeatureInitResult=1, NeedsUpdatedDriver=false,
  MultiFrameCountMax=5, MinDriver 520.0; GPU=RTX 5070 driver 32.0.16.1656; HwSchMode 缺省=系统默认。
- logs/phase6-manual/fg-cap.{log,json}

### fg-test 真假证明 (regression-fg / manual-fg3)
- translation: realFrames=60 usableGenerated=59 dup=0 disableFlag=0; minBlendResidual=1.058
  (baseline 1.106, 阈值 0.664) ; maxTrueResidual=0.171 (阈值 0.553); maxMidpointErrorPx=1.27;
  directionCorrect=true; ptsMonotonic=true; generatedMeanLuma=60.2。
- cut: usable=58 (58..58), crossCutGenerated=false, resetIssued=true。
- mvecConventionWinner=pixels-scaled-1-over-w; createResult=0x1; NeverProvided=0xB8。
- 自校准说明: blend 判据不是固定阈值,而是 CPU 渲染精确中间帧做基线,要求 |gen-blend|>0.6×baseline
  且 |gen-truth|<0.5×baseline —— blend 冒充(残差≈0)与重复帧(hash 相等)都会 FAIL。
- logs/phase6-manual/fg-regression.{log,json}, captures 在 fg-truth runs。

### audio-test (audio4)
- eventMode=true 48000Hz buffer=1056; framesPlanned=192000 framesWritten=192000;
  underrunsSteady=0; maxAbsDriftMs=0.015 (LSQ slope n=349, offset range [-10.26,-10.22]ms 为
  IAudioClock 量化); pauseFlushWorks=true。exit=0。
- logs/phase6-manual/audio4.{log,json}

### player probe 场景 (pp-run11 1080p / pp-4k1 4K)
- 1080p: exit=0; playPause/seek(10)/resize=true; maxAvDriftMs=32.0; nrToggle=true fgToggle=true;
  presents=1306 fg=1156 nr=953 sr=953(1080p→4K); normalPathReadback=0;
  mvecSource=zero-motion-fallback(NVOF 帧内被注入层阻断,见 WORKLOG 系统发现)。
- 4K: exit=0; 全 toggle true(SR 1:1 bypass); nr=1080 fg=1068 drift=32.0 maxInFlight=7。
- logs/phase6-manual/pp-run11.{log,json}, pp-4k1.{log,json}

### 耐久(正式 5min×2) — 运行中,完成后补记

### 耐久(2026-09-04 补记)
- 15s 冒烟: 4K30 internal=59.07Hz(fg 505), 4K60 internal=111.87Hz(fg 778) — 引擎时间线正确。
- 5min×2 正式(endurance-full): FG 生成健康(13290+12998, ~44/s), NR 26299, drift 38ms, readback=0,
  但 presents 降到 10.5/7.2Hz 且 WS 增长 4.8GB → FAIL(诚实记录)。
- 泄漏定位矩阵(全部 GRAPH_OFF+PRESENT_OFF 纯路径, pace 采样):
  4K+音频 +150KB/帧; 4K 无音频 +150KB/帧(音频排除); 1080p +16KB/帧(与帧字节成比例);
  解码器每 400 帧整体重建无效; NR/FG/GPU 图/呈现全部排除(decode-only 亦泄漏)。
  → 泄漏在 FFmpeg sw 解码路径的进程内分配,疑与注入层的分配器钩子有关(或该 vcpkg 构建行为)。
- present 上限: 每次被注入层假报 device-removed 的 Present ~17ms → 显示链路上限 ~59Hz;
  4K60 的 120Hz 内部时间线经呈现链路不可达(引擎侧 44+44=128/s 事件已达标)。
- D3D12VA: 打开+池预热(视图前)成功,但帧内解码被拖到 ~8fps → 回退软解(VEYRA_HW_DECODE=1 可复测)。
- logs/phase6-manual/{endurance-full,diag-*,recycle-test}.{log,json}

### Phase 6 组件结论
- 已验证(green): P6.0 gate fail-closed; P6.1 FG capability/create/eval/release; P6.2 FG 真假
  (自校准 blend 基线); P6.4 WASAPI; P6.3 PresentSink(flip-discard+图形呈现+窗口缩放回退);
  P6.5 引擎(有界队列/迟到丢弃/seek 重同步); P6.6 双场景 player probe exit=0。
- 阻塞(red, 系统级): 5min 4K 耐久的内存增长与呈现速率上限; NVOF 帧内引导(DLSSG 以零引导降级,
  JSON mvecSource 显式标注)。

## Phase 6 session 2 证据补录 (2026-09-04)

### AVPacket 泄漏根除(src/media/FFmpegDemuxer.cpp av_packet_unref 显式化)
- FFmpeg-only 40s(VEYRA_GRAPH_OFF+PRESENT_OFF+NR/FG/AUDIO off;每 10s pace 采样):
  4K+音频 536/536/536MB;4K 无音频 537/538MB;1080p 523MB —— 全部平稳(logs/phase6-manual/leak-{avaudio,decode,1080}.log)
- 对照:修复前 4K 5 分钟 WS 增长 4835MB(endurance-full.json)。

### phase6 gate 自检
- gate:self-control-chars :: clean(脚本含 CR/LF/TAB 外控制字符即 FAIL)。
- 修复行:178/179(clip 路径 U+000C)与 182(ffmpeg bin 路径 U+000B/U+0008/'installedd'),字节级修复。

### Present 失败判别矩阵(同一二进制,600 次/轮)
| 配置 | 结果 |
|---|---|
| 裸(窗口+交换链+清屏) | ok=600 |
| +NGX core / +snippet / +capability / +FG create | ok=600 ×4 |
| +SRV 直写可见堆 | ok=0(全 DEVICE_REMOVED,INVALID_CALL,无 DRED) |
| +SRV 直写非可见堆 | ok=600 |
| +UAV / +CBV 直写可见堆 | ok=600 ×2 |
| +SRV 经 staging 复制 | ok=600(复测×2) |
| 同语义探针不同地址(stage13,mask=0..64) | ok=0(复测×2;vsync=1 同败) |
| 引擎(无视图) | 681 presents 0 失败 |
| 引擎+仅 UAV / 仅 raw-SRV | 440/439 次后败于 resize+1s |
| 引擎+任何纹理 SRV | 第 1 次即败 |
| 进程模块扫描 | 仅 NvTelemetry×2 为外来模块 |
日志: logs/phase6-manual/{bare-s*,mtx-s*,r9-*,r13*,b13-*,kn-*,vg-*,bis*,nofeat-*,nopso-*,small-sink}.*

### 状态
- 等待用户 A/B(关闭捕获/覆盖软件后重跑 stage13 探针)→ 决定根因为覆盖软件钩子或驱动缺陷。

## Phase 6 session 3 证据 (2026-09-04)

### descriptor_present_probe(独立、无 NGX/FFmpeg/NVOF/WASAPI;诊断全开)
7 案例 × 3 轮全 PASS(exit 0,presentSucceeded=600,presentFailed=0,removedReason=S_OK,
errMessages=0,corruptionMessages=0):
A bareClear / B nonVisibleSrv / **C directVisibleSrv** / D copiedVisibleSrv / E boundHeapNoDraw /
F sampledSrvDraw / G uavThenSample。logs/phase6-manual/dpp/r{1,2,3}c{A..G}.{log,json}

### 根因实锤
- nr_harness/parity_compare.cpp:543 既有注释 "MipLevels = 1; // 0 is invalid; the debug layer removes
  the device" —— 旧 makeSrv 恰好 MipLevels=0(非法)→ 设备移除在 Present 上显现。全部旧矩阵由此闭环。

### 修复后播放器
- 1080p(fixed-1080):Present FAILED=0;NVOF 305/0;FG 305;NR 331;SR 353;mvecSource=nvof;WS +206MB;
  seek 10/10;resize OK;drift 262ms(超标, pacing 待修)。
- 4K(fixed-4k):Present FAILED=0;NVOF 288/0;FG 288;全部 toggle OK;drift 262ms(同上)。

### 作废更正
- 上一 session 的 stage9/stage13 判别矩阵、"staging workaround 有效"、"驱动 616.56 缺陷/注入层"结论
  全部作废(嵌套括号导致 stage9 未执行,假阳性)。

## Phase 6 session 4 证据 (2026-09-04, 用户指令 s5)

### P0.1 音频验收(A 短测)
- p4b-1080.json: audioUnderruns=0, audioOverruns=0, audioBufferedMsEnd=1007.3,
  audioClockPtsMsEnd=55382.7(60s 片尾一致), audioSeekCount 含 10 seek。
- 旧 decodeUntil 绝对时间比较 bug(16974 overrun)已根除。

### P0.2 drift 分位数(无截断)
- p0-1080.json: driftMinMs=-1049.0 p50=853.2 p95=3776.6 p99=4402.9 max=4502.5,
  latenessSampleCount=565(全部 present 前记录)。
- p4b-1080(NVOF grid-4 后): p50=821.4 p95=2844.6。droppedLatePresents=0。

### P0.4 NVOF S10.5/grid-4/方向
- nvof-dots.json + log: 随机点 +8px(grid-4 SHORT2): dx p50=-32.06(raw=-1026)dy=0;
  像素单位=raw/(32*gridSize)(实测 4 倍因子);方向 current->previous(负)。
- caps: nvOFGetCaps 调用成功但 grids 列表返回空、min=max=32(如实记录,待查参数)。

### P0.5 逐 pass GPU(串行 fence+QPC,VEYRA_GPU_TS=1)
- ts-1080.log gpu-pass-ms: upload 0.01 | yuv 0.00 | sr 0.00 | encode 0.00 |
  nvof_call 0.18 | nr 2.34/2.75 | decode_blit 24.68/26.01 | fg 2.37/2.70 (p50/p95, ms)

### P0.3/P0.6
- PresentItem 资源绑定/epoch/fence 在源码(present 用 item.textureSlot;seek++resetEpoch);
- ResizeBuffers:queue-idle fence + 引用释放 + 硬失败(无 DWM 回退)。

## Phase 6 session 8 证据 (s9, 2026-09-05)

### fault-injection(nvof-fault-inject.json,exit 0)
7/7 REJECTED-CLEAN(null-device/inputA/inputB/flowOut/costOut/inFence/outFence 各一例,
均 return false + InvalidArgument + initialized=false,无崩溃,无 NVOF 副作用)。

### InfoQueue 三阶段(iso11-full-L1)
startup: stored-cleared=1 | runtime: stored=3178 retrieved=3178 failures=0 err=0 corr=0
warn=0 info=3178 cap=unlimited saturated=0 | (teardown 阶段因交换链崩溃未达)。
旧缺陷对照:容量 1024 时 failures==stored(两段式检索在 GBV 下全败)——已修。
### message-ID 样本
全部 3178 条为 INFO 级(调试层常规跟踪);ERROR/CORRUPTION=0。代表性 ID 直方图在
JSON diagHistogram(按需读取)。

## s10 — NVOF/GPU/InfoQueue teardown ownership — 2026-09-05T01:45:00+08:00

**任务**:修复并验证 NVOF/GPU/InfoQueue teardown 所有权(用户 session-8 指令 s10-I~V)。

**结果**:全部自然 return;矩阵 8/8;三根因修复(非抑制)。

### 释放顺序(实现)
- in-scope:lastNvofSignal 保存 → stub JSON(INCOMPLETE)→ 停生产者/音频 → ring-wait-idle →
  nvof-out-fence-drain(HRESULT+waitResult 检查,硬失败)→ ring-wait-idle-2 → queue-final-drain
  → NR/FG/SR release → nvof-unregister(纹理存活)→ release-nvof-resources(4 纹理,DLL 加载中)
  → nvof-shutdown(destroy+FreeLibrary)→ nvof-event-close → ngx-params-destroy → iat-shim-restore
  → ngx-core-shutdown → staged 资源显式释放(rtvHeap/passes/guidance/frame/working/upload)。
- scope end:资源自然析构(device 存活)。
- post-scope:sink-shutdown(交换链先于队列;backbuffers→swapchain→window→factory)→
  ring-shutdown → teardown scan → ReportLiveDeviceObjects → final scan → InfoQueue.Reset →
  context-shutdown → demuxer/decoder → final JSON(processCompleted=true 仅自然 return 前)。

### 根因证据
1. NVOF 纹理 FreeLibrary 后 Release SEGV:t10-L0-r2(17:11)"before release-nvof-resources"
   后崩溃;修复后同组标记通过。所有权规则已写入 NvOfSession.h。
2. 0x87D:WER event 1000(exception 0x0000087D,KERNELBASE.dll);PowerShell Start-Process
   实测 TRUE_EXIT=2173(0x87D);SEH 捕获后 InfoQueue 立即扫出触发消息:
   id=921 ID3D12Resource final-release with GPU operations in-flight on Command Queue。
   根因=最后 Present 在最终 fence signal 之后提交,waitIdle 不覆盖。drainQueue 修复后
   L1/L2 三阶段 0 ERROR(修复非抑制;与 NVOF 无关,s9 三方交互结论撤回)。
3. NF 空句柄:NR evaluate seh=0xC0000005(nrHandle==nullptr)→ break 跳过 in-scope teardown
   → return 时析构顺序颠倒 SEGV。toggle 按 nrHandle 门控后 NF 自然 12。

### 矩阵(全部自然 return,禁 ExitProcess)
| run | exit | nvof exec/fail | diag rt/td/fin err |
|---|---|---|---|
| L0-r1/r2 | 12/12 | 450/0 | off |
| L1-r1/r2 | 12/12 | 450-451/0 | 0/0/0 |
| L2-r1/r2 | 12/12 | 427-430/0 | 1764-1772(runtime GBV id=938)/0/0 |
| NF-r1/r2 | 12/12 | 0/0 | off |
- fence drain 每轮 expected==completed==lastSignal waitResult=0;mvecSource=nvof。
- 新 blocker:L2 GBV id=938 GPU_BASED_VALIDATION_DESCRIPTOR_UNINITIALIZED(引擎运行期,
  非 teardown;样本在 JSON diagErrorSamples;verdict 正确 FAIL)。

**原始日志**:logs/phase6-manual/t10/{L0,L1,L2,NF}-{r1,r2}.{log,json};L1-r3.json 为修复前
INCOMPLETE stub 崩溃样本。

## Control rebaseline audit / 2026-09-06T13:50:17+08:00

- Type: read-only implementation/status audit plus user-authorized control-document rebaseline; **not a Phase gate**.
- Pre-change preflight: 70/70, exit 0.
- Current Release build: exit 0, Ninja no work to do.
- Valid retained evidence: Phase 0–4 checkpoints; historical Feature 18/SR/parity/NVOF/DLSSG/WASAPI/Present component artifacts.
- Invalidated conclusion: historical Phase 5 product-level pass. Exact reasons: `src/core/EnhanceGraph.cpp` is metrics-only; old phase5 gate accepts a depth manifest without a provider; its second “4K” endurance run uses the same 1080p source/path.
- Current Phase 6 truth: t10 L0/L1/L2/NF matrices naturally tear down, but every scenario verdict is FAIL. Example `logs/phase6-manual/t10/L0-r2.json`: driftP95=2828.229ms versus 50ms threshold, NVOF execute/fail=450/0, FG=450, readback=0. L2 has 1764–1772 runtime GBV id=938 errors.
- Product artifact check: no `apps/veyra`, CaptureCardSource, ImageExportSink, VideoExportSink or working DAV2 provider; `tools/player_probe/main.cpp` is 3641 lines.
- Worktree check: 31 porcelain entries before the control update and 44 after it; uncommitted Phase 6 code preserved; no reset/clean/checkout performed.
- Dependency correction: `third_party_local/nvidia/Optical_Flow_SDK_5.0.7` exists; Video Codec SDK 13.1 remains absent.
- State action: Phase 5 reopened; Phase 6/7 locked; lastGoodCommit restored to Phase 4 content checkpoint `8cdf1ea2f9b0bb1331f9219a2305cef5efd3c8d8`.
- No new RTX runtime test, phase gate, Reviewer or checkpoint was claimed in this audit.
- Post-rebaseline preflight: 70/70, exit 0; final control manifest hash `45A54E674D32A8055B338C0AE9B77F4A6440D7BC08D4BCF00D27CE8E01D556BF`; phase sequence/state/evidence/Git-pointer checks all passed.

## Repair v2 handoff — 2026-09-08
Current evidence and unresolved review findings: docs/REPAIR_PROGRESS_2026-09-08.md. Latest joint: logs/repair-v2/joint-bb22bfa233ee455ca030ba0eccdf5a86/result.json (15 software cases exit0; final static review NOT passed). Budget ledger: logs/repair-v2/runtime-budget.json, 260.5401069 seconds spent. User requested stop and new-session continuation; no new Phase pass.

## Repair v2 final known-fix delivery 2026-09-08
Joint joint-a077b89fd39348e4a49f4195c9d4e416: 18/18 software checks, 71.0278864s. EXE 1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266. Read-only review_known_fixes PASS in scoped fixes; global needs_review and old Phase fields unchanged. Per-invocation <=300 seconds; cumulative 346.3985650s preserved. Full evidence, failures and performance limits: docs/REPAIR_V2_DELIVERY_2026-09-08.md. No capture or publication.
