# Veyra Human Inbox

这里仅记录 Agent 无权自行完成的许可、凭据、外部硬件或发布决定。普通 bug 不放这里。缺失项不阻止当前 Phase 中与它独立的工作，但相关 gate 不得伪造通过。

## ACTIVE · NVIDIA Optical Flow SDK 5.0 package missing（用户已同意许可方向）

- 需要原因：Phase 5 必须接入真实 NVOF motion/cost、验证 current→previous、S10.5 `/32`、grid/densify 和 confidence。System32 只有 runtime DLL，不含可合法编译的 SDK header/sample。
- 当前事实：`C:\Windows\System32\nvofapi64.dll` 已存在且 NVIDIA 签名；`third_party_local/nvidia/Optical_Flow_SDK_5.0/` 当前缺失。
- 用户决定（2026-09-03）：用户明确表示接受 NVIDIA EULA，并授权 Veyra 在本机研发中使用该官方 SDK。不要再次询问用户是否愿意接受。
- 尚未完成：聊天中的授权不能替代 NVIDIA Developer Portal 上由用户本人完成的 `Accept & Download`，当前也没有发现 SDK 文件。相关目录出现并通过结构/版本/hash 核对前，P5.5 仍是外部依赖阻塞。
- Agent 已被禁止：代用户登录 Developer Program、点击接受 EULA、从无许可证/GPL 仓库抄 header。
- 用户动作：访问 <https://developer.nvidia.com/opticalflow/download>，接受条款并将 SDK 5.0 解压到：

  `third_party_local/nvidia/Optical_Flow_SDK_5.0/`

- 放好后无需改文档，下一轮 preflight/P5.5 会核对结构、版本与 hash。

## ACTIVE · 真实采集卡与测试信号

- 需要原因：Phase 7 的“采集卡模式”不能由窗口捕获、虚拟摄像头或 mock 冒充。
- 硬件条件：Windows 能通过 DirectShow/UVC 枚举、并真实提供 3840×2160 60 fps SDR 的采集设备；另一台主机或 HDMI 测试源能稳定输出 4K60 SDR 与 HDMI 音频；最好带 HDMI passthrough，方便分别测试玩家直通与 Veyra 缓冲预览。
- 用户动作：在 P7.2 前连接并保持设备可用。厂商只提供私有 SDK、未暴露 DirectShow 的卡不属于首发兼容范围。

## ACTIVE · NVIDIA Video Codec SDK 13.1 package missing（用户已同意许可方向）

- 需要原因：首发 4K H.264/HEVC 导出必须使用 D3D12 NVENC，不能继续用 raw-frame readback/pipe。
- 当前事实：系统驱动通常提供 `nvEncodeAPI64.dll`，但项目尚无可合法编译的 Video Codec SDK 13.1 header/sample。
- 用户决定（2026-09-03）：用户明确表示接受 NVIDIA EULA，并授权 Veyra 在本机研发中使用该官方 SDK。不要再次询问用户是否愿意接受。
- 尚未完成：仍需用户本人在 NVIDIA Developer Portal 完成接受/下载，或把已经从官方取得的压缩包解压到下列目录；目录出现并经核验前，P7.5 不能标完成。
- 用户动作：从 NVIDIA 官方 Video Codec SDK 页面接受条款并解压到：

  `third_party_local/nvidia/Video_Codec_SDK_13.1.0/`

- Agent 必须核对 `Interface/nvEncodeAPI.h`、D3D12 sample、版本、hash 和 EULA；不得从竞品仓库复制 header。

## PENDING · 深度模型与 Microsoft ML 包许可记录

- Agent 可从官方来源获取固定版本到 gitignored `third_party_local`，但必须先保存 SHA256 与许可证。
- 只允许商业条件清晰的 Depth Anything V2 Small / Video Depth Anything Small；不得为了更高画质混入 CC-BY-NC 的 Base/Large 权重。
- 如果官方权重条款或来源无法确认，Depth UI 保持 unavailable，P5 quality gate 仍要求明确 Auto fallback；不得下载来路不明的模型。

## PENDING · 公开分发决定

- 当前只授权本机研发与本地 checkpoint。
- 制作安装包、上传 release、分发 `nvngx_dlssnr.dll`、`nvngx_dlssg.dll`、NVOF SDK、模型或 FFmpeg binary 前，必须单独完成许可证审查并得到用户确认。

## RESOLVED HISTORY

- 2026-09-03 DLSS SR：`SuperSampling.Available=0` 的根因是 NGX DLL 搜索路径；改为规范绝对路径后 `Available=1`，1080p→4K Create + 30 Evaluate + resize 通过。不是靠更新驱动解决。
- 2026-09-02 D3D12 debug layer：用户已安装 Graphics Tools。
