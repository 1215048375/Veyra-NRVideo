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

## Cycle 010 — P1.0a fence 时间线单一 owner

### Before

- Phase: 1
- 唯一任务: 落实 Reviewer P2：移除 D3D12DeviceContext 中未使用的 nextFenceValue_/signalNextFenceValue（与 CommandSlotRing 各自从 1 起算的双计数器），在两处头文件明确 CommandSlotRing 是该 fence 时间线的唯一 signaler；重建并回归 device-info。
- 可证伪假设: 若移除影响现有行为，device-info/构建将失败。
- 预计修改文件: include/veyra/gfx/{D3D12DeviceContext.h,CommandSlotRing.h}、src/gfx/D3D12DeviceContext.cpp。
- 快速检查命令: `scripts/build.ps1 -Preset x64-debug` → 0；`veyra_runtime_probe.exe --device-info` → 0。
- 预期新增证据: 双 preset 构建 + device-info 回归通过，fence 计数器只剩 ring 一处。

### After

- 实际修改: D3D12DeviceContext.h 移除 nextFenceValue_/nextFenceValue()/signalNextFenceValue()；两处头文件注明 CommandSlotRing 是共享 fence 时间线唯一 signaler；shutdown 不再依赖已删除计数器（slot 排空由 ring.shutdown 负责）。
- 实际命令与 exit code: `build.ps1 -Preset x64-debug` → 0；`--device-info` → PASS；`-Preset x64-release` → 0。
- 新证据/日志路径: 回归输出（上）。
- 失败 fingerprint: 无。
- 是否有进展，依据: 是——Reviewer P2 落实，fence 计数器唯一化，行为无回归。
- STATE/BACKLOG 更新: cycle.completed=10；BACKLOG P1.0a → DONE。
- 下一唯一动作: P1.1 创建 fail-closed scripts/gates/phase1.ps1 + 确定性 RGBA8 测试帧契约；并行下载 NGX SDK 310.7。

---

## Cycle 011 — P1.1 phase1 gate 与测试帧契约

### Before

- Phase: 1
- 唯一任务: 创建 fail-closed 的 scripts/gates/phase1.ps1，编码 Playbook §16 Phase 1 全部门槛（Init_Ext/Create 成功、300/300 Evaluate、输出非黑非恒定无 NaN、两组 Style/Intensity 变体 hash 不同、GPU timestamp 非 0、Release/Shutdown 干净、Debug run 需 debug layer 开启且无 state error、capture 存在、runId/exeSha256 防陈旧、git ignore），并写 config/nr-default.json；验证当前无 harness 时 exit 1。
- 可证伪假设: 若 gate 在 tools/nr_harness/shaders 不存在时 exit 0，则为假 gate；预期现在 exit 1。
- 预计修改文件: scripts/gates/phase1.ps1、config/nr-default.json、loop/{JOURNAL,STATE,BACKLOG}。
- 快速检查命令: `loop-gate.ps1 -Gate phase1` → 预期 exit 1（缺失 harness 输入）。
- 预期新增证据: gate 存在、AST-clean、负向 exit 1、harness JSON 契约定稿。
- 附注: NGX SDK 310.7 已从官方 GitHub clone 并按 Playbook 契约整理到 third_party_local/nvidia/DLSS_SDK_310.7.0/（include 16 头、x64/nvsdk_ngx_s.lib=36EAB292…、rel/nvngx_dlss.dll=BE6E434A…、nvngx_dlssg.dll=135EAF07…）；全部 ignored。

### After

- 实际修改: 新增 scripts/gates/phase1.ps1（约 240 行，编码 §16 Phase 1 全部门槛）与 config/nr-default.json（Playbook 9.4 nr 段固定默认值）。
- 实际命令与 exit code: AST → AST-CLEAN；`loop-gate.ps1 -Gate phase1` → exit 1（input:GenerateTestPattern.hlsl、tools/nr_harness/main.cpp、src/ngx/NgxCoreHost.cpp、src/ngx/DlssNrRuntimeAdapter.cpp 四项缺失）。
- 新证据/日志路径: harness JSON 契约定稿（gate 内解析字段：probe/runId/exeSha256/initExt/createFeature/evaluate{attempted,succeeded,failed}/output{meanLuma,minLuma,maxLuma,sha256,nanCount,allZero,constant}/variants[].sha256（≥2 个互异且异于 baseline）/gpu.timestampNonZero/release/deviceRemoved/debugLayer；capture frame0000_proxy.png+frame0000_raw.png）。
- 失败 fingerprint: 预期 fail-closed，非缺陷。
- 是否有进展，依据: 是——Phase 1 验收从文字变为机器检查并证明当前无法通过。
- 附加: NGX SDK 310.7 整理完成（见 Before 附注）。
- STATE/BACKLOG 更新: cycle.completed=11；BACKLOG P1.1 → DONE。
- 下一唯一动作: P1.2 实现 veyra_ngx：NgxCoreHost（Init with ProjectID/参数块生命周期）、NgxParameters 强类型封装、DlssNrParameters 常量、DlssNrRuntimeAdapter（Feature 18 常量/受限加载/exports/caller-name 兼容层骨架）与严格逆序 RAII。

---

## Cycle 012 — P1.2 veyra_ngx：core host/参数封装/NR adapter

### Before

- Phase: 1
- 唯一任务: 实现 veyra_ngx 静态库（按官方 310.7 头文件签名）：NgxCoreHost（一进程一次 Init_with_ProjectID、参数块生命周期、Shutdown1，SEH 边界）、NgxParameters 强类型 Set 封装、DlssNrParameters 常量、DlssNrRuntimeAdapter（受限加载+5 exports 解析，Feature 18 常量/AppID 仅在此文件）；CMake 增加 VEYRA_ENABLE_EXPERIMENTAL_DLSSNR 门（缺 SDK 路径即 FATAL_ERROR）；harness stub --load-only 实测 core init→snippet load→exports→逆序释放。修正 NgxResult.cpp 的 Success=0x1（官方头）。
- 可证伪假设: 若 core Init 或 snippet 加载在本机失败，--load-only 非零退出并带真实 result hex。
- 预计修改文件: src/ngx/NgxCoreHost.{h,cpp}（h 在 include/veyra/ngx/）、include/veyra/ngx/{NgxParameters.h,DlssNrParameters.h,DlssNrRuntimeAdapter.h}、src/ngx/DlssNrRuntimeAdapter.cpp、src/base/NgxResult.cpp（修正）、tools/nr_harness/main.cpp（stub）、CMakeLists.txt、scripts/build.ps1（传 SDK 根/实验开关）。
- 快速检查命令: `build.ps1 -Preset x64-debug` → 0；`veyra_nr_harness.exe --load-only --runtime-dir <abs>` → 0（core init success + exports 5/5 + 干净 shutdown）。
- 预期新增证据: 真实 Init_with_ProjectID result（0x1=Success）、snippet exports、Shutdown result。

### After

- 实际修改: include/veyra/ngx/{NgxCoreHost.h,NgxParameters.h,DlssNrParameters.h,DlssNrRuntimeAdapter.h}、src/ngx/{NgxCoreHost.cpp,DlssNrRuntimeAdapter.cpp}、tools/nr_harness/main.cpp（--load-only）、CMakeLists（veyra_ngx 实验开关 + FATAL_ERROR + 按配置选 _dbg/_s 库 + 全工具静态 CRT）、scripts/build.ps1（自动传 SDK 根 + 实验开关）、src/base/NgxResult.cpp（按官方 310.7 头修正 Success=0x1 并补全 20 项 FAIL 表）、tools/runtime_probe/main.cpp 自测改用 0x1。
- 实际命令与 exit code:
  - `build.ps1 -Preset x64-debug -Clean` → 修复 3 轮（头文件需先 d3d12.h 再 nvsdk_ngx.h + 缺 <string>/<windows.h>；`Init_with_ProjectID` 大小写 D；NVIDIA 静态库是 MT 系 → 全工具 CMP0091 静态 CRT + 按配置选库）后 exit 0；x64-release exit 0。
  - `veyra_nr_harness.exe --load-only --runtime-dir <abs>` Debug 与 Release 均 exit 0（PASS）。
- 新证据/日志路径: harness 控制台——**Init_with_ProjectID result=0x1 (NVSDK_NGX_Result_Success)**（本工程首次真实 NGX Core 调用，使用自己的 projectId=8562ea46-…，SEH=0）；snippet 受限加载 exports 5/5；逆序释放：snippet FreeLibrary → core Shutdown1 result=0x1 → device shutdown 全干净。
- 失败 fingerprint: 无遗留（3 个编译期缺陷逐一修复，每个有新证据）。
- 是否有进展，依据: 是——P1.2 验收成立：core/参数封装/adapter/逆序 RAII 全部经真实运行验证。
- STATE/BACKLOG 更新: cycle.completed=12；BACKLOG P1.2 → DONE。
- 下一唯一动作: P1.3 caller-name 兼容层：PE import 表解析 → GetModuleFileNameW IAT slot 单 owner → VirtualProtect+InterlockedExchangePointer 安装 shim → 六项边界测试（nSize==0/过小/恰好 10/正常/nullptr/非 caller）→ 恢复验证。

---

## Cycle 013 — P1.3 caller-name 兼容层与边界测试

### Before

- Phase: 1
- 唯一任务: 实现 DlssNrRuntimeAdapter 的隔离兼容层：解析已加载 snippet 的 PE import 表定位 KERNEL32/API-set 的 GetModuleFileNameW IAT slot（全局单 owner）→ VirtualProtect+InterlockedExchangePointer 安装 shim（仅对本 caller module 返回字面量 L"nvngx.dll"，完整模拟 buffer/return/error 语义；其余全部转发原函数）→ 恢复并 FlushInstructionCache；harness 增加 --shim-test 六项边界测试（nSize==0 / 过小 / 恰好 10 wchar / 正常 / nullptr module / 非 caller module，含 sentinel last-error）。
- 可证伪假设: 若 IAT 定位失败、shim 语义错误或恢复不完整，--shim-test 非零退出。
- 预计修改文件: src/ngx/DlssNrRuntimeAdapter.cpp、include/veyra/ngx/DlssNrRuntimeAdapter.h、tools/nr_harness/main.cpp。
- 快速检查命令: `build.ps1 -Preset x64-debug` → 0；`veyra_nr_harness.exe --shim-test --runtime-dir <abs>` → 0（六项全过 + 恢复验证）。
- 预期新增证据: IAT slot 地址、原指针、安装/恢复日志、六项边界测试结果。

### After

- 实际修改: src/ngx/DlssNrRuntimeAdapter.cpp（PE import 表解析 FindImportedFunctionSlot、SnippetGetModuleFileNameW shim、install/restore 单 owner 逻辑）、头文件 test hooks、tools/nr_harness/main.cpp（--shim-test）。
- 实际命令与 exit code: `build.ps1 -Preset x64-debug` → 0；`--shim-test` Debug 与 Release 均 exit 0，8 项全 PASS。
- 新证据/日志路径: 真实定位 snippet IAT slot=0x00007FF9091DC080，original=0x00007FF9DD79BC00（KERNEL32）；shim-test[PASS] ×8：caller-module-resolved（0x7FF6407D0000）、zero-size（ret=0、buffer 未动、lastError 保持 0x1234）、too-small-truncates（ret=5、"nvng"、lastError=0x7A=ERROR_INSUFFICIENT_BUFFER）、exact-10-wchars（ret=9、"nvngx.dll"、lastError 不变）、roomy-64、null-module-forwards（真实 exe 路径 85 字符）、other-module-forwards（KERNEL32.DLL）、restore-reported-clean + 卸载后"IAT slot restored to original function"。
- 失败 fingerprint: 无。
- 是否有进展，依据: 是——Playbook 8.3 的 10 步实现与全部边界语义经真实 snippet 验证；shim 只影响 snippet 自身 IAT、单 owner、可完全恢复、编译开关可整体移除（VEYRA_ENABLE_EXPERIMENTAL_DLSSNR）。
- STATE/BACKLOG 更新: cycle.completed=13；BACKLOG P1.3 → DONE。
- 下一唯一动作: P1.4 严格按 Playbook 8.5 参数名/类型实现 Create Feature 18（含 ScalingRatioCallback、全部 width/height 变体、node mask），并验证 handle 非空。

---

## Cycle 014 — P1.4 Feature 18 Create

### Before

- Phase: 1
- 唯一任务: adapter 增加五个 snippet 调用的 SEH 包装（Init_Ext/CreateFeature/EvaluateFeature/ReleaseFeature/Shutdown1）；harness --create-test 按 Playbook 8.4 顺序（core Init→load→shim→Init_Ext(AppID 0x0876232C)→AllocateParameters→8.5 全部 Create 参数→CreateFeature(18)→执行并等待一次→handle 非空→逆序清理）；ScalingRatioCallback 固定 1.0。
- 可证伪假设: 若任一参数名/类型/顺序错误，CreateFeature 返回非 Success（如 0xBAD00005 InvalidParameter）或 handle 为空——按 Playbook 18 逐项 dump 排查而不是随机删参数。
- 预计修改文件: include/veyra/ngx/DlssNrRuntimeAdapter.h、src/ngx/DlssNrRuntimeAdapter.cpp、tools/nr_harness/main.cpp。
- 快速检查命令: `build.ps1 -Preset x64-debug` → 0；`veyra_nr_harness.exe --create-test --runtime-dir <abs>` → 0 且 CreateFeature result=0x1、handle!=null。
- 预期新增证据: Init_Ext/Create/Release/Shutdown 全链 result hex + handle 非空日志。

### After

- 实际修改: DlssNrRuntimeAdapter 五个 SEH 包装（Init_Ext/Create/Evaluate/Release/Shutdown1）+ scalingRatioCallback；harness --create-test 按 Playbook 8.4 顺序完整执行。
- 实际命令与 exit code: `build.ps1 -Preset x64-debug` → 0（一轮过，无编译错误）；`--create-test` Debug 与 Release 均 exit 0（PASS）。
- 新证据/日志路径: 关键链路——core Init 0x1 → shim 安装 → **snippet Init_Ext appId=0x876232C result=0x1** → 18 个 Create 参数（8.5 全表：8 个尺寸变体 + Upscaling=0 + Scale/ScalingRatio=1.0 + callback + Preset=0(int) + Width/Height(uint32) + PerfQualityValue=1(int,Balanced) + 两个 node mask(uint32)=1）→ **CreateFeature id=18 result=0x1 handle=non-null** → ReleaseFeature 0x1 → snippet Shutdown1 0x1 → restore shim → FreeLibrary → core Shutdown1 0x1。
- 失败 fingerprint: 无（一次通过）。
- 是否有进展，依据: 是——项目核心可行性在本工程内得到直接证明：signed snippet Feature 18 可用 Playbook 8.5 契约 Create 成功且完整释放。
- STATE/BACKLOG 更新: cycle.completed=14；BACKLOG P1.4 → DONE。
- 下一唯一动作: P1.5 实现 Proxy/Neural/ZeroMotion(R16G16F)/ZeroDepth(R32F)/Confidence(R8) 资源 + GenerateTestPattern.hlsl（DXC 构建期编译）+ 8.6 Evaluate 参数与 4-slot 逐帧执行。

---

## Cycle 015 — P1.5 Proxy→Feature18→Raw 执行链

### Before

- Phase: 1
- 唯一任务: 实现 shaders/GenerateTestPattern.hlsl（确定性 gradient/checker/硬边/RGB bars，随 frameId 平移；cs_6_0，DXC 构建期编译进 out/）；Phase 1 资源（Proxy/Neural RGBA8 UAV、ZeroMotion R16G16F、ZeroDepth R32F、Confidence R8，均 1920x1080）；zero 纹理一次性 UAV clear；compute root signature + descriptor heap；8.6 全部 Evaluate 参数（4 资源 + 16 subrect + 控制项，首帧 Reset=1）；4-slot 逐帧：acquire→pattern dispatch→barriers→Evaluate→UAV barrier→submit；harness 主模式接 --frames N（先小 N 验证）。
- 可证伪假设: 若资源格式/state/subrect/参数类型错误，Evaluate 返回非 0x1（0xBAD00005 等）或 debug 输出 state error——按 Playbook 18 表排查。
- 预计修改文件: shaders/GenerateTestPattern.hlsl、cmake/VeyraShaders.cmake、CMakeLists.txt、tools/nr_harness/main.cpp（主循环 + PNG writer + stats）。
- 快速检查命令: `build.ps1` → 0（含 DXC 编译）；`veyra_nr_harness.exe --runtime-dir <abs> --frames 5` → evaluate 成功 5/5。
- 预期新增证据: 每帧 Evaluate result hex、GPU timestamp、帧数计数。

### After

- 实际修改: shaders/GenerateTestPattern.hlsl（四象限确定性图案，root constant frameId）、cmake/VeyraShaders.cmake（DXC 查找+构建期编译）、tools/nr_harness/frame_loop.{h,cpp}（完整主循环：资源/UAV heap/root signature/PSO/零初始化/逐帧 Evaluate/捕获/统计/最小 PNG writer）、main.cpp 接线、FileIdentity 增加内存 sha256Hex。
- 实际命令与 exit code:
  - `build.ps1 -Preset x64-debug` → 修复 3 轮（DXC genex 引号问题→按 CMAKE_BUILD_TYPE 分支；D3D12_TEXTURE_COPY_LOCATION union 不能聚合多初始化器；**ClearUnorderedAccessViewFloat 段错误**→对照实验证实与纹理格式无关后改为 upload-buffer 复制零初始化）后 exit 0。
  - `veyra_nr_harness.exe --runtime-dir <abs> --frames 5 --capture-frame 0 --capture-dir captures/tmp` → **REAL_EXIT=0，frame-loop: PASS**。
- 新证据/日志路径: logs/tmp/p15e.out、captures/tmp/frame0000_{proxy,raw}.png（8.3MB/张，System.Drawing 验证 1920x1080）。关键数字——**Evaluate 5/5 成功 0 失败**；输出 meanLuma=0.52437、minLuma=0.05043、maxLuma=1.0、stddev=0.34352、allZero=false、constant=false、rawSha=4C3248C8…（≠proxySha=46522DAB…，证明 Feature 18 真实处理）；teardown release/snippetShutdown 双 0x1；deviceRemoved=false。
- 失败 fingerprint: phase1|frame-loop zero-init|139|ClearUnorderedAccessViewFloat crash → 换实现路径修复（upload copy），fingerprint 关闭。
- 本 fingerprint 第几次不同尝试: 2 次（SetDescriptorHeaps 补绑后仍崩 → 换路径成功）。
- 是否有进展，依据: 是——Playbook 8.7 完整帧链（pattern→barriers→全部 8.6 参数→Evaluate→UAV barrier→4-slot 提交→捕获统计）真实运行成功。
- 已知偏差记录: ClearUnorderedAccessViewFloat 在本机（无 debug layer）段错误原因未定（机制问题非格式问题），已用等效 upload-copy 路径替代；待用户装 Graphics Tools 后可复查（记入 INBOX 附注候选）。
- STATE/BACKLOG 更新: cycle.completed=15；BACKLOG P1.5 → DONE。
- 下一唯一动作: P1.6 300/300 连续 Evaluate + 两组 Style/Intensity 变体 hash + GPU timestamp + JSON 输出，对齐 phase1 gate 契约。

---

## Cycle 016 — P1.6 300/300 + 变体/时间戳/JSON；P1.7 生命周期

### Before

- Phase: 1
- 唯一任务: frame loop 增加 GPU timestamp（4-slot begin/end 对 + ResolveQueryData→avgMs）、变体段（style=1 / intensity=0.5）、gate 契约 JSON（debugLayer 报运行时实际状态）；Debug 与 Release 各跑 300 帧；P1.7 连续生命周期序列验证重建/释放。
- 可证伪假设: 若 300 帧中断、变体 hash 与 baseline 相同、时间戳为 0 或 JSON 字段缺失，则 FAIL。
- 预计修改文件: tools/nr_harness/frame_loop.{h,cpp}、main.cpp、include/veyra/gfx/CommandSlotRing.h。
- 快速检查命令: `--frames 300` 双配置 exit 0；PowerShell 解析 JSON 全字段。
- 预期新增证据: 300/300 计数、三互异 hash、avgMs、PNG/JSON 产物、4/4 生命周期。

### After

- 实际命令与 exit code:
  - Debug 300 帧（p16-full）→ exit 0：**300/300 成功 0 失败**；meanLuma=0.49425 stddev=0.32734 allZero=false constant=false sha=1BD84180…；variants style1=B5294CB6…、intensity05=8A621726…（三互异）；timestamps nonZero=true avgMs=6.32；json written=true。
  - Release 300 帧（p16-rel）→ exit 0：300/300，PASS。
  - P1.7 序列（Debug）：两轮 10 帧 loop + create-test + shim-test → **4/4 PASS**（连续 Create/Reset/Release/重建稳定，无 device removed）。
- 新证据/日志路径: logs/tmp/{p16.json,p16.log,p16rel.out,p17a-d.out}；captures/tmp/p16/frame0000_{proxy,raw}.png。
- 失败 fingerprint: 无。
- 是否有进展，依据: 是——Phase 1 Playbook §16 核心门槛全部真实达成。
- 附加修正: JSON debugLayer 字段从编译期意图改为运行时实际状态（诚实报告本机 debug layer 未启用）。
- STATE/BACKLOG 更新: cycle.completed=16；P1.6/P1.7 → DONE；currentTask → P1.8 gate。
- 下一唯一动作: 运行 phase1 gate——预期唯一红项 json-debug:debug-layer-enabled（等用户装 Graphics Tools，见 INBOX），其余应全绿。

---

## Cycle 017 — P1.8 修复 Reviewer P1（infoqueue 接入）+ P2 批

### Before

- Phase: 1
- 唯一任务: 落实 Reviewer 修复条件——debug 构建下 D3D12CreateDevice 后 QueryInterface ID3D12InfoQueue、SetMuteDebugOutput(false)、帧循环结束 GetNumStoredMessages/GetMessage 逐条入日志并在 JSON 报 storedMessages/errorMessages；gate 的 no-state-errors 改为解析该 JSON 字段（active==true 且 errorMessages==0），debug 帧数 10→30，删除误导性硬编码 nanCount；同时修 P2：8.1 精确字面量日志、adapter 路径 containment、AllocateParameters/DestroyParameters SEH 包裹。
- 可证伪假设: 若 debug layer 在 30 帧+捕获回读中报 error 级消息，新 gate 将真实失败。
- 预计修改文件: tools/nr_harness/frame_loop.cpp、src/ngx/DlssNrRuntimeAdapter.cpp、src/ngx/NgxCoreHost.cpp、scripts/gates/phase1.ps1。
- 快速检查命令: `build.ps1` 双配置 → 0；`loop-gate -Gate phase1` → 0（infoQueue active 且 0 error）。
- 预期新增证据: JSON debugInfoQueue 字段与日志中真实检索的 infoqueue 消息数。

### After

- 实际修改: frame_loop.cpp（ID3D12InfoQueue attach/clear/检索入日志+JSON、d3d12sdklayers.h、删 nanCount）、phase1.ps1（删空转 grep → infoqueue-active + no-error-messages 两项真实断言、debug 10→30 帧）、DlssNrRuntimeAdapter.cpp（8.1 精确字面量、\runtime_local\nvidia\nvngx_dlssnr.dll 后缀 containment）、NgxCoreHost.cpp（Allocate/DestroyParameters SEH 包裹）。
- 实际命令与 exit code:
  - `build.ps1 -Preset x64-debug/-release` → 双 0。
  - Debug 30 帧直跑 → exit 0：日志含 "EXPERIMENTAL LOCAL-ONLY DLSSNR ADAPTER ENABLED"、"ID3D12InfoQueue attached and recording"、"infoqueue stored=0 reported=0 errors=0"；JSON debugInfoQueue{active=true,stored=0,errors=0}。
  - `loop-gate -Gate phase1` → **exit 0，56/56**（run-id 0106547878964354a751ab7ae5ec5f38）：新增 json-debug:infoqueue-active 与 no-error-messages 均 PASS，evaluate-30-of-30 PASS，Release 300/300 仍 PASS。
- 新证据/日志路径: logs/phase1/0106547878964354a751ab7ae5ec5f38/。
- 失败 fingerprint: 无（P1 修复一次通过）。
- 是否有进展，依据: 是——"debug layer 无 state error" 从空转 grep 变为对真实检索消息的断言，检查现在可失败；全部 5 项 P2 同步修复。
- STATE/BACKLOG 更新: 待 Reviewer 终判后一并写入（P1.8 完成时）。
- 下一唯一动作: 等 Reviewer 终判 → PASS 则 checkpoint + 解锁 Phase 2；新 P0/P1 则继续修复循环。

---

## Cycle 018 — P2.1 parity CPU 黄金参考与 phase2 gate

### Before

- Phase: 2
- 唯一任务: 按 Playbook §9 的精确数学（shoulder 0.75/5.7780、标准 sRGB、OkLab/AP1 全矩阵、signed cbrt、UpgradeToneMap、luminance-only）实现 src/parity CPU 参考（RenoDxParityCodec）；tests/unit/ParityCpuReference 覆盖 9.5 全部场景（黑/白/18% 灰、0.7499/0.75/0.7501、高光 1/2/4/8、RGB 基色、肤色、负分量/alpha、64×64 图案）；创建 fail-closed 的 scripts/gates/phase2.ps1（编码 §16 Phase 2 全部门槛）；验证 gate 现在因缺 shader/捕获而 exit 1 且 CPU tests 真实 exit 0。
- 可证伪假设: 若数学实现错（矩阵转置/阈值/符号），golden tests 将以真实容差失败。
- 预计修改文件: src/parity/{RenoDxParityCodec.h,RenoDxParityCodec.cpp}、tests/unit/ParityCpuReference.cpp、CMakeLists（veyra_parity + veyra_parity_tests）、scripts/gates/phase2.ps1。
- 快速检查命令: `build.ps1 -Preset x64-debug` → 0；`veyra_parity_tests.exe` → 0；`loop-gate -Gate phase2` → 1（缺 GPU 侧产物）。
- 预期新增证据: CPU golden 全过（含阈值连续性、中性 baseline 恒等、高光恢复、无 NaN）。

### After

- 实际修改: include/veyra/parity/RenoDxParityCodec.h、src/parity/RenoDxParityCodec.cpp（§9 精确数学：shoulder 0.75/5.7780、sRGB、OkLab/AP1 全矩阵 mul(matrix,vector) 方向、signed cbrt、HueOkLab、UpgradeToneMap 双段、luminance-only、PaperWhiteScale）、tests/unit/ParityCpuReference.cpp（12 项 golden）、config/parity-default.json（1.0 中性 baseline）、scripts/gates/phase2.ps1（§16 Phase 2 全门槛）、CMakeLists（veyra_parity + veyra_parity_tests）。
- 实际命令与 exit code:
  - `build.ps1 -Preset x64-debug/-release` → 双 0（修一处缺 <algorithm>）。
  - `veyra_parity_tests.exe` Debug 与 Release → **12/12 checks, 0 failures, exit 0**。
  - `loop-gate -Gate phase2` → exit 1（input:ParityEncode.hlsl、ParityDecode.hlsl 缺失）——fail-closed 成立。
- 新证据/日志路径: 控制台输出：srgb 往返 worst≈0；shoulder 连续性 gap≈5e-5/7.5e-5；高光 1/2/4/8 有序压缩；18% 灰 code=118 与理论一致；OkLab 往返 <1e-6；中性 bypass 恒等 worst=0.001424；高光重建 finalY=3.995（original 4.0）；**shoulder 区亮度保持误差 0.000000**；量化界 losslessWorst=0.005559 < 0.0076（1-code 线性界）；quantize-within-1-code 0.499 code。
- 失败 fingerprint: 无遗留。首轮 3 项测试期望错误（肤色含 >0.75 通道应走 shoulder；矩阵 10 位截断往返 ~1e-7；彩色高光 bypass 亮度保持而非逐通道恒等——worst 0.057 来自 shoulder 有损设计的 (1,0.5,0.25) 类像素）均已按规格数学修正，codec 数学本身零改动。
- 是否有进展，依据: 是——P2.1 验收成立：CPU 黄金全绿 + phase2 gate fail-closed。
- STATE/BACKLOG 更新: cycle.completed=18；BACKLOG P2.1 → DONE。
- 下一唯一动作: P2.2/P2.3 实现 shaders/Parity{Encode,Decode}.hlsl（16x16 组、越界 return、FP16/UNORM 资源合同）与 harness --parity-compare 模式（四阶段 capture + GPU/CPU 对比统计 + 中性 baseline/addon hash 记录）。

---

## Cycle 019 — P2.2–P2.5 parity 全链与 phase2 gate

### Before

- Phase: 2
- 唯一任务: 实现 shaders/Parity{Encode,Decode}.hlsl（DXC 构建期编译）+ harness --parity-compare（Original FP16 上传→Encode→16 帧 Evaluate→Decode→Final；GPU/CPU 对比统计；四阶段捕获；中性 baseline/addon hash 记录）；跑 phase2 gate 全绿。
- 可证伪假设: GPU/CPU encode 差 >1 code 或 decode 差 >0.002 则 FAIL；debug layer 报错则 FAIL。
- 预计修改文件: shaders/Parity{Encode,Decode}.hlsl、tools/nr_harness/{parity_compare.h,parity_compare.cpp,harness_util.h,harness_util.cpp}、frame_loop.cpp（改用共享 util）、main.cpp、CMakeLists。
- 快速检查命令: `--parity-compare --frames 16` → 0；`loop-gate -Gate phase2` → 0。
- 预期新增证据: maxCodeDelta≤1、maxAbsError≤0.002、四阶段捕获、raw≠final hash。

### After

- 实际命令与 exit code:
  - 调试历程（debug layer + infoQueue 实战立功）：①HLSL `linear` 是保留字+向量三目需 select()；②SRV desc `Texture2D.MipLevels=0` 非法→debug layer 直接 RemoveDevice（id=31/232）；③UAV range `OffsetInDescriptorsFromTableStart=4` 与绑定时 +4 堆偏移双重叠加越过 8 槽堆尾（id=646/1023）；④NGX Evaluate 后共享资源状态与 DATA_STATIC bind 校验冲突（id=538/527）→ DESCRIPTORS_VOLATILE + decode 前置 barrier 小提交；⑤decode 差异主成分是 FP16 在 ≥4.0 区间的存储量化（1 ulp=0.0039）→ CPU 期望先量化 FP16 再比较（测得 0.001953125=恰 1 ulp）。
  - Debug --parity-compare → **P2_EXIT=0：encode maxCodeDelta=1、decode maxAbsError=0.001953125、nanInf=0、infoQueue 0 错误、四阶段捕获 written=true、parity: PASS**。
  - `loop-gate -Gate phase2` → **exit 0：35/35 checks**（run-id 32fb351bb11b49d2b718027bbfdb33f4，5.0s）。
- 新证据/日志路径: logs/phase2/32fb351bb11b49d2b718027bbfdb33f4/；captures/phase2/<runId>/ 七文件（00_original.rgba16f.bin 16588800B+json、01_proxy.png、02_raw_dlssnr.png、03_final.rgba16f.bin+preview+json）；raw=6EE7EDEE… ≠ final=D2588673…；画面抽验 meanLuma：proxy 0.543→raw 0.546→final 0.567（高光恢复，无洗白/压黑）。
- 失败 fingerprint: 无遗留（5 个缺陷全部由 debug layer 证据定位修复）。
- 是否有进展，依据: 是——Playbook §16 Phase 2 全部门槛机器验证通过。
- STATE/BACKLOG 更新: cycle.completed=19；P2.2–P2.5 → DONE；STATE gate_passed 待 Reviewer。
- 下一唯一动作: P2.6 只读 Reviewer（BASE_COMMIT=1dcf834…）→ PASS 则 checkpoint 解锁 Phase 3。

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
