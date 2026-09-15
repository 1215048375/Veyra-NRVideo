# 视频播放列表

用户授权本地实施播放列表，未授权发布。复用 EngineController::open 和共享增强图，不改变解码、音频、HDR、运行组件或导出。

实现：独立可调整大小的列表窗口；视频多选/拖入、双击播放、上一项/下一项、上下移、移除/清空；顺序播放默认，到末项停止，可选列表循环/单项循环。打开单个视频加入列表，图片和采集不入队；停止/失败不自动跳转。每次 EOF 只消费一次且绑定 sessionId，避免旧会话触发连续切换。移除当前条目继续播放现有视频，但解除自动续播。列表本次在会话内保留，不自动恢复播放。

验收：队列边界、重排/删除当前索引、旧会话/重复 EOF、循环与手动切换测试；编译 UI 翻译单元和完整构建（依赖可用时）；记录实际执行范围。当前 checkout 没有 out、runtime_local、third_party_local 或 C:/veyra-deps，不能引用历史 GPU 测试作为本次验收。

## 实施与续接

源码基线 `6a54659`，开工时工作区干净。新增 `include/veyra/engine/Playlist.h`、`apps/veyra/ui/PlaylistWindow.{h,cpp}`；`AppShell.cpp` 接入底栏/专业侧栏/全屏列表入口、Ctrl+L、Ctrl+PageUp/PageDown、多文件拖入及 sessionId 绑定的 EOF 消费。旧“最近打开”移至列表窗口保留。移除当前项继续显示现有媒体但不续播；停止保留列表导航位置，解除自动续播；打开图片/采集/PS5解除列表会话。打开失败停在当前条目，沿用播放器现有错误状态，不无限跳过重试。

未新建解码/NGX/音频或导出管线。列表上限4096，文件名支持Unicode，重复路径不重复追加，排序使用用户明确的列表顺序；本次不保存跨重启列表。普通“打开”仍可选择单个视频或图片；批量选择入口为列表内“添加视频”。主窗口多文件拖入追加并播放首个视频，列表窗口拖入只追加，不打断当前播放。

新增 `tests/unit/PlaylistTests.cpp`、`tests/integration/PlaylistWindowTests.cpp`，CMake登记两个目标；新增 `scripts/acceptance/playlist.ps1` 提供只依赖MSVC/Windows SDK的可重复检查（每次独立配置目录、300秒上限、测试只操作自己创建的窗口句柄）。双语README标注开发分支未发布，说明入口和保留范围。

## 实际命令和证据

运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/playlist.ps1 -Root .`：

- MSVC 2022 Community v17.14.12 环境，C++20；队列21检查，`/W4 /WX`通过。
- 真实列表窗口735检查通过：96/144/192 DPI，500×360、640×480、900×600；各控件包含/互不重叠，双击、上一项/下一项、移动、删除、清空、最近打开回调和循环选项。
- 既有 UiContractTests 通过：默认全关、模式/布局、音量PCM与设置保存/损坏保护；实际命令传入全新临时配置目录。
- `AppShell.cpp` 分别在普通和 `VEYRA_ENABLE_REMOTEPLAY` 定义下编译通过；产品 `PlaylistWindow.cpp` 编译并链接进窗口测试。此项是产品翻译单元验证，不等于完整 Veyra.exe 链接。
- 日志 `out/playlist/acceptance-a2ff5c0d3e4c4eb6a4e95fcd4e1e9dc5.log`；最后将测试窗口句柄改为API直接返回后复测日志 `out/playlist/acceptance-597747bc4c864ee9b85ed9137de7a48b.log`。
- `git diff --check`通过；未加入SDK、DLL、模型、媒体、配置或日志到源码；未修改运行库、patched FFmpeg，也没有push/Release。

失败保留：窗口测试初次清理处命名空间错误（`window-check.log`），随后缺少Theme使用的日志链接对象（`window-check-2.log`），补正后 `window-check-3.log` 通过。首次统一脚本复用既有UI测试目录，触发“missing preferences do not enable effects”；修正为每次GUID独立目录，并提前取得进程句柄确保Windows PowerShell读取退出码，原失败在 `acceptance.log` / `acceptance.stderr.log`。

尝试完整构建入口 `cmd /c out/playlist/full-build.cmd`，指定的Visual Studio内置CMake路径不存在，退出失败，记录 `out/playlist/full-build.log`。PATH未发现CMake/Ninja，且该checkout缺少已批准本地SDK、patched FFmpeg开发库和runtime。完整构建、`delivery.ps1`、真实媒体连续播放/音画尾部、NR/FG Create/Evaluate、实卡检查均**未执行**；不能根据上述独立测试声称完整应用验收通过。

下一步唯一任务：取得本机完整依赖/开发目录，生成完整Veyra并实播短视频队列，验证自动衔接、失败停止、暂停/seek、跨分辨率与HDR切换及原delivery回归。没有此证据前不标记软件交付验收完成，不发布。

## 2026-09-15 播放控制扩展

按用户追加需求实现：进度条命中高度 12→28 DIP、滑块直径 10→18 DIP；主控制区新增上一个、下一个、从头播放、播放方式切换和独立列表循环。顺序/随机/单曲循环与列表循环分别设置，默认顺序且不循环。随机模式生成无重复的排列，上一项按相同排列返回，开启列表循环后重复此排列。主窗口“打开”支持多选并追加、播放本次选择的第一个视频；单张图片仍可打开，列表内“添加”只追加不打断播放。本次设置和列表仍仅在应用会话内保留。

主窗口底栏增加 48 DIP，为进度条与按钮留出空间；普通、专业、小窗口、全屏布局同步调整。继续复用 EngineController 的 open/seek/pause 和原 EOF session 保护，单曲循环不限制手动切换。打开/停止过渡期间不接受从头播放，避免冲突。

实际验证：`scripts/acceptance/playlist.ps1 -Root .` 通过 83 个队列检查、865 个真实 HWND 窗口/进度条检查（96/144/192 DPI）、UiContractTests 四种 DPI 布局与既有设置/PCM检查；AppShell 普通/REMOTEPLAY 两种编译均通过。日志 `out/ci-dependencies/transport-tests-final.log`。

完整构建使用 VS x64 toolchain、项目本地 CMake/Ninja，执行 `cmake --build out/ci-full --target veyra --parallel 4`，成功链接 `out/ci-full/veyra.exe`，日志 `out/ci-dependencies/transport-build-unrestricted.log`。前两次沙箱内构建被 Git 用户配置权限告警/子模块进程阻断（transport-build.log、transport-build-final.log），正常权限下依赖校验及构建通过；没有修改或跳过依赖身份检查。

这替代上文当时“缺少依赖、无法完整构建”的状态。实际媒体连续播放、音画尾部、跨 HDR/分辨率切换、GPU/NR/FG Create/Evaluate 及完整 delivery gate 本轮未执行；不得将队列/窗口验证当作实卡验收。下一步唯一验收任务：使用实际媒体列表验证连续播放、从头播放和随机/循环衔接。未 push、发布或新增 proprietary 文件入 Git。
程序启动短测：out/ci-full/veyra.exe --smoke-empty --smoke-seconds 5 --smoke-view daily / small / fullscreen，三个模式均正常退出（exit=0），日志 out/ci-dependencies/transport-smoke.log；属于空媒体启动检查，未观察真实媒体画面。git diff --check 通过。
