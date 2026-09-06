# Veyra 当前 Goal 入口

按 `docs/ACTIVE_DELIVERY_PLAN.md` F0→F6 接管完成本机软件。先读 AGENTS/README/Playbook/ProductSpec/竞品审查与当前 WORKLOG/STATE，再 preflight。保留固定 runtime 和用户未提交改动。已有真实共享 pipeline，修输入/证据/Guidance，接共享 controller、Win32 Player、WIC 图片、D3D12 NVENC 视频和 DirectShow Capture。

用户授权每次完整自动测试 <=300 秒，优先短测；不再跑旧30分钟矩阵。当前采集卡未连接，由用户亲自实卡验收，标 awaiting_user_capture_test；实现与视频验证继续，不假报硬件通过。允许跨旧阶段开展独立产品实现，验收状态不冒进。

Maker 唯一写入；每轮 JOURNAL 先记假设和窄测，改后记录真实退出/输出到 STATE/EVIDENCE/WORKLOG。最终只读独立 Reviewer 检查。禁止修改固定 runtime、下载其他泄露版本、关远程/更新驱动、上传/打包/发布。公网分发权未解决保持 distribution_blocked。Goal 完成指本次约定的本机软件交付，不表示公开授权。检测 STOP 立即停机；代码缺陷继续修，不把历史假统计当完成。
