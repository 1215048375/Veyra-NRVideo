# Veyra Human Inbox

本文件只放需要用户新授权、凭据、许可接受、外部硬件或公开发布决定的阻塞，不放普通 bug 和待办。

当前没有需要用户处理的阻塞。

（已解决）2026-09-03 Phase 4 SR：绝对路径修复后 SR 完全可用，无需修订 Playbook。

（已解决）2026-09-02 Phase 0 D3D12 debug layer：用户安装 Graphics Tools。

## 2026-09-03 · Phase 5 · NVOF SDK EULA（阻塞 Phase 5 完整门禁）

- 发现时间与 Phase：2026-09-03，Phase 5 Reviewer 首轮。
- 精确阻塞：Playbook §16 Phase 5 要求 NVOF 集成（current->previous 方向验证、10.5/32 转换、grid/densify、GuidanceFrame 共享），需要 NVIDIA Optical Flow SDK 5.0 头文件和 sample。SDK 下载需 NVIDIA Developer Program 登录并接受 EULA，Agent 无权代做。
- 已做的验证：System32 nvofapi64.dll v32.0.16.1656 签名 Valid；Zero Guidance 30/30 Evaluate 非黑非恒定（三方可复现）。
- 不受它影响的工作：Phase 5 的 Zero Guidance、DLL 探测、gate 基建、Phase 4 checkpoint。
- 用户最小问题：是否接受 NVIDIA Optical Flow SDK 5.0 的 EULA（https://developer.nvidia.com/opticalflow/download）并下载到 third_party_local/nvidia/Optical_Flow_SDK_5.0/？或者授权以 capability 查询 + Zero fallback 作为 Phase 5 完成标准（按 BACKLOG P5.5 分支）？

## 2026-09-03 · Phase 4 · DLSS SR capability 不可用（阻塞 Phase 4 gate）

- 发现时间与 Phase：2026-09-03，Phase 4（--sr-test 实测时发现）。
- 精确阻塞：NGX capability 查询返回 `SuperSampling.Available = 0`（驱动 616.56 / RTX 5070 / DLSS SDK 310.7 的 nvngx_dlss.dll）。SR `CreateFeature` 两次返回 `0xBAD0000B`（FAIL_UnableToInitializeFeature）。1:1 bypass 路径正常。
- 已做的验证：capability 查询 result=0x1（Success）且 params=non-null；960x540->1080p 和 720p->1440p 两种 upscale Create 均失败于同一 result；needsUpdatedDriver=0（驱动不是明确的过期原因）。
- 为什么不能安全自动决定：Phase 4 Reviewer 裁定 Playbook §16 Phase 4 无 capability 豁免条款（对比 Phase 5 明文允许 NVOF Zero fallback），SR upscale/subrect/resize 三项门槛在 capability 恢复前不可验证。这是硬件/驱动/SDK 匹配问题，Agent 无权升级驱动或换 SDK 版本。
- 不受它影响、已经继续执行的工作：Phase 4 的 SR bypass（1:1 跳过）、capability 探测、SDK hash 验证、gate 基建均为有效保留工作。
- 用户只需回答的最小问题（三选一）：
  (a) 升级 NVIDIA 驱动到最新版本后告知我重试；
  (b) 换用与当前驱动匹配的其他官方 DLSS SDK 版本（需要你提供版本号或同意我尝试最新版）；
  (c) 明示授权修订 Playbook §16 Phase 4，为 SR 增加 capability-aware 条款（仿 Phase 5 NVOF 写法："SR 不可用时以真实 capability 结果触发 bypass-only fallback"），我更新 Playbook 后重跑 gate+Reviewer。

新增条目必须包含：

- 发现时间与 Phase；
- 精确阻塞和真实错误；
- 已做的 3 个不同验证/尝试（如果适用）；
- 为什么不能安全自动决定；
- 不受它影响、已经继续执行的工作；
- 用户只需回答的最小问题。
