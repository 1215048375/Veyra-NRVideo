# Veyra Gate Evidence

这里只登记真实存在的门禁证据。格式固定：

## Phase 1 / phase1 / 2026-09-02T22:20:00+08:00

- Commit: 验证内容 checkpoint 在 Reviewer 通过后登记于 STATE.phases[1].commit；gate 运行时工作树为 a8feb21 + 用户安装 Graphics Tools 的环境变化
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase1`
- Exit code: 0（外层 70 checks；内部 phase1.ps1 55/55，run-id 7a535572145b4d73b25da192adf847ee，9.1s）
- Build configuration: x64-debug（带 D3D12 debug layer，实跑 10 帧零 state error）+ x64-release（300 帧）均经 build.ps1 真实构建
- Hardware/runtime identity: RTX 5070 / 0x10DE / driver 32.0.16.1656 / FL 12_2；staged nvngx_dlssnr.dll 165840496 / E16B…FC8E / Valid / NVIDIA；NGX SDK 310.7 官方 GitHub clone
- Logs/captures: logs/phase1/7a535572145b4d73b25da192adf847ee/（harness-release.{log,json}、harness-debug.{log,json}、captures/）
- Quantitative result: **Release 300/300 Evaluate 成功 0 失败**；output meanLuma=0.49425 min=0.05098 max=0.94397 stddev≈0.327 allZero=false constant=false；baseline=1BD84180… style1=B5294CB6… intensity05=8A621726…（三 hash 互异）；GPU timestamps nonZero=true avgMs=6.32；teardown release=0x1 snippetShutdown=0x1；deviceRemoved=false；**Debug run debugLayer=true 且日志 0 条 state/descriptor 错误**
- Reviewer: not run yet（gate 首次全绿后立即安排）
- Open P0/P1: none known
- Conclusion: Playbook §16 Phase 1 全部门槛（Init_Ext/Create 成功、300/300、非黑非恒定、参数可变 hash、GPU 计时、干净释放、debug layer 无 state error、capture 存在）机器验证通过

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
