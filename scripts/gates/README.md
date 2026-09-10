# Phase gate contract

> 2026-09-10：旧 Loop/控制哈希/Phase 队列已由用户废弃，下文是历史合同。当前直接执行构建、相关回归和必要的 `delivery.ps1`，不经 `loop-gate.ps1`，不受旧 STOP 或 CONTROL_HASHES 约束。单次测试最多 300 秒；运行时身份和源码隔离校验继续生效。

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

每个 scripts/gates/phaseN.ps1 都必须：

1. 接受必填参数 param([string]$Root)；
2. Set-StrictMode -Version Latest，失败时非零退出；
3. 不修改源代码、控制面、STATE/BACKLOG/EVIDENCE/WORKLOG；可以运行被测程序，让它在 gitignored 的 logs/captures 下生成本次 run-id 对应的新日志、counter 和 capture；
4. fail closed：命令、日志、capture、counter、硬件能力或解析字段缺失都失败；
5. 实际运行当前阶段的程序/测试，不能只搜索旧日志中的“PASS”；
6. 检查 Debug/Release 和 Playbook 要求的运行矩阵；
7. 打印每项 check、实际值、阈值、artifact 路径和最终 exit code；
8. 不访问 renodx add-on，不联网换 DLSSNR 文件，不上传 artifact；
9. 可重复运行；每次生成唯一 run-id，验证证据的 run-id、时间、输入 hash 和当前可执行文件 hash，不能把旧残留误认成本次结果；
10. exit 0 只表示本阶段机器可验证条件通过，不代替独立 Reviewer。

Launch V1 Phase 5–7 额外要求：

- `phase5.ps1` 必须解析真实 SR→NR graph、NVOF 非零 motion、confidence、DAV2/Auto fallback、scene/reset，以及 1080p60/native-4K60 各 30 分钟 endurance；旧的 Zero-only gate 必须重写，不能继续使用；
- `phase6.ps1` 必须运行真实 DLSSG + 4K present/audio probe，证明 generated frame 不等于前后帧和 50/50 blend，并检查 A/B、A/B/C、PTS/cadence/latency/queue/VRAM；
- `phase7.ps1` 是联合 gate，4K Player、真实 4K60 DirectShow/UVC Capture、Image Export、D3D12 NVENC 4K H.264/HEVC Video Export、产品 UI/恢复五组证据缺一即失败；
- 硬件/SDK 不可用是诚实的 gate failure，不是跳过项。不得把 capability unavailable 自动改写为产品完成。
- gate 必须证明被 Player/Capture/Export 链接的产品 library 执行了真实工作；仅在 `*_probe/main.cpp` 或 harness 中重写一套相似流程不能通过。
- 每个运行 JSON 必须绑定本次 run-id、exe/input/config/runtime hash。4K 检查还必须记录实际 source/working/output extent 和资源格式；不能从文件名、窗口尺寸或最终 resize 推断。
- Product Spec 要求 30 分钟时不得缩成 5 分钟。开发期可单独运行 5 分钟 smoke，但字段和 gate 名必须明确 `smoke`，不能满足 endurance check。
- manifest 只能通过 dependency-identity check；不能单独通过 provider-created/evaluated/output-valid/quality check。
- Debug layer/GBV/InfoQueue 必须记录 stored/retrieved/failure/saturation/error/corruption；检索不完整或 descriptor-uninitialized 均失败，不能过滤消息制造 0 error。

基础 `scripts/loop-gate.ps1` 会在 phase gate 前后 hash 所有 Git 已跟踪和未忽略的
项目文件；phase gate 只能让被测程序写入已忽略的 `logs/`、`captures/` 或 build
目录。任何源码、gate、STATE/记录或控制面变化都会让外层 gate 失败。

建议骨架：

~~~powershell
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Resolve known executables and inputs from $Root.
# Run the actual test/probe and capture its exit code.
# Parse quantitative output and fail if any required field/artifact is absent.

if (-not $allChecksPassed) {
    exit 1
}
exit 0
~~~

禁止的假 gate：

- 只检查 exe 或日志“存在”；
- 输出 PASS 后无条件 exit 0；
- gate 自己伪造/补写结果文件，或用 placeholder 代替被测程序的真实输出；
- 不核对 run-id、输入/exe hash，直接复用任意旧日志或 capture；
- 用当前日期、代码行数、编译成功代替 Create/Evaluate/present 证据；
- 捕获到黑图、恒定图、重复帧仍因 API 返回 0 而通过。
- 用窗口/桌面 capture 冒充物理采集卡，或只验证设备枚举不验证持续帧流；
- 只检查输出文件存在，不用 ffprobe/自身 decoder 检查帧数、尺寸、时长、音轨和首尾内容；
- motion/depth texture 存在但全零、恒定、过期或方向错误仍通过；
- 把简单 blend、重复 present 或理论 2X counter 当成 DLSSG 生成帧。
- 用 1080p→4K 的单次 SR harness 冒充 native 4K Player/Capture/Export；
- 第二次运行同一 1080p 素材却把 JSON 字段命名为 4K/endurance；
- `EnhanceGraph` 只计数、复制 packet，真正 GPU work 仍由 harness 私有代码执行；
- 仅因 model/SDK manifest 存在就把 depth/NVENC/NVOF provider 标为完成；
- 用整帧 GPU→CPU readback/raw pipe 冒充 D3D12 NVENC export；
- 采集 A/B/C 窗口超过 Product Spec 上限，或不记录 deliberate lookahead 帧数/毫秒数。
