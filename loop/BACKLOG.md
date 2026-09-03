# Veyra Launch V1 Backlog

状态：`TODO / DOING / BLOCKED / GATE_PASS / REVIEW_PASS / DONE`。Maker 每个 cycle 只处理当前 Phase 第一个前置满足的 TODO。若某项因外部许可/硬件明确标为 BLOCKED，可继续同一 Phase 后面与它独立的 TODO；该 Phase 最终 gate 仍不得通过。

## Phase 0–4 — 已验证基础

- [x] Phase 0：D3D12 device、4-slot command ring、runtime identity/probe。checkpoint `1fb7afa5779a6c6a0a585d4ecf18a640260a2fcf`。
- [x] Phase 1：signed-snippet Feature 18 Create；300/300 Evaluate；真实非黑输出。checkpoint `1dcf834f56986d334a2a6256b196fa1d878ad9b1`。
- [x] Phase 2：parity encode/decode CPU/GPU golden 与完整 Feature 18 pipeline。checkpoint `e0250b5431ee6677a0917b97af821c5579e61d77`。
- [x] Phase 3：FFmpeg D3D12VA decode、YUV→linear、seek/endurance。checkpoint `2c9f2f19d818e7dd34fa6df4cf7d7c88ee12792a`。
- [x] Phase 4：DLSS SR 绝对路径修复、1080p→4K 30 Evaluate、resize。checkpoint `8cdf1ea2f9b0bb1331f9219a2305cef5efd3c8d8`。

这些 checkpoint 是基础证据，不代表 V1 三种模式完成。旧 Phase 5 Zero-only gate 证据因产品边界改变而失效。

## Phase 5 — 统一 Guidance 与质量核心（当前）

- [x] **P5.0 控制面重基线**：Fast-track Product Spec/Playbook/竞品审计/Loop 已统一；CONTROL_HASHES 扩为 10 个受保护文件；preflight 68/68 exit 0；Release build exit 0；历史 Phase 0–4 证据保留，旧 Zero-only Phase 5 证据失效。（2026-09-03）
- [x] **P5.0b 首发/4K 重基线**：用户撤销最小 MVP；首发改为 4K SDR、A/B/C 有界后帧模式、D3D12 NVENC 与产品级恢复/诊断；不把 HDR 混同 4K。（2026-09-03）
- [ ] **P5.1 新 gate 先失败**：重写 `scripts/gates/phase5.ps1`，至少要求统一 graph、真实 SR→NR、scene/cadence、non-zero NVOF motion、confidence、DAV2/明确 Auto fallback、1080p60+4K60 endurance。实现缺失时必须 exit 1。
- [ ] **P5.2 统一契约**：实现 `FramePacket`、`FrameWindow(prev/current/next/lookaheadFrames)`、`GuidanceFrame`、`ResetCoordinator`、`EnhanceGraph` 与 fake/zero provider unit tests；reset epoch 覆盖 seek/cut/drop/resize/source switch/pause/device lost。
- [ ] **P5.3 产品顺序**：把现有 D3D12VA/YUV、optional SR、parity encode、Feature 18、parity decode 串成真实视频 graph；native 4K bypass SR，1080p→4K 走 SR；输出非黑、非恒定、随输入变化；记录各 pass GPU timestamp；主路径 readback=0。
- [ ] **P5.4 Scene/Cadence**：生成 translation/occlusion/cut-flash-duplicate 固定片；实现 histogram+SAD+PTS/sequence analyzer；硬切 reset、闪光不误切、duplicate 不跨帧生成历史。
- [x] **P5.5 NVOF provider** BLOCKED: Optical Flow SDK 5.0 directory absent. User must download from NVIDIA Developer Portal. Continue P5.6+ independent work. (2026-09-03)：从本地 Optical Flow SDK 5.0 编译；System32 runtime 受限加载；current→previous、S10.5 `/32`、grid/densify、cost→confidence；`+8 px` 合成片方向/幅值通过；缺 SDK 时标 BLOCKED 并继续 P5.6/P5.7 的独立部分。
- [ ] **P5.6 Guidance validator**：GPU 实现 NVOF cost、luma warp residual、out-of-frame、forward/back consistency；生成 R8 confidence 并平滑衰减 motion；保存 motion/confidence 可视化与统计。
- [ ] **P5.7 Depth 依赖 manifest**：固定 Windows App SDK ML/ORT 包、DAV2 Small FP16 model 的官方 URL/version/hash/license/input/output/shape；只放 `third_party_local`；未知或非商用模型不得用。
- [ ] **P5.8 DAV2 provider**：同 device DirectML session、sequential run、固定 shape、device tensor/I/O binding；P02/P98+EMA、motion reprojection、age/residual、Auto 降级；实时 render thread 无同步 GPU→CPU→GPU 往返。
- [ ] **P5.9 质量矩阵**：固定 corpus 输出 off/zero/motion/motion+depth/auto；保存 input/config/runtime/output hash、flow/depth/confidence、reset timeline、GPU p50/p95。Depth 在二维/快速切镜变坏时 Auto 必须降级。
- [ ] **P5.10 耐久与 gate**：1080p60 与 native 4K60 graph 各 30 分钟，NR/NVOF/DAV2 计数真实、queue/resource pool/working-set/VRAM 有界、RTX5070 headroom≥1.5 GiB、device removed=0；phase5 gate exit 0。
- [ ] **P5.11 Reviewer/checkpoint**：新上下文只读 Reviewer，修完 P0/P1，gate 重跑，更新状态并创建本地 checkpoint。

## Phase 6 — DLSSG 2X 与实时播放 engine（锁定）

- [ ] **P6.0 新 gate 先失败**：`scripts/gates/phase6.ps1` 要求真实 DLSSG、generated 非重复/非 blend、PTS/cadence/reset、D3D12 present、WASAPI、4K、A/B 与 A/B/C 延迟和有界队列。
- [ ] **P6.1 DlssFgBackend**：用官方 310.7 header/runtime；capability/HAGS/runtime identity；Create/Evaluate/Release；ResourceNeverProvided flags；2X only。
- [ ] **P6.2 FG 方向与真假**：known translation 片验证 motion scale/方向；60 real → 59 generated；generated hash 不等邻帧，不等 50/50 blend；切镜/duplicate/drop 不跨界生成。
- [ ] **P6.3 PresentSink**：flip-discard、VSync/tearing、resize、3-buffer；Feature 18 后画面→FG→UI/diagnostics→present；主路径 readback=0。
- [ ] **P6.4 Audio**：FFmpeg audio decode + WASAPI event mode；audio master、pause/seek/stop flush；underrun/overrun/drift 日志。
- [ ] **P6.5 EngineController**：UI-safe commands；source/render/audio 生命周期；Capture ingress mailbox=1、A/B window=2、A/B/C window=3，Player/Export backpressure；模式/主动 lookahead 毫秒/fallback 明示。
- [ ] **P6.6 Player probe**：真实 4K H.264/HEVC + audio/subtitle，play/pause/10 seek/resize/loop；A/V drift ≤50 ms；NR/SR/FG 可独立开关。
- [ ] **P6.7 耐久与 gate**：4K30 与 4K60 Player 各 30 分钟；4K60 NR-only 不持续积压，FG2X 产生正确 120 Hz 内部时间线；latency breakdown/queue/VRAM 达 Product Spec；phase6 gate exit 0。
- [ ] **P6.8 Reviewer/checkpoint**：只读 Reviewer，修 P0/P1，本地 checkpoint。

## Phase 7 — 三个首发产品闭环（锁定）

- [ ] **P7.0 联合 gate 先失败**：`scripts/gates/phase7.ps1` 分别验证 4K Player、4K Capture、Image Export、4K H.264/HEVC Video Export、产品 UI/恢复；任何一项缺失都 exit 1。
- [ ] **P7.1 Player tab**：Win32 打开、播放/暂停、seek、loop、全屏、基础字幕、质量档、NR/SR/FG 开关、设置持久化与实时诊断；实际驱动 Phase 6 engine。
- [ ] **P7.2 CaptureCardSource**：FFmpeg avdevice+dshow / DirectShow 枚举；实际 codec/color/size/fps 回报；1080p/2160p 30/60 video+audio；ingress mailbox=1；drop 触发全链 reset；HDR 输入 fail closed。
- [ ] **P7.3 Capture tab**：设备/格式/音频/输出/NR Low Latency/FG Low Latency/Buffered Quality、NR/SR/FG、start/stop/fullscreen；主动 lookahead、fallback、丢帧和分项 latency 可见。
- [ ] **P7.4 Image export**：WIC PNG/JPEG、EXIF orientation、optional SR + reset Feature18、PNG/JPEG output、不覆盖、partial+validate+atomic rename。
- [ ] **P7.5 NVENC dependency/backend**：用户提供 Video Codec SDK 13.1；System32 `nvEncodeAPI64.dll` 受限加载；D3D12 resource registration + per-slot fences；H.264/HEVC 4K capability；只有压缩 bitstream 回 CPU。
- [ ] **P7.6 Video export**：同一 EnhanceGraph；最高 4K H.264/HEVC input、D3D12 NVENC H.264/HEVC MP4/MKV CFR output；optional FG2X；audio remux/AAC；字幕 policy；cancel/crash partial recovery；ffprobe+self-decode 验证。
- [ ] **P7.7 Export tab**：input/output/size/FPS/H.264-HEVC/quality/start/cancel/progress/error；图片与视频都实际工作。
- [ ] **P7.8 产品恢复/诊断**：首次运行检查、最近文件/设备、设置持久化、source reconnect、device-lost、残留 partial 提示、日志包导出。
- [ ] **P7.9 三模式端到端**：4K30/60 Player 各 30 分钟；真实 4K60 采集卡 30 分钟；图片 corpus；4K H.264/HEVC 视频 corpus 含音频/字幕/FG；所有 runtime/result/queue/latency/VRAM 证据完整。
- [ ] **P7.10 Magpie 同源 A/B**：同机同输入同输出条件，比较至少五类片段；只记录有证据的场景限定结论，不把未胜出写成“全面更好”。
- [ ] **P7.11 许可证/分发审计**：Git 无 proprietary/local assets；THIRD_PARTY_NOTICES 完整；不能分发的 runtime/model/SDK 有明确用户供应流程；未清零时标 distribution blocked。
- [ ] **P7.12 Reviewer/final checkpoint**：联合 gate + 新上下文 Reviewer；无 P0/P1；功能状态 release_candidate；分发 blocker 清零后才可 STATE complete；本地 commit；停止，不自动发布。

## 不能移出 V1 的门槛

- 真实物理采集设备，不是窗口/桌面 capture；
- 真正可交互播放器，不是只解码 CLI；
- 图片和视频两种导出，不是截图保存；
- Feature 18 真实 Evaluate；
- NVOF/estimated depth 的真实身份与 fallback；
- DLSSG generated frame 真实性；
- 音频、时序、reset、延迟、内存与许可证证据。
- native 4K Player/Capture/Export，不用 1080p harness、静态图片或虚拟摄像头冒充；
- D3D12 NVENC H.264/HEVC，不用 raw-frame pipe 冒充首发导出；
- 首次运行、设置持久化、设备恢复、partial job recovery 和日志导出。
