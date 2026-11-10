# Gecko 与 Chromium 差异统计（2026-11-04）

调查日期采用 Asia/Shanghai。本文同时交付工程结构对照和 MDN Browser Compatibility Data（BCD）的全量节点统计。**全量仅指遍历固定 BCD 快照的全部记录；不表示穷尽两套代码的所有实现、缺陷、像素、时序或性能差异。**

这是资料调查与离线数据分析，没有运行最新 Firefox/Chromium 的网页、性能、WPT、旧系统或 SDK 验收。已有 XLWebEngineUI 运行结果不用于推断这两个浏览器的表现。

**版本边界**

Gecko 是引擎；Chromium 是包含 Blink、V8、网络、进程管理等的完整项目。较对等的比较是 Gecko 平台与 Chromium Content 平台，以及 Gecko 布局实现与 Blink。Firefox、Google Chrome、CEF、GeckoView 的产品能力和默认设置应另看。[S1–S6]

| 渠道/平台 | Gecko / Firefox | Chromium / Chrome 对应版本 | 证据与用途 |
| --- | --- | --- | --- |
| 最新桌面稳定版 | Firefox 159.0.1，2026-11-03 发布；Gecko 主版本 159 | Windows/Mac：157.0.8103.38/.39；Linux：157.0.8103.38，2026-11-03 发布 | Mozilla 版本接口、发布说明；ChromiumDash 和 Chrome Releases [S1–S3] |
| Windows 最新稳定构建记录 | 同上 | ChromiumDash 返回 157.0.8103.39；Chromium 提交 `9a31f0acec1854d44e49a73fbf8017598151a583` | 固定此次 Chromium 稳定分支身份 [S2] |
| 当前开发主线 | `config/milestone.txt` 为 161.0a1 | `chrome/VERSION` 为 159.0.8142.0 | 只作开发主线标识；浮动 main 文件会继续变化 [S4–S5] |
| Mozilla Beta / Developer Edition | 160.0b4 | 本文不拿 Beta 替代稳定版 | Mozilla 官方版本接口 [S1] |
| Mozilla ESR | 153.6.0esr；接口另外列出 115.44.0esr | Chrome Extended Stable 为另一条渠道，不能按相同数字对应 | 不纳入“最新稳定引擎”主统计 [S1] |
| Android 主统计 | Firefox Android 159 的 BCD 列 | Chrome Android 157 的 BCD 列 | Android Chrome 157.0.8103.38 已在 2026-11-03 发布；Firefox Android 此处是兼容性版本口径，不声称补丁号与桌面相同 [S3,S7] |

Chromium 没有适用于所有发行商的唯一“最新安装包”。Chrome 官方稳定版本用于定位其 Chromium 分支；不同 Chromium 自编译、Linux 发行版、CEF、Electron、WebView 的补丁、GN 开关、编解码器及服务集成可能不同。BCD 的 `chrome` 列是本文的兼容性代理，不是对任何 Chromium 构建的逐项保证。[S2,S3,S7,S21]

**全量统计的方法和限制**

固定数据为 `@mdn/browser-compat-data` 8.1.8，数据时间 `2026-11-02T14:19:59.386Z`，上游提交 `29768a4caa9a88722ef6ba4bad772eb4c3667218`，CC0-1.0，未修改。下载的 npm 包通过其 SHA-512 integrity 校验，并另记 SHA-256：[S7]

```text
tarball SHA-256: 27614666b724fffe9ce738cd0582c7023237758bd403c193f5679fa69ff946a2
data.json SHA-256: 6d9dc7ff753812a2dcf99e93a9070bdbba11f7f3653bb531bc610c6eb9d0331a
```

BCD 快照的浏览器渠道状态仍把 Chrome 157 标成 Beta，本文使用经官方发布确认的稳定版 157，**不从该旧标签推断当前渠道**。快照晚于首次 Firefox 159 发布，早于本次两个稳定补丁发布；没有自行假设补丁增加新 Web 功能。

统计逐一遍历全部 `__compat` 节点：接口、接口成员、参数、CSS 属性及取值、HTML 属性、扩展 API 等都可以各占一条。因此父接口和成员会同时计数，条数不能直接当成独立标准数量、功能价值、标准通过率或总体质量评分。差异节点涉及 477 个 `web-features:` 标签；标签也不是逐一独立的标准。

| 状态代码 | 准确含义 |
| --- | --- |
| `default` | 当前版本存在无前缀、无额外开关的支持分支，BCD 没有标记该分支为 partial；仍必须读平台/驱动/权限等注释 |
| `partial` | 当前版本存在标准名称、默认启用的分支，但 BCD 标记部分实现 |
| `named` | 只有不需开关的前缀或替代名称分支 |
| `gated` | 当前可用分支需要偏好设置或运行开关，不能记成默认支持 |
| `unavailable` | 当前版本未记录可用分支，包含尚未加入、已移除、稳定版之外的 preview |
| `unknown` | 缺少该浏览器列，或版本边界不足以判断；不当作不支持 |

算法按 `version_added <= 目标版本 < version_removed` 取当前分支；删除边界为包含端。支持数组逐分支处理；`true` 表示已支持但起始版本不明；`≤N` 在目标低于 N 时保留不确定性。主统计不把 `preview` 当成稳定版支持。当前支持分支的 partial、开关、前缀、替代名都参与对比；不把不同的历史首次支持版本计成当前差异。限制注释另表保留，不因文字不同直接判定行为不同。

每个节点单独计数，没有把父节点的限制自动传播给所有后代；平台条件经常只写在接口级注释。因此 `default` **不等于每个平台都可用，也不等于该接口的每个成员都完整支持**。`standard_track`、`experimental`、`deprecated` 直接保留 BCD 标记；标准轨道包含提案，不等于已经成为正式标准。[S7]

**桌面稳定版：Firefox 159 对 Chrome 157**

| 互斥分类 | 条数 | 解释 |
| --- | ---: | --- |
| 当前支持分支结构相同 | 14,842 | 包含双方都未提供的记录，不等于双方全部通过 |
| 仅 Chromium 代理列标准名称默认可用 | 3,857 | Chromium 为 default/partial，Firefox 为 unavailable/named/gated |
| 仅 Firefox 标准名称默认可用 | 1,184 | Firefox 为 default/partial，Chromium 为 unavailable/named/gated |
| 其他支持分支差异 | 1,050 | 包括双方均可用但 partial、前缀别名、开关分支等不同；不全部都是互操作故障 |
| 合计 | 20,933 | 全部节点均已归类，桌面没有 unknown |

后三个差异分类合计 **6,091 条**。另有 **764 条**落在“分支结构相同”但限制注释不同的记录，单独交付，不能直接加成 6,855 个行为差异。双方标准名称默认可用（default 或 partial）的交集是 14,877 条；其中仍可能有其他支持分支差异。

只筛 `standard_track=true` 且 `deprecated=false`，得到 17,011 条：结构相同 12,712，Chromium 默认独有 3,041，Firefox 默认独有 367，其他分支差异 891。这个筛选仍包含实验提案，不能作为正式标准得分。

| BCD 分类 | 总记录 | Chromium 默认独有 | Firefox 默认独有 | 其他分支差异 | 差异合计 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Web API / DOM / Canvas / GPU 等 `api` | 10,319 | 2,410 | 286 | 719 | 3,415 |
| CSS | 4,303 | 748 | 128 | 226 | 1,102 |
| HTML | 845 | 130 | 20 | 14 | 164 |
| JavaScript | 1,402 | 0 | 20 | 10 | 30 |
| SVG | 471 | 23 | 14 | 5 | 42 |
| MathML | 137 | 0 | 48 | 0 | 48 |
| WebAssembly | 342 | 0 | 6 | 0 | 6 |
| HTTP | 418 | 195 | 7 | 16 | 218 |
| Web App Manifest | 38 | 36 | 0 | 0 | 36 |
| 媒体类型 `mediatypes` | 17 | 0 | 0 | 0 | 0 |
| WebDriver | 550 | 65 | 23 | 50 | 138 |
| 浏览器扩展 WebExtensions | 2,091 | 250 | 632 | 10 | 892 |
| 合计 | 20,933 | 3,857 | 1,184 | 1,050 | 6,091 |

与本项目最直接相关的 HTML、CSS、API、JS、SVG、MathML、Wasm 七类合计 17,819 条，差异 4,807 条：Chromium 默认独有 3,311，Firefox 默认独有 522，其他分支差异 974。这个数不是 XLWebEngineUI 的差距统计；没有对本项目逐条测量。

**Android 与开发主线单列**

| 口径 | 结构相同 | Chromium 默认独有 | Firefox 默认独有 | 其他分支差异 | unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android：159 / 157 | 13,784 | 3,833 | 559 | 666 | 2,091 |
| 开发版本估计：Gecko 161 / Chromium 159 | 15,187 | 3,441 | 1,216 | 1,089 | 0 |

Android 的 2,091 条 unknown 全部来自此快照缺少 `chrome_android` 的 WebExtensions 列，不能解读为 Chrome Android 提供或不提供这些扩展 API。其余已知记录差异合计 5,058；另有 457 条仅限制注释不同。

开发版本表只是把同一快照的数字目标改为 161/159，并计入 `preview` 分支的离线估计，差异合计 5,746。**它没有固定两份开发二进制、没有运行功能探测，不能称为 Nightly/Canary 实测或发布承诺。**主统计、结论均采用稳定版。

**架构和工程差异**

下表描述两套实现的主要结构，不把结构差异当成性能排名。细粒度实现会随提交和平台变化。

| 维度 | Gecko 平台 | Chromium 平台 | 实际含义/证据 |
| --- | --- | --- | --- |
| 项目边界 | Gecko 含布局、DOM、JS、网络、IPC、平台组件；Firefox 在上层构成产品 | Blink 是渲染引擎，Chromium 还包含 Content、V8、网络及浏览器产品层 | “Gecko vs Chromium”不是单一库对单一库 [S6,S8] |
| 主要实现技术 | C++、Rust、JS；Stylo/WebRender 是 Rust 组件 | 大量 C++，V8/Blink/Skia 等组件与多语言工具链 | 不能把 Gecko 称为纯 Rust，也不能据语言推断安全性 [S6,S9,S12] |
| HTML/DOM 对象 | Gecko DOM、WebIDL 绑定 | Blink DOM、V8 绑定 | 接口规范相同，内部类型、对象生命周期与原生桥不同 [S6,S8] |
| 样式计算 | Stylo，具并行样式计算设计 | Blink 自有样式与失效系统 | 失效粒度、选择器缓存、并行调度不同 [S9,S8] |
| 布局 | Gecko 自有 frame/reflow 系统 | Blink LayoutNG 系列 | 内在尺寸、分片、基线、双向文本边界仍需逐页比较 [S8,S10] |
| JS 引擎 | SpiderMonkey | V8 | 两者都不是 QuickJS；GC、JIT、嵌入 API 不可互换 [S11,S12] |
| JS 执行层级 | Baseline Interpreter、Baseline Compiler、WarpMonkey | Ignition、Sparkplug、Maglev、TurboFan/Turboshaft | 预热成本和优化策略不同，不能推出所有脚本谁快 [S11,S12] |
| Wasm 编译 | SpiderMonkey 的 Baseline/Ion 后端体系 | V8 的独立 Wasm 编译实现 | WebAssembly 支持需按提案和指令看，不能只比顶层 API [S11,S7] |
| 内存回收 | SpiderMonkey GC，与 DOM 生命周期/循环回收协作 | V8 GC，与 Oilpan/cppgc 的 C++ 对象回收协作 | 宿主对象、JS 引用与原生对象回收边界不同 [S13,S14] |
| 页面绘制 | Gecko display list → WebRender scene/frame | Blink paint → cc 的合成/栅格 → Viz 显示 | 不是简单比较“两边都用了 GPU” [S10,S15,S16] |
| Skia 的位置 | 可作为部分绘图路径组件；不能把 WebRender 直接等同 Skia | Skia 是主要绘图组件，具体 Ganesh/Graphite 选择随配置/平台变化 | 同一第三方库也不意味着相同管线或输出 [S10,S15] |
| 异步滚动 | APZ，按合成时的滚动/缩放变换与命中协调 | cc/compositor 的输入和滚动协调、Viz 合成 | 双方都能让部分滚动脱离主线程；不是 Gecko 独有 [S17,S15,S16] |
| 进程隔离 | Fission 等内容进程隔离与 GPU/媒体等进程 | Site Isolation、渲染/GPU/utility/network 等进程 | 双方均为多进程平台；进程数不固定等于标签页数 [S6,S18] |
| 进程/线程通信 | IPDL actors 等 | Mojo interfaces/services 等 | 协议、所有权、端点线程和生成器不同 [S19,S20] |
| 内部组件 | XPCOM/XPIDL、WebIDL 等 | Content embedder 接口、Mojo、Blink/V8 体系 | 不存在可直接替换的共同内部 ABI [S6,S20] |
| WebGPU | wgpu 体系，Naga 等相关组件 | Dawn，WGSL 编译器 Tint | 后端、验证、特性暴露、驱动屏蔽规则不同 [S22,S23] |
| WebGL | Gecko 自有 WebGL 集成及平台 GL/ANGLE 等路径 | Blink WebGL 集成，ANGLE 等图形适配 | 扩展集和真实设备支持不能由顶层 WebGL2 断言推得 [S7,S24] |
| 网络 | Necko；加密集成 NSS 等 | `net` / Network Service | 两套是完整浏览器网络实现；本 UI 引擎没有引入其网络栈的需求 [S6,S25] |
| 自定义资源协议 | Gecko channel/protocol 相关集成 | Content/CEF 的 URL 加载与 scheme 集成 | 都需另核安全源、CSP、CORS、worker、资源取消与生命周期；不是注册名字即全支持 [S6,S20,S25] |
| PDF | Firefox 内置 PDF.js | Chromium 集成 PDFium | PDF 文本、表单、注释、打印属于不同组件，非 HTML 布局器的简单附带能力 [S26,S27] |
| 无障碍 | Local/RemoteAccessible 与各平台映射 | AXTree/平台适配与多进程同步 | 双方均需平台桥；原生 UIA/IA2/ATK/AX 的树和事件不能仅靠 DOM 推断 [S28,S29] |
| Android 嵌入 | 官方 GeckoView 是随应用分发的独立组件 | Android WebView 是独立的产品集成边界；不能拿 Chrome API 表代替 WebView | 需要分别核版本、API、更新方式与默认开关 [S30,S7] |
| 桌面嵌入 | 此次确认到的 GeckoView 官方入口面向 Android；桌面使用 Gecko 需确认所选方案的维护范围 | CEF 明确提供桌面嵌入框架 | 本文不把“没有找到等价入口”写成 Gecko 绝对不能桌面嵌入 [S30,S31] |
| 公共 SDK / ABI | Gecko 内部接口不等于项目要求的独立稳定 C ABI | CEF 提供自己的包装，仍需按其 API/ABI 版本规则配套 | 本项目公共 ABI、线程、取消、所有权需要自行定义 [S6,S31] |
| 自动化 | WebDriver BiDi、Marionette；Firefox 141 已移除实验 CDP 支持 | CDP 及 ChromeDriver/BiDi 等工具体系 | 不能将仅支持 CDP 的脚本直接连接当前 Firefox [S32,S33,S34] |
| 扩展阻塞请求 | Firefox 的 webRequest 体系保留阻塞能力 | Chrome MV3 普通扩展不能使用 webRequestBlocking；策略安装有例外 | 这是产品扩展策略差异，不能简化为 Chromium 无法拦截网络 [S35,S36] |
| 扩展后台 | Firefox 与 Chrome 的后台页面、事件页、service worker 支持不同 | Chrome MV3 以扩展 service worker 为主要后台模型 | Manifest 版本相同不代表可直接通用；全部 API 逐项表已包含 [S37,S7] |
| 构建体系 | mach / mozbuild / moz.build / Cargo 等 | depot_tools、GN/Ninja 等 | 依赖锁、工具链、生成代码和补丁无法互换 [S38,S39] |
| 平台要求 | Firefox 159 官方要求 Windows 10+；GNU/Linux 官方二进制列出 glibc 等要求 | Chrome 官方要求 Windows 10+；Linux 官方列支持发行版 | 都不能自动满足本项目 Win7/musl 边界；源码可移植性与官方支持不同 [S40,S41] |
| 许可 | 自有代码以 MPL 2.0 为主，另有第三方许可 | 根 LICENSE 为 BSD 风格，Blink 历史代码及其他依赖有各自许可 | 两套均不能按单一根 LICENSE 给整个 SDK 下结论 [S42–S44] |

**现代 Web 功能逐项摘要**

下表的状态全部来自同一 BCD 快照与指定稳定主版本。`有` 对应 default，`部分` 对应 partial，`前缀/别名` 对应 named，`开关` 对应 gated，`未记录` 对应 unavailable；这些是数据库记录，不是本次实测。行名采用可复核的 BCD 路径；完整成员、参数、注释见 CSV。[S7]

| 功能及 BCD 路径 | Gecko 159 | Chromium 157 代理 |
| --- | --- | --- |
| Subgrid：`css.properties.grid-template-columns.subgrid` | 有 | 有 |
| :has()：`css.selectors.has` | 有 | 有 |
| 容器查询入口：`css.properties.container-type` | 有 | 有 |
| Anchor Positioning：`css.properties.anchor-name` | 有 | 有 |
| 锚点区域：`css.properties.position-area` | 有 | 有 |
| 滚动动画时间线：`css.properties.animation-timeline` | 有 | 有 |
| ScrollTimeline：`api.ScrollTimeline` | 有 | 有 |
| ViewTimeline：`api.ViewTimeline` | 有 | 有 |
| 同文档 View Transition：`api.Document.startViewTransition` | 有 | 有 |
| 跨文档 View Transition：`css.at-rules.view-transition` | 未记录 | 有 |
| 内在尺寸动画：`css.properties.interpolate-size` | 未记录 | 有 |
| 表单 field-sizing：`css.properties.field-sizing` | 有 | 有 |
| 文字盒裁剪：`css.properties.text-box-trim` | 有 | 有 |
| 文字盒边界：`css.properties.text-box-edge` | 有 | 有 |
| 异形圆角：`css.properties.corner-shape` | 未记录 | 有 |
| CSS 自定义函数：`css.at-rules.function` | 未记录 | 有 |
| 字体宽度新属性名：`css.properties.font-width` | 有 | 未记录 |
| 上下标字体合成控制：`css.properties.font-synthesis-position` | 有 | 未记录 |
| 滚动条颜色：`css.properties.scrollbar-color` | 有 | 有 |
| line-clamp 顶层：`css.properties.line-clamp` | 前缀/别名 | 前缀/别名 |
| CSS Typed OM 值：`api.CSSStyleValue` | 未记录 | 有 |
| CSS Typed OM 映射：`api.Element.computedStyleMap` | 未记录 | 有 |
| Web Animations：`api.Element.animate` | 有 | 有 |
| HTML Sanitizer：`api.Sanitizer` | 有 | 有 |
| EditContext：`api.EditContext` | 未记录 | 有 |
| Custom Highlight：`api.Highlight` | 有 | 有 |
| dialog：`api.HTMLDialogElement` | 有 | 有 |
| input showPicker：`api.HTMLInputElement.showPicker` | 有 | 有 |
| select showPicker：`api.HTMLSelectElement.showPicker` | 有 | 有 |
| 月份输入：`html.elements.input.type_month` | 未记录 | 有 |
| 周输入：`html.elements.input.type_week` | 未记录 | 有 |
| Popover：`html.global_attributes.popover` | 有 | 有 |
| OffscreenCanvas：`api.OffscreenCanvas` | 有 | 有 |
| Canvas Path2D：`api.Path2D` | 有 | 有 |
| Canvas filter：`api.CanvasRenderingContext2D.filter` | 有 | 有 |
| Canvas 绘制 DOM 元素：`api.CanvasRenderingContext2D.drawElementImage` | 未记录 | 有 |
| Canvas 捕获 DOM 元素图像：`api.HTMLCanvasElement.captureElementImage` | 未记录 | 有 |
| WebGL 2：`api.WebGL2RenderingContext` | 有 | 有 |
| WebGPU 顶层：`api.GPU` | 部分 | 有 |
| WebGPU navigator 入口：`api.Navigator.gpu` | 部分 | 有 |
| WebCodecs 音频解码：`api.AudioDecoder` | 有 | 有 |
| WebCodecs 视频解码：`api.VideoDecoder` | 有 | 有 |
| WebCodecs 音频编码：`api.AudioEncoder` | 有 | 有 |
| WebCodecs 视频编码：`api.VideoEncoder` | 有 | 有 |
| AudioWorklet：`api.AudioWorklet` | 有 | 有 |
| 语音识别：`api.SpeechRecognition` | 未记录 | 有 |
| 语音合成：`api.SpeechSynthesis` | 有 | 有 |
| Media Source：`api.MediaSource` | 有 | 有 |
| Media Recorder：`api.MediaRecorder` | 有 | 有 |
| WebRTC PeerConnection：`api.RTCPeerConnection` | 有 | 有 |
| Document Picture-in-Picture：`api.DocumentPictureInPicture` | 有 | 有 |
| Dedicated Worker：`api.Worker` | 有 | 有 |
| Shared Worker：`api.SharedWorker` | 有 | 有 |
| Service Worker：`api.ServiceWorker` | 有 | 有 |
| IndexedDB：`api.IDBFactory` | 有 | 有 |
| File System 基础句柄：`api.FileSystemHandle` | 有 | 有 |
| 宿主文件选择器：`api.Window.showOpenFilePicker` | 未记录 | 有 |
| Storage Buckets：`api.StorageBucket` | 未记录 | 有 |
| WebUSB：`api.USB` | 未记录 | 有 |
| WebHID：`api.HID` | 未记录 | 有 |
| WebSerial 基础接口：`api.Serial` | 有 | 有 |
| Web Bluetooth：`api.Bluetooth` | 未记录 | 部分 |
| 本地字体访问：`api.Window.queryLocalFonts` | 未记录 | 有 |
| Clipboard 入口：`api.Navigator.clipboard` | 有 | 有 |
| WebNN 入口：`api.Navigator.ml` | 未记录 | 开关 |
| WebXR 入口：`api.Navigator.xr` | 未记录 | 有 |
| Presentation：`api.PresentationRequest` | 未记录 | 有 |
| Payment Request：`api.PaymentRequest` | 开关 | 有 |
| WebAuthn：`api.PublicKeyCredential` | 有 | 有 |
| 电池状态：`api.BatteryManager` | 未记录 | 有 |
| Idle Detection：`api.IdleDetector` | 未记录 | 有 |
| EyeDropper：`api.EyeDropper` | 未记录 | 有 |
| UA Client Hints：`api.Navigator.userAgentData` | 未记录 | 有 |
| URLPattern：`api.URLPattern` | 有 | 有 |
| Temporal 顶层：`javascript.builtins.Temporal` | 有 | 有 |
| Iterator 顶层：`javascript.builtins.Iterator` | 有 | 有 |
| Promise.try：`javascript.builtins.Promise.try` | 有 | 有 |
| JSON.rawJSON：`javascript.builtins.JSON.rawJSON` | 有 | 有 |
| Wasm GC：`webassembly.garbage-collection` | 有 | 有 |
| Wasm memory64：`webassembly.memory64` | 有 | 有 |
| Wasm 异常处理：`webassembly.exception-handling` | 有 | 有 |
| 扩展 MathML menclose：`mathml.elements.menclose` | 有 | 未记录 |

**几个容易沿用旧结论的点**

Firefox 159 已默认启用滚动驱动动画，Firefox 147 起已有 `anchor-name`。因此“Firefox 不支持 ScrollTimeline / Anchor Positioning”不能用于本次最新稳定版对照。[S24,S45,S7]

BCD 记录桌面 Firefox 151 起提供 Serial 基础接口与 Document Picture-in-Picture；但 Serial 的蓝牙过滤参数等仍有差异，Firefox Android 的 Serial 列仍为未提供。不能把 WebUSB、WebHID、WebSerial 三者合并成一个判断。[S46,S47]

同文档 `Document.startViewTransition()` 双方都有；`@view-transition` 跨文档入口仍不同。顶层某 API 已存在，不代表它的每个子功能都相同。类似地，两边均有 OffscreenCanvas、Path2D、Canvas filter、WebCodecs 主接口、Temporal、Wasm GC/memory64，但仍须查看成员和平台限制。[S7]

Gecko 的 MathML 独有记录包括 `menclose`、`mlabeledtr` 等；其中一部分是旧语法、非标准或已弃用条目。Chromium 的新增硬件或应用 API 里也包含实验提案。数量多的一方不自动代表更符合规范或更适合 UI。[S7]

**WebGPU 单列，因为本项目正在接 Dawn**

| 层次 | Gecko / Firefox 159 | Chromium / Chrome 157 | 不能混淆的边界 |
| --- | --- | --- | --- |
| 实现库 | wgpu；相关编译组件 Naga | Dawn + Tint | 这是底层实现差异 [S22,S23] |
| BCD 的桌面 GPU 接口记录 | `partial` | `default`，仍有 OS/GPU 注释 | default 不是全部 Linux GPU 都支持 |
| Windows | BCD 记录 Firefox 141 起支持 | BCD 记录支持 | 设备、驱动、屏蔽名单和限制值仍需实测 [S48] |
| macOS | BCD 记录 Apple silicon 的分版本支持；Intel macOS 未支持 | BCD 记录支持 macOS | 不把原生 Metal 后端存在等同所有 Firefox macOS 设备已开放 [S48] |
| Linux | 此快照 Firefox 注释为不支持 | 此快照注明 Linux 仅 Intel Gen12+ GPU | 不写成两边全部 Linux 通用 [S48] |
| Android | 此快照 Firefox Android 未提供 | Chrome Android 121 起有接口 | 不是 GeckoView/所有 Android GPU 的额外实测 [S48] |
| worker 上下文 | Firefox 注释排除 service worker | 以各接口与 worker 子项记录为准 | 不能由 Window 可用推得所有 worker 可用 [S48] |

以上平台条件是 **2026-11-02 BCD 快照的已记录情况**；本次没有硬件验证，也没有把后来计划中的支持填入统计。完整原始注释和 bug 链接在 CSV / data.json。

对 XLWebEngineUI 而言，原生 Dawn 能创建纹理、Skia Graphite 能绘制 View、网页能执行 `navigator.gpu`、不同 Runtime/worker 的设备生命周期正确，是不同层的工作。Chromium 采用 Dawn 不能证明本项目已有 Chromium 的 WebGPU 网页绑定、权限、安全上下文和异步语义。项目现有 Dawn 状态以 WORKMEM/TODO 的实际证据为准，不在本调查中重新验收。

**媒体、字体、隐私和性能的证据边界**

| 领域 | 可确认的差别 | 本次没有给出的结论 |
| --- | --- | --- |
| 编解码/DRM | Chrome 与 Chromium 的构建、编解码器、CDM 和分发配置有区别；WebCodecs 顶层接口相同也不等于可处理相同 codec/profile | 不写“Chromium/Gecko 支持全部 H.264/HEVC/AV1/HDR”，没有逐格式/设备播放结果 [S21,S7] |
| 字体与文字 | 两套布局、样式、绘图实现不同；BCD 含字体格式、属性、API 的具体差异 | 没有固定字体/字号/缩放下的中文断行、字形、抗锯齿、基线和色彩像素对照 [S7,S8,S10] |
| 隐私和存储 | Firefox 有自身反跟踪体系；隔离、授权与扩展策略在产品层实现 | 不把 Google Chrome 默认策略等同任意 Chromium，也不以 API 多寡判断隐私优劣 [S35,S36,S49] |
| 首帧/交互/JS | JIT、布局、绘制、异步滚动及 IPC 设计有结构差异 | 没有在同设备同构建配置下得出谁快多少、谁省电或谁更省内存 [S11,S12,S15,S17] |
| 二进制/安装包体积 | 打包范围、调试信息、组件/静态构建、语言资源、编解码与符号不同会影响大小 | 没有以两个不同 SDK 的 ZIP 大小推断内核体积 [S21,S38,S39] |
| 安全 | 双方均有隔离、沙箱和安全修复流程；边界与实现不同 | 不按本月 CVE 数目、Rust 占比或进程数判定哪边“绝对更安全” [S18,S19,S20] |
| WPT/Interop | 可以在固定测试提交、浏览器构建、平台和默认开关后做行为对照 | 本次 wpt.fyi API 请求 TLS 握手超时，没有拿到匹配版本的对齐运行；因此未生成 WPT 差异数或通过率 |

要继续统计行为差异，应固定 Firefox 159.0.1 与 Chromium 157.0.8103.39 的二进制哈希、共同 WPT 提交、OS/GPU/驱动/字体/缩放和开关，分别比较 testharness、reftest、crash/timeout，再把差异映射到规范条款。性能另用相同原页测首帧、布局/绘制、滚动帧时间、JS 冷热执行、进程树内存和 GPU 资源。它们不能由 BCD CSV 推导。

**对本项目的用途**

项目定义为 Web UI 引擎，HTTP/HTTPS 和网络获取由宿主承担；本次统计中 HTTP、网络设备、浏览器扩展和浏览器产品界面是比较范围的一部分，不因此自动加入 XLWebEngineUI 的产品范围。

HTML/CSS/DOM/JS/Canvas/SVG/文字/输入/AX/渲染合成是更直接的参照。对 UI 页面兼容，Chromium 的 API 面与成熟嵌入入口值得参考；Gecko 的另一套布局、回收、WebRender/APZ、MathML 和标准实现可用作独立对照。是否复用整套内核、单独复用组件或维持当前实现，不能只按 6,091 条的数量决定。

闭源商用需要对**实际纳入的文件与依赖闭包**检查。Mozilla MPL FAQ 明确允许与专有代码组成 Larger Work，但分发时对 MPL 覆盖文件仍有相应源码义务；这不是整个宿主都必须开源。Chromium 根 BSD 许可也不是整个依赖树全为 BSD 的保证；官方许可工具明确保留 Blink/WebKit 的 BSD/LGPL 历史许可记录。静态封装和修改第三方代码须逐文件确认，而不能只读根 LICENSE。[S42–S44]

**交付物与复算**

源码分析工具为 `tools/compare_browser_compat.py`，使用 Python 标准库；不执行 npm 包内代码。固定下载 URL、提交、许可、SHA-256/SHA-512 和分析版本都保存在该工具中，build 可删除后重新取得原始输入。

```powershell
python -B tools/compare_browser_compat.py --fetch --out build/reports/gecko-chromium-comparison/2026-11-04
```

`--fetch` 只在缺少 data.json 时下载固定版本，不查询 latest；已有输入也须匹配固定哈希。数据/分类方法修订时应另留目录，不能悄悄替换本次统计口径。

报告目录 `build/reports/gecko-chromium-comparison/2026-11-04/`：

| 文件 | 内容 |
| --- | --- |
| `desktop_stable-differences.csv` | 6,091 条桌面稳定差异，含四个浏览器原始支持数据、当前分支、状态、注释、标准标记、源文件、MDN/规范地址 |
| `desktop_stable-notes-only.csv` | 764 条支持结构相同但限制注释不同的记录 |
| `summary.json` | 所有分类、状态组合、现代条目筛选及标签聚合，包含固定数据来源身份 |
| `gecko-chromium-comparison-2026-11-04.zip` | 全部 20,933 条原始节点、桌面/Android/开发版完整分类和差异表、原始 BCD、许可、本文与工具 |
| `verification.json` | 输入哈希、独立 CSV 重计数与统计守恒结果；不表示浏览器行为通过 |

CSV 使用 UTF-8 BOM。结构化列为 JSON 字符串，`feature` 是稳定检索键；可按 `category`、`classification`、`deprecated`、`standard_track`、`experimental` 筛选。`firefox_default_only` / `chromium_default_only` 描述该节点标准名称默认可用的单方记录，不保证所有平台可用。

本次只交付调查结论和统计工具，不构建引擎、不生成新 SDK、不把局部资料检查当作完整引擎验收。项目原始计划及用户 HTML 只读。

**来源索引（官方资料/上游一手资料）**

为保留可追溯性，以下列出实际查阅地址。浮动文档/主线日后会变化；全量能力统计以固定 v8.1.8 数据为准。

| ID | 来源地址 |
| --- | --- |
| S1 | `https://product-details.mozilla.org/1.0/firefox_versions.json`；`https://www.firefox.com/en-US/firefox/159.0.1/releasenotes/` |
| S2 | `https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1` |
| S3 | `https://chromereleases.googleblog.com/2026/11/` |
| S4 | `https://raw.githubusercontent.com/mozilla-firefox/firefox/main/config/milestone.txt` |
| S5 | `https://raw.githubusercontent.com/chromium/chromium/main/chrome/VERSION` |
| S6 | `https://firefox-source-docs.mozilla.org/overview/gecko.html` |
| S7 | `https://github.com/mdn/browser-compat-data/releases/tag/v8.1.8`；`https://registry.npmjs.org/@mdn/browser-compat-data/8.1.8`；`https://raw.githubusercontent.com/mdn/browser-compat-data/main/docs/data-guidelines/README.md` |
| S8 | `https://www.chromium.org/blink/` |
| S9 | `https://github.com/mozilla-firefox/firefox/blob/main/docs/glossary/index.md` |
| S10 | `https://firefox-source-docs.mozilla.org/gfx/RenderingOverview.html` |
| S11 | `https://firefox-source-docs.mozilla.org/js/index.html` |
| S12 | `https://chromium.googlesource.com/v8/v8/+/701254bc23346416c19c6793da86ba13de35bef4/docs/runtime/tiering.md` |
| S13 | `https://firefox-source-docs.mozilla.org/js/gc.html`；`https://spidermonkey.dev/assets/pdf/SpiderMonkey%20Byte-sized%20Architectures.pdf` |
| S14 | `https://chromium.googlesource.com/v8/v8.git/+/refs/heads/13.1.95/include/cppgc/` |
| S15 | `https://raw.githubusercontent.com/chromium/chromium/main/docs/how_cc_works.md` |
| S16 | `https://raw.githubusercontent.com/chromium/chromium/main/components/viz/README.md` |
| S17 | `https://firefox-source-docs.mozilla.org/gfx/AsyncPanZoom.html` |
| S18 | `https://www.chromium.org/Home/chromium-security/site-isolation/` |
| S19 | `https://firefox-source-docs.mozilla.org/ipc/ipdl.html` |
| S20 | `https://chromium.googlesource.com/chromium/src/+/HEAD/docs/mojo_and_services.md` |
| S21 | `https://raw.githubusercontent.com/chromium/chromium/main/docs/chromium_browser_vs_google_chrome.md` |
| S22 | `https://github.com/gfx-rs/wgpu` |
| S23 | `https://dawn.googlesource.com/dawn` |
| S24 | `https://developer.mozilla.org/en-US/docs/Mozilla/Firefox/Releases/159` |
| S25 | `https://chromium.googlesource.com/chromium/src.git/+/master/services/network/` |
| S26 | `https://mozilla.github.io/pdf.js/getting_started/` |
| S27 | `https://github.com/chromium/pdfium`（官方历史镜像；仓库标明当前源码地址为 `https://pdfium.googlesource.com/pdfium/`） |
| S28 | `https://gecko-docs.mozilla.org-l1.s3.us-west-2.amazonaws.com/accessible/Architecture.html` |
| S29 | `https://new.chromium.org/developers/design-documents/accessibility/` |
| S30 | `https://mozilla.github.io/geckoview/` |
| S31 | `https://github.com/chromiumembedded/cef` |
| S32 | `https://firefox-source-docs.mozilla.org/remote/Prefs.html` |
| S33 | `https://firefox-source-docs.mozilla.org/remote/marionette/Intro.html` |
| S34 | `https://chromedevtools.github.io/devtools-protocol/` |
| S35 | `https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/webRequest` |
| S36 | `https://developer.chrome.com/docs/extensions/reference/api/webRequest` |
| S37 | `https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Background_scripts` |
| S38 | `https://firefox-source-docs.mozilla.org/build/buildsystem/` |
| S39 | `https://github.com/chromiumembedded/cef/blob/master/docs/branches_and_building.md` |
| S40 | `https://www.firefox.com/en-US/firefox/159.0/system-requirements/` |
| S41 | `https://support.google.com/chrome/a/answer/7100626` |
| S42 | `https://www.mozilla.org/en-US/MPL/2.0/FAQ/` |
| S43 | `https://raw.githubusercontent.com/chromium/chromium/main/LICENSE` |
| S44 | `https://github.com/chromium/chromium/blob/main/tools/licenses/licenses.py` |
| S45 | `https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Properties/anchor-name` |
| S46 | `https://raw.githubusercontent.com/mdn/browser-compat-data/v8.1.8/api/Serial.json` |
| S47 | `https://raw.githubusercontent.com/mdn/browser-compat-data/v8.1.8/api/DocumentPictureInPicture.json` |
| S48 | `https://github.com/mdn/browser-compat-data/blob/v8.1.8/api/GPU.json` |
| S49 | `https://firefox-source-docs.mozilla.org/toolkit/components/antitracking/anti-tracking/index.html` |
