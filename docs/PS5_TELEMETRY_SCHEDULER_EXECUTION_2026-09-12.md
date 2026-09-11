# PS5遥测、停帧、FG调度与解码施工记录

目标active，未完成，不发布。当前分支codex/ps5-scheduler-telemetry-decode；开工adff31b，tag checkpoint/ps5-scheduler-decode-pre-repair-2026-09-12。方案PS5_TELEMETRY_STALL_FG_DECODE_REPAIR_PLAN_2026-09-12.md，用户要求全部完成。

## P0第一步：独立接收/解码观测

SessionInbox追加一秒有界窗口、完整视频payload速率/接收与解码fps、最后视频/音频/解码时间、实际解码调用平均/P95和接收后等待。不是UDP线速、不是网络端到端延迟。RemotePlaySource在submit/drain位置计时和计数，SessionSource安全共享只读inbox快照；Engine::snapshot直接读这些线程安全数据，不再只能等GPU循环推送。

FrameFlow记录最后GPU就绪、提交和Present；独立monitor每秒输出remoteplay-progress，即使解码owner卡在调用内仍能观察接收进度。视频停顿在Streaming状态下1秒触发受限关键帧恢复，后续间隔2秒，一次停顿最多3次；恢复后重置次数，最终沿用30秒失败边界。没有强杀线程、无无限重建。尚需故障注入与用户停帧复测，不宣称冻结根因已经修复。

实际命令：out/remoteplay/audit-20260911/build-product.cmd；日志logs/ps5-p0-progress-build.log（31步成功）、ps5-p0-progress-tests-build.log（2步）。scripts/run-short-test.ps1运行veyra_remoteplay_boundary_tests，TimeoutSeconds30，logs/ps5-p0-progress-boundary.stdout.log，exit0；新增用例验证独立快照的真实解码耗时和一秒后旧样本不显示为实时。

尚未完成：P0视频阶段UI/完整恢复矩阵，P1文件提前增强和FG恢复，P2面板，P3硬解。不能将此节点称为整体完成。

## P1：文件有界提前增强（第一轮实机通过）

复用唯一 GPU owner 的 LiveGpuScheduler，文件预览也使用最多 2 个保留 lease 的增强批次；启动仅在有界预取就绪后打开音频锚点，稳态保持音频主时钟，按每帧真实 PTS 呈现。暂停、seek、变更继续取消旧批次，不放宽过期容忍。修复 paused seek 被背压分支阻断。音画偏差改用最后实际呈现帧；性能不足提示改为最近一秒，不使用累计计数常亮。

真实原版 NR SHA256 符合 AGENTS。产品构建 build-product.cmd 成功（logs/ps5-p1-lookahead-build2.log）。使用 logs/audio-jitter-20260911/4k30fps.mp4（8秒）和实际 RTX5070/NVIDIA runtime，--native --nr --fg-multiplier 2 --no-sr：25秒 smoke 退出0，240源帧、238生成，稳态每秒约60次提交、expiredGenerated=0、播放1.000x。logs/ps5-p1-file4k30-runtime.log。与修前用户4K30样本不是相同媒体，不宣称全部媒体60fps保证。

15秒 --smoke-controls 同参数回归退出0：暂停→暂停seek到1秒→恢复完成，controlsStep=3，稳态提交约60fps，过期0，声音领先最终0.40ms、绝对偏差P95 0.88ms；logs/ps5-p1-controls-runtime.log。尚需长媒体、持续欠速、恢复、单帧EOF与XeSS回归，UI改动正在进行，硬解未实现。
