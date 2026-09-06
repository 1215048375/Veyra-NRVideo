# Veyra Launch V1 施工手册

版本：1.2（强 Agent 接管恢复版）

日期：2026-09-06

适用范围：Phase 5–7；Phase 0–4 已有基础代码与历史证据。
目标读者：可以执行命令和写 C++，但不应被迫猜架构、参数方向或验收口径的 Agent。

## 0. 本手册的权威顺序

1. 安全、二进制、许可证与 gate 规则以 `AGENTS.md` 为最高优先级；
2. 产品必须解决什么以 `VEYRA_PRODUCT_SPEC_V1.md` 为准；
3. 如何实现以本文件为准；
4. 当前唯一任务以 `loop/STATE.json` + `loop/BACKLOG.md` 为准；
5. 已经真实做过什么以 `docs/WORKLOG.md` 与 `loop/EVIDENCE.md` 为准。

旧的“采集卡/深度/导出不是 V1”决定已经作废。不要从 Git history 复制旧路线回来。

## 0.1 2026-09-06 接管结论：先纠正完成状态

新 Agent 不得从 `STATE.json` 旧的“Phase 5 passed”字面继续 Phase 6。对当前树的对抗式审查发现：

- Phase 0–4 的 checkpoint 和实测证据继续有效；
- Feature 18、SR、parity、NVOF、DLSSG、WASAPI、Present 的单项/组合 probe 有真实价值，禁止推倒重写；
- 旧 `phase5.ps1` 允许只有 depth manifest 时通过，并把第二次 1080p endurance 放在 4K 检查位置；
- `src/core/EnhanceGraph.cpp` 只复制 packet 和增加计数，注释明确真正 GPU pipeline 仍由 harness 编排；
- `tools/player_probe/main.cpp` 约 3600 行，是实验集成场，不是可复用产品 engine；
- 最新 t10 播放器矩阵虽已解决 teardown 崩溃，但所有场景仍为 FAIL：drift P95 约 2.8 秒，且 L2/GBV 报大量 descriptor-uninitialized；
- Phase 7 产品文件基本不存在：没有 `veyra_app`、CaptureCardSource、ImageExportSink、VideoExportSink 或完整 Depth provider。

因此 Phase 5 必须重开为 `in_progress`，Phase 6/7 锁定。历史 Phase 5 commit 只作为组件证据来源，不再是 `lastGoodCommit`。当前可交付 Launch V1 进度约 40%，不是按 6/8 Phase 计算的 75%。

## 0.2 实验 DLSS 5 后端决定

NVIDIA 已正式面向合作游戏发布 DLSS 5，但公开 Developer/Streamline 包尚未提供可供 Veyra 直接替换的通用 DLSS 5 接口。用户于 2026-09-06 决定继续使用固定 hash 的 `nvngx_dlssnr.dll` Feature 18 做本机研发。

执行含义：

1. 保留并复用 `DlssNrRuntimeAdapter`、签名 snippet 和已验证参数契约；
2. 不再等待“官方 SDK 上线”才施工；
3. 不代表效果与官方游戏路径等价，除非同源 A/B 证明；
4. runtime 继续外置、可关闭、精确 hash 校验、绝对路径加载；
5. 不从游戏、驱动缓存或网络替换版本，不 patch、不提交、不打包、不上传；
6. 功能全部完成而分发权仍未解决时，最终状态只能是 `distribution_blocked`。

## 0.3 强 Agent 接管恢复计划（唯一执行顺序）

下面 R0–R12 是当前施工主线。`loop/BACKLOG.md` 提供同一顺序的可勾选原子项。任何更早 R 项未达到 exit 0/Reviewer 条件，不得跳到后面做 UI 或导出。

### R0：保护现有工作树并建立可信基线

目的：保存原 Agent 未提交的真实成果，同时避免把失败状态误标 checkpoint。

第一组命令必须原样执行：

```powershell
git status --porcelain=v1
git diff --stat
git diff --check
git log -10 --oneline
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release
```

已知接管基线：preflight 70/70 和 Release build 曾于 2026-09-06 exit 0；控制面更新前有 31 个 status entry，完成本次文档重基线后为 44 个（其中包含本次计划/状态修改）；`validation/fixed_clips/test_h264_1080p.mp4` 处于删除状态；`loop/local/fixed_clips/test_av_{1080p,4k}.mp4` 为已忽略本地素材。新 Agent 必须重新确认，不能抄这个结果。

动作：

- 对 `git diff --binary` 计算 hash 并写 JOURNAL，作为接管指纹；
- 不执行 reset、clean、checkout 或整树格式化；
- 逐文件把未提交代码分为 `keep / repair / generated-or-obsolete`，在 JOURNAL 列表化；
- 受版本控制测试片的删除先保持原状。R1 改为确定性生成 corpus 后，再单独向用户报告是否应提交该删除；
- 失败代码可以在当前工作树继续修，不能把失败矩阵提交成 `phase-6:` checkpoint；
- 先为当前源码跑最窄的 NVOF、FG、audio 和 player probe，日志写入新的 run-id，避免依赖旧 t10 文件。

R0 完成证据：preflight/build exit 0、接管 diff hash、四类资产清单、没有专有文件被跟踪。R0 不是 Phase gate。

### R1：重写 Phase 5 gate，先证明会失败

目的：删除旧 gate 的两个假通过口，不删历史日志。

只修改 `scripts/gates/phase5.ps1` 及为 gate 服务的确定性 clip generator。新 gate 必须先在当前实现上 exit 1，至少准确命中：

- `EnhanceGraph` 没有真实提交完整 GPU graph；
- DAV2 未实现时只能报告明确 Auto fallback，不能因 manifest 存在宣称 provider 完成；
- native 4K 路径没有当前 run 的真实 extent/Feature 18/NVOF/VRAM 证据；
- 30 分钟要求不能改成 5 分钟；
- current executable/input/config/runtime hash 或 run-id 缺失；
- 主路径出现全帧 readback、每 pass CPU wait 或无界队列。

新 gate 输入不得依赖已删除的 tracked MP4。使用 `veyra_clip_gen` 在 `loop/local/fixed_clips` 或本次 `logs/phase5/<runId>/inputs` 生成 1080p60 与 4K60 translation/occlusion/cut/ui/particles 片，记录生成命令和 SHA256。

新 gate 必查 JSON：

```text
sourceExtent, workingExtent, outputExtent
sourceFrames, processedFrames, nrEvaluateCount, nvofExecuteCount
guidanceProvenance, nonZeroMotionCount, confidenceP05/P50/P95
depthMode, depthFallbackReason, depthAgeP95
resetCountsByReason, sceneCutCount, crossCutHistoryCount
gpuPassP50/P95, normalPathReadbackCount, cpuFenceWaitPerFrame
queueHighWater, resourcePoolPeak, vramBudgetHeadroomMiB
workingSetStart/Peak/End, deviceRemovedCount
exeHash, inputHash, configHash, runtimeHash, runId
```

R1 完成证据：新 gate 对当前缺陷 fail closed；失败项与本计划一致。绝不先改阈值让它绿。

### R2：把数据契约补全到产品规格

当前 `FramePacket/FrameWindow/GuidanceFrame` 只能算草稿。按下列文件拆分并保留兼容迁移层：

```text
include/veyra/pipeline/FramePacket.h
include/veyra/pipeline/FrameWindow.h
include/veyra/pipeline/GuidanceFrame.h
include/veyra/pipeline/ResetCoordinator.h
src/pipeline/ResetCoordinator.cpp
tests/unit/PipelineContractTests.cpp
```

必须补齐：

- PTS/duration 使用 rational 或 100ns 整数，禁止 `uint64 ptsUs` 无法表达负/未知 PTS；
- `ColorDescription` 含 format/range/matrix/transfer/primaries/rotation/SAR 和 assumed 标志；
- `FrameFlags` 含 open/seek/cut/drop/duplicate/resize/discontinuity/pause-resume/device-lost/EOS；
- GPU resource 不裸借用无生命周期信息：至少携带 owner slot、expected state、ready fence/value、extent/format；
- `FrameWindow` 固定 `prev/current/next` 和 `lookaheadFrames=0/1/2`，删除可无限增长的 vector；
- motion 为 current→previous、post-SR working pixels；depth R32F；confidence R8_UNORM；记录 provenance/age/sourceSequence；
- reset 为单调 epoch，所有 SR/NR/FG/NVOF/depth/cadence 在同一 real-frame boundary 消费；
- unit tests 覆盖 8 类 reset、负/缺失 PTS、跨 epoch history 拒绝、窗口容量上限和资源 fence 所有权。

R2 完成证据：Debug/Release unit tests exit 0；旧 probe 经适配仍构建；没有复制一套第二契约。

### R3：把真实 GPU 链从 player probe 抽成共享 EnhanceGraph

创建并在 CMake 中实际链接：

```text
veyra_pipeline
  FramePacket/Window/ResetCoordinator/SceneCadenceAnalyzer/EnhanceGraph
veyra_guidance
  ZeroGuidanceProvider/NvofGuidanceProvider/GuidanceValidator/DepthAnythingProvider
veyra_sources
  MediaFileSource（先实现）
veyra_sinks
  D3D12PresentSink/WasapiAudioSink（先迁移）
```

`EnhanceGraph::process` 必须真正执行且记录：

```text
ingress color normalize
-> SR bypass or DLSS SR
-> scene/cadence
-> NVOF inputs + execute + densify/cost
-> confidence validation + optional depth
-> parity encode
-> Feature 18 Evaluate
-> parity decode
-> optional DLSSG pair generation
-> bounded post mix/clamp
-> ProcessedFrame(real + optional generated + fences/timestamps)
```

迁移原则：

- 从 `player_probe` 提取已经证明过的代码，不复制后再留下两份；
- backend 对象由 render thread 单一所有，RAII 逆序：Feature→params→runtime→NGX core，NVOF 必须 unregister→release registered textures→destroy/unload；
- create/resize 可以一次等待，逐帧使用 4–6 slot 和 GPU fence；禁止每个 pass `WaitForSingleObject`；
- UI、source、sink 不得调用 private NGX 参数或 shader barrier；
- `player_probe` 最终缩为参数解析、组装 source/graph/sink、运行 scenario、写 JSON；目标小于 800 行。不能为了行数机械拆无语义 helper。

R3 最窄证明：同一 `veyra_pipeline` library 被 `player_probe` 和一个 headless integration test 同时链接；两者对同一 300 帧输入产生相同 output/config hash 和 Evaluate 计数；旧 metrics-only graph 测试必须失败。

### R4：完成 Guidance、Depth 与 reset 质量核心

NVOF：

- 使用当前已存在的官方本地 `third_party_local/nvidia/Optical_Flow_SDK_5.0.7`；INBOX 中旧“5.0 缺失”记录已作废；
- 保留 B8G8R8A8 input、R16G16_SINT S10.5 `/32`、grid 4、cost R8_UINT 和当前→前帧方向的十组位移证据；
- 将 `NvOfSession` 包进 `NvofGuidanceProvider`，第一帧/reset 不消费旧 A；
- 至少用 cost、luma warp residual、out-of-frame/occlusion 生成 confidence；离线与 Buffered Quality 加 forward/back consistency；
- confidence 平滑衰减 motion，不用单个魔法阈值把所有低纹理区切成 0。

Depth：

- 先核对 `third_party_local/depth/manifest.json`，它目前只证明候选身份，不证明模型/runtime 已齐；
- 只允许商业条款明确的 DAV2 Small FP16；锁定 URL/version/hash/license/input/output/shape/opset；
- 建 `DepthAnythingProvider`，同一 D3D12 adapter，sequential session，固定 shape；正常路径用 GPU binding，不能在 render thread 同步整帧 readback/upload；
- P02/P98+EMA、current→previous motion reprojection、age、residual、scene cut reset；
- `Auto` 在 missing/late/stale/inconsistent 时明确切 Motion Only，并把原因写 JSON/UI；
- 直到真实 depth visual/statistics 通过，不能把 `NvofDav2` 当默认。

Scene/reset：把现有 CPU analyzer 扩展到实际 GPU histogram/SAD 输入；seek、cut、drop、resize、source switch、pause/resume、device lost 各做一个 integration case，确认 SR/NR/FG/NVOF/depth 的 reset epoch 同值。

R4 质量矩阵必须输出 `off/zero/motion/motion+depth/auto`，保存 flow/depth/confidence/reset 可视化和 timing。画质差的样本允许 Auto 回退，但不得静默。

### R5：重新关闭 Phase 5

按 R1 的 gate 跑完整 1080p60 和 native 4K60 各 30 分钟。4K 必须由 JSON 与资源描述符同时证明 working extent 为 3840×2160；不能只看窗口或输出文件尺寸。

通过顺序固定：

1. Release build；
2. unit/integration/quality corpus；
3. `loop-gate phase5` exit 0；
4. 新上下文只读 Reviewer 独立重跑；
5. 修完 P0/P1 后再次 gate+Reviewer；
6. checkpoint；
7. 才把 STATE Phase 5 改回 passed、Phase 6 解锁。

### R6：Phase 6 正确性与性能修复

先处理现有真实失败，不先扩功能。

1. **GBV id=938**：根据错误中的 command list、root parameter、descriptor index 建最小复现；所有被 root descriptor table 覆盖的槽位在 dispatch 前写入有效 descriptor 或显式 null descriptor。禁止关闭 GBV、过滤消息或减少扫描条数过关。
2. **GPU timing**：为 upload、YUV、SR、NVOF input/execute/densify、confidence、parity encode、NR、decode、FG、composite、present 分配 query-heap timestamp；ResolveQueryData 到小型 timing buffer 可异步读取，不能每帧等待。
3. **去串行化**：定位 P95 超预算 pass；消除重复 4K blit、重复颜色转换、per-pass fence wait 和临时纹理；使用 keyed resource pool 与 4–6 command slots。
4. **A/V scheduler**：WASAPI audio clock 为主；Player 不丢真实源帧，慢于实时就明确暂停/降 feature，不允许 lateness 积到秒级；seek 原子 flush/re-anchor/reset。
5. **FG cadence**：59/59 truth 证据保留；产品路径再次证明 generated 非重复/非 blend、PTS 严格中点、cut/duplicate/drop 不跨界。
6. **延迟窗口**：NR Low Latency=0、FG Low Latency=1、Buffered Quality=2；队列上限写在类型和 JSON，不以实际暂时没增长代替约束。
7. **teardown**：保留 s10 的 drainQueue 和 NVOF 逆序释放；每次修性能后运行 L0/L1/L2/no-feature 矩阵，禁止 `ExitProcess` 掩盖析构问题。

Phase 6 快速门槛：1080p 与 4K scenario 均 `driftP95<=50ms`、drop=0、GBV error/corruption=0、readback=0、自然 return。随后跑 4K30/60 各 30 分钟和 Reviewer。

### R7：建立真正的应用壳与 Player tab

在 Phase 6 gate 通过后创建 `apps/veyra/veyra_app`，Win32 单进程、一个 render engine、三个 tab。先做 Player vertical slice：

- `MediaFileSource`：MP4/MKV/MOV，H.264/HEVC，D3D12VA 优先和有提示的软件 fallback；
- `EngineController`：UI command queue、source/render/audio 生命周期；
- `D3D12PresentSink`：窗口/全屏、VSync/tearing、resize；
- `WasapiAudioSink`：event mode、pause/seek/stop；
- Player UI：open/play/pause/seek/loop/fullscreen、output extent、NR/SR/FG/quality；
- 外置 SRT 和首条内嵌文本字幕；libass 合成在 FG 后；
- Diagnostics：runtime identity、Feature state、guidance、reset、FPS、GPU ms、queue、drop、A/V drift；
- 所有 fallback 在 UI 黄色显示，Feature failure 不得显示原图同时声称开启。

Player tab 必须驱动与 probe 相同的 library，不接受 UI 内复制 pipeline。

### R8：CaptureCardSource 与 Capture tab

具体来源：FFmpeg `libavdevice` 的 `dshow`，设备列表优先用 DirectShow COM 枚举 friendly name；实际打开仍由 FFmpeg library 完成。

实现顺序：

1. CMake/vcpkg 确认 avdevice；
2. 枚举 video/audio 和真实 media types；
3. 打开用户选定的 1080p/2160p 30/60 SDR；回读并显示实际 codec/size/fps/color；
4. video latest-frame mailbox 容量 1，覆盖计 drop 并触发 reset；audio ring 有界；
5. 接同一个 EnhanceGraph 和 Present/Audio sink；
6. UI 暴露三种延迟模式和 lookahead 帧/内部毫秒；
7. P010/HDR fail closed；不支持的私有设备明确报告；
8. 真实 4K60+HDMI audio 30 分钟验证。没有真实硬件时只能 BLOCKED，不能用虚拟摄像头过最终 gate。

### R9：图片导出

`ImageSource`/`ImageExportSink` 使用 WIC：PNG/JPEG decode、EXIF orientation、sRGB full；单帧 reset=1，motion/depth Zero，optional SR→Feature18；输出写唯一 `.partial`，decode-back 检查尺寸/非黑/hash 后原子 rename，默认不覆盖。图片模式仍调用共享 graph，禁止另写 Python/OpenCV pipeline。

### R10：D3D12 NVENC 视频导出

前置：用户从 NVIDIA 官方取得 Video Codec SDK 13.1 并放入 `third_party_local/nvidia/Video_Codec_SDK_13.1.0`。若缺失，R10 标 BLOCKED，但可继续不依赖它的 R7–R9/R11。

实现文件：

```text
include/veyra/sink/NvencD3D12Encoder.h
src/sink/NvencD3D12Encoder.cpp
include/veyra/sink/VideoExportSink.h
src/sink/VideoExportSink.cpp
tests/integration/VideoExportProbe.cpp
```

必须严格使用系统 `C:\Windows\System32\nvEncodeAPI64.dll` 和 SDK 13.1 header：查询 H.264/HEVC/4K/async/input format；预分配 4–8 slot；注册 D3D12 NV12 resource；input/output fence point；每 slot map/encode/unmap，不能每帧 register；只读取压缩 bitstream；FFmpeg `libavformat` mux MP4/MKV；音频 remux 或 AAC；字幕复制/转换/明确拒绝。

FG 2X 时 generated PTS 严格落在相邻 real PTS 中间。完成后 ffprobe+self-decode 检查 codec、3840×2160、FPS、帧数、duration、音轨、首尾非黑。cancel/crash 只删除本 job `.partial`，成功文件不动。

### R11：产品行为与三页 UI 收尾

- Capture/Player/Export 三 tab 全部调用同一个 controller；
- `%LOCALAPPDATA%/Veyra` 保存设置，仓库 config 只提供默认；
- 首次启动 dependency/capability 检查；
- 最近文件/设备；
- source reconnect、device lost recreate；
- 残留 partial/job manifest 提示和恢复/清理；
- 日志包导出时排除 runtime、SDK、用户媒体和模型；
- 错误分类与可操作消息；
- 不存在 runtime 时应用仍能打开设置/诊断，但 DLSS 开关 fail closed。

### R12：Phase 7 联合 gate、Reviewer 与发布状态

`phase7.ps1` 必须在一个 run manifest 中分别绑定：

- 4K30/60 Player 各 30 分钟；
- 真实 4K60 Capture 30 分钟；
- PNG/JPEG corpus；
- 4K H.264 和 HEVC export，含 audio/subtitle policy 和 optional FG；
- 设置恢复、device lost/reconnect、cancel/partial recovery、日志导出；
- Git/包内容不含专有 runtime、SDK、模型或用户素材。

五组任何一组缺失均 exit 1。Reviewer PASS 后只能创建本地功能 checkpoint。分发权未解决时 STATE=`distribution_blocked`；禁止制作或上传含实验 DLL 的公开安装包。

## 1. 每次开工的固定动作

在项目根目录依次运行，任何失败先记录，不要修改 gate 自我放行：

```powershell
git status --short --branch
git log -1 --oneline
rg --files
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
```

再读：

```text
README.md
AGENTS.md
VEYRA_PRODUCT_SPEC_V1.md
VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md
docs/COMPETITOR_AUDIT_2026-09-03.md
docs/WORKLOG.md
loop/STATE.json
loop/BACKLOG.md
loop/INBOX.md
```

不要因为 `out/` 有旧 exe 就认为当前源码已通过。每条证据必须绑定 run-id、当前 exe hash、输入 hash、配置 hash 和 runtime hash。

## 2. 当前代码：保留什么

以下是已经存在、应复用而不是重写的基础：

```text
src/base + include/veyra/*
  Logger, HRESULT/NGX result, FileIdentity

src/gfx + include/veyra/gfx/*
  D3D12DeviceContext, 4-slot CommandSlotRing, fence/timestamp

src/ngx + include/veyra/ngx/*
  NgxCoreHost, DlssNrRuntimeAdapter, DlssSrBackend, 参数集中定义

src/parity + shaders/Parity*.hlsl
  RenoDX-equivalent proxy encode/decode 与 CPU golden

src/media + include/veyra/media/*
  FFmpegDemuxer, FFmpegVideoDecoder, D3D12VA 共享设备

shaders/YuvToLinearRgb.hlsl
  YUV range/matrix/transfer -> linear RGB

tools/nr_harness, tools/media_probe
  已有 Feature 18 / SR / 视频耐久证据入口
```

Phase 0–4 并不包含播放器、capture、export、NVOF、DAV2 或 DLSSG。命名存在不等于功能存在。

## 3. 目标目录与 target

按下面结构增量添加。不要把所有类塞进 `main.cpp`。

```text
include/veyra/pipeline/
  FramePacket.h
  GuidanceFrame.h
  ResetCoordinator.h
  EnhanceGraph.h

src/pipeline/
  ResetCoordinator.cpp
  EnhanceGraph.cpp
  SceneCadenceAnalyzer.cpp

include/veyra/guidance/
  IGuidanceProvider.h
  ZeroGuidanceProvider.h
  NvofGuidanceProvider.h
  DepthAnythingProvider.h
  GuidanceValidator.h

src/guidance/
  ZeroGuidanceProvider.cpp
  NvofGuidanceProvider.cpp
  DepthAnythingProvider.cpp
  GuidanceValidator.cpp

shaders/guidance/
  NvofInput.hlsl
  NvofDensify.hlsl
  GuidanceValidate.hlsl
  SceneHistogram.hlsl
  DepthPreprocess.hlsl
  DepthNormalizeReproject.hlsl

include/veyra/ngx/DlssFgBackend.h
src/ngx/DlssFgBackend.cpp

include/veyra/source/
  IFrameSource.h
  MediaFileSource.h
  CaptureCardSource.h
  ImageSource.h

src/source/
  MediaFileSource.cpp
  CaptureCardSource.cpp
  ImageSource.cpp

include/veyra/sink/
  IFrameSink.h
  D3D12PresentSink.h
  WasapiAudioSink.h
  ImageExportSink.h
  VideoExportSink.h

src/sink/
  D3D12PresentSink.cpp
  WasapiAudioSink.cpp
  ImageExportSink.cpp
  VideoExportSink.cpp

apps/veyra/
  main.cpp
  MainWindow.cpp/.h
  CapturePage.cpp/.h
  PlayerPage.cpp/.h
  ExportPage.cpp/.h

tests/
  unit/GuidanceMathTests.cpp
  unit/CadenceTests.cpp
  integration/...
```

CMake targets：

```text
veyra_pipeline    depends base,gfx,parity,ngx
veyra_guidance    depends pipeline,gfx and optional NVOF/DML
veyra_sources     depends media,pipeline,avdevice,WIC
veyra_sinks       depends pipeline,gfx,FFmpeg/WIC/WASAPI
veyra_app         WIN32 executable, owns UI only
veyra_quality_probe
veyra_capture_probe
veyra_export_probe
```

`veyra_app` 不直接调用 `NVSDK_NGX_*`，不直接管理 NVOF session，也不写 shader barrier。它只组装 source/graph/sink 和状态。

## 4. 本地依赖放置

所有本地/有许可约束的内容都在已 gitignore 的目录。禁止拷进 `src` 或提交。

```text
runtime_local/nvidia/
  nvngx_dlssnr.dll
  nvngx_dlss.dll
  nvngx_dlssg.dll
  runtime-manifest.json

third_party_local/nvidia/DLSS_SDK_310.7.0/
  include/
  lib/Windows_x86_64/

third_party_local/nvidia/Optical_Flow_SDK_5.0.7/
  NvOFInterface/
  Common/NvOFBase/
  NvOFBasicSamples/...

third_party_local/nvidia/Video_Codec_SDK_13.1.0/
  Interface/nvEncodeAPI.h
  Samples/NvCodec/NvEncoder/NvEncoderD3D12.*

third_party_local/microsoft/windowsappsdk.ml/1.8.2124/
  include/
  lib/native/x64/
  runtimes-framework/win-x64/native/

third_party_local/models/depth-anything-v2-small/
  model_fp16.onnx
  LICENSE.txt
  manifest.json

third_party_local/models/video-depth-anything-small/
  model.onnx              # 仅离线高质量实现实际选用时需要
  LICENSE.txt
  manifest.json

third_party_local/ffmpeg/bin/
  ffprobe.exe
  LICENSE.txt
  manifest.json
```

### 4.1 已知存在

- 根目录 `nvngx_dlssnr.dll` 与 `runtime_local/nvidia/nvngx_dlssnr.dll`；
- `runtime_local/nvidia/nvngx_dlss.dll`；
- `third_party_local/nvidia/DLSS_SDK_310.7.0`；
- `third_party_local/nvidia/Optical_Flow_SDK_5.0.7`，含 `NvOFInterface`、D3D12 header/sample 和 EULA；其代码已经构建并完成 NVOF 合成位移 probe，但仍不得提交；
- SDK 内 `lib/Windows_x86_64/rel/nvngx_dlssg.dll`：7,519,856 bytes，version `310.7.0.0`，SHA256 `135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F`，Authenticode `Valid / NVIDIA Corporation`；已 stage，DLSSG capability/Create/Evaluate 与 59/59 truth probe 已通过，产品 Phase 6 仍未通过；
- vcpkg FFmpeg decode 依赖。

### 4.2 当前外部阻塞

- Optical Flow SDK 5.0.7 已存在；不要再把它写成外部阻塞。Video Codec SDK 13.1 仍缺失，需要用户从 NVIDIA Developer Program 正式取得；Agent 不得替用户点击同意；
- depth model、Windows App SDK ML/ORT 包和 ffprobe binary 必须在使用前记录来源、版本、SHA256、许可证；
- capture gate 需要一块 Windows 能枚举并提供 4K60 SDR + HDMI audio 的真实采集卡和回环信号。

依赖缺失时先完成不依赖它的接口、CPU golden、shader、synthetic gate 和 UI。只有真正走到硬件集成任务才写 `INBOX` 并阻塞；不要一开始空等。

### 4.3 runtime staging

继续使用 `scripts/stage-runtime.ps1` 的安全原则：只从已知绝对路径复制；复制前后校验 size/hash/signature；manifest 记录 source path 和 timestamp。新增 DLSSG 时只从 `third_party_local/nvidia/DLSS_SDK_310.7.0/lib/Windows_x86_64/rel/nvngx_dlssg.dll` 复制，并匹配上面的锁定身份；不能从 GitHub release 或游戏目录随便捡版本。

任何 runtime hash 改变都是新的实验变量；必须新建证据，不得复用旧 gate。

## 5. 构建开关

保留并使用：

```cmake
VEYRA_ENABLE_EXPERIMENTAL_DLSSNR
VEYRA_ENABLE_DLSS_SR
VEYRA_ENABLE_DLSS_FG
VEYRA_ENABLE_NVOF
VEYRA_ENABLE_D3D12_DEBUG
```

新增：

```cmake
VEYRA_ENABLE_DEPTH_DML       # default OFF
VEYRA_ENABLE_CAPTURE_DSHOW   # Windows default ON when avdevice exists
VEYRA_ENABLE_EXPORT          # default ON when WIC + FFmpeg exist
VEYRA_ENABLE_NVENC_D3D12     # release export default ON; dependency missing -> configure fail
VEYRA_WINDOWS_ML_ROOT        # local extracted package
VEYRA_DEPTH_MODEL_ROOT       # local model directory
VEYRA_VIDEO_CODEC_SDK_ROOT   # local Video Codec SDK 13.1 root
VEYRA_FFPROBE_EXE            # validation only; absolute path, not baked into source
```

规则：功能开关 ON 而依赖缺失时 CMake configure 直接失败并写精确缺失路径；不能编译一个运行时才静默变 Zero 的假 backend。

在 `vcpkg.json` 给 FFmpeg 加 `avdevice`，并固定 libass/FreeType/HarfBuzz 版本用于字幕。不要为了 capture 再引入 OpenCV/Python。首发 UI 用 Win32，不增加 WebView2/Gradio。

## 6. 线程与队列

固定 owner：

```text
UI thread            Win32 message loop; never blocks on long GPU/FFmpeg work
source thread        file demux/decode OR dshow capture
render thread        sole owner of EnhanceGraph and all NGX calls
audio thread         WASAPI event-driven render
export writer thread consumes bounded NVENC bitstream packets and libavformat mux
depth worker         one DML session, sequential Run only
```

不同模式：

| 模式 | 视频 source queue | 策略 |
|---|---:|---|
| Capture ingress | 1 | 新帧覆盖未处理旧帧；drop++；next frame reset |
| Capture graph window | 2 or 3 | FG Low Latency 保留 A/B；Buffered Quality 保留 A/B/C；绝不继续增长 |
| Player | 4 | backpressure；不丢源帧；audio master |
| Export | 4 | backpressure；绝不丢源帧；writer 慢则阻塞 producer |

1080p GPU command slot 3–4 个，4K 使用 4–6 个并以显存 budget 动态拒绝超配。Feature create/rebuild 可等待一次；逐帧不能在每个 pass 后 `WaitForSingleObject`。只在资源跨队列、NVENC slot 重用或 present 时用 fence 建立依赖。

4K 资源纪律：一张 3840×2160 RGBA16F texture 约 63.3 MiB，不能在每个 pass/每帧临时创建。建立按 `workingExtent/format/flags` 键控的 resource pool，记录 current/peak allocation；在启动 4K graph 前调用 `IDXGIAdapter3::QueryVideoMemoryInfo`，把 NGX 内部估算、swapchain、decode surfaces、A/B/C history、motion/depth/confidence、NVENC slots 全部计入预算。参考 RTX 5070 12 GB gate 要求运行峰值仍至少保留 1.5 GiB budget headroom。

延迟模式的窗口固定定义：

```text
NR Low Latency:     current=B, prev=A optional, next=null; 不主动等 C；FG off
FG Low Latency:     prev=A, current=B, next=null; B 到达后生成 A½；lookaheadFrames=1
Buffered Quality:   prev=A, current=B, next=C; C 只验证 A/B guidance；lookaheadFrames=2
Export Quality:     文件级有界 lookahead；不受交互延迟门槛
```

`lookaheadFrames` 是数据依赖，不是延迟测量。FG pair 的稳态附加显示延迟理论下限约 `0.5/f + graph/pacing`，保守调度可能接近 `1/f`；加入 C 通常再增加约 `1/f`。采集卡已经造成的延迟不能抵扣这些依赖。若玩家使用 HDMI passthrough 操控，Veyra 的缓冲只影响观众/录制；若玩家看 Veyra 窗口操控，UI 默认不启用 Buffered Quality。

收益优先级固定：B 使 A↔B 双向 flow/遮挡/中间帧成为可能，是主要提升；C 只用于加速度、depth temporal stability、cut confirmation 和 trust refinement，是次要提升。质量数据未证明 C 有收益时，Auto 必须退回 A/B，不能只因模式名更高级就强制多等一帧。

后帧/双向 flow 的规范依据是 NVIDIA NVOFA FRUC guide：<https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html>。它说明 consecutive previous/next frames 与 forward/backward flow；Veyra 不因此改用 FRUC 冒充 DLSSG。

## 7. 统一接口

先实现接口和 fake providers，再碰硬件：

```cpp
class IFrameSource {
public:
    virtual SourceInfo info() const = 0;
    virtual Result start() = 0;
    virtual SourceReadResult read(FramePacket&, AudioPacket*) = 0;
    virtual Result seek(Rational) = 0; // unsupported for capture/image
    virtual void stop() noexcept = 0;
};

class IGuidanceProvider {
public:
    virtual Result initialize(const GuidanceConfig&, D3D12DeviceContext&) = 0;
    virtual Result produce(const FrameWindow&, GuidanceFrame&) = 0;
    virtual void reset(uint64_t epoch, ResetReason) noexcept = 0;
};

class IFrameSink {
public:
    virtual Result begin(const StreamDescription&) = 0;
    virtual Result consume(const ProcessedFrame&) = 0;
    virtual Result end() = 0;
    virtual void cancel() noexcept = 0;
};
```

`FrameWindow` 明确保存 `prev/current/next`、各自 PTS/sequence 和 `lookaheadFrames`。`next` 缺失不是错误；provider 只能执行与当前模式相符的因果算法。DLSSG 不接收任意第三帧 C；C 只用于 Veyra 自己的 consistency/depth/cut/trust 计算。

接口的真实定义可以调整语法，但不得丢失：PTS/duration/sequence、color metadata、source flags、reset epoch、provenance、fence ownership、lookahead/cancel/EOS。

## 8. Feature 18：不要改坏已通过的协议

### 8.1 runtime 与调用路径

`DlssNrRuntimeAdapter` 继续从 `runtime_local/nvidia/nvngx_dlssnr.dll` 绝对路径加载，受限 search flags，解析并逐项验证：

```text
NVSDK_NGX_D3D12_Init_Ext
NVSDK_NGX_D3D12_CreateFeature
NVSDK_NGX_D3D12_EvaluateFeature
NVSDK_NGX_D3D12_ReleaseFeature
NVSDK_NGX_D3D12_Shutdown1
```

使用已经实测的 signed-snippet App ID `0x0876232C` 和 caller compatibility adapter。不得：

- patch DLL 文件；
- 把 addon 改名成 DLL；
- 改回未通过的普通 core Feature 18 路径；
- 省略 SEH/result logging；
- 在 UI/source/export 中复制一份 private parameter code。

### 8.2 Create 参数类型

唯一字符串真源是 `include/veyra/ngx/DlssNrParameters.h`。当前中性同分辨率 create：

| 参数 | setter | 值 |
|---|---|---|
| `DLSSNR.Width/Height` | U32 | NR input extent |
| `DLSSNR.InputWidth/InputHeight` | U32 | NR input extent |
| `DLSSNR.OutputWidth/OutputHeight` | U32 | NR output extent |
| `DLSSNR.Output.Width/.Height` | U32 | NR output extent |
| `DLSSNR.Upscaling` | U32 | 0；SR 是独立前置 pass |
| `DLSSNR.Scale` | F32 | 1.0 |
| `DLSSNR.ScalingRatio` | F32 | 1.0 |
| `DLSSNR.Hint.Render.Preset` | I32 | 已验证 profile 值，默认 0 |
| `Width/Height` | U32 | output extent |
| `PerfQualityValue` | I32 | 当前 baseline 1 |
| `CreationNodeMask/VisibilityNodeMask` | U32 | 1 |

任何新增 NR 参数先在 harness 做 one-variable A/B，记录类型、值、result 和输出 hash，再进入 UI。

### 8.3 Evaluate 资源和参数

```text
DLSSNR.Color   -> parity encoded Proxy
DLSSNR.Output  -> Raw Neural output
DLSSNR.MVec    -> GuidanceFrame.motion or explicit ZeroMotion
DLSSNR.Depth   -> GuidanceFrame.depth or explicit ZeroDepth
```

所有 Color/Output/MVec/Depth subrect 的 BaseX/BaseY/Width/Height 每帧都设置，不能依赖旧参数残留。

| 参数 | setter | 规则 |
|---|---|---|
| `DLSSNR.MVecScaleX/Y` | F32 | consumer adapter 后为 1.0；若传未适配 source extent，必须用合成平移片推导并记录 |
| `DLSSNR.DepthInverted` | I32 | 与实际 depth 约定一致；estimated baseline 固定并 A/B |
| `DLSS.Indicator.Invert.X/Y.Axis` | I32 | baseline 0 |
| `DLSSNR.Enabled` | I32 | 1 |
| `DLSSNR.Reset` | I32 | `ResetCoordinator.epoch` 变化的首帧为 1 |
| `DLSSNR.Style` | I32 | UI 值；默认 0 |
| `DLSSNR.Intensity` | F32 | 0–2；默认 1 |
| `DLSSNR.LocalToneStrength` | F32 | 默认 1 |
| `DLSSNR.LocalStructureStrength` | F32 | 默认 1 |
| `DLSSNR.SkinStructureStrength` | F32 | 默认 -1 |
| `DLSSNR.UseAutoMask` | I32 | 默认 0，只有独立 A/B 后开放 |
| `DLSSNR.UICorrection` | I32 | 播放/导出默认 0；烧录 UI 不等同游戏 UI resource |

每帧日志至少含 frame sequence/PTS/reset epoch、guidance provenance、资源 extent/format/state、Evaluate result/SEH、GPU ms、output hash sampling。

## 9. 颜色与 parity

统一 working format：`R16G16B16A16_FLOAT` linear。Feature 18 proxy/raw 继续使用 Phase 2 已验证的 encode/decode，不把 raw output 直接 present。

每个 source 的 ingress 必须明确：

```text
pixel format: NV12 / YUV420P / YUY2 / BGRA ...
range: limited/full
matrix: BT.601/709/2020
transfer: sRGB/BT.709...
primaries
rotation/sample aspect ratio
```

metadata 缺失时采用有日志的可预测规则：SD 默认 601，HD SDR 默认 709；UI 显示“assumed”。不要根据“看着有点灰”去调 NR intensity 掩盖颜色错误。

输出 SDR 为 BT.709 limited（视频）或 sRGB full（PNG/JPEG）。只做一次 output transfer。

## 10. DLSS SR

复用 `DlssSrBackend` 和 Phase 4 已通过的绝对 runtime 路径修复。Product graph 规则：

- output 与 source 同尺寸时 bypass，计数 `srBypassCount`；
- output 更大时先 SR，再 parity/Feature 18；
- V1 SR 明确使用 Zero Guidance；它不能等待后面才从 post-SR 帧生成的 NVOF/DAV2，禁止形成 `SR -> NVOF -> SR` 循环依赖；
- SR 输出定义为 `workingExtent`。NVOF、DAV2/confidence、Feature 18 与 DLSSG 都消费这个 extent；SR bypass 时 `workingExtent == sourceExtent`；
- SR 只用官方 SDK 310.7 header/helper；
- render/output subrect、jitter、motion scale、exposure 每帧显式设置；
- 从最终像素做 SR 没有游戏原生 subpixel jitter，baseline jitter=0，不能宣传等同游戏 SR；
- 若后续实现 synthetic jitter，只能做默认关闭的实验，必须证明不闪烁；
- SR output 需真实非黑/非恒定，并与 bilinear baseline 不同；不能只看 `Available=1`。

Phase 5 首个质量任务把真实视频 `SR -> NR` 串起来，不能继续用独立 SR harness 代替产品顺序。

## 11. NVOF motion

### 11.1 头文件与动态库

- include 只从 `third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFInterface` 与该包的 `Common/NvOFBase`；
- runtime 只用 `C:\Windows\System32\nvofapi64.dll`；
- `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`；
- 解析 `NvOFGetMaxSupportedApiVersion` 和 `NvOFAPICreateInstanceD3D12`；
- 不把 `nvofapi64.dll` 复制进输出或安装包。

### 11.2 初始化

严格参考 SDK 5.0 D3D12 sample 的结构大小/version：

1. 查询 API version；
2. `nvCreateOpticalFlowD3D12` 使用 Veyra 的同一 D3D12 device；
3. query supported input/output/cost formats 与 grid；
4. 选择 performance preset；优先硬件支持的 4x4 grid，再 densify；
5. 创建 current/previous ABGR8 input、S10.5 vector output、cost output；
6. 注册资源，保存 handle；
7. 建立 app fence point 与 OF fence point；
8. resize/device lost 时逆序 unregister/destroy/recreate。

NVOF context 单线程使用，不并发调用。

### 11.3 每帧

```text
current/previous post-SR linear color（SR bypass 时就是 ingress color）
 -> NvofInput compute: explicit linear->sRGB, verified channel order
 -> signal app input fence
 -> nvOFExecuteD3D12(current, previous, output, cost)
 -> graphics queue waits OF output fence
 -> NvofDensify: int16 S10.5 / 32 -> RG16F workingExtent pixels
 -> cost -> confidence
 -> GuidanceValidate
```

第一帧、reset frame 不调用带旧 previous 的 flow，发布 Zero Motion + reset。之后 current 成为 previous。

方向 gate：生成已知向右移动 `+8 px/frame` 的图案；warp current 到 previous 后 residual 应显著下降。若结果需要 `-8` 才正确，修 provider 的方向，不在 consumer 随意翻转。

### 11.4 confidence

最少四项：

- NVOF cost；
- forward/back error（实时可按预算隔帧做，离线必须做）；
- luma warp residual；
- out-of-frame/occlusion mask。

depth 可用时再加入 depth residual。最终 `confidence` 0–1，motion 乘以 smooth confidence；不要硬阈值造成边缘断裂。必须提供可视化和统计 p05/p50/p95。

## 12. Depth Anything

### 12.1 模型与 runtime

第一选择是 Depth Anything V2 Small FP16。模型目录 `manifest.json` 必须含：URL、下载日期、SHA256、输入/输出名、shape、opset、license、商业可用判定。任何一项未知就不进入 release gate。

Windows ML/ONNX Runtime DML 用固定 package，必须：

- 与 Veyra 同一 adapter/device；
- `SessionOptionsAppendExecutionProvider_DML1(IDMLDevice*, commandQueue*)`；
- disable memory pattern；execution mode sequential；
- 固定 input shape；
- 同一 session 不并发 `Run`；
- 用 I/O binding/device tensor 把输入输出留在 GPU；
- 若当前 package 无法安全绑定 Veyra D3D12 buffer，先实现有界异步 staging 但明确计数，Capture `Low Latency` 默认关闭 depth。禁止同步 render thread 等待 CPU inference。

### 12.2 preprocess/postprocess

Preprocess shader：

- letterbox/resize 到模型 shape；
- RGB 顺序与 normalization 逐项按模型定义；
- FP16 NCHW buffer；
- 记录 crop/scale 以映射回 source extent。

Postprocess：

- 检查 finite；
- 计算 P02/P98，更新 EMA；
- 映射为 `[0,1]` R32F；
- 明确定义 near/far 与 `DepthInverted`；
- motion reprojection 上一深度；
- current inference 与 reprojected history 根据 confidence/residual 混合；
- scene cut/reset 清空 EMA/history；
- depth result 带 source sequence 与 age，过期不能使用。

### 12.3 调度

```text
Low Latency: off or interval 8, never block current frame
Balanced: interval 4, reproject between results
Export Quality: interval 1 or Video Depth Anything Small, no dropping
```

Auto 降级顺序：stable depth -> motion only -> zero。日志/UI 写明原因：missing model、late result、age、residual、cut、DML failure。

不要从 Magpie 复制 GPL 代码。算法思想需独立实现，类名/组织/代码不能机械对应。

## 13. SceneCadenceAnalyzer

GPU 生成 64-bin luma histogram 和 160×90 thumbnail SAD。输入还有：NVOF confidence summary、PTS delta、sequence gap、duplicate hash。

建议 baseline（随后由 corpus 固化，不可无证据乱调）：

```text
hard discontinuity: seek/source switch/resize/drop -> immediate reset
probable cut: histogram distance high AND (SAD high OR confidence collapse)
duplicate: perceptual hash equal + SAD extremely low
flash: histogram high but motion/edge structure remains coherent -> do not reset unless next frame confirms
```

输出 `CadenceDecision`：normal/cut/duplicate/drop/discontinuity，带各 score。它是 ResetCoordinator 的唯一内容分析输入。

## 14. DLSSG 2X

### 14.1 集成来源

只用本地官方 DLSS SDK 310.7 的：

```text
nvsdk_ngx_helpers_dlssg.h
nvsdk_ngx_defs_dlssg.h
nvsdk_ngx_params_dlssg.h
doc/DLSS-FG Programming Guide.pdf
sample（若本地 SDK 有）
```

同时以 NVIDIA 的公开 DLSS-G integration guide 交叉核对资源、frame constants 和 pacing：<https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md>。Veyra 仍直接 NGX，不引入 Streamline runtime。

不要抄 Magpie GPL 实现，不要抄无许可证仓库。

### 14.2 backend

`DlssFgBackend` 封装 capability/create/evaluate/release。Create 前记录：GPU architecture、driver、HAGS、runtime hash/version、MultiFrameCountMax。

首发只请求 2X（每对 real frames 1 个 generated frame）。资源至少包含 SDK helper 要求的：

- current/previous real color（Feature 18 后、UI 前）；
- motion；
- depth 或明确 ZeroDepth；
- output interpolated；
- camera/constants、frame id、reset；
- 对永不提供的 HUD-less/UI/UIAlpha/BidirectionalDistortionField/OutputReal 按官方 header 设置 `ResourceNeverProvided_Flags`，不能传悬空 texture。

motion scale 不能沿用 Feature 18 值。按官方 DLSSG header 定义和合成平移片确认；在结果未确认前把 `{1,1}` 与 `{1/width,1/height}` 都叫 diagnostic candidate，不得写死宣传。

### 14.3 cadence

DLSSG 输出的 generated PTS = `(prevPTS + currentPTS)/2`。呈现顺序：previous real（已经提交）→ generated → current real。参数、reset 和 quality mode 只在 real-frame boundary 改。

切镜/duplicate/drop/seek：不生成跨边界帧；reset backend；直接 present current real。

证明 generated frame 真实：

- Evaluate success 且 output resource 非空；
- generated hash != previous hash；generated hash != current hash；
- generated 不是简单 50/50 blend（pixel residual 对 blend baseline 超过固定阈值）；
- 已知平移片中物体位于合理中间位置；
- 60 个 real frames 得到严格 59 个可用中间帧（首帧前无生成），时间单调。

## 15. 三个 source

### 15.1 MediaFileSource

组合现有 `FFmpegDemuxer` + `FFmpegVideoDecoder`：

1. demux 保存 stream time_base、PTS、duration、rotation/color metadata；
2. 优先 `AV_PIX_FMT_D3D12` 共享 Veyra device；
3. decode surface 经 YUV shader 到 linear RGBA16F；
4. seek：`av_seek_frame`/`avformat_seek_file`，flush codec，increment reset epoch；
5. EOF 完整 drain；
6. audio packet 送独立 decoder（新增），转换为 WASAPI mix format；
7. player 以 audio clock 为 master，export 以源 PTS 为真源。

### 15.2 CaptureCardSource

给 vcpkg FFmpeg 开 `avdevice`，调用 `avdevice_register_all()`，`av_find_input_format("dshow")`。

枚举可以用 dshow device listing 回调或独立安全 probe；不要解析本地化 stderr 作为长期 API。若 FFmpeg 暂无稳定枚举 API，可用 Windows DirectShow COM 枚举 friendly name，再将用户选中的精确 name 传给 dshow input。

打开字典至少尝试用户明确选择的：

```text
video_size=<1920x1080 or 3840x2160 selected from the device's real modes>
framerate=60
pixel_format/device-specific format
rtbufsize=<bounded>
fflags=nobuffer
flags=low_delay
probesize/analyzeduration kept small but nonzero
```

打开后读取实际 stream codec/size/fps/color；不一致则 UI 显示实际值或失败，不能静默当请求模式。P010/HDR 未实现前必须拒绝或要求源设备输出 SDR，禁止按 NV12/SDR 误读。

source thread 只保留最新视频 packet/frame；覆盖旧帧时 `droppedByVeyra++`，下一 frame flags `drop|discontinuity`。音频用独立有界 ring，过载时按时钟策略丢弃并记录。

首发支持 Windows 已暴露给 DirectShow 的 UVC/采集设备，认证模式包含 1080p30/60 与 2160p30/60。厂商专用设备显示“not exposed through DirectShow”，不崩溃；通用支持不等于宣称所有品牌型号均已认证。

### 15.3 ImageSource

WIC：`IWICImagingFactory2` → decoder → frame → color transform/format converter → upload。应用 EXIF orientation；不支持 multipage/animation 时取第一帧并警告。单 frame flags 包含 open/reset/EOS。

图片不做 FG。SR 可选；NR 只执行 reset frame。可选 still accumulation 必须另过 gate，默认 off。

## 16. 三个 sink

### 16.1 D3D12PresentSink

- flip-discard swapchain，2–3 buffers；
- 支持 VSync on 和 tearing on（硬件允许时）；
- present texture 与 UI overlay 分层；
- resize 等待自身必要 fence 后重建，不 `Flush` 每帧；
- 记录 acquire/submit/present result、backbuffer id、queue depth、CPU/GPU timestamp；
- Capture 的“内部延迟”从 decoded/captured frame 到达 Veyra 的 QPC 到 present submit QPC，不冒充 HDMI 端到端。

### 16.2 WasapiAudioSink

- shared mode、event-driven；
- player/capture 两种来源都走同一 sink；
- resample 到 mix format，ring buffer 有固定上限；
- player audio clock 是视频调度主钟；capture 尽量跟设备 timestamp，发生 drop 时不让音频无界累积；
- pause/seek/stop flush，clock epoch 与 ResetCoordinator 对齐；
- 记录 underrun/overrun、played frames、drift。

### 16.3 ImageExportSink

输出先写同目录唯一 `.partial`，成功 decode-back 验证后 `MoveFileExW` 原子替换为最终新文件；默认不覆盖。PNG lossless，JPEG quality 可调。失败/取消只删本任务明确创建的 partial。

### 16.4 VideoExportSink

首发必须是 D3D12 NVENC，不允许用全帧 readback/raw pipe 过门禁：

规范真源：<https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html>，特别是 D3D12 external resources、input/output fence points、register/map/unmap/unregister 生命周期。

1. 只从 `third_party_local/nvidia/Video_Codec_SDK_13.1.0/Interface/nvEncodeAPI.h` 编译；动态加载系统 `C:\Windows\System32\nvEncodeAPI64.dll`，校验签名/version，禁止随包复制；
2. `NvEncodeAPICreateInstance` 后查询 H.264/HEVC、3840×2160、目标 profile/level、async encode 和 input format capability；缺失就明确失败；
3. 用现有 D3D12 device 打开 session，输入为 default-heap `ID3D12Resource`；Feature 18/FG 后由 shader 转 NV12，native 4K 不经过 CPU；
4. 预分配有界 4–8 slot。每 slot 有 input resource、registered handle、input/output fence point、bitstream output 和状态机；不得每帧 register/unregister；
5. 按官方 D3D12 contract 调 `NvEncRegisterResource`、`NvEncMapInputResource`、`NvEncEncodePicture`；NVENC 等 input fence，完成后 signal output fence；资源复用前等待对应 slot，不全局 flush；
6. 只锁定/读取压缩 bitstream，不回读 RGBA/NV12 pixels；把 packet、PTS、DTS、duration 交给 FFmpeg `libavformat` 写 MP4/MKV；
7. H.264 与 HEVC 都必须有 UI 选项；默认 4K 用 HEVC，码率/CQ/preset 选择写入 job manifest；
8. 音频优先 packet remux；不兼容时解码后 AAC。字幕可兼容时复制/转换；否则开始前列出会丢失的 stream 并让任务失败或由用户明确关闭，不得静默丢；
9. 输出为同目录唯一 `.partial`；正常 EOS flush encoder/mux trailer 后做 ffprobe + Veyra self-decode，才原子 rename；
10. cancel/device-lost/crash recovery 只清理本 job 的 partial，逆序 unmap/unregister/destroy；下次启动列出遗留 job manifest，不误删成功文件；
11. `ffprobe.exe` 只用于独立验证，来自绝对配置路径并有 manifest；诊断 raw-pipe backend 若保留，必须编译为默认 OFF 且永远不能满足 Phase 7 export gate。

输出 CFR。target FPS 支持 23.976/24/25/30/50/60/120 或 source nominal×2，最高 3840×2160；时间以整数 time base 写，VFR 原样导出不在本次首发。

## 17. UI 组装

先让 probes 端到端，再做 UI。Win32 一个窗口、TabControl 三页，不引入 WebView：

```text
Capture: device / format / audio / output size / quality / NR SR FG / start stop fullscreen
Player: open / play pause / seek / loop / output size / quality / NR SR FG / fullscreen
Export: input / output / image-video / size / FPS / H.264-HEVC / quality / start cancel / progress
Diagnostics: runtime, backend, fallback, reset, latency breakdown, lookahead frames, GPU ms, VRAM, queue, drops
```

UI 事件只提交 command 到 engine controller。禁止 UI thread 直接等待 decode/GPU/child process。

错误分类：

```text
Unsupported      device/codec/GPU capability not available
MissingLocalAsset SDK/runtime/model/ffmpeg absent
InvalidMedia     bad input or unsupported metadata
RuntimeFailure   NGX/NVOF/DML/FFmpeg returned failure
DeviceLost       D3D12 device removed/recreate failed
Cancelled        user cancelled, not an error
```

所有 fallback 以黄色状态显示；Feature 18/FG failure 不能静默显示原图却标“DLSS on”。

产品级 UI 还必须：设置写 `%LOCALAPPDATA%/Veyra`、首次启动 dependency/capability 检查、最近文件与设备恢复、4K/HDR 支持状态、日志包导出、残留 `.partial` 作业提示、device-lost 后可重开。不得把这些行为留给命令行。

## 18. 质量预设

配置序列化到 `config/`，但本地用户选项写 AppData，不覆盖仓库默认。

```text
Low Latency
  SR auto when target larger
  NR Natural/intensity 1
  NVOF motion
  depth off or interval 8 if budget permits
  FG off for zero deliberate buffering; enabling FG switches to FG Low Latency
  capture mailbox 1

FG Low Latency
  bounded A/B window
  wait for B, generate A-half, then present B
  lookaheadFrames=1; measured display latency is not assumed to equal 1/f
  NVOF forward/back on A/B when budget permits

Buffered Quality
  bounded A/B/C window
  C only validates A/B motion/depth/cut/trust
  lookaheadFrames=2; C usually adds about 1/f versus pair mode
  recommended when player uses HDMI passthrough, not the Veyra preview

Balanced
  SR auto
  NR Natural/intensity 1
  NVOF + confidence
  DAV2 interval 4 + reprojection
  FG optional

Export Quality
  no dropped frames
  file-level forward/back flow validation and bounded future lookahead
  DAV2 interval 1 or VDA Small
  NR + optional SR
  FG 2X to selected CFR

Old Video
  conservative deblock/deband pre-pass
  strict confidence/history rejection
  detail/color mix exposed

AI Video
  aggressive cut/discontinuity detection
  depth Auto, falls back on inconsistent geometry
  no assumption of physically valid motion
```

`Old Video` 与 `AI Video` 仍必须通过 same graph；不允许独立 Python pipeline。

## 19. 观测字段

每次 run JSON 至少：

```text
runId, timestamp, gitCommit, dirty
exeHash, configHash, inputHash
gpu, driver, HAGS
dlssnr/dlss/dlssg/nvof/DML/model/ffmpeg identities
sourceKind, actual format/size/fps/color metadata
decoded/captured/source frames
dropped/duplicated/real/generated/presented/encoded frames
srCreate/evaluate/bypass counts + results
nrCreate/evaluate/reset counts + results/SEH
nvofExecute/fallback/zero counts + direction/grid/cost/confidence
depthInference/reproject/stale/drop counts + age/timing
fgCreate/evaluate/generated counts + results
queue high-water marks
GPU p50/p95 per pass
capture ingress->present p50/p95
audio underrun/overrun/A-V drift
working set start/peak/end
output probe: codec,size,fps,frames,duration,audio
```

日志中的 `available`、`created`、`evaluated` 分开。capability available 不等于 create 成功，create 成功不等于输出正确。

## 20. 测试资产

新增生成器，不把版权视频提交：

- `translation_8px_1080p60.mp4`：高对比方块每帧 +8 px；
- `occlusion_1080p60.mp4`：前景遮挡后显露纹理；
- `cut_flash_duplicate_1080p60.mp4`：硬切、单帧闪光、重复、PTS gap；
- `particles_alpha_1080p60.mp4`：烟/粒子/半透明；
- `ui_text_1080p60.mp4`：固定文字覆盖运动背景；
- 上述 translation/occlusion/cut/particles/ui 五组同时生成 `3840x2160@60` 版本，用于全分辨率 graph、FG、显存与 cadence gate；
- `real_4k_h264_30.mp4` 与 `real_4k_hevc_60.mp4`：有音频、颜色 metadata 和字幕的可再分发测试素材或程序生成素材；
- `old_video_artifacts_720p30.mp4`：可程序生成 block/ringing/noise；
- `ai_warp_720p30.mp4`：程序化非刚体形变；
- `capture_loop`：用另一台主机或测试图输出到真实采集卡。

每个生成资产记录生成命令和 SHA256。不要把黑帧、纯色或重复帧当唯一测试。

## 21. Phase 5：统一质量核心

本节描述目标形态；当前恢复执行以 0.3 的 R1–R5 为准。2026-09-03/04 的旧 gate/pass 不能代替下面任一项，只有重建后的 phase5 gate 与 Reviewer 才能重新关账。

严格按 `loop/BACKLOG.md` 的第一个 TODO：

1. 重建 fail-closed phase5 gate；先让它因实现缺失而失败；
2. `FramePacket/GuidanceFrame/ResetCoordinator/EnhanceGraph`；
3. 真实视频 `SR -> parity -> Feature 18 -> parity`；
4. SceneCadenceAnalyzer + synthetic tests；
5. NVOF provider + direction/grid/cost/confidence；
6. DAV2 DML provider + age/reprojection/Auto fallback；
7. quality corpus 输出 `off/zero/motion/motion+depth/auto`；
8. 1080p60 与 4K60 各 30 分钟 endurance，记录 D3D12 budget/usage；
9. gate + 独立 Reviewer + checkpoint。

Phase 5 gate 必须失败于：只有 Zero、motion 全零、known translation 方向错、depth 恒定/陈旧、reset 缺失、SR/NR 顺序错、readback 出现在 live graph。

## 22. Phase 6：DLSSG 与实时 engine

已有 backend/probe 只作为输入资产。当前执行以 0.3 的 R6 为准；在 drift、GBV、共享 engine、30 分钟耐久全部通过前，Phase 6 状态保持 locked/not passed。

1. 重建 fail-closed phase6 gate；
2. DlssFgBackend capability/create/evaluate/release；
3. known translation 证明 generated 非重复/非 blend；
4. cadence/reset/PTS；
5. PresentSink + WASAPI + EngineController；
6. player probe 跑真实视频和音频；
7. 4K30/60 player、A/B 与 A/B/C 调度、latency breakdown、queue/VRAM/endurance；
8. gate + Reviewer + checkpoint。

Phase 6 完成时可以有 CLI/probe，但必须已经是真实 display/audio/FG engine；不能只有 DLL 探测。

## 23. Phase 7：三个产品闭环

当前执行以 0.3 的 R7–R12 为准。Phase 7 从未开始；不存在 UI 文件或命令行 probe 不能作为提前完成证据。

1. `MediaFileSource + PresentSink + WASAPI` 完整 4K 播放器，含基础字幕、设置恢复和日志导出；
2. `CaptureCardSource + PresentSink + WASAPI` 真实 4K60 采集卡，验证 NR Low Latency、FG Low Latency、Buffered Quality；
3. `ImageSource + ImageExportSink`；
4. `MediaFileSource + D3D12 NvencExportSink`，4K H.264/HEVC、audio/subtitle policy、FG 2X、cancel/crash recovery、probe；
5. Win32 三页 UI；
6. 三模式 fallback/错误/诊断可见；
7. 4K30/60 Player 各 30 分钟、4K60 Capture 30 分钟、4K H.264/HEVC Export corpus；
8. 同源 Magpie A/B，只能写场景限定结论；
9. phase7 gate + 独立 Reviewer；
10. 首次运行诊断、设置持久化、device-lost/source reconnect、partial job recovery；
11. 本地 checkpoint，标记 release candidate；许可证/分发阻塞清零前停止 Goal，不得自动发布或打包 proprietary runtime。

Phase 7 gate 是联合 gate。图片导出通过不能替代视频导出；屏幕/窗口捕获不能替代物理采集设备；CLI 能跑不能替代播放器交互；Zero guidance 不能替代质量核心。

## 24. 无人值守原子任务模板

每个 cycle 只做一个原子任务，格式：

```text
Before:
  git status
  preflight
  journal intent

Implement:
  最小代码/测试，不改无关文件

Verify:
  Debug + Release build（与任务相关）
  unit/integration probe
  current phase gate
  inspect JSON/log/capture, not exit code only

Record:
  WORKLOG
  JOURNAL
  EVIDENCE
  STATE
  local checkpoint only after gate/review rules allow
```

同一 failure fingerprint 最多三个真正不同方案。换变量名、重复命令、删检查不算不同方案。

## 25. 禁止捷径

- 不复制 Magpie GPL 或无许可证竞品源码；
- 不把 `renodx-dlss5-1.addon64` 注入 Veyra 主程序；
- 不在磁盘 patch NVIDIA runtime；
- 不用 Zero Motion/Depth 冒充 NVOF/DAV2；
- 不用 blend/duplicate 冒充 DLSSG；
- 不在 capture path 用窗口截图冒充采集卡；
- 不用 Python/Gradio 另做一套 export 核心；
- 不把 README 声明当实测；
- 不用单张“更锐”截图宣称优于 Magpie；
- 不因赶时间放宽 gate。赶时间的方法是缩格式矩阵、复用 graph、先做 vertical slice，不是说谎。

## 26. 每次向用户汇报

必须写：

- 当前 Phase 和还差哪个门槛；
- 修改文件；
- 实际执行的命令、exit code、run-id、日志路径；
- Feature 18/SR/NVOF/DML/DLSSG 的真实 result；
- 是否在 RTX、真实采集卡、真实音频上运行；没运行就明确“未执行”；
- fallback、质量和许可证风险；
- 下一条唯一原子任务。

“代码应该能工作”“Magpie 已证明所以无需测试”“驱动更新后大概好”都不是结果。
