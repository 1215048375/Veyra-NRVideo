# Veyra Gate Evidence

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
