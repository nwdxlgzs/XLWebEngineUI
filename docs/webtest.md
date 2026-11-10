# WebTest 本地试用程序

原生窗口已共用主指针桥，可直接点击summary、按钮/链接和label，按下转交焦点，拖动保留文字选区；同时提供Enter/Space/Tab和Windows UIA展开收起。完整DOM MouseEvent/PointerEvent与作者伪元素仍待，见 details.md。

宿主字体未覆盖某个字符时，文字层保留原始文本和无障碍范围，绘制字体的缺字占位；不会因一个缺字取消整页。占位不代表该语言已具备字体覆盖，开发者可通过 `--font` 提供合适字体。用户网页 smoke 仍只验证加载与首帧，不认证字体和 CSS 的完整视觉效果。

SDK 发布包的 bin 目录包含 WebTest（Windows 为 WebTest.exe），与包内动态库配套。它是引擎的真实宿主程序，开发者解压后即可预览，无需编译、服务器或浏览器内核。

Windows 图形窗口自动开启 UI Automation 桥，支持树查询、控件模式和动态通知，离屏 --check 不开启。TextPattern、原生子树嫁接、其他平台桥与读屏实测边界见 windows-accessibility.md。

Linux musl 包的 WebTest 是启动入口，调用同目录 WebTest.bin 和包内匹配加载器；无需目标机另装 musl。请保持 bin/lib 的包内结构。该加载器与 SDK 输入记录的 musl SHA256 一致，许可随包保留。图形模式仍需要系统显示服务器，离屏 --check 不需要显示服务。

进入要测试的目录，从终端运行包内程序：

```text
cd your-ui-directory
<sdk>/bin/WebTest
```

默认显示当前工作目录的 index.html，960×640 可调整窗口，关闭窗口退出。窗口已启用滚轮滚动、左键点击/拖动与Shift点击阅读选区，并接通下文的键盘、文本控件编辑和纯文本剪贴板；滚动、选区和光标使用引擎自身布局。滚动条控件和普通阅读选区的键盘扩选仍待实现。程序所在目录与网页根目录独立；依赖相对当前工作目录而不是可执行文件寻找。Windows 用实际路径执行 WebTest.exe，Linux 用 ./ 或绝对路径。包内 share/doc/XLWebEngineUI/examples/webtest 是可直接运行的样例目录。

可选命令：

```text
WebTest nested/page.html --width 1200 --height 800
WebTest --root /path/to/ui
WebTest --check --snapshot frame.ppm
WebTest --font fonts/MyFont.ttf
WebTest --frames 2
WebTest --no-scripts
WebTest --graphics auto
WebTest --check --graphics swiftshader --snapshot frame.ppm
```

--check 使用真实离屏渲染，完成一帧后退出；--snapshot 显式写出 PPM RGB 图像，不会自动生成截图或数据文件。--frames 控制窗口模式成功渲染的帧数，适合简短显示验证。--help 列出用法。默认不扫描挂载整棵目录，不启动服务器，不请求网络。

窗口模式默认请求 AUTO 后端，离屏 --check 默认 Raster。--graphics 可选 auto、raster、d3d12、angle-d3d11、vulkan、angle-gl、swiftshader，按对应位置向后降级，控制台输出实际 active、失败候选掩码和恢复次数，具体枚举与平台边界见 graphics-backends.md。GPU 阶段仍通过读回后交 SDL 呈现；不将请求后端名称当成实际设备成功的证据。

`shader_compiler=2`表示当前D3D12 context选择了静态DXC，`1`为可选FXC，`0`表示该后端未单列此编译器。支持SM6的设备使用静态DXC，不需旁置dxcompiler.dll；选择旧DirectX路径时FXC仍按需解析。SDK的源码依赖与Windows工具链分发条件分别说明在graphics-dependencies.md。

宿主将根目录映射到 xl-webengine-ui://app/。HTML、相对 CSS 和 xl.readText 用到的资源首次请求才从根目录读取并在内存缓存。规范化后的物理文件可通过多个 URI 复用同一份内容。单次会话最多缓存256MiB；越界路径、指向根目录外的符号链接或非普通文件拒绝读取。显式--root才改变根目录。资源替换尚未热重载，改文件后重新启动程序。

WebTest 在 DOM 解析完成后，按文档顺序显式执行经典内联/外部 script，默认每次执行预算1秒。console 的 log/info/warn/error/debug 输出到终端，单条消息和缓存数量有界。它不实现浏览器的解析中断、async/defer调度和模块加载；不支持的模块会提示。引擎本身的HTML解析仍不自动运行脚本，其他宿主可自行选择执行策略。JavaScript错误或主页面加载失败退出1，实际请求到的缺失资源退出2；布局缺口计数显示在控制台，退出0不代表完整网页兼容。

当前目录与fonts子目录里的TTF/OTF在首次预览文字时自动加入，--font可额外指定根目录内字体。别名使用文件名（不含扩展名）；CSS可使用该别名选择，也会加入本次预览的字体回退。最多128项，所有本地字体合计最多64MiB，重复路径不重复载入。未找到可用字形时才解压程序内置Roboto回退，数据只进内存；其原始OFL许可随包提供。回退覆盖拉丁等文字，中文等其他字形需本地合适字体；不把缺失字形伪装成完整支持。

窗口已启用Tab/Shift+Tab聚焦、HTML按钮Enter/Space与复选框Space激活，箭头/Page/Home/End/空格翻页，以及可取消的DOM keydown/keyup；新聚焦目标滚入视口并显示轮廓。input文本类型/textarea已接文字编辑、选择、撤销/重做及SDL组合输入入口；Ctrl/Meta+C/X/V接系统纯文本剪贴板及可取消的copy/cut/paste事件，密码默认不导出明文。实际平台IME、剪贴板跨进程互操作和完整表单/平台AX仍待验收。键盘及编辑范围见docs/keyboard.md、docs/editing.md、docs/clipboard.md；动态焦点样式范围见docs/focus-styles.md。

这仍是开发中的引擎。内部协议当前支持HTML/CSS/显式文本读取，图片、CSS字体URL、模块、媒体、完整输入及平台AX桥仍在实现。WebTest使用包内引擎的实际能力，不调用系统浏览器补齐缺口。首次文字准备为异步内存解压，程序等待并重试；静止窗口复用保留帧。

`--graphics opengl`选择Skia原生desktop OpenGL，`angle-gl`选择静态ANGLE的GL路径。Windows包在bin中包含可删除的可选d3dcompiler_47.dll；静态DXC不依赖它。
