# 本机交付状态 — 2026-09-07

状态：本机软件联合短测与独立只读复核均已通过，可交给用户使用和实卡验收。不是公开发行许可；实卡未接入。最终复验与范围见 REVIEW_F6.md。

最终构建EXE：`70DD23C44337004FC734FBAE8BB6139E185C52370AE97CCE6C3432A3C6F30DE1`；独立联合短测run `9be0614da5d642e394a35c71d80f6207`，40.31秒通过，包含新增4K图片保存与真实NVENC中途取消。下方31.59秒是初测历史，不是最终二进制身份。

## 已有实际软件

- `Veyra.cmd` → `out/build/x64-release/veyra.exe`，Win32 UI + 同一共享增强引擎。
- 本地 H.264/HEVC 播放，音频主时钟，暂停/定位/全屏/开关/最近文件，外部同名 SRT。
- WIC PNG/JPEG 图片输入和保存；D3D12 NVENC 原生 4K H.264/HEVC MP4 + 兼容音轨，FG 2X、取消/partial。
- DirectShow 视频设备/实际格式/HDMI 音频选择、容量1实时邮箱；没有实卡运行证据。

## 本轮实测

构建：`scripts/build.ps1 -Root . -Preset x64-release` exit0。

联合门禁：`scripts/gates/delivery.ps1 -Root .`，run `d28879b01b5c44dd86cad33d6f386d90`，31.59秒、16项通过。原始 JSON：`logs/delivery/d28879b01b5c44dd86cad33d6f386d90/result.json`。

- Native 1080：60 NR，真实 NVOF/非零 motion 接入，GBV 0错误，诊断 PNG 可见非黑测试图。
- Native 4K：12 NR，actual source/working/output =3840×2160。
- 4K输入实时档 FG2X：264源帧、263生成帧，短测 absolute lateness P95=14.88ms；不是端到端光子延迟。
- 暂停中 seek → resume → JPEG 保存，exit0。
- PNG 经过实际应用打开/增强/显示，exit0。
- H264 与 HEVC 各12源帧→23输出帧，原生4K；ffprobe `-count_frames`逐帧解码、均保留音轨。
- 本次 EXE SHA256：`900DD752D749036EC482B3B9557AD23D12C60D58A58E37B44C1C960D5DC1B747`。

## 重要修复与否定结论

修了 NV12 错误上传、chroma 尺寸、uint根常量按float传递、motion统计步长；将 NVOF 放到 NR 之前并实际绑定；加入场景切换清历史、命令时间戳、正确退出；修音频环与暂停定位，明确48k双声道浮点格式由WASAPI转换到设备格式。

原生4K GPU分段实测 `logs/f6-profile-split.*`：NR-only约22–23ms、parity decode/output约0.28ms。以前“decode shader24ms”根因结论错误。用户已接受默认实时档/原生4K可选；原生4K60不能凭改队列保证，导出不受实时预算限制。

## 未被证明 / 明确限制

实卡视频音频与按键体感延迟、长时稳定、全面seek/resize/device-loss压力、多音轨/音频转码、内嵌字幕、depth provider、VFR原样时间线、HDR、安装包及专有分发权均未证明或不提供。部分完整产品体验仍是显式错误后手动重开，不宣称无缝恢复。使用范围与操作见 USER_GUIDE。

下一步唯一任务：用户接实卡按USER_GUIDE验收并反馈。软件本机交付停止自动扩展，不重跑历史30分钟矩阵，不自动发布。
