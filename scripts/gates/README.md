# Phase gate contract

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
