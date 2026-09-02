# Veyra Loop Backlog

状态标记：TODO、DOING、BLOCKED、GATE_PASS、REVIEW_PASS、DONE。主 Agent 只能选择当前 Phase 的第一个 TODO；前置 Phase 未完成时，后续项保持锁定。

## Phase 0 — Runtime probe + D3D12 skeleton（当前）

- [x] P0.1 运行 preflight，核对真实工作区、二进制身份、工具链和 GPU/driver；把结果写入 JOURNAL/STATE。（2026-09-02 Goal Cycle 001：preflight 54/54 exit 0；RTX 5070/616.56/12227 MiB/compute 12.0；nvofapi64 32.0.16.1656；MSVC 14.44.35207；CMake 3.31.6；Ninja 1.12.1；DXC 1.8；Git 2.53。证据：loop/JOURNAL.md Cycle 001。）
- [x] P0.2 确认 .gitignore 后按 LOOP_ENGINE 固定顺序初始化本地 Git；显式 staged allowlist、建立 baseline commit、切到 agent/veyra-v1-loop、写 baselineCommit 状态指针；验证两个二进制、runtime_local、third_party_local、reference_local、captures、logs 均未被跟踪。（2026-09-02：baseline=2086282，loop 指针提交=09abf5d，8/8 ignore 探针通过，17 文件 allowlist，Git 后 preflight 66/66。）
- [x] P0.3 先创建 fail-closed 的 scripts/gates/phase0.ps1，编码 Playbook 的 Phase 0 门槛；此时因为工程/输出不存在必须失败。（2026-09-02：gate 建立并 AST-clean；负向运行 exit 1，5 项缺失输入全报；无项目变异。）
- [x] P0.4 建立最小 CMake/C++20 Win32 x64 工程与 Debug/Release presets，不引入 FFmpeg/UI。（2026-09-02：Ninja presets 双配置构建 exit 0，stub 运行 exit 0，build.ps1 解析 vswhere/vcvars，presets 无机器路径。）
- [x] P0.5 实现结构化 logger、HRESULT/NGX result 字符串、文件 size/hash/signature 检查。（2026-09-02：--self-test 实测 BCrypt SHA256、WinVerifyTrust、CryptQueryObject 签名者、版本提取；staged DLL 身份与固定契约全匹配；双 preset 构建 exit 0。）
- [x] P0.6 实现 D3D12DeviceContext：RTX adapter、device、direct queue、debug layer、4-slot command allocator/list/fence/event ring。（2026-09-02：--device-info 实测 RTX 5070/0x10DE/FL 12_2/4-slot ring + timestamp query 全通过；debug layer 缺失记 INBOX。）
- [x] P0.7 实现 veyra_runtime_probe：受限绝对路径加载本地复制的 nvngx_dlssnr.dll，验证 required exports；不得加载 addon。（2026-09-02：LoadLibraryExW 受限 flags 成功，exports 5/5 实测，3s 冒烟 PASS；addon 从未被加载。）
- [x] P0.8 Debug/Release 构建并实际运行 probe；D3D12 窗口循环 5 分钟无 device removed；记录命令、exit code、GPU/driver、hash/signature/exports、日志路径。（2026-09-02：phase0 gate exit 0/70 checks；run-id a5fd6348…；Release 300s/30002 帧 deviceRemoved=false；证据见 loop/EVIDENCE.md。）
- [x] P0.9 当前 gate 通过、只读 Reviewer 通过、修完 P0/P1、写 checkpoint。（2026-09-02：Reviewer PASS，GATE_EXIT 0/0，无 P0/P1；4 条 P2 已记录并按协议处理。）

Phase 0 gate：以 Playbook 第 16 节 Phase 0 为准。未完成 P0.9 禁止开始 Phase 1。

## Phase 1 — Feature 18 native harness（锁定）

- [x] P1.0a（Reviewer P2 加固项，不降低任何门槛）: 在接入 Evaluate 循环前把 ID3D12Fence 时间线计数器收敛为单一 owner（ring 从 device context 派生 fence 值或明确所有权注释），避免 Phase 1 起重复/非单调 fence 值。（2026-09-02：移除 context 冗余计数器，ring 为唯一 signaler，双 preset+device-info 回归通过。）
- [ ] P1.1 先建立 phase1 gate 和确定性 RGBA8 测试帧/输出统计。
- [ ] P1.2 封装 NGX Core、parameter block、DLSSNR runtime 和严格逆序 RAII。
- [ ] P1.3 实现受隔离、可关闭的 caller-name 兼容层；禁止 patch DLL。
- [ ] P1.4 严格按 Playbook 参数名与类型 Create Feature 18。
- [ ] P1.5 实现 Proxy→Feature18→Raw 的 D3D12 资源、barrier、subrect 与异步 4-slot 执行。
- [ ] P1.6 连续 300/300 Evaluate；验证输出非黑、非恒定、随输入/参数变化；记录 GPU timings/result/capture。
- [ ] P1.7 重建/reset/释放测试。
- [ ] P1.8 gate、只读 Reviewer、P0/P1 修复、checkpoint。

## Phase 2 — RenoDX-equivalent parity codec（锁定）

- [ ] P2.1 为 transfer/range/matrix/proxy encode/decode 建 CPU golden tests 和 phase2 gate。
- [ ] P2.2 实现 Original→Parity Encode shader，显式定义输入/输出色域、transfer、range 和资源格式。
- [ ] P2.3 实现 Parity Decode 与 Raw/bypass 对照；记录三个 1.0 中性 codec baseline、来源说明、addon hash 和 capture hash。当前没有 preset；不得加载 addon 取值。
- [ ] P2.4 CPU/GPU golden、identity/bypass、灰阶/色卡/高光测试达到 Playbook 容差。
- [ ] P2.5 完整 Original→Encode→Feature18→Decode capture；不得以主观好看作为 gate。
- [ ] P2.6 gate、只读 Reviewer、P0/P1 修复、checkpoint。

## Phase 3 — Minimal video pipeline（锁定）

- [ ] P3.1 引入固定版本 FFmpeg 依赖和 phase3 gate，不改变 NGX harness。
- [ ] P3.2 先完成 H.264/HEVC 软件 decode 最小基线，正确按 PTS/VFR 驱动，不猜固定 FPS。
- [ ] P3.3 共享 Veyra D3D12 device 的 D3D12VA decode，记录实际 AV_PIX_FMT_D3D12、hw device 和 shared device identity。
- [ ] P3.4 完成 YUV range/matrix/transfer→linear RGB，禁止正常路径 GPU→CPU 像素回读。
- [ ] P3.5 建有界 decode/process/present 队列和 backpressure。
- [ ] P3.6 实现 open/seek/resize/pause-resume/device-lost 的历史 reset；10 次 seek 无旧历史影像。
- [ ] P3.7 真实视频 30 分钟、seek 压测、内存/队列/延迟证据。
- [ ] P3.8 gate、只读 Reviewer、P0/P1 修复、checkpoint。

## Phase 4 — DLSS SR（锁定）

- [ ] P4.1 建 phase4 gate 和明确的 SR off/on 可观测计数。
- [ ] P4.2 从 Playbook 固定的官方 310.7 manifest 接入 DLSS SR，严格处理 render/output subrect、资源状态和 reset。
- [ ] P4.3 验证 bypass、resize/seek、输入输出尺寸与 SR feature identity；不得把 NR 冒充 SR。
- [ ] P4.4 gate、只读 Reviewer、P0/P1 修复、checkpoint。

## Phase 5 — Zero/NVOF guidance（锁定）

- [ ] P5.1 保留并验证明示的 Zero Guidance 路径。
- [ ] P5.2 查询 System32 NVOF runtime/version/capability；建立 phase5 gate、flow/confidence 可视化与合成平移片。
- [ ] P5.3 接入 NVOF，验证 current→previous 符号、fixed 10.5 / 32 转换、grid/scale/densify、scene cut、seek/reset；绝不以零向量冒充 NVOF。
- [ ] P5.4 让 NR/FG 消费同一个 GuidanceFrame；验证 API context 无多线程并发调用；比较 Zero/NVOF 日志和 reset。
- [ ] P5.5 gate、只读 Reviewer、P0/P1 修复、checkpoint。若 SDK 获取需要用户接受条款，先完成 Phase 5 内 capability/Zero fallback/接口测试，写 INBOX；gate 保持未通过且 Phase 6 继续锁定。

## Phase 6 — DLSSG 2X（锁定）

- [ ] P6.1 建 phase6 gate：generated count、内容 hash、timestamp/cadence、disable flag。
- [ ] P6.2 接入真实 DLSSG 2X 输入、history 和 reset；默认按官方 header 将 source-pixel motion 用 `1/width,1/height` 归一化，Magpie `{1,1}` 仅保留显式 diagnostic mode。
- [ ] P6.3 用已知像素平移片验证 DLSSG motion 方向/幅值和 scale mode，证明 generated frame 不是重复 present；验证 seek/pause/resume/scene-cut。
- [ ] P6.4 确保 UI/OSD 在 FG 后合成，队列和 present cadence 有界。
- [ ] P6.5 gate、只读 Reviewer、P0/P1 修复、checkpoint。

## Phase 7 — Audio + minimum UI（锁定）

- [ ] P7.1 建 phase7 端到端 gate 和 A/V sync/latency 采样。
- [ ] P7.2 实现 audio-master 时钟、音频输出、drop/repeat 策略。
- [ ] P7.3 实现最小 open/play/pause/seek、功能开关、诊断 OSD；UI 位于 FG 后。
- [ ] P7.4 执行 60 分钟 A/V drift、seek storm、pause/resume、resize、device recovery、FG on/off 音频时长和功能组合矩阵。
- [ ] P7.5 Debug/Release 最终构建、端到端证据、许可证/跟踪文件审计。
- [ ] P7.6 gate、只读 Reviewer、修完全部 P0/P1、最终 checkpoint，按 Loop COMPLETE 条件收口。

## 不属于本 Goal

HDR、3X/4X、采集卡、Depth Anything、FRUC、主观画质调优、公开发布和安装包全部留在 V1 Goal 之外。
