# HDR 全增强与多声道施工记录

2026-09-14，基线 edabd3c，分支 codex/hdr-multichannel；回退点 checkpoint/pre-hdr-multichannel-20260914。用户授权实施，PS5 真实多声道不纳入本轮，未授权新发布。

## A：HDR 核心原型（已通过首轮 GPU 数值测试）

保留线性 BT.709/scRGB HDR 基底，NR 处理固定 203nit 映射副本后合成有界变化；近黑/压缩高光衰减，不从 SDR 逆造原始高光。Video SR 同样保留基底并比对同采样坐标代理。DLSS FG 颜色标志与 RGB10/PQ 资源同时切换；XeSS 使用 HDR10 交换链；原帧/生成帧/对比参考格式统一。默认效果全关不变，运行组件未替换。

命令：`cmd /c out\release-1.1.0-build.cmd`（125 步及测试增量构建成功），`out/remoteplay/product-repair/veyra_hdr_enhancement_tests.exe 0/2/4/5` 分别执行。每项不到 30 秒。

- shader 零残差 1024 像素完全一致，包含黑位、近黑、203/1000/4000nit 与广色域负分量。
- mode2：NR+DLSS SR+DLSS FG，NR/SR各20次，18张有效生成，1000nit参考色块998.932nit。
- mode4：NR先行+Video SR+DLSS FG，同上20/20/18与998.932nit。
- mode5：NR+DLSS SR+XeSS FG，20/20/16，RGB10 HDR10 初始化成功，参考色块998.932nit。
- 证据：logs/hdr-multichannel/shader.txt、dlss-hdr.txt、vsr-nrfirst-hdr.txt、xess-hdr.txt。NGX Create/Release success 0x1，SEH0；XeSS Init0。
- 初次构建脚本用了 cmd 不识别的正斜杠路径，未构建即失败；改反斜杠成功，日志 out/hdr-build.log/out/hdr-build2.log。

以上是合成输入与数值/接口测试，不是 HDR 实屏画质验收或所有 NR 版本组合验收。无 PS5/采集实卡新测试。未发布。

## 进行中

5.1 音频链改造尚未验收；后续补齐文件/采集颜色与手动覆盖、HLG、HDR 导出/截图、状态信息、生命周期和回归。当前不得对外宣传全部功能完成。
