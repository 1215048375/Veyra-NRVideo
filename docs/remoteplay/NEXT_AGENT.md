# 交给下一位 Agent 的接续指令

唯一交接入口：[`../REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md`](../REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md)。先完整阅读该文档和工作区最新 `AGENTS.md`，保护当前未提交改动，不要 reset、checkout 或重新运行用户包中的 `apply.py`，也不要自动 push 或发布。

**先读交接文档第 18 节二次审计。** Windows/MSVC 原生 Chiaki 初始化已通过，但新审计真实复现了 H.264 配置头导致首帧失败、PCM 截断、音频时间轴错误和解码重排 PTS 错配；同时发现移植删去了原包的元数据回调。不能拿67项 core测试通过当作 source 正确。

当前唯一任务：按第 17 节先建立会失败的 H.264 config/AU 真解码 source 回归，再修第6、18节的数据适配问题。音频必须用固定段锚点加样本偏移，旧交接逐块使用当前 arrival 的建议已撤回；元数据、实际尺寸、IDR/解码恢复、音频重启和停止失败都需覆盖。原始错误与审计工具在忽略的 `logs/remoteplay-audit-20260911/`、`out/remoteplay/audit-20260911/`。

完成上述闭环后，按交接文档第 12 节继续生产 CMake、统一引擎路径、Remote Play 音频、DPAPI/UI、手柄和 PS5 实机验收。不能因为离线测试或 native 初始化通过就声称串流功能已经完成。
