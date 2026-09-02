# Veyra 项目 Agent 执行规则

本文件对在本目录工作的所有 Agent 生效。不要只扫标题；开工前必须完整阅读：

1. `README.md`——当前状态和唯一入口；
2. `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`——唯一施工手册；
3. `VEYRA_PRODUCT_SPEC_V1.md`——产品与技术边界；
4. 当前阶段的 `docs/WORKLOG.md`——已完成、失败记录和下一步。

如果任务以 Goal/无人值守模式运行，还必须先完整阅读：

5. `loop/LOOP_ENGINE.md`——循环、恢复、审查和停机协议；
6. `loop/STATE.json`——当前真实阶段和下一动作；
7. `loop/BACKLOG.md` 与 `loop/INBOX.md`——唯一任务队列和人工阻塞；
8. `loop/REVIEW_PROMPT.md`——阶段独立复核的固定只读任务。

Goal 启动后，`.gitignore`、上述规则/方案、`loop/CONTROL_HASHES.json`、
基础 `scripts/loop-gate.ps1` 和 `scripts/gates/README.md` 都是只读控制面。
preflight 报控制面 hash 漂移时必须停下，不得改 manifest 或 gate 自我放行。

若施工手册与 Product Spec 冲突，以施工手册中更具体、更新的实现指令为准；若本文件与施工手册冲突，以本文件的安全、许可证和阶段门禁为准。

## 不可擅自改变的决定

- V1 是 Windows x64、C++20、Win32、D3D12 项目。
- V1 直接调用 NGX；不要同时接入 Streamline。Streamline 仅作未来替换方案和文档参考。
- 固定帧 harness 分两步：Phase 1 直接生成 RGBA8 Proxy → Feature 18 → Raw 抓帧；Phase 2 才加入 Original → Parity Encode → Feature 18 → Parity Decode。不是先做完整播放器。
- `nvngx_dlssnr.dll` 是 DLSSNR 运行时；只从项目根目录的已知文件复制到 `runtime_local`，不得联网寻找“更新偷跑版”、不得修改、重签名或提交 Git。
- `renodx-dlss5-1.addon64` 是未签名的 ReShade/RenoDX 二进制 add-on，不是配置文件。它只准用于隔离的参考/对照环境，最终程序不得加载、注入、链接或随包分发它。
- 最终主线不依赖 ReShade。所谓“类似 ReShade”指独立复现它的前后颜色传递、参数映射和输出处理。
- V1 先做 SDR、同分辨率 DLSSNR、Zero Guidance，再按阶段做 DLSS SR、NVOF、DLSSG 2X。HDR、3X/4X、采集卡、Depth Anything、FRUC 都不是 V1 主线。
- 已接受 Magpie Experimental 对“可调用”的可行性证明，不再做市场/画质可行性研究。但 Create/Evaluate、格式、状态、时序、资源生命周期仍必须做工程验证。

## 二进制身份，任何不一致都立即停工

```text
nvngx_dlssnr.dll
  SHA256: E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
  Size:   165840496 bytes
  Version: 310.8.0.0
  Signature: Valid, NVIDIA

renodx-dlss5-1.addon64
  SHA256: 837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F
  Size:   359424 bytes
  Signature: NotSigned
```

如果 hash 不同，不要“先试试看”；记录实际值并询问用户。

## 阶段门禁

严格按 Playbook 的 Phase 0→7 完成 V1；Phase 7 通过后停止，不再自动扩展。每个阶段必须同时满足：

1. 代码可构建；
2. 本阶段列出的自动或手工检查通过；
3. `docs/WORKLOG.md` 写明命令、结果、日志路径、失败与修复；
4. 没有把 proprietary runtime 或本地 SDK 提交进版本控制；
5. 下一阶段没有建立在未验证假设上。

Phase 1 没有完成连续 300 次 Feature 18 Evaluate，就禁止开始 FFmpeg、UI、音频、SR 或 FG。

## 无人值守 Goal Loop

- 用户以 `loop/GOAL_PROMPT.md` 启动 Goal 时，允许在阶段硬门禁和独立 Reviewer 均通过后自动进入下一 Phase；安全、许可证和验收规则没有自动豁免。
- 主 Agent 是唯一写入者。禁止多个 Agent 同时修改同一 checkout；阶段 Reviewer 必须是新上下文、只读，不能替 Maker 改代码。
- 每个 cycle 只处理 `loop/BACKLOG.md` 当前 Phase 的第一个可执行原子任务。改动前写 `loop/JOURNAL.md`，改动后运行当前 gate。
- 统一 gate 入口：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phaseN`。
- 同一 failure fingerprint 最多尝试 3 个真正不同、能增加证据的方案；连续 5 轮无新证据或总计 80 轮后必须留档停机，不得空转。
- `loop/STOP` 是用户停机开关。看到它就更新状态并退出，Agent 不得删除。
- 未实际获得独立 Reviewer 结论时只能标 `needs_review`；不能降级成“自己再看一遍”后自动放行。
- 只准本地 checkpoint commit。禁止无人值守 push、PR、发布、上传 artifact 或制作安装包。

## 实现纪律

- 先检查现状：`git status`、`rg --files`、hash、工具版本。不要覆盖用户已有改动。
- 每次只实现当前阶段最小闭环，不顺手重构整个工程。
- 任何 NGX/NVOF 返回值、HRESULT、SEH、资源尺寸/格式和 GPU timestamp 都必须进入日志。
- 所有历史型模块在 open/seek/resize/pause-resume/scene-cut/device-lost 时显式 reset。
- 正常播放路径禁止 GPU→CPU 像素回读、每 pass CPU fence wait、无界帧队列和多份隐式颜色转换。
- 创建 Feature 可以等待一次；逐帧执行用 3–4 个 command slots/fence values 轮转，不能每帧 `WaitForSingleObject`。
- NGX 参数名和参数类型必须逐项照 Playbook；不要凭名字猜 `int`/`uint32_t`/`float`/resource。
- 所有 DLL 用绝对路径、`LoadLibraryExW` 和受限 search flags 加载；禁止依赖当前工作目录搜索。
- 不允许把 `renodx-dlss5-1.addon64` 改名为 DLL，不允许尝试从中 `GetProcAddress` 当普通库调用。
- 不允许在磁盘上 patch `nvngx_dlssnr.dll`。调用方兼容层必须独立封装、运行时可关闭，并明确标记 local experimental only。
- C++ 层保持 RAII；逆序释放 Feature、parameter block、runtime、NGX Core、D3D12 resources。
- Shader 先独立测试。颜色结果异常时先查 transfer、range、resource format、subrect、state barrier，别先调“画质参数”。

## 许可证与分发红线

- Magpie 是 GPLv3。闭源 Veyra 不得复制其源码或做机械改名；只可把其公开行为当作黑盒/接口参考后独立实现。若要直接复用，先让用户明确接受 GPLv3 以及对应源码义务。
- NVIDIA SDK/runtime 和当前 DLSSNR 文件的分发权不能假定。默认只做本机研发，`runtime_local/`、`third_party_local/`、抓帧和 SDK 压缩包必须 gitignore。
- 不得上传、发布、打包或向第三方发送项目中的两个二进制。
- 任何准备公开发布、签名安装包或 CI 上传 artifact 的动作，都必须先停下并让用户确认许可证与分发来源。

## 每次交付必须报告

- 当前 Phase 与完成门槛；
- 修改的文件；
- 实际运行过的构建/测试命令；
- Create/Evaluate 或失败码的真实日志，不得用“应该可以”代替；
- 已知风险和下一条唯一任务；
- 若没实际在 RTX/NVIDIA runtime 上执行，明确写“未执行”，绝不能声称成功。
- Goal 模式还必须同步更新 `loop/STATE.json`、`loop/EVIDENCE.md` 和 `loop/JOURNAL.md`，保证上下文压缩或进程中断后可从磁盘恢复。

## 禁止用假完成糊弄

以下均不算完成：只写接口桩、只编译未运行、仅显示理论 FPS、把 Raw NR 直接展示却声称完成 RenoDX parity、以 Zero Motion 冒充 NVOF、以重复上一帧冒充 DLSSG、或捕获到黑图仍把返回码 0 当成功。
