# 图形后端实施契约

用户要求多后端及按能力降级；仅编译进库不能作为视图实际使用 GPU 的证据。View 已接入 Ganesh surface、候选调度和丢失恢复，实际验证范围由阶段报告记录。默认创建仍为 Raster；宿主通过 `xl_view_set_graphics_backend` 或 `xl::View::graphics_backend` 显式选择。Windows 当前候选链为 D3D12→ANGLE D3D11→系统 Vulkan→原生OpenGL→ANGLE OpenGL→静态 SwiftShader→Raster；显式候选失败后从该位置继续向后选择。其他平台的静态依赖和运行时实现分别验收，不能从 Windows 证据推导跨平台可用。

| 平台 | 目标候选顺序 | 必须保留的终点 |
|---|---|---|
| Windows | D3D12、ANGLE/D3D11、Vulkan 驱动、原生 OpenGL/ANGLE GL、静态 SwiftShader | Skia Raster |
| Linux | Vulkan 驱动、EGL/OpenGL、静态 SwiftShader | Skia Raster |
| macOS | Metal、ANGLE Metal/OpenGL、静态 SwiftShader | Skia Raster |
| Web | WebGPU、WebGL2/WebGL | Wasm Raster/Canvas |

`third_party/recipes/skia.json` 显式启用 Ganesh、Graphite、OpenGL、Vulkan，Windows 增加 Ganesh D3D12；Metal/Web 的配置与宿主工具链接入分别验收。Dawn/WebGPU 的完整传递依赖、ANGLE、SwiftShader 和 DXC 的静态闭包必须单列真实构建与行为证据，不能以布尔开关替代。

Vulkan-Headers、VulkanMemoryAllocator、D3D12MemoryAllocator、SPIRV-Cross 按锁定 Skia 的 DEPS 固定提交和哈希；保留宽松许可，原始来源只读。Skia FXC 调用通过 third_party/patches/skia-optional-d3dcompiler.json 改为首次编译着色器时加载，搜索引擎映像所在目录及 Windows 系统目录；不搜索网页当前目录。d3dcompiler_47.dll 是用户允许的可删除可选组件，缺失必须导致该候选失败并降级，不能使 SDK 加载失败。Windows SDK现在强制包含third_party/redist锁定的SDK8.1 x64原版DLL（6.3.9600.16384），同目录附来源/哈希/再分发说明；安装前核SHA，完整签名保留。删除本地副本后仍可使用独立安装的系统FXC；系统也没有可用编译器时，该候选失败并继续降级。

dxcompiler.dll 对应 DXC 编译器与旧 FXC 不可互换：先建立静态 DXC 目标和完整 LLVM/Clang 私有闭包，再让实际使用 DXIL 的路径调用它；不拿 Windows import .lib 冒充静态实现。是否含 dxil 验证组件、签名组件的许可和分发条件独立记录。ANGLE 的 EGL/GLES 与 SwiftShader ICD 均应使用私有静态入口及符号隔离，允许系统驱动作为平台边界，SDK 不应依赖旁置的 EGL/GLES/SwiftShader/DXC DLL。

每个 View 记录实际后端、候选失败原因及资源代次，线程归属与生命周期明确。创建/扩容/绘制/提交/读回失败和设备丢失应使保留场景完整重绘至下一候选；失败前的部分内容不能发布，静止帧复用和局部损伤在各路径保持。借用窗口不得改宿主图形上下文或窗口所有权，外部纹理/视频和原生混排共享同步、坐标、裁剪及 AX 契约。后端测试需包含实际像素、创建失败、丢失恢复、静止复用和关闭，Win7/旧 Linux/macOS/Web 未经实测仍标未验证。

`xl_view_get_graphics_info` 返回 requested/active、attempted_mask/failed_mask、shader_compiler、generation/recoveries；mask 使用 `1u << xl_graphics_backend`，失败目前仅表示候选不能继续使用，尚不提供设备诊断字符串。`shader_compiler` 使用原保留字段位置，结构大小不变：0无单列编译器、1可选FXC、2静态DXC；它报告当前context的选择。`xl_view_abandon_graphics_backend` 在 View 所属 UI 线程报告上下文失效，下一帧从下一候选恢复，不在失效设备上重试。宿主也可重新显式选择以重建候选链。所有这些操作沿用 Runtime/View 的线程与重入规则。

当前 GPU surface 在提交后同步读回 RGBA，由既有 SDL 路径上传呈现；这是 GPU 绘制与 CPU 呈现的阶段实现，尚未提供交换链直呈或外部纹理零拷贝。局部损伤、静止不重画、曝光复用继续使用原有契约。读回期间切换设备时重新读取完整帧，避免只上传旧损伤区域而留下不同后端的混合图像。ANGLE 使用私有 EGL 上下文，多个 View 共享的 display 按引用计数销毁，并在离开引擎时恢复之前的私有 EGL/Windows 宿主 WGL 上下文。

独立 Runtime 可在各自 UI 线程使用 ANGLE。共享 display 的上下文仍共用 D3D11 renderer/immediate context，因此以 display 为粒度串行化完整 Skia 操作（初始化、绘制/提交、读回、销毁），而不只锁单个 EGL/GL 调用。不同 display 与其他后端不共用该锁。多线程测试保留同时创建、读回和缩放，D3D 调试层曾捕获的并发调用不能靠关闭调试层或跳过测试解决。

三方静态消费的可重建测试源在 `tests/graphics/angle_static.cpp` 与 `swiftshader_static.cpp`；CMake 的 `graphics.angle_static` 与 `graphics.swiftshader_static` 分别执行实际 EGL/GLES 像素读回和 Vulkan 设备枚举。`graphics.backends` 进一步检查 View 像素、静止复用、缩放、多 View/线程及显式失效恢复。只链接工厂的 `graphics.compiled_backends` 继续单列，不作为实际渲染证据。

`graphics.fxc_fallback` 将引擎与失败编译器复制到独立临时目录：编译器返回失败且不提供错误 blob，ANGLE 对缺少入口的编译器返回错误，不触发断言。测试检查恢复后的实际像素、失败掩码和恢复次数，结束删除副本。`graphics.dxc_without_fxc` 在同一条件下要求SM6 D3D12实际绘制、保持active=2/compiler=2、无失败候选且未加载DXC/DXIL DLL。旧设备没有SM6时单列skip，不能算现代路径通过。Skia补丁处理空诊断，私有Debug接口与GN的`GPU_TEST_UTILS` ABI保持一致。系统DLL不参与测试改写。

DXC 的独立可重建消费入口为 `python -B tools/graphics_deps.py verify-dxcompiler --target windows-x64 --configuration Debug`，从受控配方构建真实静态闭包后，以 CMake 编译 `tests/graphics/dxcompiler_static.cpp` 并执行 HLSL→DXIL 检查。其运行时库清单从 CMake 链接图生成，排除 TableGen 等构建工具；COFF linker roots/aliases 随私有符号重命名，CRT/ATL ABI 保留，Debug 迭代器选项与本项目一致。依赖分发边界见 `graphics-dependencies.md`。

2026-11-04 Windows 独立 DXC 静态消费已通过（key `49bfd9448a73`）：两个线程并发首次初始化，分别编译得到2548字节DXIL对象，并核验无效HLSL诊断；进程未加载DXC/DXIL DLL。证据在 `build/reports/graphics-smoke/windows-x64/dxcompiler-static/49bfd9448a73`，验证后保留日志/报告并删除消费构建/PDB暂存。

引擎现通过受控Skia补丁为每个D3D12 context选择编译函数。`CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL)`成功且支持SM6时使用私有静态DXC，将Skia的vs/ps/cs_5_1目标映射到对应6_0目标，保留矩阵打包、调试/优化及严格IEEE选项。旧设备继续可选FXC。DXC对象每次编译在作用域内销毁，首次全局初始化由私有编译器管理；没有进程全局替换分配器，也不依赖外置编译器DLL。两处Skia着色器调用都经过同一context回调，失败进入既有恢复链。

Windows `20261103T182706Z-891f183b`四项实际检查通过（20.05s）：五后端View、ANGLE缺FXC降级、D3D12在FXC必定失败时仍正确绘制、WebTest文字页面保持DXC后端。该轮无skip。接着以静态SDK和动态SDK同时验证并生成阶段包；不将单机Windows结果当成旧系统/macOS/Linux/Web验收。

图形依赖的安装 prefix 只保留隔离后的最终静态闭包、头文件、许可与合并报告；隔离成功后删除重复输入归档和符号映射暂存。Ninja 工作目录仍保留上游增量产物，所有内容均可由锁定来源、配方和补丁重建。`build_products` 记录历史构建产物名称，不表示中间归档仍安装在 prefix；`intermediate_archives_retained=false` 明确该状态。失败 prefix 也提前登记并在构建期间持有租约，纳入每类别/目标最近 3 份清理，避免异常退出后留下无人管理的库副本。

Linux 输入同样选择私有静态 ANGLE 和 SwiftShader，DXC 仅用于 Windows。SwiftShader 的最低宿主 CMake 要求由 `third_party/toolchains.lock.json` 锁定的 Linux 工具满足；`python -B tools/bootstrap.py cmake` 可单独准备，Windows 入口转交 WSL。该工具从校验归档恢复到 `build/deps/src/host-cmake-linux-x64`，缺失文件按归档清单修复。其版本和归档哈希计入 SwiftShader 构建键，目录纳入引用管理；不更换其他组件的 CMake 或目标编译器。旧版本门槛失败时留下的纯源码目录可以复用，已配置目录必须匹配编译器。

musl SDK 需要兼容目标 libc 的系统图形驱动，不能把构建主机的 glibc 驱动当作可用保证。`graphics.angle_static` 在 Linux 上选择 OpenGL；初始化不可用时返回 77 并明确记录跳过，其余错误仍失败。内置 SwiftShader 是不依赖该驱动的候选，动态/静态 View 测试要求实际选中它并核对像素、两线程、缩放和失效后的 Raster 恢复；`webtest.gpu` 要求文字页面保持 SwiftShader。完成状态以目标平台的阶段报告为准。

Linux View 首次选择 GPU 候选时才创建自己的图形执行线程，显式预留 4 MiB 栈。GPU 创建、缩放、绘制、提交、读回、失效与释放均在此线程串行执行；调用同步等待完成，只有一个待执行槽位，没有不断增长的任务队列。这样 SPIR-V 校验和 JIT 不消耗宿主小栈 UI 线程的空间，也不更改宿主的默认线程属性。DOM、事件循环、SDL 窗口操作继续由原 UI 线程拥有；从未请求 GPU 的 Raster View 不创建该线程。销毁先在图形线程释放 surface/context/device，再停止并 join 线程。窗口交换链直呈和异步合成仍待后续。

原生OpenGL使用新增枚举`XL_GRAPHICS_OPENGL=7`及WebTest `--graphics opengl`，旧枚举值/结构布局与111函数导出不变。Windows创建独立隐藏WGL上下文，用系统opengl32加wglGetProcAddress直接组装Skia GL interface；每次操作及失败销毁恢复宿主上下文。Linux通过显式libEGL/libOpenGL/libGL运行时边界创建独立desktop-GL pbuffer，恢复线程原EGL API与上下文，display按引用计数管理。它不经过静态ANGLE。Windows已实测active7、像素、静止/缩放、多View及宿主WGL恢复；本机musl缺系统EGL/GL驱动，已实测回退SwiftShader，不能写成Linux原生GL已成功或旧平台已验证。

`graphics.optional_fxc`使用锁定原版DLL及引擎的私有临时副本实际渲染，检查ANGLE D3D11/FXC无失败及已加载模块的绝对路径确实在应用目录。既有失败编译器与现代静态DXC检查继续保留。阶段SDK包含物理重复库审查；相对.so符号链接不重复存储库载荷。SVG/XML/Wuffs已启用并接入网页SVG/IMG，详svg-and-images.md。Dawn/Tint与Skia Graphite现在实际启用，新增枚举8/9及WebTest dawn/dawn-swiftshader；位于ANGLE GL之后、原Ganesh SwiftShader之前，具体平台验证与网页WebGPU API边界见dawn.md。网页navigator.gpu和Canvas WebGPU仍未实现，不能以原生后端通过代替。
