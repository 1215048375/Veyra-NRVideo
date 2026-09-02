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

## Cycle 002 — P0.2 本地 Git 初始化

### Before

- Phase: 0
- 唯一任务: 按 LOOP_ENGINE §6 固定顺序初始化本地 Git：ignore 探针 → 显式 allowlist staging → baseline commit → agent/veyra-v1-loop 分支 → baselineCommit 状态指针。
- 可证伪假设: 若任一敏感路径未被 ignore、staged 列表出现专有文件、或 preflight 的 state-git-links 检查失败，则 Git 初始化不合规并回退修正。
- 预计修改文件: .git/（新建）、loop/STATE.json（baselineCommit 指针）、loop/JOURNAL.md、loop/BACKLOG.md。
- 快速检查命令: `git check-ignore -v --no-index -- <8 个敏感路径>`；`git diff --cached --name-only`；`scripts/loop-gate.ps1 -Gate preflight`。
- 预期新增证据: baselineCommit 真实 hash；preflight 通过且含 git-repository/state-git-links/git-sensitive-files/git-effective-ignore 全绿。

### After

- 实际修改: `git init -b main`；repo-local 身份 Veyra Goal Agent / veyra-agent@local.invalid（未动 global config）；显式 add 17 个控制面/状态文档文件；`chore: establish Veyra baseline` = 2086282478ae5d1e07e8162d47148633b59882fe；切 agent/veyra-v1-loop；`loop: record baseline commit` = 09abf5d451e11cf7c167a9599392b50152da8666；STATE.baselineCommit 已写入。
- 实际命令与 exit code:
  - `git check-ignore -v --no-index -- nvngx_dlssnr.dll renodx-dlss5-1.addon64 runtime_local/.veyra-ignore-probe third_party_local/.veyra-ignore-probe reference_local/.veyra-ignore-probe captures/.veyra-ignore-probe logs/.veyra-ignore-probe loop/STOP` → exit 0，8/8 全部命中 .gitignore 规则。
  - `git diff --cached --name-only` → 恰好 17 个已审查文件，无任何专有二进制/local 目录。
  - `scripts/loop-gate.ps1 -Gate preflight`（Git 就位后）→ exit 0，66/66 checks；新增 git-repository、state-git-links（baseline 为 HEAD 祖先）、git-sensitive-files=none tracked、8× git-effective-ignore、git-diff-check 全部通过。
- 新证据/日志路径: Git 对象库（.git）；EVIDENCE 未新增 Phase 条目（preflight 非 Phase gate）。
- 失败 fingerprint: 无（全部命令 exit 0）。
- 本 fingerprint 第几次不同尝试: 不适用。
- 是否有进展，依据: 是——仓库从"不存在"变为有真实 baseline 与 loop 分支，且所有专有文件被验证不可进入版本控制。
- STATE/BACKLOG 更新: STATE.baselineCommit=2086282…、cycle.completed=2、currentTask/nextAction 指向 P0.3；BACKLOG P0.2 → DONE。
- 下一唯一动作: P0.3 创建 fail-closed 的 scripts/gates/phase0.ps1，编码 Playbook Phase 0 门槛，并验证它在当前空工程状态下 exit 1。

---

## Cycle 003 — P0.3 fail-closed phase0 gate

### Before

- Phase: 0
- 唯一任务: 创建 scripts/gates/phase0.ps1，把 Playbook §16 Phase 0 完成门槛（Debug/Release 构建、5 分钟窗口无 device removed、runtime hash/签名与 manifest 一致、proprietary ignored、真实 probe 输出解析）编码为确定性检查；验证它在当前无工程状态下 exit 1。
- 可证伪假设: 若 gate 在 CMakeLists.txt/src/probe 不存在时 exit 0，则它是假 gate，必须重写；预期现在 exit 1。
- 预计修改文件: scripts/gates/phase0.ps1（新增）、loop/JOURNAL.md、loop/STATE.json、loop/BACKLOG.md。
- 快速检查命令: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase0` → 预期 exit 1 且首个失败项为缺失工程输入。
- 预期新增证据: gate 脚本存在且 AST 可解析；负向 exit 1 记录。

### After

- 实际修改: 新增 scripts/gates/phase0.ps1（约 300 行，ASCII-only 避免 PS 5.1 代码页问题）。
- 实际命令与 exit code:
  - AST 解析 phase0.ps1 → 无错误（AST-CLEAN）。
  - `scripts/loop-gate.ps1 -Gate phase0` → exit 1；phase-gate:phase0 exitCode=1，内部 5/5 检查失败：input:CMakeLists.txt、CMakePresets.json、scripts/build.ps1、scripts/stage-runtime.ps1、tools/runtime_probe/main.cpp 全部缺失；phase-gate-no-project-mutation 通过（未改任何非忽略文件）；phase-gate-current-state 通过（requested=0 current=0）。
- 新证据/日志路径: gate 设计契约——解析 probe 输出的 JSON（runId/exeSha256/adapter/driver/featureLevel/commandSlots=4/runtime identity/5 exports/nvofapi/experimentalDlssnr/windowLoop≥299s 且 deviceRemoved=false）；Release 跑 300 秒窗口，Debug 跑 3 秒冒烟。
- 失败 fingerprint: phase0|loop-gate -Gate phase0|1|missing project inputs —— 这是预期的 fail-closed 证明，不是待修复缺陷。
- 本 fingerprint 第几次不同尝试: 不适用（负向验收）。
- 是否有进展，依据: 是——Phase 0 门槛从"文字"变为"机器可验证的 fail-closed 检查"，且证明当前空工程无法通过。
- STATE/BACKLOG 更新: cycle.completed=3、currentTask/nextAction 指向 P0.4；BACKLOG P0.3 → DONE。
- 下一唯一动作: P0.4 建立最小 CMake/C++20 Win32 x64 工程（CMakeLists.txt、CMakePresets.json、scripts/build.ps1、scripts/stage-runtime.ps1、src 骨架），不含 FFmpeg/UI。

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
