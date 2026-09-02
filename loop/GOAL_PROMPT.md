# 可直接交给 Goal 模式的任务

将下面整段作为另一个 Agent 的目标。推荐把 Goal 最大循环数设为 80；如果产品另有硬上限，以更小的硬上限为准，达到上限时必须留档，不能伪报完成。

~~~text
目标：在当前 Veyra DLSS Video Player 工作区内，严格按
AGENTS.md、loop/LOOP_ENGINE.md、
VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md 和
VEYRA_PRODUCT_SPEC_V1.md，
从当前真实状态继续施工，完成 Phase 0 到 Phase 7 的 V1。

你是唯一有写权限的 Maker。每个回合先恢复 loop/STATE.json，
核对磁盘/Git/日志，再只做 loop/BACKLOG.md 中当前 Phase 的第一个
未完成原子任务。改动前写 loop/JOURNAL.md；改动后先跑窄测试，
再运行 scripts/loop-gate.ps1 的当前 phase gate。任何“通过”都必须
来自实际命令、exit code、日志、counter 或 capture，禁止把 stub、
仅编译、理论结果、黑图或重复帧写成成功。

不要重做“Magpie 是否证明 DLSS 可用于普通软件/视频”的可行性研究，
也不要做主观画质优化；这里的测试只验证工程调用、数据契约、颜色
parity、时序、延迟和稳定性。

同一 failure fingerprint 最多做 3 个有新证据的不同尝试；连续 5 个
cycle 没有新增可验证证据就停止重复并持久化阻塞；总计最多 80 个
完整 cycle。可绕开的外部阻塞先记录，再继续当前 Phase 内不依赖它的
工作；当前 gate 未通过时禁止启动后续 Phase。

每个 Phase 的 gate 首次通过后，严格使用 loop/REVIEW_PROMPT.md，把
其中的占位值替换后交给一个新上下文子 Agent 做只读 Reviewer：它不得
编辑文件，必须独立重跑 gate 并按 Playbook 第 19 节检查。修完全部
P0/P1、gate 再次通过、Reviewer 通过后，更新
STATE/BACKLOG/EVIDENCE/JOURNAL/WORKLOG，做本地 checkpoint commit，
然后自动进入下一 Phase，不等待用户确认。禁止并行写入 Agent。

你被授权在项目目录内编辑、构建、运行测试、初始化本地 Git、创建
本地 commit，并从 Playbook 指定的官方/上游来源获取开发依赖到
gitignore 的 local 目录。你没有权限 push、发 PR、上传/发布/打包
artifact、安装或降级驱动、修改系统安全设置、接受许可证条款、
寻找其他 DLSSNR 泄露文件、修改签名 DLL、加载 RenoDX add-on、
复制 Magpie GPL 源码或破坏用户改动。

Goal 启动后，README、.gitignore、AGENTS、LOOP_ENGINE、GOAL_PROMPT、
REVIEW_PROMPT、CONTROL_HASHES、基础 loop-gate、gate contract、
Playbook 和 Product Spec 是只读控制面，禁止通过改规则或重写 hash manifest
来过关。你只能新增/修改当前 Phase 的 phaseN gate；BACKLOG 只能更新
状态或增加不降低门槛的子任务。

若 loop/STOP 存在，保存状态并立即停止，不得删除它。只有 Phase 0
到 7 的 gate 与独立 Reviewer 全部真实通过、Playbook 第 21 节全部
有证据、无 P0/P1、专有文件未入 Git，才可把目标标为 complete。
无法独立复核时状态必须是 needs_review；达到循环上限时状态必须是
needs_handoff；同一真实阻塞连续三个 Goal 回合且无其他安全工作时
才可标 blocked。不要因为预算或时间将近就声称完成。

立即从 BOOT/RECONCILE 开始，不要先重写方案，不要询问用户已经由
文档明确回答的问题。首条命令应运行 preflight gate。
~~~

说明：这是本项目唯一的无人值守执行入口。非 Goal 模式只能完成当前 Phase 后停下报告。
