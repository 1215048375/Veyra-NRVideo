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

## Cycle 004 — P0.4 最小 CMake/C++20 工程

### Before

- Phase: 0
- 唯一任务: 建立最小 CMake/C++20 Win32 x64 工程与 Debug/Release presets（Ninja 生成器、out/build 二进制目录），scripts/build.ps1（vswhere/vcvars 解析机器路径，避免提交绝对路径）与 scripts/stage-runtime.ps1（复制 DLL + 写 manifest + 持久 ngx-local.json）；probe 以最小 stub main 验证可构建可运行。
- 可证伪假设: 若 presets 含机器绝对路径、或 Debug/Release 任一配置失败、或 build.ps1 在普通 shell（无 VS env）不可用，则工程骨架不合规。
- 预计修改文件: CMakeLists.txt、CMakePresets.json、cmake/VeyraWarnings.cmake、scripts/build.ps1、scripts/stage-runtime.ps1、tools/runtime_probe/main.cpp。
- 快速检查命令: `powershell -File scripts/build.ps1 -Preset x64-debug` 与 `-Preset x64-release` 均 exit 0；`out/build/x64-*/veyra_runtime_probe.exe` 可运行；`scripts/stage-runtime.ps1` 后 runtime_local/nvidia DLL+manifest 就位。
- 预期新增证据: 两 preset 构建成功 + stub 运行 exit 0 + staged runtime 与 manifest。

### After

- 实际修改: 新增 CMakeLists.txt（C++20、MSVC-only 校验、按阶段增长的 target）、CMakePresets.json（Ninja、binaryDir=out/build/x64-*、debug 层 debug=ON/release=OFF、无机器路径）、cmake/VeyraWarnings.cmake（/W4 /permissive- /Zc:__cplusplus /WX 仅本项目 target）、scripts/build.ps1（vswhere→vcvars64→cmake preset，批处理写在 ignored out/ 下）、scripts/stage-runtime.ps1（固定身份校验后复制 DLL、写 manifest、一次性持久 ngx-local.json GUID）、tools/runtime_probe/main.cpp（stub）。
- 实际命令与 exit code:
  - `scripts/stage-runtime.ps1` → exit 0：DLL 复制至 runtime_local/nvidia、runtime-manifest.json 写入、持久 ngx-local.json 创建。
  - `scripts/build.ps1 -Preset x64-debug` → exit 0（[2/2] 编译+链接 veyra_runtime_probe.exe）。
  - `scripts/build.ps1 -Preset x64-release` → exit 0。
  - `out/build/x64-debug/veyra_runtime_probe.exe --self-check` → exit 0（stub 输出 argc/argv）。
- 新证据/日志路径: out/build/x64-{debug,release}/（ignored）；runtime_local/nvidia/{nvngx_dlssnr.dll,runtime-manifest.json}（ignored）。
- 失败 fingerprint: 无。
- 本 fingerprint 第几次不同尝试: 不适用。
- 是否有进展，依据: 是——工程从零变为 Debug/Release 双配置可构建可运行的最小骨架，且专有 runtime 已按 Playbook 4.3/4.4 staging。
- STATE/BACKLOG 更新: cycle.completed=4、currentTask/nextAction 指向 P0.5；BACKLOG P0.4 → DONE。
- 下一唯一动作: P0.5 实现 veyra_base：结构化 logger、HRESULT/NGX result 字符串、文件 size/hash/signature 检查，并用 probe --self-test 实测。

---

## Cycle 005 — P0.5 veyra_base：logger/result 字符串/文件身份

### Before

- Phase: 0
- 唯一任务: 实现 veyra_base 静态库（结构化 logger、Status/HRESULT/NGX result 字符串、文件 size/SHA256/version/Authenticode 签名检查），probe 增加 --self-test 模式实测这些功能。
- 可证伪假设: 若 SHA256/WinTrust/版本提取任一实现有误，--self-test 对 staged nvngx_dlssnr.dll 的固定身份（165840496/E16B…/Valid/NVIDIA）校验将失败并 exit 非零。
- 预计修改文件: include/veyra/{Log.h,Result.h,NgxResult.h,FileIdentity.h}、src/base/{Log.cpp,NgxResult.cpp,FileIdentity.cpp}、CMakeLists.txt（+veyra_base target）、tools/runtime_probe/main.cpp（--self-test）。
- 快速检查命令: `scripts/build.ps1 -Preset x64-debug` → 0；`veyra_runtime_probe.exe --self-test --runtime-dir <abs runtime_local/nvidia>` → 0。
- 预期新增证据: self-test 真实输出 exe/DLL 的 SHA256、版本、签名链；staged DLL 与固定常量全匹配。

### After

- 实际修改: 新增 include/veyra/{Log.h,Result.h,NgxResult.h,FileIdentity.h}、src/base/{Log.cpp,NgxResult.cpp,FileIdentity.cpp}；CMakeLists 增加 veyra_base 静态库（bcrypt/wintrust/crypt32/version）；main.cpp 增加 --self-test。
- 实际命令与 exit code:
  - `scripts/build.ps1 -Preset x64-debug` → 前两次 exit 6：C4838（DXGI 字面量 unsigned→long 收缩）、WinVerifyTrust 形参需非 const GUID*、匿名命名空间内 log:: 未限定、wchar→char range 构造 C4244；逐一修复后 exit 0。
  - `scripts/build.ps1 -Preset x64-release` → exit 0。
  - `veyra_runtime_probe.exe --self-test --runtime-dir <abs>` → exit 0，输出：result-strings ok（S_OK / DXGI_ERROR_DEVICE_REMOVED / NVSDK_NGX_Result_Success）；exe 自身 identity（1235456 字节，sha256=75F9F06F…，签名 Invalid(2148204800)=TRUST_E_NOSIGNATURE，符合未签名自编程序预期）；staged DLL identity 全匹配（165840496 / E16B…FC8E / 310.8.0.0 / Valid / NVIDIA Corporation）。
  - 教训记录：一次构建失败后误跑了旧 stub exe 并显示 exit 0——陈旧产物陷阱，phase0 gate 的 runId+exeSha256 契约正是为此设计。
- 新证据/日志路径: self-test 控制台输出（已记录于上）；无新 capture。
- 失败 fingerprint: phase0|build x64-debug|6|C4838/WinVerifyTrust const/log 未限定/C4244 —— 4 个不同编译错误在同一指纹序列内全部修复。
- 本 fingerprint 第几次不同尝试: 每个错误独立修复；当前无遗留。
- 是否有进展，依据: 是——veyra_base 的 hash/签名/版本/日志/字符串功能全部经真实运行验证，DLL 身份校验与 preflight 的 PowerShell 结果一致。
- STATE/BACKLOG 更新: cycle.completed=5、currentTask 指向 P0.6；BACKLOG P0.5 → DONE。
- 下一唯一动作: P0.6 veyra_gfx：D3D12DeviceContext（RTX adapter、device、direct queue、debug layer、4-slot allocator/list/fence/event ring）+ probe --device-info 实测。

---

## Cycle 006 — P0.6 veyra_gfx D3D12DeviceContext

### Before

- Phase: 0
- 唯一任务: 实现 veyra_gfx（RTX adapter 枚举/筛选、D3D12 device ≥12_0、direct queue、fence/event、4-slot allocator+list ring + timestamp query heap），probe 增加 --device-info 在真实 GPU 上实测并 exercise ring。
- 可证伪假设: 若 adapter 筛选、FL、queue/fence 或 slot ring 有误，--device-info 将失败或报告非 NVIDIA adapter；成功时日志必须显示 RTX 5070/0x10DE/12_x/4 slots。
- 预计修改文件: include/veyra/gfx/{D3D12DeviceContext.h,CommandSlotRing.h}、src/gfx/{D3D12DeviceContext.cpp,CommandSlotRing.cpp}、CMakeLists.txt（+veyra_gfx）、tools/runtime_probe/main.cpp（--device-info）。
- 快速检查命令: `scripts/build.ps1 -Preset x64-debug` → 0；`veyra_runtime_probe.exe --device-info` → 0 且日志含 NVIDIA adapter 与 slot ring exercise。
- 预期新增证据: 真实 D3D12 device 初始化日志（adapter/driver/FL/LUID/VRAM、debug layer 状态、fence signal/wait）。

### After

- 实际修改: 新增 include/veyra/gfx/{D3D12DeviceContext.h,CommandSlotRing.h}、src/gfx/{D3D12DeviceContext.cpp,CommandSlotRing.cpp}；CMakeLists 增加 veyra_gfx（d3d12/dxgi/dxguid）；main.cpp 增加 --device-info/--debug-layer。
- 实际命令与 exit code:
  - `scripts/build.ps1 -Preset x64-debug/-release` → 修复 3 轮编译错误（adapter_ 成员重名/ComPtr 冲突、DXGI_ADAPTER_DESC3 不在 IDXGIAdapter1 上、queue_->Signal 需 .Get()）后双双 exit 0。
  - `veyra_runtime_probe.exe --device-info [--debug-layer]` → exit 0（PASS）。
- 新证据/日志路径: probe 输出：adapter[0] NVIDIA GeForce RTX 5070 vendorId=0x10DE software=false dedicatedVideoMiB=11943；featureLevelMax=12_2；device context driver=32.0.16.1656 (registry/nvlddmkm.sys)；4-slot ring acquire→timestamp pair→submit→signal(fenceValue 1..4)→waitIdle 全通过；timestamp heap 8 indices，frequency=1000000000 Hz。
- 失败 fingerprint: 无遗留。调试期三个修正：DXGI adapter desc 无 Version 字段（改用注册表 ImagePath→DriverStore nvlddmkm.sys 文件版本）；`\SystemRoot` 前缀不被 ExpandEnvironmentStringsW 展开（手动替换）；DisplayVersion 值本驱动不存在。
- 本 fingerprint 第几次不同尝试: 不适用。
- 是否有进展，依据: 是——真实 RTX 5070 上完成 device/queue/fence/4-slot ring 的创建、提交、timestamp query 与同步，全部有日志。
- 附加发现: D3D12 debug layer 未安装（0x887A002D，需 Windows Graphics Tools 可选功能），已记 INBOX；不阻塞 Phase 0。
- STATE/BACKLOG 更新: cycle.completed=6、currentTask 指向 P0.7；BACKLOG P0.6 → DONE。
- 下一唯一动作: P0.7 完整 probe：受限绝对路径 LoadLibraryExW 加载 staged DLL、5 exports 验证、System32 nvofapi64 探测、Win32 窗口+flip swapchain 循环、JSON+log 输出。

---

## Cycle 007 — P0.7 完整 runtime probe

### Before

- Phase: 0
- 唯一任务: 实现 veyra_runtime_probe 完整模式：--runtime-dir 绝对路径校验、staged DLL 身份核验、LoadLibraryExW(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|SYSTEM32) 加载、5 个 required exports 逐项 GetProcAddress、System32 nvofapi64 版本探测、固定尺寸 Win32 窗口 + flip-model swapchain + 4-slot ring 每帧 clear/present 循环、结构化 log 文件与 JSON summary 输出（含 runId/exeSha256 防陈旧产物）。
- 可证伪假设: 若 DLL 加载失败、任一 export 缺失、窗口循环 device removed、或 JSON 字段与 gate 契约不符，probe 将非零退出且不写 JSON。
- 预计修改文件: tools/runtime_probe/main.cpp（完整重写）、CMakeLists.txt（VEYRA_ENABLE_D3D12_DEBUG→编译宏）。
- 快速检查命令: `scripts/build.ps1 -Preset x64-debug` → 0；`veyra_runtime_probe.exe --runtime-dir <abs> --window-seconds 3 --run-id test --log-file logs/tmp/probe.log --json-file logs/tmp/probe.json` → exit 0 且 JSON 字段齐全。
- 预期新增证据: 短窗口真实运行：exports 5/5、窗口 N 秒 M 帧、deviceRemoved=false、JSON 完整。

### After

- 实际修改: tools/runtime_probe/main.cpp 完整实现（约 700 行）；CMakeLists 把 VEYRA_ENABLE_D3D12_DEBUG 管道为 VEYRA_D3D12_DEBUG 编译宏。
- 实际命令与 exit code:
  - `scripts/build.ps1 -Preset x64-debug` → 修复 2 轮（GetCurrentBackBufferIndex 需 IDXGISwapChain3 + CreateSwapChainForHwnd 需先拿 swapchain1 再 QI）后 exit 0；x64-release exit 0。
  - `veyra_runtime_probe.exe --runtime-dir <abs> --window-seconds 3 --run-id smoke-test-001 --log-file logs/tmp/probe-smoke.log --json-file logs/tmp/probe-smoke.json` → exit 0（PASS）。
  - JSON 由 PowerShell ConvertFrom-Json 解析验证（JSON-PARSE-OK）。
- 新证据/日志路径: logs/tmp/probe-smoke.{log,json}（ignored）；关键实测值——LoadLibraryExW(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|SYSTEM32) 成功；exports 5/5（含真实地址 0x7FF9…）；nvofapi64 present=true version=32.0.16.1656（System32）；RTX 5070/0x10DE/FL 12_2/driver 32.0.16.1656；窗口循环 3 秒 302 帧 deviceRemoved=false；JSON 含 runId+exeSha256 防陈旧契约。
- 失败 fingerprint: 无遗留。
- 本 fingerprint 第几次不同尝试: 不适用。
- 是否有进展，依据: 是——Phase 0 完整 probe 链（身份→受限加载→exports→nvofapi→device→窗口/swapchain/4-slot 循环→清理→JSON）端到端真实运行成功。
- STATE/BACKLOG 更新: cycle.completed=7、currentTask 指向 P0.8；BACKLOG P0.7 → DONE。
- 下一唯一动作: P0.8 用 scripts/loop-gate.ps1 -Gate phase0 实跑（含 Debug 3s + Release 300s 窗口），登记 EVIDENCE。

---

## Cycle 008 — P0.8 phase0 gate 实跑

### Before

- Phase: 0
- 唯一任务: 通过统一入口 `scripts/loop-gate.ps1 -Gate phase0` 实跑 P0.3 建立的门禁：双 preset 重建 + Debug probe（3s 窗口）+ Release probe（300s 窗口）+ 全部阈值解析 + Git ignore 检查；exit 0 后把证据登记 EVIDENCE。
- 可证伪假设: 若任一阈值不达标（窗口不足 299s、framesPresented=0、身份不匹配、exports 缺失、git 跟踪了敏感文件），gate exit 1。
- 预计修改文件: loop/JOURNAL.md、loop/EVIDENCE.md、loop/STATE.json、loop/BACKLOG.md。
- 快速检查命令: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase0` → 预期 exit 0。
- 预期新增证据: gate 全绿输出 + logs/phase0/<runId>/ 四个 artifact（debug/release 的 log+json）。

### After

- 实际修改: scripts/gates/phase0.ps1 三处修复（FileVersionRaw 取代区域敏感 FileVersion；`-File` 调用下移除 `-Clean:$false` 字符串转换问题；目录 ignore 探测改为目录内探针文件）+ 本 Journal/EVIDENCE/WORKLOG/STATE/BACKLOG 更新。
- 实际命令与 exit code:
  - 第 1 次 `loop-gate.ps1 -Gate phase0` → exit 1：gate 内 19 项中 runtime:fileversion 失败（PowerShell 区域格式返回 "310,8,0,0"）。
  - 第 2 次 → exit 1：build:x64-debug/release 双失败——`-File` 下 `-Clean:$false` 以字符串 "$false" 传给 SwitchParameter 无法转换。
  - 第 3 次 → exit 1：构建与双 probe 全过，但 git-ignore:third_party_local/reference_local/captures 失败——目录尚不存在时带斜杠目录模式对裸路径不匹配。
  - 第 4 次 → **exit 0：VEYRA GATE PASSED: phase0 (70 checks)**，内部 gate 61 检查全过，run-id a5fd6348b3084b44857b1f1ffc96a449，总耗时 308.3s。
- 新证据/日志路径: logs/phase0/a5fd6348b3084b44857b1f1ffc96a449/{probe-debug.log,probe-debug.json,probe-release.log,probe-release.json}。关键数字——Debug：3 秒 303 帧；Release：**300 秒 30002 帧，deviceRemoved=false，reason=0x00000000**；exports 5/5；exe sha256 debug=E75D22D0…、release=0C1AB463…。
- 失败 fingerprint: phase0|loop-gate -Gate phase0|1|区域格式/SwitchParameter 转换/不存在目录的 ignore 匹配——3 个不同缺陷依次修复，每个都有新证据。
- 本 fingerprint 第几次不同尝试: 第 4 次运行通过；3 次失败原因互不相同（未触发同指纹 3 次上限）。
- 是否有进展，依据: 是——Phase 0 的机器验收门禁首次真实 exit 0，包含 5 分钟窗口循环无 device removed。
- STATE/BACKLOG 更新: phases[0]=gate_passed + gateEvidence；cycle.completed=8；BACKLOG P0.8 → DONE。
- 下一唯一动作: P0.9 记录 Reviewer 前指纹，按 REVIEW_PROMPT 启动只读 Reviewer 子 Agent。

---

## Cycle 009 — P0.9 只读 Reviewer 与 Phase 0 checkpoint

### Before

- Phase: 0
- 唯一任务: 按 REVIEW_PROMPT 启动新上下文只读 Reviewer（N=0、BASE_COMMIT=2086282…、GATE_EVIDENCE=loop/EVIDENCE.md#phase-0--phase0--2026-09-02t210909-0800）；修复全部 P0/P1；做验证内容 checkpoint commit 并解锁 Phase 1。
- 可证伪假设: 若 Reviewer 独立重跑 gate 失败、发现 P0/P1、或工作树指纹被 Reviewer 改变，则 Phase 0 不能关账。
- 预计修改文件: loop/{JOURNAL,EVIDENCE,STATE,BACKLOG}.md、docs/WORKLOG.md、Git 提交。
- 快速检查命令: Reviewer 输出 GATE_EXIT 0/0 且 VERDICT PASS；`git status --porcelain` 在 Reviewer 前后一致。
- 预期新增证据: Reviewer 独立 run-id 与结论；phase-N checkpoint 真实 hash 写入 STATE。

### After

- 实际修改: 本 Journal/EVIDENCE/BACKLOG/WORKLOG/STATE 更新 + 两个 checkpoint 提交。
- 实际命令与 exit code: Reviewer（Explore 子 Agent，只读）独立执行 preflight（exit 0，66 checks）、phase0（exit 0，外层 70/内层 61，自建 run-id a55d5fb41b164abc88fc2760f0b635ec，308.3s，300s/30003 帧）、BASE_COMMIT cat-file 验证、diff 审查、git status；输出 VERDICT PASS。
- 新证据/日志路径: logs/phase0/a55d5fb41b164abc88fc2760f0b635ec/（Reviewer run）；工作树指纹 Reviewer 前后均为 e69de29b（未变异）。
- 失败 fingerprint: 无。
- 本 fingerprint 第几次不同尝试: 不适用。
- Reviewer P2 结论（不阻塞，按 LOOP_ENGINE 处理）:
  1. D3D12DeviceContext::nextFenceValue_ 与 CommandSlotRing::nextFenceValue_ 双计数器并存——Phase 0 无冲突（仅 ring 发 Signal），Phase 1 Evaluate 循环前必须收敛为单一 owner → 已加入 BACKLOG Phase 1 子任务 P1.2a。
  2. phase0 gate 的 windowLoop 未设帧率下限（建议 frames ≥ duration×30）→ 记录在案；Phase 0 已关账，若未来重跑该 gate 再加固。
  3. gate 构建为增量构建，从未 clean 构建（两次独立 run exe hash 一致证明可复现）→ 记录在案。
  4. D3D12 debug layer 缺失（已在 INBOX，Phase 1 前置）。
- 是否有进展，依据: 是——Phase 0 通过 gate + 独立复核，完成本 Goal 第一个完整阶段闭环。
- STATE/BACKLOG 更新: phases[0] → review_passed（随后 checkpoint 后 → passed + commit hash）；phase.id → 1 解锁；BACKLOG P0.9 → DONE + 新增 P1.2a（不降低门槛的加固子任务）。
- 下一唯一动作: Phase 1 P1.1——建立 scripts/gates/phase1.ps1 与确定性 RGBA8 测试帧/输出统计。

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
