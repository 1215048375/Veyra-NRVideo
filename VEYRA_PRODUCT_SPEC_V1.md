# Veyra DLSS 视频播放器
## Product Spec V1（调用复现优先）

> 文档状态：Locked V1 Product Boundary  
> 日期：2026-09-01  
> 当前验证环境：Windows 11 x64 / GeForce RTX 5070 / Driver 616.56  
> 核心目标：不研究 DLSS 5 是否适合普通视频，不自研模型画质；只完成稳定调用，并尽量复现 RenoDX/ReShade 的处理结果。

---

# 0. 前提与边界

本方案接受以下已经被 Magpie Experimental 证明的事实，不再重复做可行性研究：

1. 普通软件窗口、游戏捕获画面和视频帧可以作为 DLSS 输入。
2. 独立应用可以调用 DLSS Neural Rendering、DLSS Super Resolution 和 DLSS Frame Generation。
3. 视频没有游戏原生 Motion、Depth、Camera、HUD-less Buffer 时，仍可通过 Zero Guidance 或估算 Guidance 驱动相关功能。
4. 第一阶段不评价 DLSS 5 对不同视频类型“好不好看”，不训练模型，也不承担 NVIDIA 模型本身的画质优化。

本规格只解决四件事：

```text
1. 播放器稳定取得 GPU 视频帧
2. 原生创建并执行 DLSSNR Feature 18
3. 复现 RenoDX/ReShade 的颜色代理、参数与输出处理
4. 原生执行 DLSS Frame Generation，并正确显示生成帧
```

“不再做可行性测试”不等于“不做工程验收”。仍必须记录 Create/Evaluate 是否成功、是否真的生成新帧、输出时序是否正确，以及与 ReShade 是否存在明显的颜色管线偏差；这些是完成定义，不是重新研究 DLSS 是否可用于视频。

---

# 1. 产品定义

做一个面向 NVIDIA RTX 的极简 Windows 视频播放器与后续采集卡 Viewer：

- FFmpeg 负责文件解封装、视频解码和音频解码；
- D3D12 负责核心 GPU 管线；
- DLSS Super Resolution 负责需要的分辨率放大；
- `nvngx_dlssnr.dll` Feature 18 负责 DLSS 5 Neural Rendering；
- RenoDX Parity Layer 负责复现 ReShade 插件前后的颜色转换和输出处理；
- NVIDIA NGX DLSS Frame Generation 负责 2X，后续再开放 3X/4X；
- ReShade 只作为开发期参考实现和对照输出，不进入最终产品运行链；
- 播放器 UI、字幕和 OSD 在 Frame Generation 之后绘制。

产品第一优先级不是媒体库和播放器功能，而是：

> 同一输入、同一 DLSSNR DLL、同一参数下，Veyra 能稳定执行 Feature 18，并获得与 RenoDX/ReShade 同类的画面处理结果。

---

# 2. 已确认的本地运行时事实

当前目录中的 `nvngx_dlssnr.dll`：

```text
Version:                  310.8.0.0
SHA-256:                  E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
Authenticode:             Valid / NVIDIA Corporation
Minimum Driver:           615.00
GPU Architecture:         NVSDK_NGX_GPU_Arch_Blackwell2
Feature Runtime:          NVIDIA DLSSNR - DVS PRODUCTION
```

当前目录中的 `renodx-dlss5-1.addon64`：

```text
Internal Name:            renodx-dlss5.addon64
Version:                  0.2026.0827.2036
SHA-256:                  837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F
Authenticode:             Not signed
Type:                     ReShade binary add-on, not a configuration file
```

插件暴露出的行为包括：

- 拦截 D3D12 NGX Create/Evaluate；
- 在已有 DLSS 输出之后执行 Feature 18；
- 使用 Color、MotionVectors、Depth 和 Output 合同；
- 建立 Control-compatible color transfer；
- 使用 soft-clip、sRGB proxy、UpgradeToneMap、HDR/chroma transfer；
- 提供 Preset、Style、Intensity、Local Tone、Local Structure、Skin Structure、Auto Mask、UI Correction、Depth Convention 和 Motion Scale 参数。

因此，“接近 ReShade”的主要任务不是重新训练或优化 DLSS 5，而是复现它在 Feature 18 前后的数据合同。

当前原始签名 DLL 的首个明确目标平台是本机 RTX 5070。不要在 V1 文案中宣称支持所有 RTX；其他显卡必须等实际 runtime capability 返回后再加入支持列表。

---

# 3. 最终处理管线

```text
FFmpeg Demux / Decode
        ↓
D3D12 Video Frame
        ↓
YUV → Working RGB
range / matrix / transfer / chroma
        ↓
Optional DLSS Super Resolution
只在输入分辨率低于输出分辨率时启用
        ↓
RenoDX Parity Encode
Control-compatible proxy / soft clip / transfer
        ↓
DLSSNR Feature 18
Color + Motion/Zero Motion + Depth/Zero Depth
        ↓
RenoDX Parity Decode
UpgradeToneMap / color strength / luminance protection
        ↓
DLSS Frame Generation
2X first
        ↓
Subtitle / OSD / Player UI
        ↓
DXGI Present
```

两条分辨率路径必须分开：

```text
同分辨率：Decode → Parity Encode → DLSSNR → Parity Decode

放大播放：Decode → DLSS SR → Parity Encode → DLSSNR → Parity Decode
```

DLSSNR 不承担播放器的分辨率放大。需要 1080p→4K 时，由 DLSS SR 独立完成。

---

# 4. 技术架构

## 4.1 基础技术栈

- C++20
- CMake
- Win32 Window
- D3D12
- DXGI Flip Model Swap Chain
- FFmpeg shared libraries
- WASAPI
- NVIDIA NGX / DLSS SDK headers
- NVIDIA NGX DLSSG helper contract；Streamline 只保留为 V1 之后的替换研究
- NVIDIA Optical Flow API
- Dear ImGui 仅用于开发面板；正式 UI 可继续使用或改为轻量自绘

禁止在 V1 引入：

- Qt
- Electron / Chromium
- MFC 大型 UI 架构
- OpenCV
- 插件系统
- 媒体库和网络视频平台
- Depth Anything / ONNX Runtime
- 自研 Motion Cleaner
- FRUC、XeSS FG 等第二帧生成后端

## 4.2 共享 D3D12 设备

播放器、DLSS SR、DLSSNR、DLSSG 和 Guidance 应尽量共享同一物理 Adapter 和 D3D12 Device。

```text
D3D12DeviceContext
├─ Graphics/Compute Queue
├─ Video Decode Queue
├─ NGX Core Session
├─ Descriptor Allocator
├─ Texture Pool
├─ Fence Timeline
└─ GPU Timestamp Queries
```

正常播放路径禁止 CPU readback。只有开发期截图、诊断和极小状态值允许 readback。

## 4.3 NGX Core Host

建立进程级唯一的 `NgxCoreHost`：

- 使用本项目自己的 Project ID 和 `NVSDK_NGX_ENGINE_TYPE_CUSTOM`；
- 负责 NGX Init、Capability Parameters、Parameter Allocation 和最终 Shutdown；
- DLSS SR、DLSSNR、DLSSG 作为独立 consumer 共用生命周期；
- 所有 proprietary API 调用外层增加 SEH 防护和结构化错误日志；
- 禁止多个模块各自随意 Init/Shutdown NGX；
- Device Lost 或 Adapter Change 时整体销毁并重建。

## 4.4 DLSSNR Runtime Adapter

`DlssNrBackend` 只暴露稳定的内部接口：

```cpp
struct DlssNrCreateDesc {
    uint32_t width;
    uint32_t height;
    uint32_t preset;
    DXGI_FORMAT inputFormat;
    DXGI_FORMAT outputFormat;
};

struct DlssNrEvalDesc {
    GpuTexture color;
    GpuTexture motion;
    GpuTexture depth;
    GpuTexture output;
    DlssNrControls controls;
    bool reset;
    uint64_t frameId;
};
```

实现要求：

- 动态加载 `nvngx_dlssnr.dll`；
- 获取 D3D12 Init/Create/Evaluate/Release/Shutdown exports；
- 当前 pre-release signed-snippet 兼容行为隔离在单独 adapter 中；
- Feature ID、私有参数名和 runtime workaround 不得泄漏到播放器业务层；
- 将来 NVIDIA 发布正式 Streamline DLSS 5 后，只替换 backend，不重写播放器；
- runtime 加载前核对文件名、版本、SHA-256、NVIDIA 签名、最低驱动和 GPU capability；
- 不自动下载、不写入 Git、不进入公开安装包。

## 4.5 参数层

NGX Parameter 必须强类型封装：

```cpp
SetU32(name, value)
SetI32(name, value)
SetF32(name, value)
SetD3D12Resource(name, resource)
```

V1 支持参数：

- Width / Height
- Render Preset
- Style
- Intensity
- Local Tone Strength
- Local Structure Strength
- Skin Structure Strength
- Automatic Mask
- UI Correction
- Motion Vector Scale X/Y
- Depth Inverted
- Enabled
- Reset
- Color / Motion / Depth / Output subrect

DLSSNR 参数默认值以 Playbook 已锁定的当前 runtime 调用合同为准。项目中没有 ReShade preset 配置文件；`renodx-dlss5-1.addon64` 是二进制 add-on，不能把它冒充 preset。Parity codec 的三个可调值在 V1 固定使用中性工程基线 1.0；若以后用户提供真实 preset，只新增可选对照 profile，不改变 V1 门禁。

---

# 5. RenoDX Parity Layer

## 5.1 定位

ReShade/RenoDX 只承担两个角色：

1. 当前 add-on 中可观察到的行为和 shader 数学参考；
2. 用户以后提供同版本 preset/capture 时的可选固定输入对照。

最终程序不加载 ReShade，也不加载 `.addon64`。

## 5.2 必须复现的阶段

```text
Original Working RGB
        ↓
Control-compatible proxy encode
        ↓
Feature 18 raw neural output
        ↓
Proxy decode / UpgradeToneMap
        ↓
Color-strength and luminance reconstruction
        ↓
Final RGB
```

根据当前 `renodx-dlss5-1.addon64` 内嵌 shader，Parity Codec 按下面的确定行为实现，不从零猜算法。

### A. Proxy Encode

输入为播放器正确解码后的线性 RGB：

1. 保留一份高精度 `Original`；
2. `Original.rgb` 除以 `PaperWhiteScale`，负值截为 0；
3. 对 0.75 以上的高光使用指数 shoulder soft-clip，压到可供当前模型使用的范围；
4. 执行标准 sRGB Encode；
5. 写入 RGBA8 `Proxy`，作为 `DLSSNR.Color`。

V1 资源建议：

```text
Original: R16G16B16A16_FLOAT
Proxy:    R8G8B8A8_UNORM
Neural:   R8G8B8A8_UNORM
Final:    R16G16B16A16_FLOAT
```

### B. Feature 18

```text
DLSSNR.Color  = Proxy
DLSSNR.Output = Neural
DLSSNR.MVec   = Motion or Zero Motion
DLSSNR.Depth  = Depth or Zero Depth
```

Feature 18 处理的是经过 Control-compatible transfer 的 `Proxy`，不是直接处理播放器最终显示用的线性 FP16 画面。

### C. Parity Decode

输出阶段同时读取 `Original`、`Proxy` 和 `Neural`：

1. 对 `Proxy` 和 `Neural` 做 sRGB Decode；
2. 使用 BT.709 luminance 系数计算三者亮度；
3. 根据 `Original` 与 `Proxy` 的亮度差，对 Neural 恢复被 soft-clip 压掉的亮度范围；
4. 在 OkLab 中恢复色相/色度关系；
5. 转入 AP1 约束负色域，再转回 BT.709；
6. 使用 `TransferStrength` 在 Original 与升级结果之间混合；
7. 单独构造 luminance-only 结果，再用 `ColorStrength` 决定保留多少 Neural 色彩变化；
8. 乘回 `PaperWhiteScale`，保留 Original alpha，写入 Final FP16。

这三个常量必须进入配置和抓帧清单：

```text
PaperWhiteScale
TransferStrength
ColorStrength
```

它们属于 RenoDX Parity Codec，不等同于 Feature 18 自身的 `Intensity`、`LocalToneStrength` 或 `LocalStructureStrength`。

内部保留四个可抓取观察点：

```text
00_original
01_proxy_input
02_raw_dlssnr
03_final_parity_output
```

## 5.3 实现原则

- 将 Parity Codec 写成独立 D3D12 compute pass；
- 编码和解码都可单独 bypass；
- 第一版先对齐 SDR；
- HDR Transfer 参数和 HDR typed UAV 进入 V1.1；
- 不加入主观锐化、降噪、肤质保护等额外滤镜；
- 若结果与 ReShade 不同，优先检查格式、transfer、range、参数类型、subrect 和资源状态，不先怀疑模型。

## 5.4 “类似 ReShade”的完成定义

没有外部 RenoDX capture 时，完成条件由 CPU reference、GPU shader 对照和四阶段 capture 确定；不得编造一份“参考图”。用户以后提供同 addon hash、preset、输入和分辨率的 capture 时，再追加对照，不替换基础门禁。完成条件是：

- CPU/GPU encode/decode 达到 Playbook 数值容差，四阶段资源真实存在；
- 固定灰阶、色卡和高光输入不出现系统性洗白、压黑、截高光或整体色相漂移；
- Style、Intensity、Structure、Auto Mask 以 Playbook 的精确名称/类型传入，开关或数值变化产生可观测且非恒定的输出；只有存在合格 reference 时才宣称调节方向与 RenoDX 一致；
- Raw DLSSNR 与 Final Parity Output 可以分别抓取，确认差异来自 parity layer；
- 若存在合格外部 reference capture，同一输入/参数下再检查整体亮度、gamma、饱和度与局部对比不存在明显系统性偏差。

这是接口复现验收，不是评价 DLSS 5 本身的审美效果。

---

# 6. Frame Guidance

V1 不自研 Guidance 算法，只实现 Magpie 已证明可工作的最小合同。

统一内部规范：

```text
Motion Direction: Current → Previous
Motion Unit:      Source Pixels
Motion Format:    R16G16_FLOAT
Depth Format:     R32_FLOAT
Frame Identity:   uint64 frameId
```

Provider：

```text
ZeroGuidanceProvider
├─ Zero Motion
└─ Zero Depth

NvofGuidanceProvider
├─ NVIDIA Optical Flow
├─ Dense Motion output
└─ Zero Depth by default
```

V1 默认：

- DLSSNR：NVOF Motion，可回退 Zero Motion；
- DLSSG：NVOF Motion + Zero Depth；
- Estimated Depth：不实现；
- Motion Cleanup / Occlusion：不实现；
- NVOF cost 只归一化成 diagnostic confidence，供可视化、日志和 scene-cut 诊断；V1 不把 confidence 冒充 NR/FG 的正式输入；
- 文件和采集模式使用同一 provider contract。

必须 Reset 的事件：

- Seek
- Resize
- Source Change
- Device Lost
- Long Pause Resume
- 硬切检测命中
- Guidance frameId 不连续

Scene Cut V1 只需要一个轻量直方图/帧差检测器，用于触发 Reset，不承担画质优化。

---

# 7. DLSS Super Resolution

DLSS SR 与 DLSSNR 是两个独立阶段：

- 输出尺寸等于输入尺寸：跳过 DLSS SR；
- 输出尺寸大于输入尺寸：先 DLSS SR，再 DLSSNR；
- DLSSNR 的输入/output 尺寸在 V1 保持一致；
- UI 中将“分辨率增强”和“DLSS 5 Neural Rendering”分成两个开关，避免概念混淆。

用户 UI：

```text
Upscale: OFF / Quality / Balanced / Performance
DLSS 5:  OFF / ON
```

V1 的 DLSS SR 固定使用 Zero Guidance；共享 NVOF 在 SR 输出之后生成，只供下游
DLSSNR/DLSSG 使用，不能形成“先有 SR 输出才能算 NVOF、SR 又等待该 NVOF”的
循环依赖。未来若要给 SR 实际 motion，必须另设 render-resolution provider，
不复用本 V1 的 full-resolution `GuidanceFrame`。

---

# 8. DLSS Frame Generation

## 8.1 V1 范围

- Native NGX DLSSG；
- 只发布 2X；
- 3X/4X 代码结构预留但 UI 隐藏；
- 使用 capability parameters 查询可用性和最大倍率；
- 不加入 FRUC 或其他后端。

## 8.2 输入合同

```text
Final DLSSNR Color
+ NVOF Motion / Zero Motion fallback
+ Zero Depth
+ Reset State
→ DLSSG
```

内部 `GuidanceFrame` 的 motion 单位固定为 source pixels；DLSSG adapter 必须按
官方 310.7 header 的语义把它归一化到 `[-1,1]`，即 full-resolution motion
使用 `mvecScale = {1.0 / width, 1.0 / height}`。NR 的私有 Feature 18 合同仍按
其已验证参数使用 `MVecScaleX/Y = 1`，两个 backend 不得误共用同一 scale 值。
固定 Magpie experimental 提交虽然在 DLSSG 写了 `{1,1}`，但这与官方 header
注释冲突；该值只保留为显式 `magpie-unit` 诊断模式，不能静默成为 Veyra 默认。
Phase 6 用已知像素平移片记录两种 mode、实际 scale、输出 hash/counter 和方向/
幅值结论，不以主观观感选择。

V1 明确标记未提供：

- HUD-less
- UI Color
- UI Alpha
- Bidirectional Distortion Field
- Output Real

播放器自己的 UI 和字幕放在 FG 后绘制，因此不会进入 DLSSG。

## 8.3 时序

30→60 示例：

```text
Real A       0.00 ms
Generated   16.67 ms
Real B      33.33 ms
Generated   50.00 ms
Real C      66.67 ms
```

要求：

- 音频仍是 master clock；
- 生成帧具有独立 presentation timestamp；
- 不通过重复 Present 假造输出 FPS；
- Seek、暂停恢复和源切换后先 Reset，再接受新生成帧；
- 记录每次 Evaluate、Interpolation Enabled/Disabled 和发布成功数。

---

# 9. 播放器

## 9.1 V1 用户功能

- 打开本地视频；
- 播放、暂停、Seek；
- 音量和静音；
- 窗口/全屏；
- Upscale OFF/模式；
- DLSS 5 OFF/ON；
- Frame Generation OFF/2X；
- 显示输入分辨率、输入 FPS、输出 FPS 和 backend 状态。

## 9.2 媒体管线

V1 的解码路径：

```text
FFmpeg D3D12VA
→ D3D12 texture

Fallback:
software decode
→ GPU upload
```

D3D11VA/D3D11On12 不在 V1；它会额外引入 device/queue/handle 同步和资源状态
边界，不能作为弱模型“顺手加”的第三条路径。

V1 gate 的最低格式矩阵：

- H.264 / HEVC video；
- AAC / PCM audio；
- MP4 / MKV container。

AV1、VP9、Opus、FLAC、MOV、WebM 可以在 FFmpeg 已提供 decoder/demuxer 时作为
best-effort 路径，但不允许拖延 Phase 3/7，也不能在没有固定测试片证据时写成
已支持。

## 9.3 UI

底栏只保留：

```text
Play | Volume | Upscale | DLSS 5 | FG 2X | Fullscreen
```

开发面板显示：

- NGX Core status
- DLSSNR Create/Evaluate result
- DLSSNR active parameters
- SR/NR/FG GPU time
- Feature 18 successful frame count
- FG real/generated frame count
- Guidance type
- Reset count and reason
- Queue depth / dropped frames

---

# 10. 工程目录

```text
Veyra/
├─ CMakeLists.txt
├─ cmake/
├─ third_party/
│  └─ README.md
├─ runtime_local/                 # gitignored，不进入发行包
├─ src/
│  ├─ app/
│  │  ├─ App.cpp
│  │  ├─ MainWindow.cpp
│  │  └─ Settings.cpp
│  ├─ gfx/
│  │  ├─ D3D12DeviceContext.cpp
│  │  ├─ SwapChain.cpp
│  │  ├─ TexturePool.cpp
│  │  ├─ ColorConverter.cpp
│  │  └─ GpuProfiler.cpp
│  ├─ media/
│  │  ├─ FFmpegDemuxer.cpp
│  │  ├─ FFmpegVideoDecoder.cpp
│  │  ├─ FFmpegAudioDecoder.cpp
│  │  ├─ PlaybackClock.cpp
│  │  └─ AudioWASAPI.cpp
│  ├─ ngx/
│  │  ├─ NgxCoreHost.cpp
│  │  ├─ NgxParameters.cpp
│  │  ├─ DlssSrBackend.cpp
│  │  ├─ DlssNrBackend.cpp
│  │  ├─ DlssNrRuntimeAdapter.cpp
│  │  └─ DlssFgBackend.cpp
│  ├─ parity/
│  │  ├─ RenoDxParityCodec.cpp
│  │  ├─ RenoDxParityProfile.cpp
│  │  └─ ParityCapture.cpp
│  ├─ guidance/
│  │  ├─ FrameGuidance.h
│  │  ├─ ZeroGuidanceProvider.cpp
│  │  ├─ NvofGuidanceProvider.cpp
│  │  └─ SceneCutReset.cpp
│  ├─ player/
│  │  ├─ VideoPipeline.cpp
│  │  ├─ FrameScheduler.cpp
│  │  └─ Presenter.cpp
│  └─ ui/
│     ├─ PlayerControls.cpp
│     └─ DebugPanel.cpp
├─ tools/
│  ├─ nr_harness/
│  └─ parity_capture/
└─ validation/
   ├─ fixed_frames/
   ├─ fixed_clips/
   └─ expected_manifest.json
```

`validation/` 不是用于研究模型好坏，而是防止接口、色彩和时序在代码修改后回归。

---

# 11. 开发阶段

## Phase 0 — Runtime Manifest 与 D3D12 Skeleton

- C++20 / CMake；
- Win32 Window；
- D3D12 Device、Queue、SwapChain；
- runtime manifest；
- DLL hash/signature/version/capability 检查；
- logging、SEH guard、GPU timestamp。

完成条件：

- 当前 RTX 5070 / 616.56 环境识别正确；
- D3D12 窗口稳定；
- proprietary runtime 不进入 Git。

## Phase 1 — Feature 18 Native Harness

先实现独立序列执行器，不先造完整播放器：

```text
fixed frame sequence
→ D3D12 texture
→ NGX Core
→ signed DLSSNR runtime adapter
→ Feature 18 Create/Evaluate
→ output capture
```

完成条件：

- Feature 18 Create 成功；
- 连续 300 帧 Evaluate 成功；
- Preset、Style、Intensity 等参数可改变；
- Release/Shutdown 无泄漏或崩溃；
- Raw output 可抓取。

## Phase 2 — RenoDX Parity Codec

- Proxy encode；
- Feature 18；
- Proxy decode / tone mapping；
- 参数映射；
- Original/Proxy/Raw/Final 四阶段确定性抓帧。

完成条件：

- CPU/GPU reference 数值门禁通过，固定灰阶/色卡/高光没有系统性 gamma、亮度、饱和度或高光偏差；
- 参数变化产生可观测、非恒定输出；只有存在同 addon hash、preset、输入和分辨率的合格外部 reference 时，才追加“与 RenoDX 同方向”的结论；
- 可切换 Raw / Parity Final。

## Phase 3 — Minimal Video Pipeline

- FFmpeg demux/decode；
- 先做 video-only；
- 连续 PTS；
- DLSSNR history；
- Seek/reset；
- D3D12 present。

完成条件：常见 1080p/4K 文件可连续播放，Feature 18 每个 real frame 稳定执行。

## Phase 4 — DLSS Super Resolution

- 输入/输出分辨率策略；
- DLSS SR Create/Evaluate；
- SR → NR 顺序；
- resize/reset。

完成条件：同分辨率自动跳过 SR，放大模式先 SR 后 NR，不发生错误 subrect 或资源错配。

## Phase 5 — NVOF Guidance

- Zero Guidance baseline；
- NVIDIA Optical Flow；
- Current→Previous / source-pixel contract；
- frameId 和 reset。

完成条件：DLSSNR 和 DLSSG 均消费同一个 source-pixel `GuidanceFrame`，各自 adapter
执行自己的 scale 转换；当前机器 capability 可用时必须验证真实 NVOF，真实查询为
unsupported 时才自动回退 Zero，不能主动跳过后把 Zero 标成 NVOF。

## Phase 6 — DLSS Frame Generation 2X

- capability query；
- NGX DLSSG Create/Evaluate；
- 生成帧发布；
- PTS/cadence；
- FG 后 UI。

完成条件：

- 确认 interpolation enabled；
- 每两个 real frame 之间发布一个 generated frame；
- 输出 FPS 统计来自实际 present，不是理论倍率；
- Seek/pause/resize 后无旧历史生成帧。

## Phase 7 — Audio 与最小 UI

- FFmpeg audio decode；
- WASAPI；
- audio master clock；
- 播放控制；
- 正式底栏与隐藏调试面板。

完成条件：长时间播放不发生持续 A/V drift，FG 不改变音频时长。

# 12. 验收标准

## 12.1 DLSSNR 调用

- 日志明确记录 runtime hash、版本、Feature ID 和 Create result；
- 每帧记录 Evaluate result；
- 输出不是未处理输入的别名或错误 Copy；
- 参数类型固定且可追踪；
- reset 后首帧行为稳定；
- 不因连续 Seek、Resize、暂停恢复崩溃。

## 12.2 RenoDX/ReShade 相似性

- 固定输入和固定参数；
- 保存 Original、Proxy、Raw NR、Final 四阶段；
- CPU/GPU parity 数值测试达到 Playbook 容差；
- 检查全局亮度、gamma、饱和度、局部对比和高光范围；
- 不做“哪个更好看”的主观评分；
- 无外部 reference 时按已记录 shader 数学验收；有合格 reference 时才追加同条件对照。

## 12.3 Frame Generation

- capability 查询成功；
- Create/Evaluate 成功；
- generated frame count 与 real frame count 对应；
- 生成帧拥有正确 PTS；
- present cadence 连续；
- reset 后不混入旧 source frame；
- UI/字幕不进入生成输入。

## 12.4 性能与稳定性

- 正常路径无 CPU frame readback；
- 每个 GPU stage 有 timestamp；
- texture/fence/parameter/feature 生命周期无泄漏；
- 记录 1080p 和 4K 的实测 GPU 时间，不在开发前虚构固定毫秒目标；
- Device Lost 能显式失败或重建，不能静默黑屏。

---

# 13. 依赖与代码边界

## 13.1 Magpie

Magpie 已经完成可行性证明，并可作为：

- API 调用顺序参考；
- 参数合同参考；
- Guidance 数据格式参考；
- DLSSG 状态和计数参考。

如果 Veyra 计划闭源，不复制 Magpie GPLv3 源码；根据公开 SDK 接口和可观察行为独立实现。若决定接受 GPLv3 并公开对应源码，则可以重新评估直接复用的成本。

## 13.2 RenoDX Add-on

- 开发期本地参考；
- 不链接进产品；
- 不随安装包分发；
- 不把 `.addon64` 当配置文件；
- V1 codec 配置使用三个 1.0 中性工程 baseline 并记录到 validation manifest；
- 只有用户另行提供真实 `ReShade.ini`/preset 和配套 capture 时，才建立可选 reference profile。

## 13.3 NVIDIA Runtime

- `nvngx_dlssnr.dll` 只放 `runtime_local/`；
- runtime 目录加入 `.gitignore`；
- 不自动下载；
- 不修改签名 DLL；
- 发行方式与正式 SDK 出现后单独决定；
- backend 必须可被将来的官方 Streamline DLSS 5 实现替换。

---

# 14. V1 明确不做什么

- 不研究 DLSS 5 是否适合普通视频；
- 不评价不同内容类型的模型审美；
- 不训练或微调 AI 模型；
- 不自研 Motion Cleaner；
- 不接入 Depth Anything；
- 不开发 FRUC fallback；
- 不追求所有 RTX 显卡兼容；
- 不做 HDR；
- 不做 3X/4X；
- 不做复杂字幕和媒体库；
- 不把 ReShade 放入最终依赖；
- 不先做采集卡而延误文件播放器主链。

---

# 15. 最终 V1 定义

```text
V1.0 =
Minimal FFmpeg Player
+ D3D12 GPU-resident pipeline
+ Optional DLSS Super Resolution
+ Native DLSSNR Feature 18
+ RenoDX-equivalent parity layer
+ NVOF / Zero Guidance
+ Native DLSS Frame Generation 2X
+ WASAPI sync
+ Minimal UI
```

V1 的发布门槛不是“我们优化了 NVIDIA 的模型”，而是：

> Veyra 能在不依赖 ReShade 的情况下稳定调用 DLSSNR 与 DLSSG，按已记录的 RenoDX parity 数学正确处理颜色、参数和输出；若存在合格外部 reference，再证明不存在明显的管线级偏差。

---

# 16. 开工顺序

Agent 或开发者必须严格按以下顺序工作：

```text
1. Phase 0：D3D12 + runtime manifest
2. Phase 1：Feature 18 native harness
3. Phase 2：RenoDX parity codec
4. Phase 3：video-only FFmpeg pipeline
5. Phase 4：DLSS SR
6. Phase 5：NVOF guidance
7. Phase 6：DLSSG 2X
8. Phase 7：audio/UI
```

禁止越过 Phase 1/2 直接堆播放器功能。调用链和 parity layer 是项目主体，播放器只是宿主。
