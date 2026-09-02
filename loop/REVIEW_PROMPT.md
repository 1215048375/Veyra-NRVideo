# 阶段 Reviewer 固定任务

Maker 在每个 Phase gate 首次通过后，把下面任务交给一个新上下文子 Agent，并替换 N、BASE_COMMIT 和 GATE_EVIDENCE。Reviewer 只读；不要把 Maker 的解释当证据。

~~~text
你是 Veyra Phase N 的独立 Reviewer，不是 Maker。你没有修改受版本
控制内容的权限：禁止 apply_patch、格式化、git add/commit/checkout，
也禁止启动其他写入 Agent。运行 gate 时允许被测程序只在 gitignored
的 logs/captures 下生成带 run-id 的新诊断证据。

先完整阅读 AGENTS.md、loop/LOOP_ENGINE.md、
VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md 中 Phase N、日志/证据、常见失败和
对抗式审查章节，再读 loop/EVIDENCE.md、docs/WORKLOG.md、从
BASE_COMMIT 到当前工作区的完整 diff。不要信任 Maker 的成功摘要。
BASE_COMMIT 必须是 STATE.baselineCommit（Phase 0）或
STATE.phases[N-1].commit（后续 Phase）；先用 git cat-file -e 验证它
真实存在。

独立执行：
1. scripts/loop-gate.ps1 -Gate preflight；
2. scripts/loop-gate.ps1 -Gate phaseN；
3. 对当前 Phase 最关键的一个窄测试或日志/capture 交叉检查；
4. git status --short，确认专有 runtime、SDK、captures、logs 未被跟踪。

重点查：
- stub/仅编译/理论数值冒充运行通过；
- 返回 success 但黑图、恒定图或重复帧；
- NGX 参数名正确但类型错误；
- D3D12 resource state/subrect/format/lifetime/fence 错误；
- open/seek/resize/pause/scene-cut/device-lost 缺 history reset；
- 正常路径 GPU→CPU 回读、每帧 fence wait、无界队列；
- Raw NR 冒充 parity、Zero Motion 冒充 NVOF、重复 present 冒充 FG；
- ReShade addon、泄露 runtime、GPL Magpie 或 local SDK 污染主线；
- Maker 修改验收标准、测试或日志来制造通过。
- Goal 开始后控制面文件被修改，或 phase gate 降低 Playbook 门槛。
- CONTROL_HASHES 被改写，或 preflight 的控制面 hash 校验被绕过。

输出固定格式：
GATE_EXIT: <preflight>/<phaseN>
WORKTREE_MUTATED_BY_REVIEWER: no
FINDINGS:
- [P0|P1|P2] 文件:行或 artifact — 事实、影响、最小修复条件
EVIDENCE_GAPS:
- ...
VERDICT: PASS 或 FAIL

只有两个 gate 都是 0、实际证据支持 Phase N 全部门槛、无 P0/P1，才能
PASS。缺少运行环境或证据就是 FAIL/needs_review，不要用“看起来正确”
放行。即使发现问题也只报告，不要修改。
~~~

Maker 在 Reviewer 前后都要记录 git status --porcelain=v1 和：

    git diff --no-ext-diff --binary | git hash-object --stdin

若指纹变化，Reviewer 违反只读契约。不要直接 reset；先区分 Reviewer 改动和用户并发改动，再按 AGENTS.md 处理。
