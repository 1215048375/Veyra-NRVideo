# PS5遥测、停帧、FG调度与解码施工记录

目标active，未完成，不发布。当前分支codex/ps5-scheduler-telemetry-decode；开工adff31b，tag checkpoint/ps5-scheduler-decode-pre-repair-2026-09-12。方案PS5_TELEMETRY_STALL_FG_DECODE_REPAIR_PLAN_2026-09-12.md，用户要求全部完成。

## P0第一步：独立接收/解码观测

SessionInbox追加一秒有界窗口、完整视频payload速率/接收与解码fps、最后视频/音频/解码时间、实际解码调用平均/P95和接收后等待。不是UDP线速、不是网络端到端延迟。RemotePlaySource在submit/drain位置计时和计数，SessionSource安全共享只读inbox快照；Engine::snapshot直接读这些线程安全数据，不再只能等GPU循环推送。

FrameFlow记录最后GPU就绪、提交和Present；独立monitor每秒输出remoteplay-progress，即使解码owner卡在调用内仍能观察接收进度。视频停顿在Streaming状态下1秒触发受限关键帧恢复，后续间隔2秒，一次停顿最多3次；恢复后重置次数，最终沿用30秒失败边界。没有强杀线程、无无限重建。尚需故障注入与用户停帧复测，不宣称冻结根因已经修复。

实际命令：out/remoteplay/audit-20260911/build-product.cmd；日志logs/ps5-p0-progress-build.log（31步成功）、ps5-p0-progress-tests-build.log（2步）。scripts/run-short-test.ps1运行veyra_remoteplay_boundary_tests，TimeoutSeconds30，logs/ps5-p0-progress-boundary.stdout.log，exit0；新增用例验证独立快照的真实解码耗时和一秒后旧样本不显示为实时。

尚未完成：P0视频阶段UI/完整恢复矩阵，P1文件提前增强和FG恢复，P2面板，P3硬解。不能将此节点称为整体完成。
