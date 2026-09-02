# Veyra Gate Evidence

这里只登记真实存在的门禁证据。格式固定：

## Phase N / Gate name / timestamp

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
