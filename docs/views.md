# 公开视图与保留式软件绘制

当前实现正在接入 P2。Windows/musl Release 开发构建已验证六项根测试；最终两配置/双平台 SDK 验证尚未完成。HTML/CSS/DOM/JS、文字编辑、AX、GPU 与媒体仍按 PLAN 实施，矩形绘制接口不是网页内核。

## 所有权与宿主驱动

`xl_view_create` 支持独立窗口、借用原生窗口和离屏视图。全部视图调用属于 Runtime 的 UI 线程；原生窗口还要求在宿主主线程建立和驱动。第一个原生视图确定 SDK 视频线程，其他线程的 Runtime 可以使用离屏视图，但不能接管视频线程。

Runtime 必须比视图长寿，存在视图时销毁 Runtime 返回 `XL_BUSY`。借用视图只销毁 SDL 包装并恢复窗口过程，原生窗口继续由宿主拥有；宿主应先销毁视图，再销毁窗口。平台调用期间的同线程重入创建/销毁会返回 busy。

`xl_runtime_pump_events` 默认只收集私有 SDL 队列中的窗口维护事件。只有显式传入 `XL_PUMP_NATIVE_EVENTS` 才调用平台事件泵；SDK 不创建消息循环线程。Windows 的系统泵可能派发本线程其他宿主窗口消息，宿主应明确选择何时调用。键盘、鼠标、文字、IME 及完整网页事件投递仍待接入。

SDL 由锁定源码构建为私有静态实现，其动态 API 环境替换在带标记的源码适配层关闭。SDK 不借用宿主另一份 SDL 的全局状态。静态归档中的 SDL、Skia、ICU 与图形依赖私有 C/C++ 符号统一改名，保留标准 C++ ABI/RTTI；根测试已加入宿主同名 Skia/SDL 定义，移动后四种消费与包审查继续逐配置验证。

## 绘制、失效与读回

`xl_view_set_rects` 替换保留的矩形绘制列表。坐标属于当前 drawable 像素，是底层绘制入口；后续 DOM/CSS 管线负责 CSS 单位、DPI、样式和布局到绘制快照的转换。矩形以给定顺序、ARGB 颜色和 SrcOver 混合绘制。

更新计算变化项新旧范围，扩展抗锯齿边界，在持久 Skia surface 中裁剪并重绘。曝光重用保留像素，不重复栅格化；没有失效或呈现请求时停止重复绘制/提交。原生提交在普通局部变化时转换并上传受影响区域，曝光和失败重试使用完整保留帧。

`xl_view_copy_rgba` 复制最后已绘制帧，格式为 RGBA8、直通 alpha；不自动执行新绘制。首次绘制及尺寸重建后尚无帧时返回 busy，缓冲不足保持输出不变，行填充不写入。绘制计数、提交计数、栅格化/上传像素和跳过计数可以用于验收静止和增量路径。

独立窗口尺寸参数采用平台窗口单位，像素几何以 stats 返回的 drawable 尺寸为准。借用窗口由宿主布局和缩放；SDK resize 返回 unsupported。离屏 resize 直接指定像素尺寸。完整 DPI/多屏切换、GPU 丢失恢复和混排层叠继续单独验证。

## 当前证据与后续范围

`tests/views/view_tests.cpp` 比较多次增量更新与新建完整绘制的像素，检查静止复用、无关 ICU 不解压、线程/生命周期、缓冲和 ABI 边界。`tests/views/native_tests.cpp` 从 Win32/X11 读取真实窗口像素，验证局部提交、曝光复用、借用原生子窗口及销毁后宿主仍有效。当前阶段不是 Win7/UOS 实机、现代网页或完整输入/无障碍验收。

Debug Skia 需要其 DEPS 指定的 SPIRV-Tools、SPIRV-Headers 和 Expat。来源和完整许可位于 `third_party/skia-sources.lock.json`，不通过关闭验证器规避依赖。安装收集 GN 产出的递归静态归档闭包，包含辅助依赖；Windows FreeType 归档中的原始 `.res` 在符号处理时原样保留，MSVC 与 GNU 长名称表均有回归。
