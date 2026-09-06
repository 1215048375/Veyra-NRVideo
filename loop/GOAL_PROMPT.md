# 可直接交给 Goal 模式的任务

将下面整段作为另一个 Agent 的目标。推荐把 Goal 最大循环数设为 120；如果产品另有硬上限，以更小的硬上限为准，达到上限时必须留档，不能伪报完成。

~~~text
目标：接管当前 Veyra DLSS Video Player 工作区，严格按 AGENTS.md、
loop/LOOP_ENGINE.md、VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md 和
VEYRA_PRODUCT_SPEC_V1.md，从 2026-09-06 的真实恢复基线继续，完成
Launch V1。V1 不是 MVP：物理采集卡实时增强、可交互播放器、图片导出、
视频导出和 native 4K SDR 必须同时交付，并共同使用一套
FrameSource -> EnhanceGraph -> FrameSink。

重要纠错：不要信任旧“Phase 5 passed”。Phase 0–4 证据有效；Phase 5
已因 gate 与实现不符重开。旧 gate 接受 manifest-only depth、把第二次
1080p endurance 放进 4K 检查位置；src/core/EnhanceGraph.cpp 只计数，
真正 GPU 链仍在 harness；tools/player_probe/main.cpp 约 3600 行。Phase 6
已有值得保留的未提交代码和真实 FG/NVOF/WASAPI/Present 证据，但最新
player run 的 drift P95 约 2.8 秒且 L2/GBV 有 descriptor-uninitialized，
所以 Phase 6/7 必须保持 locked，禁止从 UI 或 Capture 开始。

用户已决定继续使用当前固定 hash 的实验 nvngx_dlssnr.dll/Feature 18 做
本机研发。不要等待公开 DLSS 5 SDK，也不要重新争论市场可行性；同时
不得声称它与官方游戏/Magpie 画质等价。不得寻找其他泄露版本、从游戏
或驱动缓存抽 DLL、patch/重签/提交/打包/上传 runtime。分发权未解决时
最终只能 distribution_blocked。

你是唯一写入 Maker。启动后完整阅读上述四份文件以及 README、
docs/COMPETITOR_AUDIT_2026-09-03.md、loop/STATE.json、BACKLOG、INBOX、
EVIDENCE、JOURNAL 和 WORKLOG 最新记录。若 loop/STOP 存在立即停机。
首轮按 Playbook R0 执行，首条项目命令必须是：

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight

随后运行 git status --porcelain=v1、git diff --stat、git diff --check、
git log -10、Release build，并对当前 git diff --binary 计算接管指纹。
当前工作树在控制面重基线后约有 44 个未提交 status entry；禁止 reset、clean、checkout、
覆盖或整树格式化。先把每个未提交文件归类为 keep/repair/obsolete，保留
其他 Agent 的真实成果。validation/fixed_clips 下的 tracked MP4 删除先
不处理，等新 gate 改用确定性生成 corpus 后单独报告。

施工严格执行 Playbook 0.3 的 R0→R12 和 BACKLOG 第一个可执行原子项：
R0 保存接管基线；R1 重写 phase5 gate 并先证明失败；R2 补全 packet/
window/guidance/reset 契约；R3 把真实 GPU graph 从 player_probe 提取到
共享库；R4 完成 NVOF confidence、DAV2/Auto、scene/reset；R5 重新通过
Phase 5 gate+Reviewer；R6 修 GBV、GPU timing、性能、A/V drift 并通过
Phase 6；R7 Player app；R8 Capture；R9 Image Export；R10 D3D12 NVENC
Video Export；R11 UI/设置/恢复；R12 Phase 7 联合 gate+Reviewer。

每个 cycle 只能做一个可证伪原子任务。改代码前先在 JOURNAL 写假设、
预计文件、窄测试和新增证据；改后先跑窄测试，再跑当前 phase gate。
所有成功必须来自本次 run-id、exe/input/config/runtime hash、exit code、
真实 Create/Evaluate、counter、GPU timestamp 或 capture。以下一律是假完成：
stub、只增加 counter、仅编译、manifest 代替 provider、harness-only 链路、
1080p 重跑冒充 native 4K、Zero motion 冒充 NVOF、blend/duplicate 冒充 FG、
raw pipe 冒充 D3D12 NVENC、窗口捕获冒充物理采集卡。

R3 必须删除双实现：把已验证代码从 player_probe 移入 veyra_pipeline/
veyra_guidance/veyra_sources/veyra_sinks；probe 最终只做组装和写证据。
Player、Capture、Export 不得复制 NGX、颜色、Guidance、reset 或资源生命周期。
逐帧路径禁止全帧 GPU->CPU readback、每 pass CPU fence wait、无界队列。

Phase 5 新 gate 必须在真实共享 library 上验证 1080p60 和 native 4K60
各 30 分钟、NVOF 非零 motion/confidence、DAV2 或明确且可测的 Auto fallback、
reset/scene、真实 extent、GPU timing、VRAM/queue/memory。依赖 manifest
不能单独过 provider gate。Phase 6 必须先修 GBV id=938，加入 query-heap
GPU timing，把 1080p/4K player drift P95 降到 <=50ms，L0/L1/L2 均自然
return、0 ERROR/CORRUPTION，再跑 4K30/60 30 分钟。不得关掉验证层或删
错误来过关。

Video Codec SDK 13.1 和真实 4K60 采集设备是外部阻塞，只阻塞对应原子项；
可继续同 Phase 内独立工作。NVOF SDK 5.0.7 已存在，不得继续写成缺失。
同一 failure fingerprint 最多 3 个真正不同且增加证据的方案；连续 5 个
cycle 无新证据或总计 120 cycle 就留档停止，禁止空转。

每个 Phase gate 首次通过后，必须把 loop/REVIEW_PROMPT.md 交给一个新
上下文只读 Reviewer。Reviewer 独立重跑 gate，不能修改工作区。修完全部
P0/P1、gate 与 Reviewer 再通过后才能本地 checkpoint、更新 STATE 并解锁
下一 Phase。禁止并行写同一 checkout，禁止 push、PR、发布、上传、签名、
制作安装包、安装/降级驱动或替用户接受 EULA。

Goal 启动后所有控制面只读，不得重写计划、gate contract 或 hash manifest
来自我放行。只有 Phase 0–7 全部有当前规格的 gate+Reviewer、无 P0/P1、
三入口和 4K 证据完整、专有文件未入 Git，才可标功能 release_candidate；
分发 blocker 清零前绝不能标 complete 或公开首发。

现在立即从 BOOT/RECONCILE 与 R0 开始。不要先写 UI，不要再问文档已经
回答的问题，也不要用旧摘要代替重新运行命令。
~~~

说明：这是本项目唯一的无人值守执行入口。非 Goal 模式只能完成当前 Phase 后停下报告。
