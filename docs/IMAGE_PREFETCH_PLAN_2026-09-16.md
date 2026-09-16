# 图片目录预解码队列（2026-09-16）

用户要求批量排队解码10～20张，实施默认10张滑动窗口：当前图片优先，其余按距离交替准备前后图片；后台单线程，缓存解码后的原始RGBA，增强仍通过现有共享处理图执行。未承诺消除GPU初始化及NR/SR耗时，不新增增强/播放循环。

缓存最多10张、RGBA缓存及后台待加入缓冲合计上限512MiB；当前显示图、WIC内部缓冲和GPU资源另计。超过剩余预算的图片不预加载，实际打开仍保留原有大图/分块路径。窗口变化取消排队任务，正在进行的WIC调用完成后丢弃过期结果；不声称可中断WIC。命中以共享只读对象传递，检查文件大小/修改时间，修改后的图片不能返回旧缓存。离开目录清空缓存；持有中的显示图不被释放。原EXIF方向校正和解码器复用。

新增 ImageDecodeCache.h，EngineController 接收预加载窗口并读取命中对象；ImageExportSink 的 loadImage 增加默认不限制的可选RGBA预算，在分配像素前检查。AppShell只提交目录窗口。没有SDK/runtime变更，没有push或发布。

验收证据：scripts/acceptance/playlist.ps1 -Root . 的99项队列/目录/缓存检查、865 HWND检查、UiContractTests与普通/REMOTEPLAY编译通过（out/ci-dependencies/image-prefetch-tests.log）。实际15张PNG目录测试确认后台解码10张且下一张在播放器命中（image-prefetch-smoke.log，详细image-prefetch-app-1789559225192683400.log）；增强全关，NGX Create/Evaluate未执行。实际小图验证不是大图性能基准，不承诺翻图加速倍数。

完整编译使用 VS DevShell 与本机依赖，命令 cmake --build out/ci-full --target veyra --parallel 4。收尾构建日志见WORKLOG。下一项唯一验收：用户实际大图目录的翻页延迟与内存体验；若耗时主要在增强初始化，再单独优化GPU资源复用。
