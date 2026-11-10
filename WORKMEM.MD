# 工作记忆

更新时间：2026-11-10（Asia/Shanghai，仅维护记录更新）。先读AGENTS/PLAN/TODO，RAW只读。此文件UTF-8≤40,000字节，仅保存续接所需事实；完整证据见TODO、docs、git。

2026-11-10用户授权敏感信息清理/新建默认GitHub账号私有仓库并推送，已完成；覆盖旧日常“不自动push”的限制仅限本次交付。私有origin=nwdxlgzs/XLWebEngineUI、默认main，初始清理快照1ba2554（工作区原无.git，旧提交号仅历史证据索引）。文档本机绝对路径转相对，凭据/IDE/诊断输出忽略，作者noreply；RAW、9用户HTML和三方锁/原许可/必要FXC DLL保留。424个真实暂存文件与Git提交Gitleaks v8.30.1扫描0发现，工具SHA/RAW与9页逐字节核同。API/ls-remote核同commit，远端全部424个blob逐项同本地；未上传build，认证仅系统凭据管理器内存使用。最终commit/tree与复核见build/reports/repository-publish/20261110/report.json及origin/main；原功能开发仍暂停，不自动恢复Linux构建。

2026-11-09/10用户授权C盘缓存安全清理已完成：go clean -cache、pip cache purge及双位置npm cache clean均退出0；独占DELETE句柄/最终路径检查后清NVIDIA/DirectX5.629GiB、VSCode1.740GiB、npx0.688GiB及>7天创建/修改的Temp27.375GiB（含旧WSL swap8.004GiB），共188053条逐文件删除记录核数量/字节及授权范围一致。另Go约6.459GiB、pip约1.70GiB及npm下载缓存由原生命令清。C盘空闲23.25→约68.08GiB，观测净增约44.83GiB，不与逻辑字节和混同。14锁定NV文件/73最终路径不可验证npx文件保留；近期、工具链/runtime、Git目录、链接/只读/其他虚拟盘保留。Go/pip余228/1436bytes，npm本地约8.7MB，VSCode7缓存目录余0；User/Backups/已安装扩展、.cache运行环境/安装维护缓存存在。报告build/reports/c-drive-cache-cleanup/20261109T155301Z/report.json及verification.json，辅助脚本全清。原项目开发仍暂停，无新构建/SDK。

2026-11-09用户再次授权WSL清理/紧缩已完成：Ubuntu原停止、无活动构建，清/tmp临时opencode副本、APT可重建二进制缓存与超额npm日志共348323840bytes（332.19MiB），日志各类留3份；保留WSLg链接/目录、工具链、Zig/uv缓存、LLVM增量目录和用户配置。fstrim→关闭→管理员DiskPart只读连接/compact/分离成功，Ubuntu启动/根可写/临时文件同步读写通过，无内核文件系统错误，再次关闭。VHD18846056448→10606346240bytes（17.55→9.88GiB），净回收8239710208bytes=7.67GiB；报告build/reports/wsl-maintenance/20261109T153701Z/report.json。辅助脚本全清、维护报告2份≤3；RAW17777070…未改，原开发goal继续暂停。

用户另行授权WSL临时清理/磁盘紧缩已完成（2026-11-04）：Ubuntu /tmp零字节编译残留1项/过期ICE socket6项删除，保留WSLg链接/套接字目录，/var/tmp为空；fstrim→关闭WSL→DiskPart只读连接/实际compact/分离成功，启动读写检查通过并再次关闭。VHD最终12736004096→11861491712bytes，净回收834MiB（启动验证较紧缩终点回增2MiB）；报告build/reports/wsl-maintenance/20261104T135933Z/report.json。未清Docker/项目依赖，开发goal仍暂停，不自动恢复构建。

用户最新已暂停goal。已SIGINT停止唯一WSL sdk_inputs.py PID15，session16072已终态1；不再有本轮活动任务，勿自动继续，等用户恢复。Windows已验SDK130220Z-fc9377cc/功能提交1eecc30有效；旧Ninja归档后备与2项测试/相关说明仍未提交。Linux777项编译完成，正常归档/输入准备中断，尚无新Linux SDK。恢复后先核原prefix完整性，再正常tools/sdk_inputs.py --target linux-x64-musl --configuration Debug --jobs 6续接增量，不伪造成功stamp；再按7项直接View/包推进。Linux source-manifest快照因后续工具/说明更改已过时，最终验证前重新冻结。

## 当前Dawn/Graphite阶段（2026-11-04，Windows SDK已交付，musl实施中）

- 功能基于b870a3c；当前另会话research提交927d37d保留，不回滚/重写。禁止子代理。RAW17777070a7ebc6418e01462c356743f9b66f1254d7432c1e5b2880a1e9a45ec5及9用户HTML只读；7新增HTML继续untracked、不提交。所有三方适配只从third_party/patches重放，不修build原源。
- third_party/dawn-sources.lock.json：Skia DEPS Dawn ce2a022290e75255f2f5408138123dfacc7ddfd2/archive SHA5b9b1d58fa40f31192ceed6d9cee28abea1e63335b27ab2e25cd9f72251e58ce。上游Abseil cf015c9（非声称与Chromium fork字节相同）、Jinja3.1.6/MarkupSafe3.0.4、EGL/VulkanUtility；复用锁定SPIRV/Vulkan-Headers。OpenGL gl.xml官方精确commit/raw 2774722bytes，SPDX实际Apache2，不是MIT；完整Apache许可随Abseil等入包。extract_prefixes只排外置test corpus/CTS，运行时/gen/include完整，原源4897文件。
- 新tools/dawn_deps.py、recipes/dawn、dawn-private-static-graphics/skia-dawn-prebuilt patches。独立静态Dawn/Tint含WGSL writer，复用已隔离Swift/DXC/ANGLE；key不含graphics-key，防Skia→Dawn→ANGLE循环。Dawn头单独按真实字节hash发build/deps/headers，Cpp/driver补丁不再改变Skia include路径；header inventory缺失可重生，缓存命中不扫描源/hostCMake整树。Skia现use_dawn=true，实际Graphite编译；按当前Ninja目标闭包收12归档，避旧spvtools副本。Windows当前Dawn923bba70c7fc/Skiaa49329c9d203/头81b22c0c0880b2e2a75c；musl Dawn9325cfe8516f、头2c118b4cd7562a3496a3。
- 已受控修CMake Threads作用域/CXX module、WRL只client避免WinRT RoTransformError、Abseil MTd、DXProgrammableCapture声明门控、静态DXC/Swift入口、安全System32加载与Win7无KB绝对路径后备。CompareObjectHandles以3Cpp集中动态探测，缺失仅放弃SharedFence包装复用、继续真实OpenSharedFence/Handle；当前DLL无硬导入，不等于Win7真机已验。私有STL ownership含Dawn/Tint/wgpu/absl、spv namespace/global，修3个返回std的spv函数未隔离冲突；archive直接9项和patch删除overlay重放最终2项0.371s通过。
- Windows native WGSL/64精确绿像素已实际Swift、RTX3060 D3D11+旁置FXC、D3D12+静态DXC、静态ANGLE GLES/GL、真实NVIDIA Vulkan；native-backends-verified.json。有一次Vk误走CPU未当硬件；系统loader修复后硬件通过。系统vulkan_dzn ICD枚举自行加载外部DXC/DXIL，CDB堆栈vulkan-module-stack.log定位；该模式记录系统驱动行为，SW/D3D12/ANGLE仍严格禁独立DXC/DXIL/EGL/GLES/Swift DLL，不宣称Vulkan进程绝无外编译器。
- DawnDevice perView Instance/Device/Context/Recorder，heap回读状态、lost回调shared atomic；上游先Clear并等repeat callback再lost，raw error userdata因而不悬空。首次CPU图像draw惰性上传，perRecorder ImageProvider用SkTiledImageUtils内容/子集+mipmap键、64项/64MiB max(textureSize,pixels*8)估算LRU。淘汰不释放活帧引用，失败标记设备走整帧候选恢复；私有TU禁RTTI，构造移cpp避免不完整GrContext类型。静止复用/滚动0新layout/多View/并发Runtime/resize/增量Canvas/SVG已测。
- Windows证据：before请求9为INVALID_ARGUMENT；缺provider的draw dropped/Compare硬链接真实失败均保留TODO。首次硬件loss全帧精确比较失败，仅红蓝渐变400通道max1/255，测试只该区域红蓝容1、其余精确；恢复另与同后端fresh View全帧精确。fresh对照漏enum cast和首帧scroll BUSY的用例错误已修。windows-stage2八项中Canvas/projection/8轮真实DLL卸载/WebTest local+dawn五项pass；最终三个Dawn View24.81s passed但另一会话新增research文件触发源码门禁拒包。重冻c067d3fcc29e678ffad2d2d23675b76c5ee4c2c9d4fb48db8edefa03d3bb47a3后quick130046Z-81a64610三项pass、正式阶段包通过；不重跑已过五项或full/Release。
- Windows SDK packages/windows-x64/20261104T130220Z-fc9377cc：SDK892479405bytes/SHAa6a6ca0754ab4021789a3c5b3b090878c8ceaf29871f50e3f14b4864e5a51303，symbols158678887/040886f8491af2f7e14c2793d8d082e5aced57af7234a38d63d605ab1000cf09。解包逐文件hash/默认WebTest、额外包内Dawn9 Canvas红+SVG[36,110,165]及删除FXC后原生GL7同像素通过；111导出、FXC原版SHA、无私有DLL硬import、物理重复库0、所有许可/独立符号流式hash+PDB头通过，runtime暂存0/符号展开0。package-windows-x64.json/check_stage.py在build/reports/dawn。静态真实去重299成员117422142bytes；当前SDK归档为该冻结源码，不因后续文档改写旧包。
- musl正常probe70752已终态0，9325cfe8516f；musl-native-verified.json，WGSL/64绿像素、两个非平凡TLS析构、NEEDED仅libc.so。原失败__cxa_thread_atexit_impl实际缺失，而旧工具链静态try_compile误HAS=1；Dawn闭包纳入同锁LLVM23.1.4未修改cxa_thread_atexit.cpp pthread fallback和许可，未改工具链/填stub。上游DSO/主线程析构限制见docs/dawn.md，musl dlclose保留代码映射，不冒充任意libc真实unmap。Python3.8 removesuffix两Python一Jinja受控等价兼容、Windows生成头前后字节相同；不声称MarkupSafe通用3.8支持。
- 36470已终态1：musl Skia777/777编译成功，但宿主Ninja1.10无-t inputs，在归档捕获失败。现tools/deps.py仅对该unknown tool改用真实DOT graph闭包，带空格路径/陈旧外部排除/未知目标失败的双端2项0.220/0.125s过，Windows真实12归档两算法完全同（ninja-real-closure.json），不重建Windows库/包。后续16072（musl-sdk-inputs2.log）正常增量续接，work build/deps/work/linux-x64-musl/skia/6ad732d3c68f/Debug；不要重复起锁。已有root Debug build/work/linux-x64-musl/f80dfd14c4b0/Debug。随后7直接项：Dawn动态/静态/卸载、CanvasCPU/projection、WebTest local/dawn，quick --package；不扩旧矩阵/full。Windows功能已本地提交1eecc30；Ninja后备修改尚未提交，待正常输入闭包和后续平台验收。Linux source-manifest需重新记录，较Windows新增仅工具后备/对应测试和状态文档，C++产品源不变。
- 两端历史静态测试Win38文件12115102208bytes/WSL18文件5378698848已清，保留增量对象/活动依赖。TLS实验435MB副本、接受后的WinDawn静态/native probe四文件1864532480bytes、musl native435708520已清（可由原目标重链），两个根static.symbols映射293569776已清。相应cleanup JSON；ad hoc build/probe/view各3份与重复source探针已清。待Linux阶段完成再收敛stage日志/双端GC3份、清包/ABI暂存、本地提交。不要恢复为保留5份。
- 下一：收Linux编译→公开View实测/阶段SDK/symbols/解包/111ABI/GC3份/本地提交；更新docs/PLAN/TODO及本文件。网页navigator.gpu/Canvas WebGPU、更多codec、原页首帧/时钟DOM重排性能、其余现代Web/媒体/AX/平台与旧系统仍完整目标，当前native后端不能替代它们。总goal active，不mark complete。

## 最近已交付阶段索引（历史验证不重复跑）

- Canvas功能695a5d5、文档b870a3c；source ee8a07ec…，Win/musl相关12项通过，双端SDK/symbols解包/111ABI/0重复物理库/GC3份，证据build/reports/canvas/report.json、symbols-check.json、source-check.json。Win091123Z-6efe95a0 SDK674783204bytes/SHA988adf9205bc2665dd14a974deef42b5e1036d7ad28e2177ae62fbd40fd0579c；musl092041Z-a0b0a638 SDK285858043/SHA79ae5344bb08737518db62fe2e94d7ab89563401ee57d20581b7cfbbe3e4ccae。最后文档只重新配置验证安装规则，没有重新压旧包，新说明随下阶段交付。
- Canvas每DOM节点context/默认300×150与CSS分离、尺寸重置/导航/clone空位图；UI记录状态/path/命令，首次draw才Raster，发布immutable image/COW。矩形/path/clip/transform/stroke/dash、现代色/渐变/blend、ImageData、drawImage Canvas/IMG/SVG、PNG toDataURL再读回均有真实像素。共享TextEngine/ICU租约，普通文字异步呈现准备，同步measure/read/export实际用时才等待worker，零面积文字不加载ICU。bitmap_revision独立layout，刷新原子scene/projection不重建AX几何，current_document_frame查版本防复制旧帧。细节docs/canvas.md，源码src/graphics/canvas_content、document/canvas、script/canvas_bindings；TU禁RTTI匹配Skia。
- 调色板原页首帧通过；原脚本timers持续触发DOM更新的行为测试60s超时如实保留。另首帧后停无关timers的隔离实验，原vDraw3次93.412ms/0新layout，20.82s通过，不能泛化为原页动画性能。Path2D/pattern/阴影滤镜/完整属性和色域、OffscreenCanvas/Blob/完整文字基线仍待。
- SVG/XML/Wuffs阶段fdfbaee；source fdfac9db…，双端9项/SDK/symbols/解包/111ABI/GC3份，完整哈希在TODO和build/reports/images/report.json。Wuffs按Skia DEPS e3f919…/SHAe849dab1…锁定Apache2，Expat MIT；受控真实svg/skresources/expat/wuffs闭包。inline SVG与IMG/内存协议/data、固有比例/布局/圆角/损伤/scroll/AX、path/use/transform/viewBox/gradient/clip/raster/text接不可变ImageContent；UI录SkPicture，渲染不调loader。ICU/字体租约最后释放，八轮真实DLL卸载通过。
- SkCodec显式PNG/JPEG/WebP/GIF/BMP/ICO/WBMP，前6有真实红像素fixture/SHA；GIF仅首帧。首次使用才解码，单图64MiB/16M像素，XML/data16MiB/深256/65536节点、DTD拒绝；Doc64项/64MiB估算不等于全部Skia内存上限，活帧持引用。完整SVG2/内部AX交互/滤镜/动画及更多图像API继续，docs/svg-and-images.md。
- 当前9用户原页只读，其中7新增保持untracked。visibility接SDL隐藏/最小化/合并事件，隐藏rAF与wakeup不忙循环；多View聚合/遮挡待。insertBefore/fragment真实Lexbor实现，details通知；SVG图标页CSS transform视觉仍缺，不能首帧通过称完整视觉。
- 同源动态库体积调查（未改产品）build/reports/dynamic-size-recheck/20261104T054434Z-f6b7757c，docs/library-and-render-performance-2026-11-04.md§10：Linux普通符号表40.49MiB，副本strip-unneeded 106.67→66.19MiB，111导出/SHF_ALLOC不变、libc.so唯一NEEDED，8次WebTest/4对像素SHA同；原副本清0。后续正式安装已采用符号分离，不把两平台文件差全算DXC。

## 当前目标和用户决定

- 总目标仍是完整跨平台Web UI引擎，goal active，不能把子阶段包当作整体完成。用户已恢复推进；不再按旧暂停记录停止。禁止擅自创建子代理。
- 本轮要求Skia各种后端，ANGLE的EGL/GLES、SwiftShader、DXC尽量静态封入；Win7允许可删除可选d3dcompiler_47.dll，DX→GL→软件逐级降级；其他平台同样多备案。依赖闭源商用是底线。
- 所有三方修改只能通过third_party/patches的受控补丁恢复；原始锁源只读，不许手工修build源码。tests/user-dev-tests是用户持续新增网页，保留其原文并用于能力推进。
- RAW人工新增闭源商用底线/补丁/用户网页三行；当前SHA256 17777070a7ebc6418e01462c356743f9b66f1254d7432c1e5b2880a1e9a45ec5。允许正常纳入本地提交，但AI不得改RAW。
- 最新缓存/报告/包保留3份，Windows和WSL都清理，保护活动进程和必需引用。所有可重建输出在根build；删build后应从受控输入恢复。
- 日常直接相关Debug增量，阶段验证通过quick --package交完整双库SDK/WebTest及独立symbols。完整CI留大版本，不重复消费矩阵/全依赖验证。按阶段本地提交；不自动push/tag/publish。

## 当前脱流定位、原生GL/可选FXC与性能阶段（2026-11-04，双端SDK交付）

- 已保留/扩展absolute公开失败用例并实现延迟脱流：普通流/Flex/Grid/固有贡献排除，padding/inline首末/初始CB、四边/百分比/auto/minmax/margin/RTL、shrink-to-fit、嵌套、Flex对齐和Grid既有轨道/命名线；不制造隐式轨道。静态inline锚点不加字符，空inline CB也处理。fixed最后合入不扩大scroll范围、忽略祖先clip/scroll、单独viewport clip，原始bounds与AX可见bounds分别保持；fixed auto z建上下文。AX/命中/文字/投影共几何。docs/absolute-positioning.md，sticky/逻辑轴/transform等继续。
- 用户最新明确Skia原生GL、Windows包必须带可删除FXC、SVG刚需、WebGPU和可闭源商用解码/重复库/首帧滚动优化。原生GL追加enum7/111导出不变，WGL直组装SkiaGL/恢复宿主context，Linux系统EGL desktop GL pbuffer恢复API/context及display引用。Windows requested7/active7真实像素/多View通过；musl缺libEGL/libGL，真实回退Swift，不能算原生GL已成功。
- third_party/redist/d3dcompiler-47锁SDK8.1原版x64 6.3.9600.16384/4173928bytes/SHA e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4、Microsoft签名Valid、官方application-local再分发依据。CMake先验SHA，SDK/WebTest包强制包含bin/d3dcompiler_47.dll及NOTICE/source.json；不手改三方。graphics.optional_fxc真实应用目录ANGLE D3D11/FXC，失败FXC回退和静态DXC共3项3.99spass。删除本地copy仍可用独立系统FXC；都无可用时继续GL/软件候选，无硬import。
- 用户原页和RAW17777070…未改；新示例2仍用户未提交。SVG/XML/Wuffs/Dawn配方仍false/网页SVG未实现，下一阶段必做真实闭包和网页资源入口，不能开关冒充支持。此处未缩总目标。docs/graphics-backends更新旧“不复制FXC”错误说明。
- 性能先范围剔除，再有边界证明时单tile直接Skia渐变；其余走原procedural shader，不分配大位图。正负tile平移/圆角/线性/Oklab alpha/径向/锥形对照，CPU54–65通道max1/255，GPU精确；测试改为实测量化界限，不谎称全页哈希相同。私有timing副本复杂页first2715.59ms，layout2080.38（text1070.57/2298次、style459.51/5544次、AX297.28嵌套），3scroll849.18其中box779.14、0layout/shape。quiet同3步CPU复杂页284.64→164.29/首帧2648.67→2511.20ms，D3D12 40.16→36.71/4464.98→3975.51；首页CPU137.35→139.97未改善。全部GPU页及首CPU页hash同，其他CPU两页hash异，不声称整页差值界限。baseline部分later并行musl编译；未当受控benchmark。证据absolute/performance-comparison.json及docs/render-performance-stage-2026-11-04.md。
- 包内物理重复库新门禁（symlink只计别名）/打包工具6项passed，安装ELF --strip-unneeded加debuglink/独立symbols。Windows静态合并移除95完全相同成员/117871195bytes；不是删后端。计时scratch初次批量/数组删除被策略拒绝，逐个固定路径删除成功，已清全部DLL/PDB606089216bytes/obj/副本及空目录，保留报告。
- 首冻e745197f…quick 064707Z-6d468915测试时终止（确认Python22636树，handle19830完整1，无包），新增fixed AX offscreen真实failed期望0得40；修独立viewport clip后CPU/Swift2项2.45spass。报告已标failed，不作通过。初始before/第一次编译warning/fixture border-box预期及测量相对路径失败均保留absolute日志。
- 最终source0e51d13e00529905533e3325c567b78d1647f72320bab4680500f50f727c87d0，absolute/source-manifest.json。Win quick065043Z-f17b4d1c/2990的14项38.08s；musl child065540Z-902b3c72（wrapper065535Z-fa0eb511）/11271的11项28.17s，均完整退出0且无skip。范围absolute动态/静态/真Swift、backgrounds CPU/Swift、projection/atomic、backend nativeGL或真实回退、WinFXC三项、WebTest三项。前相邻position/stacking/inline/Flex/Grid直接10项23.26s已pass复用；无full/双配置/移动消费/全依赖矩阵。
- Win SDK065251Z-1aa3ca04：665599538bytes/SHA3890b4a5216541756c7d6cbfeb893e3f37b4131ef7edee1f2010c2655135a458；symbols117198230/bdb19e7292616c7065d69f3559891107e401118144120845122ef07aa22e452f。build/包DLL c9ac46d8ccd9cdcf6ce13b9275473cc35f22915944df13cbb709bf68c5accb90，111精确导出/系统import无FXC/DXC/其他可选GPU硬导入。包内实际WebTest opengl active7、ANGLE FXC active3/compiler1、删除本地FXC后GL均passed，唯一FXC SHA锁一致。物理重复库0、重复许可证35863bytes。
- musl SDK070222Z-c89d5de4：276281712bytes/0c141adbeb5c1c593f8c489c61541cfeb9f13a8e7215a0d6c9dd40cfc8b59185；symbols78805296/4910b7d0bf7f91bc50ec822e3682cad423f94055080c01a523879969b01ca452。buildSO1476eb7b…，strip-unneeded包SO94e05efaa816109a90b1f5e0eba5902ab9af06e7804e12b0686294e71d8f1804/69767664bytes；111动态导出/NEEDED仅libc.so/所有SHF_ALLOC属性地址大小及字节哈希逐项同原构建。静态精确去重85成员/67121472bytes，物理重复库0、重复许可证42069bytes。
- 两端实际解包逐文件hash/WebTest/111ABI passed，absolute/report.json及两包native-abi.json。4梯度×15窄条重复tile重绘对全图精确，不因新shader快路产生差异，tile-damage-before.json。Windows→WSL GC4512完整退出0，gc.json，两平台各3阶段目录，包/ABI/原生/计时scratch均0。RAW17777070…及三HTML哈希未变，新示例2仍用户未提交。按此阶段本地提交（hash见git log -1）；不push/tag/publish，Debug/production_ready=false。总体goal active，下一优先SVG/XML与网页图片/Wuffs，随后完整Dawn/WebGPU/其他解码、首帧intrinsic/styles与全PLAN；不能以本阶段结束总引擎。

## 已交付历史索引（完整证据见TODO/docs/git，不重复旧CI）

- b4b7fcf层叠：PaintCommand独立DOM阅读序，负/auto/0/正contexts、Flex/Grid order/匿名项/outline；Paragraph.parts标owner，atomic展开主帧保留placeholder，不截留提升后代。Raster/PaintScene/投影同display，pointer逆序/文字遮挡/wheel最近可见目标，AX/复制保持DOM序。docs/stacking.md。selector-retry旧补丁误跳右侧subject使details:nth-of-type(2) p选SUMMARY；受控方向forward沿prev/backward沿next后是P。原selectors.c=锁tar逐字节fc99be75…，patch b83c40ab…；Windows/musl删除重放各9项pass，Lexbor Debug prefix d07b9f69740b/c4601efb8251。
- 层叠样式100ms超时真实115.59；仅Candidate预留、真revert-layer建回退表、Scope一次构键、string_view借稳定声明/AST/同代驻留属性名（持久map仍拥有键）后组合91.08/class81.14ms成功，预算不变。首两轮失败/中断/源门禁在TODO，不当最终。source8afb7bad…两端12项21.19/16.33s及SDK/symbols/解包hash/WebTest/111ABI/GC3份passed。旧SDKWin055719Z-d601f146/musl060501Z-e023465d；全部SHA/源manifest见TODO/stacking/report.json，此为历史不代当前。
- 8a1e717相对定位：position/inset独立margin，继承绝对值保留百分比类型；block/Flex/Grid/atomic正常流后视觉偏移，普通inline分片/整形/装饰/hit/caret/AX共享偏移。flow_bounds保留原流溢出，纯scroll共享投影。GPU圆角整数平移不能承诺精确（矩形0差、圆角18通道max31/255）；同位保留/重排/新View精确。docs/relative-positioning.md，双端阶段包/111ABI/GCpassed。
- 18c61fc背景：linear/radial/conic/repeating、现代颜色/预乘alpha/提示、层及position/size/repeat/origin/clip/attachment、根/body传播与圆角/inline/atomic/损伤/scroll。Paint缓存64项/4MiB（不是全部GPU预算），不分配巨tile；浏览器30例28代表点≤1/255，提示/硬边差如实留docs/backgrounds.md。URL图片/clip:text/混合/完整slice/动画继续。双端阶段10项/包/ABI/GCpassed，当前新增单tile快路径见新阶段。
- c413fd8滚动投影：FrameProjection静态几何与scroll祖先/各clip分开，纯scroll同revision/尺寸0layout；原子scene不可变，绑定不同不混cache，多View隔离。Lexbor empty-inline-style受控修复color-only删除空cascade，删除重放两端各9项pass。纯滚动三页与旧DLL像素一致，复杂CPU52.53ms/真实D3D12+UIA轮96.06ms，不当60Hz。docs/scroll-projection.md，双端11项/包/ABI/GCpassed。
- 库组成/原页卡顿：docs/library-and-render-performance-2026-11-04.md及reports/library-and-rendering/20261104T014346Z-7e573211，冻结原DLL9/计时9/原生9/控制1共28组。原示例2每scroll布局1622.4/总1648.1ms，shape937.6、样式287.6、AX189ms为包含关系；原第三页320ms定时DOM导致静止layout，不改HTML。Windows静态2903.58MiB、DLL124.41，DXC39.04%/Skia17.89/Swift14.94/ANGLE10.69，static debug44.94%；非当前包统计，不能以删后端缩目标。
- 76f223e现代颜色15空间/mix/light-dark/scheme与变量共缓存，37例36≤2/255，Rec2020超SDR差3；relative color/HDR/完整系统色后续，docs/css-colors.md。
- 4d14f6f嵌套：作者原文只读token编译，&/隐式关系/:is最大specificity/交错声明/media/layer/伪元素；右侧候选索引、变量环境合并、声明2048项/2MiB键缓存，超额当代存储；先清cascade后回收AST。完整CSSOM/条件组待。docs/css-nesting.md。
- 12dc8c5圆角：椭圆/百分比/线性calc统一paint/clip/hit/AX；CPU conic按tile截断会变覆盖率，stabilize_curves扩相交曲线闭包，60窄条重绘精确。媒体/layer支持重要性反向/revert/变量缓存及MQL/resize合并，宿主偏好覆盖。docs/rounded-boxes.md/media-queries.md。完整inline slice/container/supports/forced color等待。
- 8ab3e8a计数器/引号与a028a1d作者伪元素：counter/reset/increment/set/reversed/quotes/q、before/after私有树/变量/attr/AX/选择；ICU79源数据生成907locale合并123/9589bytes引号表在third_party/data，不触发ICU解压。Pseudo构造分配/映射事务/旧字符串释放已故障注入及64轮验证。docs/generated-content.md/pseudo-elements.md。
- details/summary：开闭/name互斥/反射/键盘及AX/UIA展开通知、合并toggle任务、主pointer和点击/拖选/取消，Skia默认marker/无summary文本不注作者DOM。CSS变量重要性/继承/循环/回退/@property后续范围见docs/css-variables.md/details.md。
- 静态图形历史：Windows五候选实际像素/多线程/宿主WGL恢复/失败恢复通过；静态DXC并发HLSL→DXIL无DXC/DXIL DLL，SM6 D3D12实际选compile_dxc，旧设备可选FXC。muslSwift真实像素/RenderExecutor线程、缺字保留和0新Raster线程通过。图形完整full与旧平台未验；当期keys/完整哈希在TODO与docs/graphics-dependencies.md。旧清理策略拦截不绕过；常规GC依3份执行。

## 图形与第三方恢复实现

- tools/merge_archive.py以(字节数,SHA256)精确去重COFF/ELF成员；保留不同同名对象和Windows原.res，BSD长名输出交LLVM索引/隔离；C/C++私有定义改名，标准ABI/RTTI保留，D3D12MA/spirv_cross/Vma/ANGLE私有模板纳入识别。tools/test_archive_merge5通过，真实静态checkable像素消费通过。
- Lexbor/SDL补丁由recipes移到patches，ICU数据redirect也JSON化。dependency_patches.py检查受控路径/次数，文件从原始源重新生成overlay，不在已补丁文件上叠改。cmake_build_identity记录runtime补丁hash，ICU数据stage=data不会重建ICU代码；data generator另记data patch/driver。
- dependency_support给锁源存文件清单；旧cache从归档推导清单，缺头/源码重新解包；overlay补缺后重放patch；安装prefix缺文件由install清单触发重装。quick每次resolve精确SDK输入，不只看CMakeCache。Windowsrename临时占用有界重试+锁内复制退路，成功后才写标记。
- Skia recipe集中third_party/recipes/skia.json，Ganesh/Graphite Vulkan、OpenGL，WindowsD3D12实际编译。VMA/D3Dallocator/Vulkan-Headers/SPIRV-Cross按Skia DEPS锁源码。FXC按需绝对路径加载，DXGI debug/D3D12Serialize按需系统加载，现代接口声明兼容patch。默认View仍Raster，显式多层运行时/丢失恢复已接入并在Windows实际验证，详docs/graphics-backends.md。
- tools/graphics_deps.py维护ANGLE/SwiftShader配方；chromium-zlib-utils是仅4个精确官方文件的file-set锁，下载/哈希/来源/许可证可从build空恢复，避免拉完整Chromium。所有三方修改均patches，不能手工修overlay。
- 工具定向当前archive5、patch5、dependency14、focused引用2、bootstrap7+1平台skip、Python3.8语法/管理检查通过。未跑full，不重跑全套只为保险。

## 新网页与事件实现

- tests/user-dev-tests有纯HTML与CSS示例1.html及观察和现代Web特性的差异.html。测试不改文件；自动发现HTML/HTM执行WebTest --check，仅证明脚本初始化/首帧，不能从unsupported计数0推导视觉或API全支持（后者CSS有@layer等尚缺）。
- Label代理到隐藏checkable、radio按树/name/form owner互斥、取消恢复/同节点递归抑制、动态:checked查询和样式，保留作者AST权重、不写假的checked attr。AX_RADIO和动作共用真实激活入口。radio初始化/动态重分组/方向键、完整select/range仍待，docs/checkable-controls.md。
- 原生与合成Event/EventTarget/CustomEvent共用派发器；capture/once/passive/对象callback/stop/取消/重入和重派发。Node继承，native可信/脚本重派发不可信，ClipboardData同步租约继续。监听器异常仍向调用者传播，完整错误事件/AbortSignal/ShadowDOM待补。
- Document任务以单调时钟排队UI线程render执行；timeout/interval/rAF，1024项上限、单轮64/100ms、interval最小1ms，任务内新任务下一轮。Promise job/queueMicrotask清空；导航释放回调；next_wakeup_ms API供宿主合并等待，静止未到期不忙画。rAF约16ms最早，不等于vsync。docs/script-events.md及tests/script/events_tests.cpp。
- DOM fragment/clone/innerHTML写、旧节点detached到导航结束、children/first/last/parentElement、classList/dataset、style对象稳定/get/set/remove/priority/camelCase、基础非文本value/select、getElementsByName。集合快照；模板content、完整selection/controls/SVG/Canvas/Intl/storage等仍待。
- screen原生读SDL显示模式，离屏无关联为0；当前CSS坐标=drawable像素，DPR1，完整DPI后续。WebTest先attach View再执行脚本给初始视口。不要伪造现代探测结果。

## Windows AX与生命周期要点

- UIA初始阶段104导出；当前总111，以当期ABI报告为准。native.h平台接口64项/4MiB队列；非Windows明确UNSUPPORTED。UIAutomationCore仅系统按需加载，可选Disconnect/Notification探测。自有HWND subclass WM_GETOBJECT，借用只宿主转交，不改WndProc。
- UIA工作线程只读不可变AX树，render所属UI线程执行动作；2s未开始取消、导航document身份守卫、Value走真实编辑beforeinput/input/maxlength/history/readonly；焦点需前台/Win32线程焦点。native订阅不抢宿主公开队列。
- 关闭停队列唤醒等待者、逐provider断连，保留接口失效；对象计数0且无在途调用才FreeLibrary。同步SendMessage中关桥/销毁BUSY避免COM死锁。测试实际UIA查询/动作/通知和三轮DLL卸载已通过，未把旧系统/真实读屏当已验。
- 旧CUIAutomation在新系统注册Notification可S_OK但不投递；测试优先CUIAutomation8才真实Ready通过，旧系统fallback仍需策略。密码UIA客户端可把缺值转空，判断IsPassword/不泄露，不强求HRESULT失败。
- AXUpdates不可变树+增量事件/焦点/UTF16选区/live，8批/64MiB积压溢出RESET；导航/重启RESET，live密码mask/busy/atomic/relevant/off。完整AccName/关系/TextPattern/平台选区、MSAA/IA2/AX宿主子树与macOS/Linux/Web桥仍待。

## 稳定底座与历史索引

- 仓库XLWebEngineUI/（当前检出目录），master，Win11+WSL Ubuntu20.04/WSLg。Clang/LLVM统一，Windowsclang-cl/LLD静态MT/MTd；LinuxClang/LLD+musl/compiler-rt/libc++/abi/unwind，不用Zig。当前WindowsLLVM23.1.4、WSL23.1.5按锁恢复，Linux正式目标musl，glibc-dev不可冒充。Win7/UOS/macOS/Web仍未实机验收。
- SDL3+Skia、Lexbor3.0.1/QuickJS-NG0.18已接，不改Chromium整包；公共C ABI/C++20不暴露三方。静态合并私有闭包/符号隔离，shared精确C导出；静态/动态SDK不能同一最终应用混用。
- Runtime UI线程拥有Doc/View/mount；View借Doc，存活阻止销毁Runtime/Doc。借用HWND/XID不拥有宿主窗口，X11销毁包装XSync。宿主事件循环，持久surface/旧新墨迹损伤/局部上传/静止不画/曝光复用。布局样式目前全量后损伤差分，GPU纯合成后续。
- UI引擎不实现HTTP/TLS。xl-webengine-ui://按Runtime隔离挂载，不可变content可共享，宿主读文件/网络，按逻辑源位置解析。WebTest默认CWD/index.html、惰性本地相对依赖、无服务器，压缩Roboto回退内存，musl带loader启动器。
- 资源首次用才内存解压不落dat、不在创建启动线程；显式预热、有界池/背压/合并加载/租约/取消，分配器上下文活到最后请求/租约。ICU注册/末清用显式计数互斥，段落持令牌，先purge私有SkUnicode再u_cleanup释放数据。
- Chromium common ICU原profile/33映射和许可在third_party/icu-chromium，使用ICU79.1数据：33558160→10470976 raw/2343项→4362007 zstd；raw SHA5d16b1182fafdc1e8ee5318c9acc37929ea12a7680df858471bf9d394618410e，patch迁移后字节保持。资源懒加载/线程语义不变，不退全量。
- 行内文本跨DOM整形/换行/UTF16索引、六white-space/font/盒边框/outline、Flex/Grid/统一尺寸、overflow/View独立scroll、可见范围/hit/caret/AX共几何；CSS变量/伪元素/details已分阶段接入，圆角见首节；sticky/完整定位与布局等仍待。
- 编辑统一Editor实时value/UTF16选区/组合/64项4MiB历史，beforeinput后核focus/revision/连接/readOnly，程序赋值不发input；IME预编辑/提交/取消、password mask、剪贴板同步事件租约/跨View重入BUSY。SDL原生队列有界、caret转逻辑坐标。真实IME/剪贴板跨进程、富文本contenteditable与完整输入仍待。
- 旧提交索引：8eba9b1 ICU裁剪；eeb6fd9 outline；9ad5bae AX动态；265e558库体积调查(docs/library-size-analysis.md)；1cc8cca focus；ee20950剪贴板；8b9c960编辑；dfcdb36尺寸；更早布局/视图证据见TODO/git。旧full已验，不能挪到当前指纹，也不要无故再跑。
- 总目标待：其他平台GPU/硬件与旧系统验证、零拷贝、DPI、原生/纹理/视频混排和AX子树，WebM VP8/VP9+Opus/Vorbis/时钟/字幕、现代Web/CSS/富文本、其他平台AX桥、macOS/Wasm与旧系统实机。持续根据PLAN推进，不缩减范围。

- 历史滚动阶段的双端SDK/111ABI/GC完整哈希及运行号见TODO的2026-11-04滚动投影记录和build/reports/scroll-projection/report.json，不能拿旧包替代当前定位阶段。


## Gecko/Chromium调查（2026-11-04，非引擎实施验收）

官方最新稳定Firefox159.0.1/Chromium157（Win157.0.8103.39），主线Gecko161/Chromium159。docs/gecko-chromium-comparison-2026-11-04.md与tools/compare_browser_compat.py固定BCD8.1.8/29768a4c/CC0及双哈希；20933节点，桌面6091分支差异+764注释另表，核心七类4807。全表/归档/verification/GC在build/reports/gecko-chromium-comparison/2026-11-04。82项摘要含Firefox159已启滚动动画、Serial151/Anchor147，不用旧缺口。开发版仅数据估计，WPT请求超时；没有浏览器/性能/旧系统/SDK新验收。Dawn及完整PLAN继续，RAW17777070…只读。
