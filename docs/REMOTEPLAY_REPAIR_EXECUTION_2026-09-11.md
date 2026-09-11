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

## 节点二：真实 metadata、共享产品入口与独立 owner（进行中）

已接入代码并在 `out/remoteplay/product-repair` 链接完整 `veyra.exe`；仍等待最终 UI / delivery 检查和用户 PS5 验收。

- `0002-chiaki-video-metadata.patch` 从用户 Code01 变更描述生成，经固定上游对应上下文核对；backend 使用真实 wire frame index / profile 尺寸。没有这份扩展时编译失败。原 MSVC patch 保留。
- `verify-chiaki-stage.py` 用隔离临时 Git index 从 pin＋两份 patch 重建预期 tree，比较实际完整内容；检查 clean checkout、stage HEAD/index、递归子模块与额外源码。`stage-rejection-test.log` 实际证明：同一允许文件里加一处额外改动会被拒绝，恢复后通过。没有覆盖用户依赖改动。
- `cmake/VeyraRemotePlay.cmake` 为产品和 native probe 共用构建；根 `VEYRA_ENABLE_REMOTEPLAY` 有显式 OFF/ON，OFF 不编 source/backend；ON 从固定源码编 `chiaki-lib`，不再要求悬空手填 LIB。
- `RemotePlaySessionSource` 的网络＋FFmpeg owner 持续解码，容量一 mailbox 只覆盖已解码画面并传播历史断点。GPU 继续使用同一个 `EngineController::run` / `EnhanceGraph` / Presenter / LiveGpuScheduler。首次解码后才建实际尺寸资源，尺寸变化排空并重建图。
- 独立 PCM feeder 消费 source，相对音频 PTS 推进；复用 `CaptureAudioSession` 的通用 PCM/WASAPI owner 和软件呈现补偿，没有创建 DirectShow 音频设备或复制播放器。停止顺序为 feeder join → WASAPI stop/join → Chiaki stop/join → decoder cleanup。
- `ProfileStore` 使用当前 Windows 用户 DPAPI，加密完整 profile，版本与长度校验、同目录临时文件落盘后原子替换；不写明文凭据、命令行或日志。单个保存 profile 位于 localDataDirectory 的 `remoteplay-profile.dat`。
- `RemotePlayPanel` 提供手填地址、局域网查找、Account ID、配对码、720p/1080p ×30/60、H264/H265 SDR、配对/取消/连接/唤醒/登录 PIN。工作线程不操作 HWND，定时器接结果；关闭面板不停止已连接串流。
- SDL3 3.4.14 通过本机 vcpkg 静态构建，仅用于手柄。Win32 UI owner 独立于 GPU 轮询；失焦输出 neutral，网络 owner 对超过100ms未更新输入归零。映射基础按钮/双摇杆/双扳机/PS/触摸板点击；未实现触摸板坐标、陀螺仪、麦克风、自适应扳机/震动反馈。
- 本次新增代码、两份 patch 与 SDL/Chiaki notices 均为源码/文本；没有 SDK/DLL/LIB/凭据进 Git。SDL 源与安装产物仍在项目外依赖目录。首次 vcpkg 命令因根目录 manifest 模式拒绝单包安装，改到 `C:/veyra-deps` 后安装成功，用时27秒。

### 当前实际证据

日志均在 `logs/remoteplay-audit-20260911/`：

- `metadata-build.log` / `shared-cmake-build.log`：真实修改后 Chiaki、adapter、native/source probe 编译成功；`metadata-source` / `metadata-native` exit0。native 标签改为 `metadata_api_available=1 session_started=0 video_callbacks_observed=0`，不再把接口存在叫回调次数。
- `product-build.log` 首次全新产品314步成功；`product-engine-build` / `product-panel-build` / `product-input-build` 增量编译链接成功。`product-full-build.log` 经正式 `scripts/build.ps1 -RemotePlay` 全目标追加112步通过，脚本返回exit0并复制既有FFmpeg运行依赖。
- `hevc-source-fixed.stdout.log`：真实H264/H265均解出首帧并进入Streaming，两种流均覆盖65535→0、PTS仍增长；重排10张输出错配0；PCM检查通过。最初H265合成单帧使用默认B帧编码，正确返回Waiting而使“立即输出”断言失败；将明确首帧用例编码为zerolatency后通过，没有强制改decoder来掩盖B帧延迟。
- `profile-test.stdout.log`：实际Windows DPAPI roundtrip / replace / ciphertext损坏拒绝 / invalid host拒绝通过；只有合成凭据。
- `boundary-test.stdout.log`：真实session owner的invalid connect→close→reopen三轮、SDL初始化、失焦neutral通过；后续已追加decoded mailbox真实方法测试，等待新编译回归。
- `file-source-test.stdout.log`：23 checks / 0 failures；`ui-contract-test.stdout.log`：384布局组合、PCM gain/mute、设置持久化/损坏检查通过。

下一步：补最新 owner mailbox 回归，Remote Play OFF 全新构建、面板实测与完整 delivery；完成后本地Git节点，再交用户PS5连接。实际PS5码流、硬件手柄输入、声音同步与增强性能尚未验证。当前Remote Play解码明确是FFmpeg软件解码，不能声称D3D12硬解已接入。


### 节点二收口前追加验证

- `product-mailbox-build.log` 增量8步成功；`boundary-final.stdout.log` 实际调用产品 `publishDecoded`：容量一覆盖计数、交付最新帧、历史断点与消费后不重播通过，连同invalid连接重开和SDL失焦测试全部exit0。
- `scripts/remoteplay/test-ui.ps1` 实际启动新exe的隔离empty smoke：查找PS5按钮、打开/关闭面板2次、空参数配对被本地拒绝、正常退出，`ui-panel-test.log` PASS；未发送网络请求。截图 `ui-panel/remoteplay-panel.png` 已人工查看，文字/输入/按钮完整、无白底旧控件遮挡；截图前清空Account ID和PIN。
- 以官方SDK/运行时执行的delivery尚未运行。Remote Play OFF全新目录构建正在执行。上述截图/自动行为不证明PS5连接。
