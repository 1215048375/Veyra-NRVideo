# Veyra Human Inbox

本文件只放需要用户新授权、凭据、许可接受、外部硬件或公开发布决定的阻塞，不放普通 bug 和待办。

当前没有需要用户处理的阻塞。

## 2026-09-02 · Phase 0 · D3D12 debug layer 未安装（影响 Phase 1，不阻塞 Phase 0）

- 发现时间与 Phase：2026-09-02，Phase 0（P0.6 实测时发现）。
- 精确阻塞：`D3D12GetDebugInterface` 返回 0x887A002D（DXGI_ERROR_SDK_COMPONENT_MISSING）——本机未安装 Windows "Graphics Tools" 可选功能，无法启用 D3D12 debug layer。
- 已做的验证：probe `--device-info --debug-layer` 实测两次，均 hr=0x887A002D；无 debug layer 时 device/queue/slot ring 全部正常，Phase 0 门禁不依赖 debug layer。
- 为什么不能安全自动决定：安装 Windows optional capability 是系统级变更，AGENTS.md/LOOP_ENGINE 禁止 Agent 修改系统设置。
- 不受它影响、已经继续执行的工作：Phase 0 全部任务正常推进。
- 用户只需回答的最小问题：是否同意在 Phase 1 开始前运行
  `DISM /Online /Add-Capability /CapabilityName:Tools.Graphics.DirectX~~~~0.0.1.0`
  （或 设置→应用→可选功能→添加"图形工具"）？Phase 1 的"D3D12 debug layer 无 resource-state error"门禁需要它。

新增条目必须包含：

- 发现时间与 Phase；
- 精确阻塞和真实错误；
- 已做的 3 个不同验证/尝试（如果适用）；
- 为什么不能安全自动决定；
- 不受它影响、已经继续执行的工作；
- 用户只需回答的最小问题。
