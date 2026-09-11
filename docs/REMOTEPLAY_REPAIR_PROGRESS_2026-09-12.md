# PS5 目标模式施工进度

基线计划提交325b9e9，tag checkpoint/ps5-full-repair-2026-09-12。用户指定《羊蹄山之魂》实测。目标仍active，未发布。

## 节点一实现

- AppShell timer独立编号1/2/3：遥测、动画、手柄。endTransition不再关闭手柄；SetTimer失败记录。
- RemotePlaySessionSource真实AU/decoded计数及一秒速率窗口，UI分别显示接收/解码，窗口未满显示采样中。decoded mailbox覆盖与入队丢弃分开。
- SDL gyro(rad/s)/accel(g)、双指ID/位置/抬指；Chiaki orientation tracker用于姿态，finite/时间断点检查。当前仍为8ms状态采样，极短触点边沿、传感器时间序列与校准待继续收口。
- enable_dualsense启用，rumble/trigger/motion-reset/raw haptics回调经有界feedback到输入owner；SDL普通震动、DualSense扳机report和四通道触觉分别输出。3kHz双通道触觉重排为四通道（前两声道静音），SDL转换设备频率；仅唯一手柄和唯一四通道Wireless Controller/DualSense端点自动路由，缓存100ms。多设备映射、中文端点名和蓝牙能力仍待完善。
- PS5面板加入仅观看选项，重连生效；不读取电脑设备，不发有效输入，保留协议。主机账号与本地手柄能否共存仍待实机验证。
- PS5独立displayReferred709意图：保留码流BT709标记，使用理想黑位BT1886 EOTF进linear，再走原sRGB呈现。其他来源默认保持旧行为。依据libplacebo pl_transfer_from_av将BT709映射BT1886，明确EOTF != OETF：https://raw.githubusercontent.com/haasn/libplacebo/master/src/include/libplacebo/utils/libav_internal.h 。GPU数值通过不证明用户发灰已全部解决。

## 实际证据

所有日志在项目logs目录，单次测试不超过300秒。

- build-product.cmd：ps5-ui-input-build.log、ps5-full-input-build.log、ps5-controller-test-build.log通过。feedback首建漏ControllerFeedback前置声明失败；补齐后ps5-feedback-build-retry.log通过。
- scripts/remoteplay/test-ui.ps1：真实20次模式动画完成及20次全屏切换、面板两次开关及本地错误配对拒绝；ps5-controller-final-ui/。没有以空白UI测试冒充真实串流。
- run-short-test.ps1，30秒，boundary --virtual：真实SDL虚拟设备双指生命周期、sensor单位、失焦归零/rumble释放、feedback容量通过；ps5-controller-virtual.stdout.log。没有把虚拟输入发送给PS5。
- boundary默认及H264/H265 source回归通过：ps5-controller-boundary/source.stdout.log。
- native CTest69/69、0.48秒：ps5-controller-ctest.log。
- Yuy2ColorTests --remote-yuv：真实D3D12和Presenter读回，Limited/Full乘BT601/709灰阶/色块，最大误差1个8bit码值，present误差0，4/4通过；ps5-color-gpu2.stdout.log。原YUY2回归通过：ps5-existing-color-gpu.stdout.log。读回只在测试中。
- delivery通过47.45秒：logs/delivery/fb3cd22c85c9418ca7c2a37bb43e8e27/result.json，包含真实NR/NVOF/播放/导出。
- exe SHA256：750548CD479ADD7346E8D4BA683A579F8A843352F0545B48BDE23A618572961E。此后仅颜色测试夹具增加色块，产品exe未变。
- RemotePlay OFF增量23步通过：ps5-controller-off-build.log；命令脚本out/remoteplay/audit-20260911/build-off-check.cmd。

## 当前仍要完成

真实游戏反馈、仅观看主机直连、USB/蓝牙拔插与完整效果路由、gyro静止偏移校准、触摸短边沿与传感器事件采样、设备能力显示、同源视觉对照、暂停与网络恢复。不能声称全部手柄功能实机通过。用户已收到测试请求，需打开新测试版供测试；没有发布。

UI已通过但完整控制器仍需上述收口。后续修改不要基于本页把未验收项误标完成。外部SDK/runtime不入Git。
