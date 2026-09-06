# Veyra 本机使用

双击项目根目录 `Veyra.cmd`。实际程序在 `out/build/x64-release/veyra.exe`；暂时不要移动项目目录，编译时绑定了本机 runtime / shader 路径。这不是含专有 DLL 的安装包。

## 播放器

1. 点 **Open video / image**，或拖入 MP4/MKV/H.264/HEVC 视频。
2. **DLSS NR** 是实验 Feature 18；**FG 2X** 是补帧；**SR 4K** 是在 NR 前超分到 4K。
3. **Live 1080** 是用户同意的默认实时档：4K 视频仍按 4K 解码，内部处理到 1080p，最终按窗口/全屏尺寸显示。状态行写真实 input / processing，绝不是原生 4K NR。
4. 取消 **Live 1080** 使用原生分辨率。RTX 5070 本机实测原生 4K NR 单次约 22–23ms，因此它不能保证 4K60 实时；不要在这种负载下期待仅靠补帧修复音画落后。SR 4K 也走 4K NR 高成本路径。
5. 拖进度条可 seek；暂停时也可定位。Space 暂停、F11 全屏。更改增强开关会重新打开源文件；不是无缝动态切换。
6. **Recent** 打开最近文件。NR/SR/FG/Live 设置保存在 `runtime_local/veyra.ini`。
7. 同名 UTF-8 / UTF-16LE `.srt` 自动加载，显示在画面下方独立字幕区域，不进入 NR/FG。当前不解码内嵌字幕，也不烧录字幕到导出文件。

## 图片 / 视频导出

- 打开 PNG/JPEG 后点 **Save image**，保存增强后的 PNG 或 JPG。播放视频时也可保存当前源帧增强截图。WIC 处理 JPEG EXIF 方向。
- **Export video** 选择新 MP4 文件名，Yes 选择 HEVC、No 选择 H.264。使用当前 NR/SR/FG；**Live 1080 不影响视频导出**，4K 输入仍按原生 4K 处理。
- 视频使用真正 D3D12 NVENC：GPU 纹理/fence 直接给编码器；CPU 只接压缩码流，不走 raw 像素 pipe。
- 默认保留第一条可封装到 MP4 的音轨；不兼容的音频编码会报错拒绝导出，不偷偷丢音轨。多音轨选择、音频转码尚未提供。
- Stop 取消导出；`.partial` 保留以便诊断，未完成文件不会改名成成功 MP4。没有断点续编；重新导出请选择新文件名。
- 不覆盖已有文件。若上次有同名 `.partial`，换名字或自行确认删除它后再试。
- V1 输出为 CFR，FG 2X。场景切换不跨镜头插帧，空出的时间格使用明确记录的源帧 hold，而非声称它是生成帧。

## 采集卡：等待你实机测试

1. 接采集卡，点 **Capture**。依次选择真实视频设备、实际支持的 1080p/2160p 30/60 格式、HDMI 音频输入。
2. 音频不是自动猜选；不选就无音频。点 Start capture。
3. 先默认 Live 1080、NR 开、FG 关确认视频声音；再开 FG 2X。内部实时入口只保留最新一帧，丢帧会清除历史。
4. 分别检查 1080p60、4K30/60（以设备实际提供为准）、停止重开、拔线后的错误提示、重接后重新打开 Capture、音画差和按键体感延迟。
5. 补帧必须等待后一源帧到达，不是利用采集卡获得免费未来帧。HDMI 音频目前由 DirectShow 实时监听，尚未经过实卡端到端延迟校准。

## 真实边界

仅本机 Windows x64 / RTX / SDR，最高 3840×2160、偶数尺寸；HDR 会拒绝。Depth Anything provider 当前不可用，实际是 NVOF motion + cost 门控，不是游戏原生深度/运动向量。

短测不证明长时间稳定，也不证明画质优于 Magpie/ReShade。设备丢失会停止并报错，重新打开源来重建引擎；不是无缝恢复。公开分发权尚未解决，勿打包/上传实验 runtime 或 ReShade add-on。

错误先点 **Logs** 查看 `logs/veyra-app.log`。提交反馈只需日志与操作步骤，不要发送 runtime、SDK 或私人媒体。
