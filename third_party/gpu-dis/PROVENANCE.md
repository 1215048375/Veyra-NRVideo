# 来源、版本与改动

这是 R4.2 离线原生引擎的干净文件快照；source-manifest.json 记录每个原始文件的 SHA256 和编译配方。它不含旧 Git 历史。两个目标共用一套必需的离线基础代码。

GPU DIS 参考/移植 OpenCV 5.0 系列 CPU DIS，包括 modules/video/src/dis_flow.cpp、variational_refinement.cpp 以及 DIS OpenCL/插值、梯度与灰度相关实现。来源：https://github.com/opencv/opencv 。项目根 Apache-2.0 与具体文件的 BSD 版权/免责条款并存；本项目保留这些原许可，不替上游重新署名。

移植改动包括 D3D12 资源/描述符/命令组织、HLSL shader、并行 patch 排程、精确浮点/整数语义选择、FFmpeg7.1 limited RGB24→OpenCV GRAY 输入适配、R4.2 RGBA 共享表面入口、每槽独立 binding、GPU fence/所有权检查以及集成 worker。源码中提到的 OpenCV/FFmpeg 行为是明确的来源或兼容性目标；不保证与任意其他版本或后端相同。

GPU Block 的当前估计器采用 17×17 整数候选搜索和 3×3 SAD，与严格 DIS 的 inverse search/VR 组织不同。两路线的开发均参考 CPU DIS，因此不能将实现差异描述成无上游来源的证明。上游归属和原许可同时适用于相关借用部分。

共享基础设施通过公开 API 使用 oneVPL、OpenVINO、OpenCL、XeSS/XeFG/XeLL 与 Windows 图形接口。第三方 SDK、模型、运行库和工具链不在这里发布，按原厂条款另行获取。这里没有 NVIDIA SDK 源码。

部分共享源文件沿用 probe 命名，这是历史文件组织；本导出已移除四处不用的独立诊断 main，保留正式 worker 必需的共享库/兼容路径。严格 DIS 正式入口与 Block 正式入口以 README/BUILDING 所述为准。source-manifest.json 同时保留整理前 original_sha256 和整理后 sha256，以记录来源与删除边界。

自有新增和修改部分经权利人授权采用 Apache-2.0，上游代码原有权利不变；未断言拥有第三方部分的独占版权。原许可副本见 licenses。
