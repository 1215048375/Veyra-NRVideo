# 继续修复实施记录

状态：目标模式执行中。范围按 [继续修复方案](CONTINUATION_REPAIR_PLAN_2026-09-10.md)，未完成全计划。起始代码 `be00e27`，保留上一轮方案文档改动。不启用旧Loop。

## 2026-09-11 续接实证（优先于下面历史进度）

### 最新续接：设置隔离、生成帧关系和FRUC admission

前序源码/文档本地存档 `6316376`，无push/发布。本段新增内容优先于下方同日旧快照，目标仍active。

- 设置：`EnhancementSettings::sameVideoConfiguration`将revision仅用于GPU配置隔离；纯音频请求保留revision，主循环与大图路径直接更新音频/配置状态，不排空视频、不重新处理缓存画面。重复通知无操作。视频事务排空后重读最新desired，减少过时重建；失败回滚保留随后独立提交的音频修改。设置页按完整desired比较，避免同revision音频变动不刷新。没有宣称完整单GPU所有者或所有设置均无等待。
- 生成帧：`FrameLineageTracker`记录相邻已接受源帧identity/PTS/真实到达时刻，仅同epoch/revision且A的PTS匹配时建立关系；reset、缺少对应A、缓存预览不给推算值。`FrameFlowWindow`与状态区加入A/B到达间隔、生成帧Present返回距A和B的时间，分别有一秒均值/P95/样本数；不混入真实帧总延迟。文件模式标软件取帧，实卡才使用callback锚点；不是光子延迟或免费lookahead。verbose日志`frame-lineage`保留A/B源id与端点。
- FRUC admission首次新回归失败 `continuation-fruc-admission-pixels4`：跳过i=1后，i=2复用同source parity，CUDA数组覆盖SDK仍引用的前帧，之后亚帧GT错位（4X前两张误差约2.5/2.0，重复基准约0.85/1.36）。`FrucWorker`改为按成功SDK调用轮转独立`inputSlot`，源共享纹理仍按source parity读取。候选`continuation-fruc-call-parity2/3/4`均通过六epoch、每epoch两次skip/reseed/像素恢复，3.06/3.93/4.54秒；不销毁worker、不增加CPU fence等待或像素回读。
- 实时采集FRUC重新启用与DLSS相同的整对提交前admission；XeSS仍由SDK呈现内部控制。正常`continuation-fruc-admission-live`24项PASS6.52秒，known-pan15素材SHA `EB05452E25325D9882DDD9E975051A010459FDD2432305F61E3820D85689D05B`，含有效补帧/生成帧关系/音频设置/暂停/2X4X/关闭。
- 同源过载A/B：`continuation-fruc-admission-baseline`16.74秒与`continuation-fruc-admission-overload`7.70秒，均150源帧观察点、原生4K NR+VSR4+FRUC4，测试EXE `8FB7B4566A4E7C86336375952A12DE904E31C396FA0C5D07A2E097C5F5C5797B`，worker `EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。基线Evaluate450/skip0/源处理12fps；筛选Evaluate0/skip450/源处理33fps。有效生成呈现两边0，基线expired也0，不能说450张有效生成帧都过期；只证明这组无收益计算被省掉。软件源帧年龄P95 90.68→60.13ms，非实卡、无GPU占用百分比证明、非FRUC内核加速。此前原生4K FRUC本体耗时边界仍存在。
- 验证：`continuation-lineage-contract`66项PASS0.082秒；`continuation-lineage-half`29项PASS8.84秒；`continuation-lineage-fruc`24项PASS6.57秒；设置隔离前置CPU58/half28/FRUC23也通过，后续用上述新版本覆盖。UI正常 `ui-fg-1789059745509958500` 与故障回滚 `ui-fg-1789059468450574400`均PASS，自有窗口状态截图已查看，没有完整DPI/动画验收。所有名称对应`logs/scheduler-repair-20260910/`下result/stdout/stderr，controller日志路径见各result.args。
- 实际构建命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，最新`logs/continuation-repair-20260910/build-fruc-call-parity.log` exit0。短测统一`scripts/acceptance/scheduler-short-test.ps1 -Name <以上名称> -Exe out/build/x64-release/<test>.exe -TestArgs <result.args>`；FRUC skip为`@('2|3|4','logs/video-sdk-trial-20260909/pan.nv12','--skip-pixels')`；overload为live test的`--fruc-overload[-baseline]`。
- 最终联合命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <root>`，`logs/delivery/f024e12327b5429593e762943abb2eca/result.json` 23项PASS；EXE `D3E8736F9D0ADD088B0CEA5FE8C979B99820745467CA7743654A93687EF24823`。日志`delivery-fruc-admission.log`。此前lineage版本delivery `2e071ef905724217bf652d135552607a`23项42.49秒PASS为前序版本。实际RTX runtime已执行，NR固定SHA/签名Valid未变；没有打开实体采集卡。
- AMD隔离探针新证据：显式绝对路径LoadLibraryEx成功、导出coreVersion721，ID3D12SDKConfiguration1获取成功，但CreateDeviceFactory721仍`0x887E0003`；`continuation-amd-factory-capability` exit1，不是能力失败结论，更不是成功执行网络。初次链接缺CLSID定义失败，包含initguid后构建成功。微软官方getting-started列举该码可由SDK版本/SDKLayers不匹配触发；尚未拿到debug输出定根因。Windows未改开发者模式，未换主程序Agility。缺权重/RDNA4/preview编译链和provider问题仍在。

下一条任务：音频缓慢漂移、全局PCM容量与端点恢复；然后继续完整GPU所有者调度/逐帧GPU计时覆盖及AMD依赖。现有GPU图和呈现仍双线程互斥；没有以这批通过声称全目标完成。所有测试单次<300秒，源码/runtime隔离，未push/发布。

整合检查补充：`build-fruc-ui-integrated.log`构建通过；`continuation-fruc-batch-final2/3`最新CUDA候选2X/3X通过（2.84/3.33秒），`continuation-fruc-batch-live`真实controller19项通过6.20秒，正常UI `ui-fg-1789058417812768300`通过。最新联合delivery `ad0a4e4680d748d0b97de6fe0e665c53` 23项PASS、42.35秒，EXE `24216560E25D6C84D0BF3656E3AC70BFCD8917F2BED52AE82AB9B4BF2B168778`，worker `DE38EFBE302FB3664EEC167A7FC3105154B877A4BBDFC8980DCC7C168B0C1678`。UI长错误行换行已编译，完整DPI尚未验收。

AMD新增依赖证据：官方NuGet `Microsoft.Direct3D.D3D12/1.721.3-preview`，包SHA `0131BCE1E4BACE3FC08C03018C29A09EDE2570B263721C6449B0EC75762AB22D`；D3D12Core签名Valid/Microsoft，LICENSE.txt和LICENSE-CODE.txt原样保留。仅置`third_party_local/microsoft`与ignored隔离探针，未进主程序/Release/Git。`continuation-amd-linalg-capability`实际返回0x887E0003（SDK加载失败），没有成功创建设备或查询linalg；探针进程exit0只是完成错误记录，**不是能力PASS**。未改系统开发者模式。另核实上游`NativeActualNetwork70::RecordUnsubmitted`是曾触发device hung的诊断路径，接入必须保留分段提交；当前计划中单列表假设不可直接落地。

- R1：GPU完成检查现在在呈现deadline等待及生产者两批反压期间非阻塞执行。观察者与呈现任务共享同一个FrameOutputs，不重复解析SDK状态；去掉旧processingRate与会话平均FPS赋值。Drop仍只重置历史账本、保留同revision的完成速率。尚非完整单GPU所有者状态机。
- 阶段统计最近一秒均值/P95/样本数，GPU timestamp去重，250ms刷新，有界8192条与溢出计数；P95改为nearest-rank。Submit使用processDone-processStart，不包含后续统计开销。53项合同通过 `continuation-stage-p95-watch`；worker反压期间完成观察测试通过 `continuation-worker-progress`。
- 实际RTX共享controller回放 `continuation-watch-half` 23项通过，8.51秒，含30/60处理、NR/2X/4X、暂停/设置/退出；测试EXE `EBD6CF1E4C101B36D0DFA042B4994A5EC580EE29F26346A58A7613F632524C04`。这是文件模拟采集，未打开实体采集卡。
- 文件过载保持音频主时钟/追帧恢复已通过 `continuation-file-overload-fixed`（5项、7.06秒，最大观察领先113.33ms）。修复暂停seek卡在目标前关键帧，以及seekPreviewPending在EOS不清理的问题。文件音频750ms完整PCM/采样级seek/真实WASAPI测试通过，详见下文；不承诺GPU不足时仍无停顿。
- 采集音频增加初始化/运行错误状态；不支持PCM时保留视频并显示原因。空Chunk.discontinuity原未初始化，已修正；停止/重启清空旧队列、时钟和视频锚点，持续100ms以上无输入且欠载时重锚。首版任何欠载即重置导致手动/关闭模式频繁重置，失败 `continuation-capture-audio-recovery` 保留，已修正。
- 音频恢复回归 `continuation-capture-audio-recovery2` 通过，11.05秒：自动80/160ms、手动100ms、关闭、负偏移钳制、350ms断流、停止/重启；平均软件偏差/预期偏差误差约2.7至12.8ms，未溢出。测试EXE `6FA43505594A63CED540E259DE5D65162A8A8877DC70330D718F4E4CAFA0C11B`。合成PTS+真实静音WASAPI，不是实卡/物理扫描延迟验收；微量漂移重采样和平滑仍待做。
- 原生PCM pin合同 `continuation-native-pcm` 已通过；预设schema7音频设置roundtrip `continuation-presets-audio7` 已通过。
- **FRUC重置像素错误已有通过候选并进入当前源码**：原DirectX11Resource交给FRUC内部互操作的路径撤换为应用拥有的CUDA arrays。D3D12共享纹理经D3D11自有纹理桥接，CUDA graphics map/unmap明确所有权转换，GPU fence向父进程传递完成。直接注册D3D12共享纹理失败CUDA400，记录 `continuation-fruc-cuda-map`；使用D3D11桥后正确。未加入cuCtxSynchronize、CPU像素回读或显式逐帧CPU fence等待；CUDA SDK map/unmap本身可能阻塞，不能称完全异步。
- 新FRUC常规reset只通知下个真实帧bSkipWarp更新历史，不销毁SDK/上下文/资源、不重启worker、不为reset调用ring.drainQueue；失败才回退重启自己拥有的worker。2X/3X/4X六次重置、正向/反向PTS、颜色反转与实际亚帧GT均通过 `continuation-fruc-default2/3/4`，2.70/3.14/3.64秒。初版正式候选worker SHA `5533EBA3ED6E6E925CB53412761375A66C635B9AF17158CA309CC0DA9FA39906`。
- 之后同一CUDA context/整批map-unmap候选4X像素通过 `continuation-fruc-batch-map4`，worker SHA `ECD33BDB87C4C3F38D9992DDBD63A494074E38795F9E703C0EE1B3384E95D6A1`，编译日志 `build-fruc-batch-map.log`。最新2X/3X和联合回归待补；源码未存档提交。
- FRUC实际controller已知平移回放 `continuation-fruc-cuda-known-live` 19项通过、7.11秒。另一个pan15素材的NR后FRUC报告repeated、没有有效补帧，`continuation-fruc-cuda-live`失败保留，不能篡改成有效输出；正确known-pan15素材具有独立SHA，不混淆两份素材。
- **FRUC性能仍有限**：新增同一图120帧、最多两批、每30帧reset、无像素回读的吞吐用例。初版CUDA4X原生4K实际完成120真实+348生成，FG区间中位70.58ms、图提交CPU中位73.49ms、合成图填充+整图源吞吐11.18fps，`continuation-fruc-native4k-throughput`；1080p最终批量map版本FG中位26.05ms、CPU26.61ms、源吞吐33.88fps，`continuation-fruc-batch-throughput`。计时包含互操作/排队，不是FRUC纯网络内核时长。不能把正确性通过说成原生4K实时性能已修好，也没有精确HEAD旧worker同源A/B性能结论。
- 本轮此前联合delivery `aba35e073bfb41edb6507d5f2caae220` 23项PASS、46.80秒（audio之后、最新完成观察/FRUC之前），只证明当时版本。最新联合gate待运行，不能沿用旧结果。

下一步：FRUC候选生命周期/2X3X/集成回归、状态区长文案与DPI、单所有者调度，以及AMD NR依赖/provider。本机仍缺AMD网络权重、SM6.10预览编译环境与目标RDNA4硬件，未实现AMD NR；不可用说明或光流不算实现。未push/发布。所有测试单次少于300秒。

## 已推进

- R0部分：补帧页标题、后端、倍率、光流和内容节奏按明确行位置排列，移除后追加到标题位置的ID208。NR独立实现切换仍待接入。
- XeSS初始化失败的设置事务回滚旧后端，不再把普通呈现fallback当XeSS切换成功；启动时的基础播放fallback保留，新增持久后端警告和实际活动状态。选择器上方显示applied后端与等待有效补帧/运行。
- R1首版：同一FrameFlowWindow记录源帧完成、有效生成完成、总产出、Present提交和XeSS SDK送呈现速率；按batch去重、容量8192、停流衰减。保留`s.fps`兼容字段为源处理，底栏改用总产出或明确标注的XeSS SDK提交。
- 软件总延迟使用真实源帧进入本进程至Present返回端点，最近一秒均值/P95；文件为取帧开始至Present返回驻留，非扫描/光子延迟。生成帧年龄与完整逐阶段聚合仍待补齐。
- 实时状态改为独立可滚动区域，主产出/总延迟固定，其余处理/生成/呈现/输入、各阶段、尺寸和音频状态集中显示。布局和像素审查仍在进行。

## 实际验证

运行时：`runtime_local/nvidia/nvngx_dlssnr.dll` SHA256仍为`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，签名Valid。未替换DLL。

`python scripts/acceptance/ui-fg-backends.py`：旧EXE实测失败，标题 `[994,294,1266,324]` 与后端 `[994,290,1266,318]` 重叠，证据 `logs/continuation-repair-20260910/ui-fg-1789052091042365500/result.json`。初版脚本在专业动画前取状态误报隐藏，已改为等待专业模式。

Release构建通过：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，日志 `logs/continuation-repair-20260910/build-r0*.log`、`build-r1.log`。已有FFmpeg转换警告与probe未使用变量警告，未新增编译错误。

R0正常UI切换通过：`ui-fg-1789052625652703800`，FRUC -> XeSS -> DLSS -> off，两种窗口尺寸、真实自绘popup点击提交、applied版本一致，9.73秒。对应EXE SHA `74ECED96D403B9CEB63E00836B981E81210A02B16087BF8B85B25641E69BCA44`。XeSS为实际SDK初始化与呈现，不是AMD实卡或扫描验收。

`python scripts/acceptance/ui-fg-backends.py --reject-xess`通过：`ui-fg-1789052653905109600`；故障注入在加载前返回失败，FRUC保留、applied日志不出现XeSS，再切DLSS/关闭成功。没有破坏/重命名本机DLL。测试观察应用控件revision，退出后复核日志；之前只等缓冲日志曾超时，但退出日志确认DLSS已应用，已修测试判定。

`scripts/acceptance/scheduler-short-test.ps1 -Name continuation-r1-contract -Exe out/build/x64-release/veyra_repair_contract_tests.exe`通过49项：60真实+40有效生成=100产出、90呈现、重复完成不计、XeSS单列、延迟样本衰减、1000fps支持；0.10秒，日志在`logs/scheduler-repair-20260910/continuation-r1-contract.*`。

`scripts/acceptance/scheduler-short-test.ps1 -Name continuation-r1-half -Exe out/build/x64-release/veyra_live_presentation_tests.exe -TestArgs @('loop/local/fixed_clips/test_av_1080p.mp4','logs/continuation-repair-20260910/live-half','--half-rate')`通过23项，10.98秒。GPU真实完成30/60切换、NR/2X/4X、暂停/重配/退出、有界租约与epoch。文件模拟采集，未占实体采集卡。

R1首版UI切换通过：`ui-fg-1789053218053203700/result.json`，EXE SHA `C4571EF78ED0F8883E61A2CD7B6B38D49C7719E4EB09B80502492F461817E6BC`。该次截屏被其它前台窗口遮住，不能作为状态区视觉通过证据；后续改用本应用窗口独立绘制截取，不操作其它软件。

## 当前未完成

### 续接进展（2026-09-10，仍在施工）

- R1补正：Drop只重置epoch账本，同revision的完成速率窗口保留，避免重载时永远“采样中”；新revision仍独立。51项CPU合同通过，`continuation-rates-epoch`，0.072秒。小窗口保留参数区域至少约200逻辑像素，状态明细滚动；`ui-fg-1789055596503007200`正常切换通过，含1040x540请求尺寸；不是完整100/150/200DPI验收。之前独立窗口截图发现状态区glassText绕过父DC clip，已跳过半露行，`ui-fg-1789053467482583100`复测通过。截图不能证明flip视频像素。
- 文件音频确认并修正：原`nextPtsMs_`在push前被赋为输入音频块结束PTS，错误偏移一个块；按输入PTS减swr延迟标记输出开头。处理decoder EAGAIN/EOF和resampler尾样本，seek按输出样本裁剪、清空swr历史，seekRequested改atomic、ring水位加锁。WASAPI先填真实PCM再Start，未启动/reset/clock失败返回NaN；requestSeek返回实际重锚PTS，不返回已被首缓冲消耗后的队头。初版paused-seek测试因此失败，修复后通过。
- `veyra_audio_timeline_tests`两种PCM输入（44.1/48kHz）均完整输出36000个48k样本/750ms，PTS误差约1.7e-12ms；采样级seek、设备暂停、seek重锚、无效clock测试通过。实际WASAPI输出设静音，`continuation-audio-timeline-fixed`，0.544秒，测试EXE `3658B889A2E2E19F6B8F2242BDA226C356CA67CA666664321195D4ACE2DD5481`。
- 采集音频施工：新增共享`AudioPcmSource`接口，`CaptureAudioSession`复用AudioRenderer；原NativeCaptureSink扩展明确PCM类型的终端pin。DirectShow音频不再RenderStream自动直通，协商PCM后写有界队列，图显式SystemClock，WASAPI线程转换/播放。视频Present PTS/host映射估算自动0..250ms补偿；声音队列/偏差/重锚/溢出显示。新设置自动/手动/关闭，手动-250..250ms，负值实际钳制到0；预设schema7兼容旧版，roundtrip通过`continuation-presets-audio7`。当前策略仍需审查突变、过载、端点错误和真实设备；不能称实体采集音画已经修好。
- `veyra_capture_audio_tests`合成连续PCM与80ms视频延迟，真实WASAPI：平均绝对软件偏差12.795ms，89样本，补偿86.622ms、PCM队列58ms、0溢出/0欠载，`continuation-capture-audio80`，1.698秒。仅验证合成PTS，不打开实体采集卡，不测scanout。
- 文件过载新增共同缓冲：音频领先视频>80ms暂停音频主时钟，视频保留所有源帧推进，追到20ms内恢复；刚写入，尚未构建/验证，不当通过。
- FRUC新候选：worker双向D3D11自有纹理桥接（输入、输出均经D3D11复制）。首次缺NTHANDLE导致Register=3；补标志后API成功但reset像素仍旧输出（第二对偏移-12而应-4）。`continuation-fruc-owned{,2}`失败，全部候选已撤回，Worker与HEAD无逻辑diff。尚未修复FRUC reset，不引入cuCtxSynchronize产品等待。
- AMD固定仓库成功克隆到ignored `third_party_local/amd/nr-d3d12`，sparse checkout `src/shaders/scripts`，detached `628620a74f3fcd3b5081620bd86308f57fc210a3`。README确认完整71block、SM6.10 FP8 linalg、Agility721，权重不在仓库（作者约16GB含参考dump），本机尚无权重/预览编译器/目标RX9070XT。本轮还未接provider，不将AMD光流或选择器算作AMD NR完成。未修改或抽取本机NR二进制。

最新已构建UI EXE（音频设置后、文件过载补丁前）`BAE93B5DE106E8940815FDA017CFA9E93C599902EE8531817B469B72E8219792`，`build-audio-settings.log`。第一次采集音频构建因缺mmreg.h失败，修复后通过。所有失败日志保留，单次测试<300秒。未Git提交、未push、未发布。接着验证文件过载/采集突变与Native音频pin，再推进GPU完成观测/调度和AMD依赖。

R0 NR backend/provider、R1完整计时与状态布局验收、A1 AMD完整网络、R2 FRUC同步/完整GPU状态机、R3音画同步及R4集成回归均未完成。新指标仍由原presentation worker观测GPU-ready，尚未解决该观测时机受调度等待影响的问题。文档和测试通过不是全目标完成。

下一步：完成R1窗口自身截图与小尺寸布局审查、补充统计回归，再按方案推进AMD provider和FRUC/音频。代码在工作树，未push、打包或发布。
