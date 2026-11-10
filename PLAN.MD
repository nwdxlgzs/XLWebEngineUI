# 日常实施计划

更新时间：2026-11-10（Asia/Shanghai，仅仓库交付维护）。原始基线为只读的 `RAW-PLAN.MD`。

2026-11-10用户随后明确改为公开仓库。nwdxlgzs/XLWebEngineUI现已公开，认证API核实private=false、visibility=public，无认证Git读取main成功。同步交接记录并推送；原引擎开发保持暂停，不运行构建或完整CI。证据build/reports/repository-publish/20261110/public-visibility.json。

2026-11-10用户授权清理敏感信息并新建远程仓库推送。本次仅做源码交付：按默认GitHub账号创建私有XLWebEngineUI仓库，当前检出原已无.git，建立清理后的新main初始提交，不伪造旧历史。文档本机绝对路径改为相对路径，补本地凭据/诊断输出忽略规则，作者使用GitHub noreply邮箱；扫描实际待提交文件和最终提交，保留原始计划、用户网页、三方原许可/锁源/必要再分发DLL，排除build和本地凭据。远端创建/推送后核仓库可见性、分支commit与源码树；原功能开发继续暂停，无功能构建或新SDK结论。

2026-11-09/10独立C盘缓存维护已完成：按用户指定仅清Go编译、pip/npm下载、npx、NVIDIA/DirectX、VSCode缓存及>7天且无占用的用户Temp，旧WSL swap单独核停机/独占后清除；保留近期文件、工具链/runtime、Git目录、链接、只读/锁定文件及用户配置。C盘观测空闲净增约44.83GiB；逐文件范围/数量/字节核验passed，报告build/reports/c-drive-cache-cleanup/20261109T155301Z/report.json。维护不恢复暂停开发，不做项目构建或SDK交付。

2026-11-09独立WSL维护已完成：先核活动任务与磁盘占用，仅清临时副本、APT可重建缓存和超额日志，保留工具链/增量目录/用户配置；fstrim、关闭、管理员DiskPart离线紧缩后验证Ubuntu启动和读写，再关闭。净回收7.67GiB，报告build/reports/wsl-maintenance/20261109T153701Z/report.json；不恢复下述暂停开发，不把维护作为功能验证或SDK交付。

用户现已暂停。当前WSL输入准备已中断并确认终态；Windows已验阶段SDK/提交保持，Linux公开View/SDK仍未完成。恢复后从正常增量输入准备继续，不重复777项编译或已验Windows包；重新冻结后再做7项Linux直接验证与阶段包。

musl Skia777项已编译通过，归档捕获在宿主Ninja1.10缺inputs子工具处真实失败。已为一线构建脚本增加旧Ninja graph依赖闭包后备，仍只收当前目标可达归档并排陈旧/外部Dawn库；未知目标继续失败，不能退回全目录搜库。双端两项真实Ninja fixture通过，当前Windows实际12归档在两种算法下完全一致，不重建或重压已交Windows包。正常musl sdk_inputs增量续接后继续7项View/阶段包，保留此前全部真实失败记录和用户文件。

Windows Dawn阶段SDK已交付：source c067d3fc…、quick130046Z-81a64610、packages/windows-x64/20261104T130220Z-fc9377cc，解包默认WebTest/hash、包内Dawn9与删除FXC后原生GL7的Canvas/SVG像素、111导出/许可证/独立符号/0重复物理库均passed，证据build/reports/dawn/package-windows-x64.json。先本地保存该已验证平台阶段，继续当前musl首次Skia Graphite编译和7项直接View/阶段包；不因补记状态文档重压Windows包，其冻结源码与包内说明保持原样。新状态文档随后续包安装。网页WebGPU与首帧/滚动等总目标不变，不把Windows阶段等同跨平台验收。

当前续接：Graphite缺省不上传CPU图像，新增每Recorder独立、首次draw上传的ImageProvider，内容/子集/mipmap键与64项/64MiB估算LRU，失败触发整帧后端恢复。Windows软件View动态/静态与八轮真实DLL卸载已过；硬件丢失后实测仅渐变400通道最大1/255，限定该区域/通道量化界限并另与同后端新View全帧精确对照，不放宽Canvas/SVG或所有网页。musl现有libc++错误探测TLS接口存在，独立实验复用锁定LLVM原始pthread fallback已真实WGSL/像素通过；将其作为Dawn私有闭包的一份兼容对象，锁源/许可纳入身份，不手改工具链，不以弱stub跳过线程析构。正在原配方验证双工作线程析构和native probe。随后Windows8项、musl7项（Dawn双库/卸载、Canvas/scroll直接相邻、WebTest local/dawn；Windows另硬件）顺序Debug quick --package，包内FXC/111ABI/库去重/许可与解包WebTest，双端GC3份、清验证暂存和本地提交。无full/Release/旧矩阵重跑；网页WebGPU与性能/平台总范围继续。

当前Dawn/WebGPU阶段（基于b870a3c）：先按锁定Skia DEPS ce2a022290e75255f2f5408138123dfacc7ddfd2核Dawn源码与全部实际启用传递依赖的来源/哈希/原许可，保留闭源商用要求；不拉整个Chromium，不靠关闭目标后端规避依赖缺失。独立受控配方构建Dawn/Tint的静态闭包，复用现有Clang、DXC、ANGLE/SwiftShader与锁定头文件；适配只进入third_party/patches。明确Windows可选FXC/静态DXC、Vulkan/OpenGL/旧系统能力探测及驱动加载边界，避免Dawn引入现代系统或新DLL硬依赖。Skia Dawn使用Graphite，因此为内部GpuDevice补录制/提交/读回与丢失恢复适配，不能把Ganesh接口套用后仅开启开关。先真实WGSL编译/设备/纹理像素，再接View/原始网页、静止/局部/多View及真实回退；追加后端枚举保持原值/结构/公共导出。网页navigator.gpu/Canvas WebGPU桥接另按真实API与异步生命周期推进，不以原生Dawn成功冒充JS WebGPU完整支持。日常只相关Debug/新依赖必要构建验证，验证功能阶段quick --package交双库/WebTest/symbols、解包/ABI/许可与重复库审查、Windows/WSL保留3份清暂存、本地提交，不扩大full/Release/全消费矩阵。RAW/9用户HTML只读，Canvas/CSS/性能和全平台总目标不变。

Canvas功能阶段695a5d5已交付source ee8a07ec…双端12项/SDK与symbols/解包/ABI/GC，当前后续仅将Canvas说明加入安装清单、更新SVG说明中的历史缺口文字。只配置既有Debug树并核生成的安装规则，不重编功能/重跑12项或重压同一功能包；现有归档仍准确标识其原冻结输入，新说明随下次功能阶段包安装。其余Canvas规范边界、Dawn/WebGPU、背景图像API与原页首帧/DOM失效性能继续总目标。

当前Canvas2D阶段（基于fdfbaee）：以上阶段调色板getContext真实失败和新增公开像素测试起步。私有画布按DOM节点独立，默认300×150/HTML位图尺寸与CSS显示尺寸分开；首次绘制才分配有界位图，尺寸重设（包括相同值）清位图/路径/状态，导航销毁状态。UI端维护绘制命令、矩阵/路径/裁剪/状态栈，呈现前增量栅格化为不可变图像；渲染线程不碰JS/可变画布。接真正矩形/清除、路径/圆弧/曲线、线型、save/restore/变换/裁剪、颜色/透明/合成及线性/径向/锥形渐变，复用现有replaced布局、圆角/损伤/scroll/AX。fillText通过既有文字整形与ICU租约；脚本可先记录文字，首帧按需异步准备资源后执行，不能初始化统一解压或用假字形。同步measureText/像素读取、图像绘制与导出、Path2D/阴影滤镜等继续完整Canvas目标，必须分别有真实证据，不因调色板过首帧就当完整Canvas完成。验证命令顺序/状态与路径区分/透明像素/跨帧复用/多View和Runtime/静止与滚动/作者原页，必要CPU/Swift与双端Debug阶段SDK/独立符号、解包WebTest/111ABI/GC3份及本地提交。RAW和用户网页只读，不扩full或重建三方。

Canvas已接同步measureText/ImageData读写、drawImage的Canvas/IMG/SVG与裁剪缩放、PNG导出再解码，源快照隔离；普通fillText延迟到呈现准备，同步读/测量才等待首次有界资源worker。单独位图版本刷新普通与嵌套atomic图像，不重建布局/AX几何，scroll后对照强制reflow通过。调色板原文首帧exit0；原行为实验后续受作者时钟DOM更新影响60s超时，保留真实失败；另在首帧后停无关定时器的受控实验，原vDraw三次93.412ms/0新layout通过，不据此声称原页实时性能。最终仅Canvas动态/静态/Swift/原页行为、images/projection/atomic/动态卸载/events及WebTest local/user/gpu直接Debug，Windows→musl quick --package；默认包demo加入实际Canvas绘制，不扩full/双配置/全后端矩阵。冻结后解包hash/默认WebTest及Canvas真实像素/111ABI/重复库与双端GC3份，本地保存；未实现Path2D/阴影滤镜/完整Canvas属性/图像平滑/WebGPU与其余完整引擎目标继续，不以阶段包替代。

当前SVG/XML与网页图像阶段（基于c4d863f）：先公开SVG像素失败，再锁定Skia DEPS对应Wuffs源码/内容哈希/许可，受控启用SVG、XML、GIF解码并实际构建svg/skresources等归档闭包，复用其他依赖和Debug目录。私有不可变图像内容承载SVG录制结果和栅格解码，UI线程按首次实际使用打开xl-webengine-ui://内存内容，不引入网络或文件回退；引用存续保留编码数据/字体会话，解码尺寸与缓存有明确预算，多Runtime独立。接普通inline replaced/块/Flex/Grid/脱流的固有尺寸与宽高约束、背景/内容/边框顺序、圆角/overflow、损伤/静止/scroll、命中和SVG/IMG的AX名字。inline SVG保留作者DOM，私有SVG快照可合入已解析CSS呈现值，禁脚本/网络副作用。测试shape/path/transform/viewBox/preserveAspectRatio/use/gradient/clip/image/text与动态DOM/共享资源及常用PNG/JPEG/WebP/GIF/BMP/ICO真实输入；完整SVG2、动画图像、全部滤镜/网页图像API及Dawn/WebGPU继续总目标，不能以编译或首帧冒充完成。仅相关双端Debug/阶段SDK/symbols/解包WebTest/111ABI/GC3份、本地提交；RAW与用户HTML只读，不扩full。

SVG/XML、skresources与Wuffs已在两端实际编译并接入网页绘制，不再处于下列历史记录中的关闭状态。图像CPU/Swift、文字/ICU寿命及动态卸载、隐藏窗口/rAF与insertBefore直接验证已通过；新增用户原页首帧8/9通过，调色板在真实getContext('2d')缺口失败，SVG图标页仍有CSS transform等视觉缺口，不能用首帧通过代替视觉与行为验收。现冻结源，仅images动态/静态/Swift、直接受影响absolute/scroll_projection/dynamic_load、events_tasks及WebTest local/gpu共9项，两端复用Debug做quick --package；默认包示例加入外部SVG。解包/hash/WebTest/111ABI、物理库重复及模块/许可、Windows和WSL GC3份后本地保存。下一阶段以调色板原文驱动真实Canvas2D，再推进背景URL/网页图像API、Dawn/WebGPU和其余完整PLAN；不改原页、不提供空Canvas探测接口，不重复已过相邻矩阵或扩大full。

2026-11-04用户再次明确：Skia原生OpenGL必须有独立View路径，Windows包必须旁置可删除的d3dcompiler_47.dll，SVG是必需功能，WebGPU/Dawn及允许闭源商用的解码器继续完整接入。当前WGL/系统EGL候选追加枚举7，旧值/结构/111导出不变；Windows真实active7、像素/宿主WGL恢复/多View通过，musl只证明驱动缺失时安全回退。SDK8.1原版x64 FXC 6.3.9600.16384及来源/哈希/有效Microsoft签名/再分发依据在third_party/redist，安装及阶段/WebTest包必须包含；应用目录真实加载、失败降级和静态DXC独立性已直接验证。物理重复库成为阶段包门禁，ELF安装副本strip-unneeded保留独立符号/debuglink。原页首帧/单步滚动仍卡，先保留before、剔除无交集绘制，再用私有计时副本定位剩余瓶颈。SVG/XML/Wuffs/Dawn当前仍关闭且网页SVG未接入，下一阶段须锁定传递源码/许可/哈希/补丁、编译真实目标并接网页资源路径/实际绘制，不能只改true或以编译代替支持。RAW与用户HTML只读，直接Debug/阶段包，不扩full。


脱流、原生GL/可选FXC、绘制范围剔除及单tile渐变快路径已直接通过Windows相关检查；musl的脱流/后端回退已过。原始复杂页同3步CPU滚动284.64→164.29ms/首帧2648.67→2511.20ms，D3D12滚动40.16→36.71ms；首份CPU页未改善。原页未改，GPU三页最终hash一致，CPU两页hash变化，梯度专门fixture最大1/255，不冒称全页逐像素界限。计时首帧仍约2s布局，后续优化继续。当前冻结本阶段源/PLAN/docs，仅absolute动态/静态/Swift、backgrounds CPU/Swift、projection/atomic及原生GL、FXC三路径和WebTest三项做最终双端Debug quick --package；前次相邻position/stacking/inline/Flex/Grid已pass复用，不再全矩阵。完成解包/hash/111ABI/物理重复库审查和两端GC3份后本地提交，再继续SVG/XML/网页图像及WebGPU/解码器和首帧布局优化，完整目标不变。

首冻e745197f…最终复核新增fixed视口外AX失败（期望0得40）；已确认Python22636后终止其CI树，19830完整退出1且无包。固定帧合入前增加独立viewport clip，原始框几何保留，AX可见区域与offscreen正确，纯scroll不移动它。重新冻结后做同一直接阶段范围，不扩full或重跑已过相邻矩阵。

当前推进脱流定位（基于b4b7fcf）：absolute/fixed从普通流、Flex/Grid和固有尺寸贡献中排除；私有延迟定位记录保留来源/静态位置，待真实包含块padding盒或inline分片尺寸确定后计算四边、百分比、auto尺寸与margin、min/max及RTL过约束，递归布局自身内容。fixed以viewport为包含块，在viewport滚动/裁剪完成后合入，不参与页面滚动范围；absolute跟随实际包含块滚动并按其裁剪，跨中间静态祖先不错误叠加clip。静态inline锚点不改变行高/换行，原子/跨行inline包含块及Flex/Grid脱流边界共同记录，绘制/命中/选区/AX和纯滚动复用层叠/投影。先公开失败证据再直接Debug，阶段SDK/symbols/真实解包WebTest/111ABI/GC3份与本地提交；按实际范围记录未完成机制，不以此替代sticky/transform/逻辑轴/全部现代Web和旧平台目标。RAW与用户HTML只读，不扩full或三方重验。

2026-11-04补充动态库同源复核：相对定位两端source bbb5c31f…，Windows DLL125.10MiB、Linux SO106.67MiB，但代码节94.46/42.61MiB；Linux普通符号/字符串表40.49MiB导致总文件大小看起来接近。副本strip-unneeded后66.19MiB，111动态导出及全部SHF_ALLOC不变；包内WebTest/loader的三原页Raster与示例2实际Swift共8次首帧、4对像素哈希相同。原包/产品/发布脚本未改，无依赖重建/新SDK/full；后续治理安装SO非运行时符号分离，不能把跨平台文件差额全归DXC。证据dynamic-size-recheck/20261104T054434Z-f6b7757c、docs/library-and-render-performance-2026-11-04.md第10节。

当前推进层叠绘制与命中（基于8a1e717）：按CSS2.2 Appendix E与Flex/Grid绘制顺序，建立独立于DOM阅读/文字存储顺序的私有绘制命令。position与z-index/auto/负值/0/正值、isolation及Flex/Grid order共同决定真实层叠上下文；非上下文的position:auto与原子inline不能截留需要参与祖先层叠的后代。普通inline按实际行分片输出装饰/文字，原子内容展开到共同绘制帧，场景及滚动投影保持不可变；背景、文本、轮廓、裁剪、旧新损伤与静止复用共用命令。反向命令命中、文字重叠选择和滚动遮挡同步，AX/键盘/DOM阅读顺序保持原义。先公开像素失败证据再实现端到端行为，必要双端Debug、阶段SDK/symbols/解包WebTest/111ABI/GC3份、本地提交，不扩full或依赖重建。absolute/fixed脱流/包含块、sticky、opacity/transform/filter/混合及完整Web/平台目标继续；RAW/用户HTML只读。

层叠首轮Windows六项通过，扩展回归已修复空blob、真实文字片段命中及native legend格式化归属。原details检查另暴露既有Lexbor补丁误绕右侧subject，旧/新DLL同错选SUMMARY；受控selector-retry回查方向修正后，details/嵌套两端通过、补丁删除重放各9项通过，原始三方文件未改。新selector fixture仅做查询，实际文字渲染在字体准备后验证，不拿首次字体使用误判为懒加载违规；修正后的层叠CPU/真Swift两项7.25s通过，临时诊断已恢复，单文件清理探针完成（递归清理曾被自动审批拒绝）。现冻结实现/PLAN/docs，最终仅stacking双库/Swift、position/projection/inline/atomic/Flex/Grid/selection/details/nesting与WebTest三项的双端Debug阶段包；旧直接相邻验证复用，不扩full/双配置/所有后端/全依赖。完成解包WebTest/hash/111ABI、Windows/WSL GC3份与本地提交后继续脱流/包含块、sticky和其余完整目标。

首冻d005bb…Windows15项14pass，唯一nesting默认100ms预算失败；musl15项全pass后因Windows确定需修，终止其安装中的包，保留state failed而非改通过。测量定位样式刷新分配负担，仅优化Candidate预留、回退键惰性存储和Scope构键，不改语义/预算；同页面四次原组合脚本在100ms下85.61–89.22ms均成功。全部临时计时已恢复。重新冻结后只需stacking双库/真Swift、position/projection、受该优化直接影响的variables/media/nesting/details与WebTest三项生成两端阶段包；首冻inline/atomic/Flex/Grid/selection已pass结果复用，无需再重复整套。最终仍须解包/hash/111ABI、两端GC3份和本地提交。

第二轮57f594…并行检查Windows仍有nesting第二个修改及media主题切换触及100ms预算；其余10项pass。musl12项全pass，但并行任务8716a50提交体积调查的PLAN/docs导致源门禁拒包，保留该任务内容而非回滚。进一步以稳定声明/原AST/代次内属性名驻留表作为Candidate的string_view，避免每个元素重复复制属性名；不变预算/回退或失效语义。Windowsstacking/variables/nesting/media四项4.65s全pass，原组合/类修改定点91.08/81.14ms成功。现在重冻结含8716a50的实际源，先Windows最终12项/阶段SDK，再WSL同项阶段包，减少两端编译与限时测试的资源争用。已pass相邻结果继续复用，不扩full；普通符号剥离属于另任务后续，不混本阶段实现。

当前推进相对定位（基于18c61fc）：背景阶段已双端交付并本地保存。建立独立于margin的私有position/inset计算值，接既有层叠、变量、简写、继承与回退；block/Flex/Grid/原子inline在正常流求解后偏移，普通inline保留跨节点整形与换行，在绘制片段和文字几何上施加偏移。子树、圆角/overflow、损伤、纯滚动投影、命中/选区/光标与AX使用同一视觉位置，兄弟位置和基线不变。先新增公开API失败证据，再做相关Debug增量及双端quick阶段SDK/独立symbols、解包WebTest/111ABI/GC3份、本地提交。absolute/fixed/sticky、z-index层叠上下文、变换/逻辑轴及完整引擎目标继续单列推进；不把本阶段当作完整CSS定位已完成。RAW与用户HTML保持只读，无新三方修改，不扩full。

相对几何已通过两端CPU与直接相邻十项；首轮11项仅Swift的圆角平移像素判据失败，独立测量确认矩形全图一致、圆角边缘18通道最大31/255（CPU0差异），不冒称曲线平移不变。保留原失败记录；最终以两端同一公开测试验证矩形整数平移、圆角同位置的保留/全重排/新View绘制精确一致，Windows修正后的CPU/真Swift已通过。补齐0%和抵消calc的百分比类型保留，避免高度不确定时错误优先于bottom；现冻结实现、PLAN和说明，最终仅position动态/静态/真Swift、直接inline/atomic/scroll_projection/backgrounds和三个WebTest的两端Debug阶段包。此前Flex/Grid/radius/overflow/selection通过证据复用，不扩full/双配置/依赖重验；完成解包WebTest/hash/111ABI与两端GC3份后本地提交，完整定位和总引擎继续。

当前推进背景渐变（基于c413fd8）：滚动阶段已双端SDK/111ABI/解包WebTest/GC3份并本地保存。前期最小像素证据确认linear/radial/conic均白色；本轮建立私有背景声明与渐变表达式，接既有VariableStyles重要性/层/源顺序/变量失效及color-scheme/currentColor。覆盖linear/radial/conic与repeating、双位置色标/自动分布/硬边/提示、现代颜色空间及预乘alpha；多层、background简写复位和image/position/size/repeat/origin/clip/attachment组合依次接入同一绘制盒、圆角/inline/原子scene/损伤/滚动投影。按标准及锁定Skia API实现，不改第三方源码或用户HTML；不把未实现的图片URL/定位/动画假称已支持。先必要语法/几何/像素与浏览器对照，再相关双端Debug、quick阶段包/独立symbols/实际解包WebTest/111ABI/GC3份和阶段提交，不扩full。细DOM失效、AX发布和GPU直呈/其他平台等完整目标继续。

背景层已实现并有Windows直接证据：13项相邻检查58.29s全passed，程序化tile版CPU/真Swift16.43s通过，八轮动态DLL加载/绘制渐变/卸载1.85s通过。用户示例2四个原grad块声明/滚动与完整重排像素对照不改原文；浏览器30组28组代表点≤1/255，提示指数/硬边两组差异如实记录docs/backgrounds.md。当前冻结实现与说明，最终仅background语法/动态/静态/Swift、颜色语法与动态、动态卸载和三个WebTest的两端Debug阶段包；先前直接相邻验证复用，不扩full/移动消费/所有图形矩阵。完成实际解包hash/WebTest、111ABI/包内库和两端GC保留3后本地提交。完整URL图片/clip:text/所有数学/动画/inline slice及定位、细失效、媒体/跨平台等继续总目标。

当前优先实施保留布局的滚动投影（基于76f223e）：前一颜色阶段已双端交付；最新性能调查确认纯滚动仍全布局/重建文字和AX，因此先消除这个已实测的秒级瓶颈，再继续背景渐变。保留每个绘制/文字/几何/clip的静态坐标与滚动祖先，滚动只投影位置、重组裁剪并刷新AX可见边界，复用段落/语义和DOM顺序，不增加layout_frames。嵌套与负向滚动、overflow clip、圆角、原子内联中缓存scene、多View、选区/命中/焦点revealing共用几何；DOM/style/font/视口变化仍真实重排，设备丢失重绘不误判为布局变化。先像素/AX/选区/原始用户页及强制同偏移重排对照，实测3页布局次数和耗时，再直接双端Debug/quick阶段包/独立符号/解包WebTest/111导出/GC3份与本地提交，不扩full。此阶段先保留式布局投影，独立滚动条带合成/细DOM失效/文字和AX发布缓存继续后续，完整渐变/定位/媒体/其他平台与旧系统目标不变。RAW/用户测试HTML只读。

滚动投影已完成两端直接Debug七项检查：Windows16.00s、musl8.76s，全通过且无skip；同偏移完整重排像素/全部AX几何对照、原子scene重复导航/负向滚动和多View均验。inline超时由颜色阶段暴露的Lexbor空cascade属性移除错误引起，现用受控empty-inline-style补丁修复，原始C文件与锁归档逐字节相同，补丁删除恢复两端各九项通过，临时打印已恢复。最终Windows DLL三页纯滚动0布局/0revision且像素与旧库一致；复杂页Raster52.53ms、真实HWND轮滚+UIA96.06ms，尚未60Hz达标。详docs/scroll-projection.md。现冻结实现/PLAN/docs，仅做projection双库/真Swift、inline/颜色与直接滚动输入/选区/WebTest的Debug阶段包，完成包内核验和两端GC3份后本地提交。此前直接通过结果复用，不扩full或重验所有后端；细DOM失效/AX发布/全帧绘制继续推进，同时保留渐变/定位与完整引擎后续范围。

2026-11-04库组成和三份用户原页卡顿调查已完成，详docs/library-and-render-performance-2026-11-04.md。使用调查开始时冻结的Windows嵌套包与musl圆角包，避免和并行颜色实现混样；本轮只增加测量工具/报告，不改产品运行逻辑、不重建第三方或生成阶段SDK。纯滚动仍触发全页布局，示例2布局约占帧耗时98.44%；后续性能实施优先保留布局并分离滚动，再细化DOM失效、文字/计算样式缓存和AX增量，最后改GPU读回/直呈。体积按组件与调试节分别治理，不擅自删后端或缩减完整引擎目标。Windows→WSL常规GC已退出0；三份诊断scratch直接清理被自动审批策略拦截，未执行且无具体原因，仍待可执行的清理路径。

当前推进现代颜色链路（基于4d14f6f）：最小16px色块已实测HSL/HWB/Lab/OKLCH退为白色，color-mix/light-dark被丢弃且未计入unsupported，见build/reports/css-colors/before.json。建立框架私有、不可变颜色表达式，保留高精度分量至最终sRGB输出，支持现有颜色、HSL/HWB、Lab/LCH/Oklab/OkLCH、color()预定义空间及color-mix/light-dark；混色处理权重、alpha预乘、色相路径和缺失分量。color-scheme与宿主媒体偏好共同决定used scheme，不把light-dark直接等同系统偏好。与VariableStyles同一重要性/层/源顺序和var失效规则选出颜色，当前颜色/继承/伪元素/背景与边框/outline共享解析与缓存；作者原文和第三方源码不改。先语法/数值/像素与真实原页动态主题直接Debug，再仅相关双端检查、quick阶段SDK/独立symbols、实际解包WebTest、111导出与GC3份、本地提交；不扩full。相对颜色、HDR/原生广色域输出、所有系统色/forced-color等未完成部分仍保留总目标，不以本阶段替代完整CSS。用户新增示例2及既存tools/analyze_library_size.py修改保留未提交。

颜色直接Windows九项14.35s已过，最后边界修正后syntax/动态CPU/真Swift三项7.25s已过。37例本机浏览器CSS求值+Canvas对照仅Rec2020超sRGB样例因映射策略差3/255，其余在2/255内；修显式灰色色相、类型不匹配的数学、无空格/注释token、hash转义、scheme非法声明与透明根背景传播。新示例2计数90→12、现代页6→0，仍不当视觉/API全覆盖。现在冻结代码/PLAN/docs，最终只做颜色语法/动态/静态/真Swift与WebTest local/user/gpu的两端Debug阶段包，不重跑全矩阵。独立导出/包内库和解包WebTest/GC3份后保存本地阶段提交。新性能分析docs/tools也作为用户并行输入保留，不擅自提交；保存逐文件冻结清单以区分后续外部变化。完整定位/渐变等和全RAW继续。

CSS嵌套已保存4d14f6f；最终两端阶段包和新增第三页补验通过，musl原任务源门禁失败及独立补验分开记录，详TODO/WORKMEM。以下嵌套及更早过程作为历史，不重复执行。

CSS嵌套最终musl检查18/19通过，用户原页render返回XL_NOT_FOUND，定位为新测试未注册字体；musl不隐式使用宿主系统字体，其他文字测试和WebTest已显式提供字体。本次仅补该测试的锁定字体fixture及两个测试目标的编译定义，产品代码/原用户HTML不变。旧source49b487…Windows20项passed与musl18项passed分别保留为原输入证据；修正fixture后先复验原失败，再重新冻结并只跑syntax/nesting动态、静态、SwiftShader与三个WebTest检查，生成同一新指纹的两端阶段包。此前相邻回归不重复扩大检查；ABI/解包/GC3份与阶段提交仍须完成。

当前推进CSS嵌套：在原始样式表只读文本与Lexbor平面AST之间建立受控token适配层；解析普通/相对选择器、显式&与隐式后代，使用:is父列表语义保留最大specificity而非笛卡尔积展开。连续声明按原位置拆分，嵌套media/layer等组的声明沿用最近style上下文；伪元素的直接声明匹配原父选择器，显式&不代表伪元素。编译sheet与原文/规范输入按Document缓存，先清全部cascade后才回收旧代；linked/style变更、detach/重挂/导航与变量/伪元素共用失效。对深度、token数、展开输入与总缓存设显式上限，超限返回UNSUPPORTED。先最小机制/用户原页/像素AX/动态失效直接Debug，通过后双端阶段SDK/独立symbols/解包WebTest/GC3份/本地提交；RAW及用户HTML不改，不扩full，完整CSSOM/未启用条件组及全部RAW继续总目标。

嵌套已接入并完成Windows直接8项8.79s（syntax、动态/真Swift、变量/media/pseudo/radius及用户WebTest）和受控补丁恢复8项。Lexbor新增关系候选重试补丁、既有focus-parser补齐函数各层分项权重累加；不改锁源。实际原网页动态查询曾超默认100ms，已以右侧候选索引、绑定环境替换合并、编译规则只展开一次和按原始声明缓存解析结果解决，保持预算不变；2048项缓存回收/旧AX/样式重挂已过。临时计时打印已移除，未实现条件组/CSSOM等边界见docs/css-nesting.md。现冻结源/PLAN/docs，做syntax/nesting双库/GPU及直接受影响变量/伪元素/生成内容/radius/media/动态焦点/事件/AX/生命周期/retained/WebTest双端Debug和阶段包，再核111导出、包内WebTest、GC3份与本地提交，不扩full。

当前推进圆角及统一曲线裁剪：border-radius四角/椭圆/百分比与受支持长度/变量/层叠，布局得到最终边框盒后统一缩放重叠半径；背景、各边边框、overflow内缘、文本/原子内容/嵌套滚动共用私有圆角几何。保留式比较/旧新损伤、指针与滚动命中、文字查询和AX矩形边界接同一裁剪结果；inline分片在对应起止边保留圆角，百分比暂按每片解析；完整虚拟未断行slice另行推进，详docs/rounded-boxes.md。原始用户HTML保持只读，直接双平台Debug像素/几何/AX/命中及相关回归通过后打阶段SDK/独立symbols、解包WebTest、GC3份并本地提交。完整corner-shape/变换/特效和其他RAW目标继续，不以圆角阶段结束完整引擎。

圆角动态CPU/内置SwiftShader及私有几何三项直接Debug已通过（8.90s），包括半径无效语法/线性calc、旧AX快照、曲线外点击/滚动/文字命中、静止帧和半径变化/移动的局部与整帧像素一致。测试按下改变焦点后先render再释放，遵守既有布局失效契约；临时产品诊断已清除。现冻结源/PLAN/docs，执行radius双库/GPU/几何及直接受影响box/overflow/outline/inline/atomic/Flex/Grid/selection/keyboard/AX/变量/生成内容/WebTest的双平台Debug与阶段包，核111导出、实际解包WebTest、两端GC3份并本地提交。仅本项目源码变化，不重建第三方矩阵，不扩full。

首冻68ed4c7d…在独立窄条脏区复验中发现Skia CPU conic覆盖率随tile截断变化；Windows quick6275e9bf与musl b23e3c4b已核实进程并中断，35696/82357完整退出1，均无包。修复在绘制时扩展相交曲线闭包，段落/原子scene保留完整曲线范围，避免使用被裁剪墨迹代替曲线范围；矩形局部更新和静止跳过保留。新增横纵60条窄脏区/局部像素一致及小于整帧重绘面积断言。旧首冻不能背书修复版；最终按新指纹做同样直接Debug/包，不能为该问题扩全依赖CI。

当前继续条件样式与响应式：建立统一的活动规则收集，支持media条件及link/style media，按环境变化重算并保持静止跳过；用户现代网页将media置于@layer，因此同阶段处理命名/匿名/嵌套layer顺序、important反向优先级和原有变量/伪元素共用层叠。视口、设备/交互与用户偏好共用媒体查询求值器，C/C++查询及JS matchMedia变化通知共享语义；宿主可显式覆盖偏好，原生已知信息经平台适配输入，未知信息用明确默认值。保留作者DOM和原始测试HTML，条件语法/动态隐藏/像素/AX/多视图隔离做直接双平台Debug核验，阶段SDK/独立符号、解包WebTest和两端GC3份后本地提交。完整嵌套选择器、container/property/动画等继续总目标，不以条件规则完成替代完整现代CSS。

当前已实现src/style/media_query与active_rules，统一媒体条件/层级收集；VariableStyles处理层优先、important反向、inline、revert/revert-layer及长短属性回退，共代缓存重复替换/四边展开。新增偏好覆盖和查询两个C导出/C++包装；JS订阅与resize共用64任务/100ms，不创建常驻线程。Windows六项最新8.07s全passed（parser/media/原生resize/WebTest local-user-gpu），早先变量/pseudo/generated及媒体六项7.46s也passed。用户现代页面已检验嵌套layer的侧栏断点和明暗变量；其sidebar-foot最终flex是components层覆盖layout层的真实结果，不修改原文。现在冻结源/PLAN/docs，直接双端Debug选择media双库/parser、相关变量/生成内容/事件/AX/retained/native及WebTest；阶段包解包hash/WebTest、111公开导出和原生ABI/双系统GC3份后本地保存，完整目标继续。未实现范围见docs/media-queries.md，不扩full。

首轮冻结3484c6dc…Windows17项中media查询预算及旧事件空闲断言失败，未包；Linux旧同源run经核实活动进程组SIGINT停止、40276退出1且无残余编译进程。已将窗口resize观察显式同步，无监听不排任务；generated缺省counter/quotes绕过重复tokenizer、层排序复用无层标识，并保持100ms预算。修复后media/generated/events直接三项4.84s全passed；重新冻结此版本跑相同直接Debug和stage包，不把旧失败或中断当作通过。

当前推进计数器与引号：按作者树和before/after顺序计算counter-reset/increment/set、counter()/counters()，处理同名作用域、动态失效、隐藏盒、反向计数与HTML list-item语义；quotes与四种quote关键字共用文档深度，生成文字继续进入既有绘制/AX，不污染作者DOM。语法、变量计算无效和层叠在样式入口统一处理，自动引号不触发运行期ICU预热。完整marker/自定义counter-style/图片content仍继续总目标。先修Document构造异常清理与inactive伪元素文字持有；直接相关双平台Debug验证通过后打阶段SDK、实际解包WebTest、GC保留3并本地提交，不扩full。

Windows首轮generated行为已passed4.92s；生命周期/分配失败与内容复验两项5.88s已passed。新增generation模块与私有CLDR表无新运行依赖，ICU原始锁源只读，907locale合并123差异/9589bytes；工具与源dat/commit锁定，可重新生成。最后补重复increment先合并再钳制、q的inherit/revert以及生成节点映射事务回滚；接着冻结源/PLAN/docs，执行generated双库/生命周期、受影响pseudo/变量/details/AX及WebTest local-user-GPU的Debug和阶段包。保留实际范围，不因这次小表/CMake变化运行全依赖或完整矩阵。

当前继续作者::before/::after：由独立私有渲染子项承载样式与内容，不插入作者DOM；匹配来源元素选择器并保留原specificity/important/顺序，变量scope从来源元素继承，生成项进入现有inline/block/Flex/Grid/clip/损伤链。支持字符串、attr()及替代无障碍文本，动态属性/状态/样式失效、样式查询和AX文字共用同代结果；命中回到来源元素，生成文字不伪装成可编辑DOM偏移。其他content形式、完整伪元素/条件规则继续总目标。同步补齐嵌套禁用控件指针guard；阶段归档使用level6降低开发交付耗时，full流程不变。只做此链直接Debug验证和阶段SDK，RAW只读。

实施已具备Windows动态证据：伪元素/变量/details三项6.09s全passed；最终补齐懒ICU、不可变AX更新、Runtime隔离、fieldset首legend例外和Flex下unsupported诊断后pseudo单项4.35spassed。Lexbor使用受控语法/别名/类型权重补丁，Windows9958561c7889、musl0b0cfe3a8f04；原锁源未改，补丁恢复7项/归档4项工具直接检查。现在冻结源/PLAN/docs，只做pseudo双库、变量/details、选区/键盘/基础AX/inline及WebTest相关Debug；Windows附UIA，静态归档仅实际受影响闭包合并。完成两平台stage --package、实际解包hash/WebTest与GC3份后更新TODO/WORKMEM，本地提交并继续完整PLAN，不扩full。

当前推进（details/summary）：在文档层统一首个直接summary判定、closed内容隐藏、可取消激活、键盘/焦点与AX状态；open反射、同树name互斥及异步合并toggle任务覆盖解析和动态变更。默认图形标记与无summary回退不注入作者DOM；Windows UIA通过已有不可变快照和UI线程队列提供幂等Expand/Collapse。保留用户网页原文，直接双库行为/像素/AX及相关事件与WebTest增量验证后打阶段SDK，继续完整PLAN。公开伪元素和完整列表排版另行推进，不将本阶段当作全网页兼容。

本阶段补齐xl_view_pointer主指针桥，按下/释放/拖动/取消与原生SDL队列共用几何命中、焦点和可信click，使WebTest可直接点summary；完整DOM PointerEvent/堆叠命中单列后续。阶段归档改从已安装目录流式生成ZIP/TGZ，消除CPack的额外整套SDK副本，保留实际解包hash/WebTest门禁；正式full的CPack入口保持原流程。ICU许可只保留licenses/icu/LICENSE，不再重复icu.txt。最终源冻结后只验证details双库、受影响键盘/选区/事件/AX与WebTest，Windows额外真实UIA状态通知和原生鼠标队列；两端阶段包完成后清理保留3。

2026-11-02 补充决定：用户明确给予一次 RAW 修订授权，本轮已回查完整会话并补齐基线；修订后恢复只读。工具链统一转向 Clang/LLVM，停止 Zig 路线。依赖源码准备、编译与接入立即同底座和资源服务同步推进，优先使用核实后的新版源码，旧系统缺口优先做兼容层。

2026-11-03 本次回查修订：按用户明确要求整理 RAW 原始基线，保留最初日期并注明本次修订日期；补齐 AX 查询/事件/动作及四平台桥接、共享对象与网页能力清单、宿主 ABI、三类视图、保留式绘制、完整依赖闭包、私有符号及图形 CI/包审查要求。Lexbor、Yoga/Taffy、QuickJS 家族和 AccessKit 的既有候选身份保持明确；实际选型、进度与未完成项继续在日常文档跟踪。修订结束后 RAW 恢复 AI/自动化只读，历史授权不得被续接重复使用。

## 总目标

交付 SDL3 + Skia 的跨平台 Web UI 引擎，覆盖 C++/JS、现代 HTML/CSS、系统无障碍、原生内容混排、WebM、增量渲染及静态/动态 SDK。首批 Windows/Linux x64，Linux 优先 musl。完成要求以 RAW、用户后续明确决定及真实验证为准。
- 项目底线：保证XLWebEngineUI闭源商用的可行性。
2026-11-03 最新决定：引擎不实现 HTTP/HTTPS 或网络请求栈。宿主取得字节后通过 `xl-webengine-ui://` 挂载，Runtime 间映射独立，不可变内容可跨路径/实例显式共享；内存 HTML 可指定逻辑源位置。缓存和结果按类别/目标保留最近 3 份，Windows 与 WSL 均及时清理。日常只做直接涉及的一线核验，复用增量目录。用户最新明确选择每完成一个已验证功能阶段就打包：用 quick --package 生成含WebTest的完整阶段SDK与独立符号包，标注实际Debug验证范围；完整CI仍留到大版本/正式发布。此决定覆盖RAW旧范围和下方历史打包频率，RAW继续只读。

## 阶段与依赖

1. **P0 工程与本地验证底座（主要链路已验证，收尾进行中）**：管理文件、可重建 build 布局、C ABI/C++ 包装、双库导出、本地 doctor/quick/full/release/gc、安装包消费验证、保留 3 份、安全清理、符号包、依赖缓存与本地提交已实现。Clang/LLVM 双平台完整 foundation CI 已通过，仍需新工具链清空恢复与退出保护回归；此 CI 尚未覆盖完整引擎和新增依赖最终合入 SDK。
2. **P1 任务与内置资源（资源 SDK 阶段验证通过）**：公开异步 C ABI/C++ 包装、资源/ICU/NFC、合并、宿主符号冲突、移动消费和并发关闭已通过双平台完整 CI。Windows 已增加真实 DLL 卸载回归；Linux 加载器行为、跨 Runtime 初次加载合并和统一工作池继续跟踪。
3. **P2 窗口与绘制（软件视图 SDK 已验收）**：2773f11 已保存双平台 Debug/Release、四种移动消费/打包和私有 C/C++ 符号审查通过的软件视图；继续 GPU/恢复、完整输入/DPI、混排和失效契约。
4. **P3 网页内核与脚本（行内盒 SDK 已验收）**：6430e17 保存早期网页管线，30f9f6c 保存跨节点行内排版/原文 UTF-16 范围/命中，780a502 保存六种 white-space、tab stop、正常溢出与行高继承，99a6dff 保存普通 font 简写与字体计算值，7f4dd31 保存行内盒与边框。接着推进完整行内格式化、Flex/Grid、编辑/IME、内部资源与现代网页目标，持续保持源偏移、AX 与增量数据链一致，边界见 docs/web-core.md。
5. **P4 AX 与媒体**：AX 语义随 P3 同步实现；完成各平台桥接、文本范围、动作、事件与宿主子树。接入 WebM VP8/VP9 + Opus/Vorbis、音画时钟、字幕、帧合成。
6. **P5 完整交付与扩展**：完成全部验收、性能/恢复/兼容验证，正式 SDK；按设备条件扩展 macOS/Web/其他架构。

AX、线程、生命周期及增量失效是各阶段的设计输入，不能等到 P4 才补设计。

P2 当前实现与验证范围见 docs/views.md。Debug Skia 的 SPIR-V 辅助源码按其 DEPS 新增锁定与构建；私有 SDL 用带标记的源码层关闭动态 API 环境替换，系统后端动态能力另行管理。静态 SDK 合并保留 Windows 原始资源成员，不把资源元信息当成普通可改符号对象处理。

## 当前实施安排

2026-11-04 GPU阶段：ANGLE完整GN静态aggregate及SwiftShader Subzero闭包已完成私有符号隔离，真实独立消费分别得到D3D11红像素与SwiftShader Vulkan设备；测试源已移入受控tests/graphics，CMake可重建。当前接入View的实际surface/候选链、共享EGL display、多线程/宿主WGL恢复、resize/提交/读回失效回退，先做像素与保留绘制直接测试，通过后阶段打包。默认Raster及GPU读回再SDL上传的边界保留，尚未声称零拷贝、Win7实机或其他平台GPU通过。DXC真实静态LLVM/Clang闭包继续构建；musl从删除build后的X11/依赖恢复仍进行中。所有修复通过受控源码/补丁，不手改build三方文件。

Windows后端实际单项已通过：五候选分别真实绘制，AUTO选D3D12；共享display完整操作锁修复D3D调试层抓到的immediate context并发，多UI线程像素/resize和宿主WGL恢复通过，Swift→Raster及AUTO→下一候选失效恢复通过。WebTest窗口默认AUTO，离屏默认Raster，可--graphics选择并显示实际active。接着冻结源做graphics/retained/outline/WebTest直接Debug回归与阶段包；不扩成完整CI。musl X11基栈+Xtst已恢复，仍需完成其余依赖和本阶段引擎；DXC尚未消费，不加入本包。

随后增加FXC缺失/失败恢复核验，修复Skia空诊断访问、通过ShaderErrorHandler捕获编译失败，并保持Debug GrContextOptions与GN ABI一致；ANGLE缺入口改受控错误返回、FXC改引擎/系统绝对路径加载。后端与失败注入两项已实际passed（16.25s），当前准备修正版阶段包。DXC运行时静态链接图/私有COFF指令/SDK Debug迭代器ABI已收敛，正在按同一ABI重建；独立消费命令受控，实际HLSL运行证据未完成前仍不纳入SDK。总目标保持，平台与现代网页机制继续推进。

修正版Windows阶段已完成：冻结指纹035abce4…，动态/静态实际后端、FXC失败注入与WebTest local/两用户页/GPU六项39.68s全部passed，包20261103T180443Z-50d24734实际解包hash/WebTestpassed，107导出精确一致。已交付SDK/独立symbols链接；GPU读回呈现、其他平台/Win7实机与DXC未接入边界保留。随后只做安装缓存去重管理：最终库哈希保持，清除隔离前归档和符号映射；两项工具测试passed，无重复引擎CI。继续DXC真实HLSL消费、musl SDL/Skia恢复和完整PLAN，不以此子阶段结束整体。

DXC独立静态消费已passed：key49bfd9448a73，两个线程首次初始化、有效HLSL→2548bytes DXIL和错误诊断均通过，进程无DXC/DXIL DLL。下一步在受控Skia补丁中为GrD3DBackendContext增加每context编译回调，由GrD3DGpu保存并供两处shader编译使用；D3DDevice查询Shader Model6能力后才设置静态DXC回调，旧设备维持FXC。将私有DXC闭包/许可接入SDK，验证真实View/DLL导入、保持Win7加载边界，并相应调整FXC失败测试验证ANGLE旧路径。之后阶段打包；仍不扩成完整CI。musl SDL配置继续，不用Windows证据代替其他平台。

2026-11-04静态DXC已接实际D3D12渲染：Skia per-context编译回调、设备SM6探测、SM5.1→6.0目标桥与失败链；shader_compiler用原输出保留字段报告实际选择，结构大小不变。四项直接Debug20.05s全passed且无skip（五后端/ANGLE缺FXC/DXC无FXC正确像素/WebTest文字页）。正在补静态SDK同路径及阶段包、107导出/无编译器DLL硬导入；商业运行闭包宽松许可与MSVC/ATL平台条款已分别记录，不把Debug包当正式商用发布。musl SDL已恢复，Skia复制/构建与Linux SwiftShader静态构建继续。

用户已恢复推进并提出多后端、补丁重建、包去重和用户网页测试要求。当前 build 仅剩下载归档，旧工作目录与包均不存在；正从锁、配方和补丁恢复双平台工具链及 Debug SDK，直接检验可重建性，不复跑正式矩阵。RAW 当前人工更新的 SHA256 为 17777070a7ebc6418e01462c356743f9b66f1254d7432c1e5b2880a1e9a45ec5，只读保留。

- 先完成归档精确对象去重、第三方补丁集中与依赖键覆盖、增量入口刷新精确 SDK 闭包；恢复现有 Windows UIA 阶段，并包内记录合并统计。
- 同步启用 Skia Ganesh/Graphite Vulkan 和 Windows D3D12，锁定必要传递源码；依次完成 ANGLE EGL/GLES、SwiftShader、DXC 静态构建与运行时逐层降级。详 docs/graphics-backends.md；当前视图 Raster 不冒报 GPU 已接入，macOS/Web/旧系统继续实测待办。
- tests/user-dev-tests 原始网页只读消费并自动发现 smoke；先补 label→隐藏控件、radio 互斥与动态 :checked，随后 CSS 变量、生成内容、details/summary、视觉样式和完整表单。每阶段只跑直接行为/静态消费/包内 WebTest，验证通过即打包，Windows→WSL 按类别保留3份。
- 最新用户又加入“观察和现代Web特性的差异.html”；已据其真实失败补EventTarget/CustomEvent、定时器/微任务、fragment/innerHTML/集合/classList/dataset/style属性、基础控件value和视口环境。两个原始网页已完成脚本初始化和首帧，script.events_tasks通过；全部支持程度仍看机制/视觉验收。下一次冻结源码跑共享事件通路、静态库和UIA直接回归并生成Windows阶段包，musl从空缓存恢复后同阶段验收。RAW最新还强调闭源商用底线，继续保留实际运行闭包的来源、许可和补丁。
- Skia新增后端已实际编译并以工厂地址真实静态链接测试通过；95个重复对象去重（117871195字节），不能与新增后端的体积混为同版本缩减。ANGLE静态EGL/GLES已推进真实D3D11/GL编译并补声明兼容层；SwiftShader整归档已校验，配方选择上游Subzero并排除未选LLVM目录，正在真实静态编译。DXC静态闭包与所有运行时GPU候选/丢失恢复仍继续，不能将本阶段写为多后端完整运行。


- 2026-11-03 当前实现Windows UI Automation桥：原生视图显式开启，自有HWND处理WM_GETOBJECT，借用HWND由宿主取provider自行接入；UIA线程只读不可变快照，动作64项/4MiB排队并由render所属UI线程执行、2s未开始超时取消。补树/屏幕坐标/属性/Invoke/Toggle/Value/动态事件及逐provider断连、卸载前对象计数；WebTest原生窗口默认开启。真实UIA查询/控件动作/属性/结构及CUIAutomation8 Notification、导航/超时/借用窗口和三轮DLL卸载已局部通过；当前冻结后做最终受影响Debug及双端阶段包。单列交互式focus回归daily环境缺失明确skip，正式门禁要求通过。Win7、完整TextPattern/宿主子树/MSAA/IA2、旧客户端live兼容及其他平台桥保持后续，不记完整AX完成；详docs/windows-accessibility.md。

- 2026-11-03 AX动态更新阶段已验证：View显式启用、成功render发布完整树与差分事件，批次跨线程且可晚于Runtime；队列8批/64MiB、溢出RESET，焦点/UTF16选区/属性/结构/live region共享真实快照。Windows/musl各accessibility/editing/keyboard/selection/pipeline/WebTest六项直接Debug检查passed，两端100项导出精确匹配。阶段SDK/独立符号包及实际解出WebTest/hash均passed：Windows20261103T143133Z-2ded7d30、musl20261103T143409Z-2fc28343；暂存清零、Windows→WSL gc完成/保留3份。契约和边界docs/accessibility-updates.md，证据TODO/WORKMEM。平台桥/真实读屏、完整AccName/关系继续后续；ICU已使用锁定Chromium common规则并完成裁剪阶段，本轮只核对规则和映射哈希。

- 2026-11-03 用户要求库体积调查：以已完成且哈希固定的 Windows/musl 阶段包为主样本，拆分 SDK、静态归档、动态库、调试节、私有依赖及资源，核对重复成员与构建/打包策略。只做只读取证和 build 内副本实验，形成 docs/library-size-analysis.md；不改运行时代码、不触发完整 CI、不混入正在进行的 outline 实施。

- 双库和 CI 链已有阶段验证；现在并行推进完整依赖的源码、许可、编译配方和实际接入，并继续实现资源服务、SDL3、Skia 与网页内核，不能等待底座全部收尾才拉取其他源码。
- CI 入口采用 Python 3.8+ 标准库，薄 PowerShell/Shell 包装。实际输出、下载、日志、暂存与工具缓存均归 build。
- quick 复用最近的 Debug 工作目录，通过 --tests/--build-targets 选择直接受影响目标；不自动扩大到完整矩阵、依赖行为重验或移动消费。阶段结束追加 --package，构建WebTest/静态库、归档SDK与符号、解压冒烟和清暂存。无目录时仅准备 Debug 必需输入。quick --plan 保留影响分析，实际范围写入报告；工具变化只跑对应测试。
- full/release 按配置矩阵验证，release 要求干净 Git 源码和完整必需任务。未具备真实功能的阶段 SDK 必须标注阶段，不得宣称完整引擎。
- 官方 Linux 目标仍为 musl；glibc 开发验证单独标记。已有 Zig 验证仅保留为历史证据，后续不用 Zig；Windows 改用 clang-cl/LLD，Linux 使用 Clang/LLD 与 musl 配套运行库。准备结果进入 build，缺失时明确失败，不能用 glibc 结果替代 musl。
- 暂无 Win7/UOS 真机；Win11 兼容模式和 WSL 结果分别记录，不能声称旧系统已验证。

## 仍需按证据收敛的事项

- Lexbor/QuickJS-NG 已作为当前解析与语言运行时接入；继续收敛现代标准覆盖、共享对象模型、旧系统验证及布局候选的实际缺口。
- SDL3/Skia 源码已锁定并完成 Windows 阶段编译；继续验证当前配方、完整启用能力、实际显示链及 Win7 适配点。
- musl C++ 与动态 SDK 已通过两配置消费；继续验证私有依赖合并、系统/编译器运行库边界及静态包消费范围。

## 已验证里程碑

- 本地回滚点 `cd72d1d` 建立工程与双平台开发 CI。
- 默认 `python -B tools/ci.py full` 已验证 Windows x64 + musl x64，Debug/Release、运行时、SDK 移动消费与候选包；详见 TODO。
- Windows 隔离副本已完成“构建、删除整个 build、重新构建”，源码干净、RAW 哈希不变。musl 完整清空恢复仍待补。

## 下一工作顺序

最新完成的是AX语义更新基础链与本阶段SDK，接着推进平台AX桥/读屏与富文本编辑、IME实测；整体P3/P4/P5及RAW全部目标保持。live的busy后代当前保守延迟整个区域、完整AccName/关系和平台通知策略未完成，不能把离屏批次测试当成系统无障碍验收。以下已完成阶段的详细记录保留为续接索引。

CSS outline已接通简写/长属性与真实外延绘制：块/行内fragment/Flex/Grid/原子场景使用独立轮廓项，不改变布局/AX/scroll overflow；PathOps合并多行、不重复叠透明，旧新墨迹损伤/clip/静止复用贯通，focus-visible默认轮廓进入可覆盖样式。PathOps从相同锁定Skia单独补编并纳入私有双库闭包，不重编主Skia。Windows/musl五项相关检查已passed，随后四边像素探测修正inset/outset阴影方向并新增严格断言，outline单项两端复验passed。接着补键盘焦点回归与阶段包（仅直接受影响Debug），边界见docs/outlines.md；圆角/完整stacking/平台主题/变量与高阶CSS继续保留。整体推进富文本、IME实测、AX事件/平台桥及原PLAN其余目标，不将子阶段当完整引擎完成。

outline阶段已补最终outline/keyboard/WebTest双端局部检查及阶段SDK/符号包：Windows20261103T134832Z-de02b5b4、musl20261103T134917Z-cfe51817。musl打包尾因并行只读体积调查修改PLAN/新增工具而触发全源指纹门禁；保留原始失败，重新验证当前三项测试、当前安装与归档逐文件hash、符号及实际解出WebTest全部passed，补充证据revalidation.json。没有重压缩相同归档，也没有改变引擎代码绕过验证。下一步继续富文本/编辑交互及AX事件/平台桥，同时保留全部原目标；详见TODO、WORKMEM。

用户最新要求ICU数据看齐Chromium裁剪规则：保留Chromium common原始filter/映射/许可与提交哈希，用当前锁定ICU79.1官方数据源码重建，再压缩静态内置；保持懒解压/不落地/线程契约。数据已生成：33,558,160→10,470,976字节，压缩内置4,362,007字节（旧完整压缩10,504,647）。当前优先验证实际多语言消费者，随后打两平台阶段SDK与独立符号包；不为此跑完整CI。此前两平台focus/WebTest已通过，初轮阶段打包因CPack缺描述字段失败，修正后和本次ICU变更一起验收。完成后回到CSS outline及完整PLAN，不以打包/裁剪子阶段结束整个目标。

本阶段实际结果：Windows/musl各6项直接检查均passed，两端完整SDK/独立Symbols及实际解压哈希/WebTest均passed；冻结源码指纹96c92103…，包分别在build/packages/windows-x64/20261103T130957Z-00aac041与linux-x64-musl/20261103T131044Z-ad4f3d67。全部暂存已清，保持最近3份。详细证据TODO；源码验收后只更新交接记录并加上游文件保留原始字节的Git属性，不改运行时代码，不为管理记录重跑测试。接着CSS outline，整体目标仍继续。

剪贴板已保存ee20950，动态focus阶段已验证：引擎AST适配统一:focus/:focus-within/:focus-visible及嵌套查询/层叠，不写DOM假属性；Lexbor定向配方放行两项语法并修正普通伪类权重，仅重编译该依赖。共享焦点/输入方式与默认轮廓同步，补齐无值布尔属性presence语义。Windows/musl最终focus_style通过，此前同运行时代码keyboard/pipeline两端通过；90项Windows导出精确匹配。quick刷新复用目录的实际GC引用，两项定向工具回归双端通过；清理完成/保留3份，WORKMEM约32KB。接着CSS outline外延绘制、富文本、平台IME/AX事件及桥接；边界docs/focus-styles.md，未作full/依赖行为重验/SDK消费/打包，完整P3/P4继续。

纯文本剪贴板阶段已验证：按View提供宿主read/write及自有SDL适配，统一copy/cut/paste与快捷键、事件作用域clipboardData、控件历史/beforeinput和阅读复制。先写成功再剪切，回调版本/焦点守卫与同Runtime跨View重入保护；密码及失焦阅读复制均不导出明文。Windows/musl各clipboard/selection末轮通过，前轮其余editing/keyboard/pipeline/WebTest通过，90项Windows导出精确匹配，两端清理退出/保留3份。继续动态focus/outline、富文本、IME实测及AX事件/平台桥；原生SDL剪贴板跨进程实测仍待交互环境，完整P3/P4尚未验收。日常继续只做直接受影响Debug增量，阶段完成后打包，不重复full。

文本控件当前阶段：独立Editor保存input/textarea实时value、UTF16选择、组合状态与有界历史，接C/C++/JS及beforeinput/input/change/composition事件；复用TextParagraph、AX值、控件内滚动和保留overlay。原生按键/文字/IME/选择指针按序消费，SDL文本服务与caret区域显式启停。Windows六项核心受影响测试通过、musl五项相关回归及修正测试字体后的editing通过，末轮只重跑editing；88项导出精确匹配、两端清理完成/保留3份。原生输入单列.native，两项因Screen-saver桌面skip，正式full/release将环境缺失视为失败。接着富文本contenteditable、剪贴板、动态focus样式、平台IME实测和AX事件/桥接，完整边界见docs/editing.md，不把子阶段替代完整目标。

当前键盘阶段：已完整回读RAW。统一宿主keydown/keyup、Document的JS/AX/Tab焦点、可取消事件和HTML激活默认动作；View同步轮廓、聚焦滚动及失效，WebTest启用原生键盘。Windows/musl各keyboard/pipeline/overflow/selection/WebTest五项受影响Debug测试已通过，81项Windows导出精确匹配；原生键盘证据仅Windows实际前台SendInput。复用现有目录，两端清理完成/保留3份。接着文字编辑/IME、动态focus样式与AX事件/平台桥，具体边界见docs/keyboard.md，不把此阶段替代完整交互目标。

1. 内部协议、实例隔离/内容共享与按需宿主loader已保存为ee7efee/b89b81b；HTML/CSS/经典JS/WebTest已真实消费。图片/字体URL、模块、媒体后续继续接同一协议，HTTP不进入引擎。
2. 缓存/结果保留3份和局部Debug工作流已完成；复用两平台增量目录，阶段结束打包并Windows→WSL清理。旧full证据不重复跑；完整矩阵保留到正式阶段。本次打包前发现build/work已不在磁盘，须复用依赖缓存恢复必要Debug目标，不能声称本轮复用了不存在的目录。
3. 继续P3：统一尺寸已保存为dfcdb36。现已接通overflow两轴裁剪与View独立滚动，普通/原子/Flex/Grid和视口共享平移/裁剪、范围/命中/光标/选区/AX/损伤链；嵌套剩余位移传递、反向负偏移、导航清理和WebTest原生滚轮已实现。Windows/musl各7项受影响测试通过，新增17项浏览器几何/滚动范围对照最大差值0.465px，两端清理退出；详见TODO。继续键盘/编辑/IME、平台AX文本事件及高级内在/循环尺寸、subgrid、writing-mode/bidi；滚动条控件、JS滚动、纯滚动合成/完整溢出传播保留任务，当前不代表完整CSS或P3验收。
4. 发布包WebTest及样例/说明、压缩回退字体、musl启动器与发布检查已实现。历史b89b81b预览包不能代表当前功能；本轮补齐当前两平台阶段SDK与独立符号包，以后每个已验证功能阶段均打包，记录源码指纹与实际检查范围，立即清理暂存。
5. 继续GPU/恢复、原生混排、WebM、AX平台桥；跨Runtime首次加载合并和统一池预算、Clang清空恢复/退出保护、Win7/UOS实机及macOS/Web仍待推进。全部保留总目标，按实际可验证范围分阶段。

## 以下为历史实施记录（不能当作当前状态）

### 2026-11-03 实施状态

- 18 项依赖源码已拉取、固定哈希并实际解包，许可文件核对通过；锁、配方和 Skia 定向兼容补丁已进入 third_party。
- Windows Clang 23.1.4/LLD 已完成 18 项 Release 独立编译与组合调用测试，包含 Skia Raster/Ganesh、skshaper/skparagraph；SVG/PDF 等辅助模块及 VPX SIMD 尚待接入，不缩减长期目标。
- Linux Clang/LLD 与自行编译的 compiler-rt/libc++/libc++abi/libunwind 已跑通 Debug/Release 底座及 SDK 消费，musl ABI 边界仍使用系统 1.1.24。musl 1.2.6 源码另外已锁定，独立构建与旧运行库边界验证仍待推进。
- Linux 依赖 Release 编译矩阵进行中。下一交付重点是压缩资源懒加载与 ICU 数据注册、依赖封入真实静态/动态 SDK、宿主已有依赖时的符号隔离，以及独立窗口/嵌入渲染链。

## 当前实施补充：原生显示与可信依赖选择

- `960ed04` 已保存 LLVM 迁移和 18 项核心源码、构建配方与 Windows 组合调用证据。RAW 修订授权已在 `0d1ebfb` 使用；继续保持原哈希和只读规则。
- 14 项 X11 平台来源已纳入独立锁与 autotools 配方，使用 Clang/musl 编译私有客户端栈；SDL 必须接上真实桌面后端。X server 属于协议对端，不通过加载宿主 glibc X11 库实现。
- 构建和验证共同计算精确配方键，覆盖源码、编译器、配方、补丁、配置和传递依赖。旧配方成功结果保留为历史证据，当前验证须重新构建缺失归档；Windows 当前配方已全部补齐并通过组合及原生测试，Linux 继续构建。
- 增加显式 `deps verify --native`：从真实窗口读取 Skia 绘制像素、接入原生子窗口并检查包装销毁后的宿主所有权；默认 dummy 测试仍仅属于无显示依赖调用验证。musl 最终程序必须接受 ELF 运行库审查。
- X11 locale/XIM/Compose、光标主题、GPU 驱动、音频和输入法后端需继续完成许可、资源与 ABI 边界，详见 `docs/native-display.md`；显示栈编译通过不能替代这些验收。

本阶段验证：14 项私有 X11 组件已全部编译安装；Windows 当前 18 项配方与 `verify --native` 通过（组合键 `18369bdf8407`）。双平台 foundation 完整 CI `20261102T181929Z-5f401416` 通过，包含 38 项工具测试、两配置、移动 SDK 消费和候选包；四包哈希、符号分离及 Windows 两个测试程序导入已独立复查。完整引擎仍未合入 SDK，Linux 原生 SDL/Skia 测试继续进行。

## 2026-11-03：显示实测与压缩资源链

- 私有 libXtst/XTEST 扩展已锁定并实际编译，基础 X11 继续复用。SDL 配置与最终链接使用完整私有静态依赖；Linux 18 项核心依赖组合、X11 真实像素及子窗口所有权通过，最终 ELF 只依赖 musl libc。
- 内部 ResourceService 与仅含解压器的 Zstd 配方已接入实际测试；压缩数据由构建工具生成，首次请求才解压，只读租约保护使用期。目录缓存和空闲池合计受预算限制，活跃租约另计。
- ICU 33,558,160 字节压缩为 10,504,647 字节静态字节数组；Windows/WSL 均通过首次加载、UDATA_NO_FILES、内存数据注册、Emoji、土耳其语大小写、NFC 和字素边界验证。进程级多 Runtime 协调尚未实现，当前测试严格先结束 ICU 使用再释放租约。
- `ci full` 及影响资源的 quick 接入真实依赖验证：准备当前配方、构建期资源生成器、生成带哈希目录，验证原生显示和压缩 ICU。当前第三方行为验证采用 Release；基础 SDK 两配置消费照常，正式发布仍受完整功能门禁约束。
- 下一重点是把这条已验证链路放进公开 Runtime 和静态/动态 SDK，并接着推进独立视图、网页/AX 和媒体。资源与显示层已有测试不代表完整引擎完成。

## 2026-11-03：公开资源 Runtime 与实际 SDK

- SDK 内编译资源服务、压缩 ICU 目录及仅解压的 Zstd，私有 ICU 按配置准备。完整静态归档合并 ICU/解压器并重命名压缩相关全局符号；共享库仅导出公开 C ABI。
- 请求/预热由 UI Runtime 发起，句柄可跨线程非阻塞轮询或独立取消；租约可晚于 Runtime 存活。自定义对齐分配器在首次请求前配置，用户上下文活到最后租约释放。
- ICU 会话令牌协调 SDK 内首次注册和最后清理；Runtime 的销毁不得破坏其他 Runtime 的 ICU 使用。公开 NFC 接口验证真实数据路径，数据仍按需内存解压，不落地。
- 共享 ICU 只固定 SDK 分配器的数据；自定义宿主分配器用于显式资源请求，其上下文不会被其他 Runtime 隐式持有。已初始化会话直接复用，并单独报告共享数据量；首次并发准备可能存在短期重复解压，后续再优化跨 Runtime 加载合并。
- Windows Debug/Release 已出现实际构建与移动消费通过证据；最终以本轮冻结源码后的完整 CI 为准。Linux 配置匹配的 ICU 与 SDK 正在继续验证。
- 后续继续将 SDL/Skia、网页/脚本、AX 和媒体逐层合入同一 SDK，不以资源阶段完成替代完整项目完成。

2026-11-04静态DXC实际渲染阶段已完成七项直接Debug与quick阶段包、实际解包hash/WebTest和107导出核对，包20261103T183251Z-12ec0c7a。当前继续Linux Skia和静态SwiftShader：为Swift的宿主最低CMake要求引入锁定工具，仅作用于该组件，不使SDL/ICU/X11已有编译键全部失效。Windows三方改动仍全部受控补丁，RAW只读；其他平台、旧系统、完整Web机制与AX/媒体目标均保持待推进。

Linux静态图形接入实施中：sdk_inputs选择ANGLE与SwiftShader，DXC限Windows；Swift使用单独锁定的Linux宿主CMake4.4.5（SHA256入构建identity，工具路径入引用），系统CMake/编译器键保持。旧CMake在版本门槛处失败的source-only目录可复用，不复制第二份源码；实际行为尚待编译。musl主机的glibc图形驱动不能当musl驱动，ANGLE独立测试只在初始化不可用时明确skip；View必须实际使用内置SwiftShader核像素、两UI线程、缩放及失效回Raster。用户页面下一机制是CSS变量，尚未改其原始HTML，也未把首帧smoke当视觉覆盖。

Linux实际编译补齐上游旧LLVM的glibc特性guard（mallinfo/execinfo/backtrace），全部在static-subzero受控补丁。Swift379步已完成，ELF归档对齐修复后静态隔离安装成功；ANGLE GLX头缺失通过锁定libglvnd官方当前提交、仅提取MIT GL/KHR头补齐，不借用宿主未锁定GL头。归档合并另补ANGLE gl/rx/sh私有模板符号识别，8项工具含真实ELF索引通过；此变化需Windows与musl实际静态消费/后端直接回归及阶段包。当前冻结实现源等待闭包完成，避免验证期间重复改driver。

2026-11-04 musl首次实际SDK测试发现小栈宿主线程在SPIR-V校验入口溢出，保留GDB证据并停止打包。Linux现按需建立每View图形线程（4MiB、单槽同步、创建/绘制/读回/关闭同线程），不修改宿主线程默认属性；纯Raster不建线程。缺字保留.notdef与UTF16/AX原文，不再取消整页，字体覆盖仍需宿主提供。五项直接行为10.72s全passed：backend动态/静态两UI线程和恢复、WebTest GPU/两用户页、inline缺字几何。再优化同次执行内musl工具链准备复用、删除关键输出重新准备、overlay修复只遍历原始清单相关目录；工具patch6/WSLbootstrap9passed。接着冻结最终源，只跑这些直接路径并打Windows/musl阶段包，不扩全矩阵。

2026-11-04继续CSS变量：采用每元素的计算样式代次，保留共享作者AST，分别收集变量级联、先解析后继承、包含fallback的依赖图和循环失效、token边界与嵌套fallback；支持现有属性的变量值并在代次内展开shorthand。原来的!important/来源顺序通过显式位置映射保留，计算时无效用unset掩盖早先声明，语法无效声明才丢弃。自定义属性名称在框架映射中按大小写精确比较，不改Lexbor锁源。加颜色background简写的已支持部分，并保持缺字/AX/保留帧契约。直接双库像素、继承、循环、shorthand顺序、动态主题和原始用户页验证，尚未通过前不写阶段完成。嵌套@layer/@media、动画注册变量和完整CSSOM另列待办，不把变量支持当现代CSS全覆盖。

CSS变量直接Windows动态测试已通过（最新windows-final-probe.log0.52s；完整受影响10项windows-regression.log18.84s）。测试涵盖动态主题/原始用户页、继承前解析/循环含fallback/空值/大小写/转义/EOF/token拼接、无效计算值/important/shorthand顺序、变量字体/Flex/Grid、detach/重挂与链接sheet的Runtime隔离和替换卸载；代码含单次与代次展开上限。新增docs/css-variables.md和随包变量示例。现冻结源/PLAN/docs，以变量双库、URI/字体盒模型、实际WebTest直接Debug验证两端并阶段打包，不重复全依赖/正式矩阵；其他CSS机制继续后续。


2026-11-04 Gecko/Chromium差异调查：按官方版本接口确认Firefox159.0.1/Chromium157稳定分支与161/159开发主线；只做固定BCD8.1.8全量离线统计和工程资料对照。工具tools/compare_browser_compat.py锁来源/版本/提交/许可/双哈希，报告docs/gecko-chromium-comparison-2026-11-04.md；输出统一build/reports/gecko-chromium-comparison。不是浏览器性能/WPT/平台行为或本引擎验收，不改变完整UI引擎范围；Dawn等实施继续按既有PLAN。
