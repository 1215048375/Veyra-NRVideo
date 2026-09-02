# Veyra DLSS Video Player

当前状态：只有经过审查的产品/施工文档、无人值守 Loop Engine 和两个本地参考二进制；应用源码尚未建立，当前 Phase 是 0。

## 唯一入口

- 产品边界：`VEYRA_PRODUCT_SPEC_V1.md`
- 技术施工：`VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`
- Agent 规则：`AGENTS.md`
- 无人值守目标：`loop/GOAL_PROMPT.md`
- 当前状态：`loop/STATE.json`
- 实际记录：`docs/WORKLOG.md`

不要使用历史方案或另写一套路线。Agent 开工前先运行：

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight

## 二进制边界

- `nvngx_dlssnr.dll`：用户提供、NVIDIA 签名的本地实验 runtime；只复制进已忽略的 `runtime_local/`，不提交、不发布、不自动替换。
- `renodx-dlss5-1.addon64`：未签名 RenoDX/ReShade 二进制 add-on，不是配置文件；只作隔离参考，Veyra 不加载、不链接、不分发。

项目接受 Magpie 对“能调用”的可行性证明，不再重复主观画质研究；本项目仍必须验证自身的 API 参数、颜色 parity、资源状态、时序、延迟和稳定性。

V1 只有 Phase 0–7。采集卡、HDR、3X/4X、Depth Anything、FRUC、安装包和公开发布不在当前执行范围。
