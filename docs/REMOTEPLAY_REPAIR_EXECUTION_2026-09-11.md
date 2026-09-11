# Remote Play 修复与产品集成执行记录

用户在二次审计后授权本 Agent 继续修复并完成产品集成，要求大节点本地 Git 存档和更新文档。未授权本轮远端发布。原问题、架构、依赖和验收边界见 [交接第18节](REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md#18-2026-09-11-二次代码审计用户要求交给其他-agent-修)。

## 存档与当前节点

- 修复前存档 `bf21bef`，标签 `checkpoint/remoteplay-audited-2026-09-11`。保留原代码和审计，不冒充可用版本。
- 当前分支 `agent/remoteplay-integration`。
- 节点一：源层首帧/PCM/时间戳闭环。已完成下列修改和针对性验证，仍未接主程序。

## 节点一已实现

1. H.264 配置前缀与首个 AU 合并送入 FFmpeg，避免只含 SPS/PPS 的输入终止首帧。
2. 给提交包分配独立 ID，解码输出按 AVFrame PTS 查回有界元数据表；不再套当前输入包。未知/重复输出 ID 明确失败并触发恢复，不伪造额外帧时间。
3. PCM 保存 block/cursor，固定音频段相对起点＋样本差值；到达抖动不改变连续播放速度，断点不混拼到同次 pull。
4. 首帧交付进入 Streaming；解析实际颜色与尺寸，更新 SourceInfo/Resize，720p fallback 改 BT.709；释放旧 decoder frame 后重建。
5. source owner 路径消费 IDR 请求，送 backend；decode 失败 flush、请求关键帧并返回 Waiting。尚需补模拟后端失败/恢复超时测试。
6. 16位wire序号经展开器进入 source（普通callback尚未恢复metadata，此项尚不能算实机验证）。
7. Opus settings 保持会话内样本序号，不因同格式header重来归零；明确48k限制。close检查stop结果，失败不标Idle并拒绝重连，及时清空request凭据。上述异常backend分支只有静态核对和编译，尚需注入回归。

## 实际验证

日志目录 `logs/remoteplay-audit-20260911/`，新增 `tests/remoteplay/SourceTests.cpp` 通过friend测试入口访问真实source，编译独立实现，不用shadow头也不复制生产实现。

- `native-source-build.log`：从全新目录配置并构建254目标，末尾因FFmpeg头的conversion warning被/WX升级失败。只把第三方include标为SYSTEM，保留自有代码/WX；`native-source-build2.log`后续配置/编译/链接成功。
- 完整构建入口：忽略目录 `out/remoteplay/audit-20260911/build-native-source.cmd`，vcvars64＋UTF8＋Windows TEMP，CMake目录 `native-source-repair`；传固定stage/verify、PrefixPath、PROTOC、PKG_CONFIG_EXECUTABLE、`VEYRA_RP_FFMPEG_ROOT=C:/veyra-deps/installed/x64-windows`。构建真实Chiaki adapter/native probe/core/source targets。
- 新source目标输出 `REMOTEPLAY_SOURCE_REGRESSIONS_PASS`，`source-fixed.stdout.log`：PCM 480/480、相对PTS30ms、后续到达抖动仍按样本前进、断点分段；split config首帧Frame/Streaming；SourceInfo1920→真实1280；重排10帧错配0。
- 新编译core 67/67，native初始化exit0：`core-fixed/native-fixed.stdout.log`。
- 所有运行均经 `scripts/run-short-test.ps1`、30秒上限；本地合成素材ffmpeg每次60秒上限。`scripts/remoteplay/make-source-fixtures.py --output <ignored-directory>` 生成可重现fixture，不提交媒体。
- 本轮初次诊断编译误用了旧shadow头而失败，重新生成后成功；另一个短暂源码编辑误把connect guard放入析构，在构建前读回时发现并改正。没有把这些状态作为通过证据。
- 暂存基线时 `git diff --cached --check` 发现之前未跟踪Markdown的硬换行和patch上下文空白；此前 `git diff --check` 不覆盖未跟踪文件，应据此理解旧格式结论。patch上下文空白不能删除，否则会改变补丁。当前源代码diff检查通过。

## 后续必须继续，不算整体完成

1. 恢复真实metadata通道并验证16位回绕、实际profile；完善stage全文/index/子模块校验。
2. H.265、配置切换、坏包→IDR、stop失败、同格式音频重启/非48k、PCM随机粒度等补回归。尚未完成source层全部18项验收。
3. 正式生产CMake OFF/ON闭环，把解码owner与GPU解耦、复用同一EngineController/run/graph/presenter。
4. 独立音频owner、DPAPI、配对/取消/PIN、Win32入口、手柄、发现/唤醒。
5. 完整Veyra构建/delivery、PS5实机声音/画面/输入/增强/重连和延迟验收。

没有关闭用户播放器、占用采集卡、连接PS5、执行NVIDIA runtime或完整产品测试。节点一测试只证明上述真实FFmpeg与离线行为，不证明串流可用。原交接第18节是修复前缺陷账本，当前实现状态以本文节点证据更新。
