# Veyra Loop Journal

本文件记录推理的可审计摘要和实际命令，不记录虚构结果。大日志放 logs/，这里只写路径与摘要。

## Cycle 000 — Loop bootstrap

Phase: 0

Task: 安装无人值守 Loop Engine 的持久协议、目标 Prompt 和预检入口。

Expected evidence: loop 文件存在；preflight 可运行；项目既有实现边界不被改写。

Result:

- 首次 preflight exit 1：Windows PowerShell 5.1 将无 BOM 脚本中的非 ASCII 产品方案路径按本地代码页解码，误报文件缺失；其余 hash/signature/ignore checks 通过。
- 修复：bootstrap 检查改为不依赖源码中的非 ASCII literal；后续完整审查又把产品规格迁移到稳定的 ASCII 路径。
- 2026-09-01T17:07:22+08:00 再次运行 preflight，exit 0。
- 反向运行 phase0，按预期 exit 1：本地 Git 尚未初始化且 scripts/gates/phase0.ps1 尚不存在。证明当前状态不能越过 Phase 0。
- 加入控制面 hash manifest 和完整 STATE ledger 校验后，于 2026-09-01T17:16:32+08:00 再跑 45 项 preflight，exit 0。
- 有进展：预检由真实失败变为通过，未伪造 Phase 0 完成。

## Control-plane audit — 2026-09-01T21:48:25+08:00（不计入 Goal cycle）

- 全量复核产品规格、施工手册、Loop、STATE 和 gate；删除已被否决且无引用的旧方案，保留内容迁移为 `VEYRA_PRODUCT_SPEC_V1.md`。
- 修复 ReShade preset 误判、SR/NVOF 循环依赖、D3D12VA surface 合同、NVOF 输入/重置、DLSSG motion scale 冲突和 caller shim 边界语义。
- 重建九文件控制面 manifest；preflight 54/54，exit 0。
- 反向 `phase0` exit 1：Git/phase0 gate 均不存在；反向 `phase1` exit 1：额外命中 current-phase 约束。
- 临时将 cycle.completed 设为 80 且保持 ready，preflight 命中 state-loop-bounds 并 exit 1；已恢复为 0。
- 临时提前解锁 Phase 1，preflight 命中 state-phase-sequence 并 exit 1；已恢复为 locked。
- 内存单测执行 source snapshot/compare：17 个文件、相同快照变化数 0、模拟 README hash 变化被准确检出；磁盘文件未被该测试修改。
- 未实现应用、未初始化 Git、未调用任何 NGX Feature；Phase 0 仍是唯一可施工阶段。

---

## Cycle 001 — P0.1 preflight 重跑与工具链/GPU probe

### Before

- Phase: 0
- 唯一任务: 重跑 54 项 preflight，实测工具链与 GPU/driver，把证据写入 JOURNAL/STATE，标记 P0.1 完成。
- 可证伪假设: 若任一控制面 hash 漂移、二进制身份不符、GPU/driver 或工具链与 Playbook §1.3/§1.4 不符，preflight/probe 将失败并停工。
- 预计修改文件: loop/JOURNAL.md、loop/STATE.json、loop/BACKLOG.md（仅状态）。
- 快速检查命令: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`；`nvidia-smi --query-gpu=name,driver_version,memory.total,compute_cap --format=csv,noheader`；各工具 `--version`。
- 预期新增证据: preflight exit 0（54/54）；真实 GPU/driver/OS/工具版本记录。

### After

- 实际修改: 仅本 Journal 条目、STATE.json（cycle/currentTask/nextAction/phase state=in_progress）、BACKLOG.md（P0.1 → DONE）。未触碰任何控制面文件。
- 实际命令与 exit code:
  - `scripts/loop-gate.ps1 -Gate preflight` → exit 0，54/54 checks（含 9 控制面 hash、两二进制 size/SHA256/签名、STATE ledger/limits/sequence、gitignore 8 项）。
  - `cmd /c ver` → Windows 10.0.26200.9168。
  - `nvidia-smi --query-gpu=...` → NVIDIA GeForce RTX 5070, 616.56, 12227 MiB, 12.0。
  - System32 `nvofapi64.dll` FileVersion → 32.0.16.1656。
  - `git --version` → 2.53.0.windows.2；`cmake --version` → 3.31.6-msvc6；`ninja --version` → 1.12.1；`dxc --version` → dxcompiler 1.8-1.8.2502.11 / dxil 1.8；MSVC 14.44.35207（cl 19.44.35226）。
- 新证据/日志路径: 本条目即证据；无 GPU capture（Phase 0 probe 程序尚未建立）。
- 失败 fingerprint: 无（全部命令 exit 0）。
- 本 fingerprint 第几次不同尝试: 不适用。
- 是否有进展，依据: 是——P0.1 的验收证据（preflight 0 + 实测工具链/GPU/driver 与 Playbook 一致）由本 Goal Agent 亲自采集，取代交接文档中的基线记录。
- STATE/BACKLOG 更新: STATE.cycle.completed=1，phase 0 → in_progress，currentTask/nextAction 指向 P0.2；BACKLOG P0.1 → DONE。
- 下一唯一动作: P0.2 按 LOOP_ENGINE §6 固定顺序初始化本地 Git（先验证 ignore，再显式 allowlist staging、baseline commit、切 agent/veyra-v1-loop）。

---

复制下面模板开始每个新 cycle。必须先填 Before，再改代码；完成后填 After。

## Cycle NNN — 简短任务名

### Before

- Phase:
- 唯一任务:
- 可证伪假设:
- 预计修改文件:
- 快速检查命令:
- 预期新增证据:

### After

- 实际修改:
- 实际命令与 exit code:
- 新证据/日志路径:
- 失败 fingerprint:
- 本 fingerprint 第几次不同尝试:
- 是否有进展，依据:
- STATE/BACKLOG 更新:
- 下一唯一动作:
