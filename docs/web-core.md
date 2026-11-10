# 网页管线与当前能力

作者before/after已接入独立级联、变量继承、字符串/attr生成内容、Flex/Grid、命中来源和AX文字；不注入作者DOM，具体预算和CSSOM/选区/未支持形式见pseudo-elements.md。

details/summary已提供关闭内容排除、首标题、open/name、异步toggle、默认标记/缺省标题、键盘及AX展开动作。主指针桥和Windows UIA ExpandCollapse使用同一真实状态，具体边界见 details.md；作者伪元素等仍待。

CSS 自定义属性阶段已接入每元素计算代次、大小写敏感名称、继承、循环/fallback、动态主题及现有属性的变量值，并支持单一颜色的 background 简写。具体行为、展开预算和未支持的注册/动画/嵌套条件规则见 css-variables.md；下方较早的“变量待实现”记录以该文档的实际边界为准。

文本控件阶段：input文本类型/textarea的实时value、UTF16选择、字素编辑/历史、beforeinput/input/change与组合预编辑/提交/取消已接入真实文字布局、控件内滚动、AX快照值和原生SDL文本入口；完整范围及contenteditable/平台IME实机等未完成项见docs/editing.md。较早记录中的“尚不支持编辑/IME”以该文档的具体边界为准。

键盘/焦点阶段已接入宿主C/C++输入与WebTest原生队列，Tab遍历、HTML按钮/复选框激活、可取消DOM键盘事件、聚焦滚动及可见轮廓共用Document/View状态；语义AX焦点/checked同步，具体契约及编辑/IME/动态focus样式等剩余边界见docs/keyboard.md。

2026-11-03最新阶段：尺寸约束已保存为dfcdb36；overflow两轴裁剪、View独立滚动、视口/嵌套滚动、AX可见几何和WebTest滚轮已接通。具体契约及未完成的滚动条/JS/合成边界见docs/scrolling.md。下述旧阶段的overflow待办描述以本条和该文档为准，不能据核心实现宣称完整CSS。

采用已锁定的 Lexbor 处理 HTML、DOM、CSS 声明和选择器，QuickJS-NG 处理语言执行；框架维护节点身份、文档/视图生命周期、样式失效、布局、绘制快照、文字与 AX。这是自主组成的网页管线，现代 HTML/CSS 和面向 UI 的 Web API 目标继续保留。

内容通过 `xl-webengine-ui://` 内存协议加载，Runtime 映射隔离，共享不可变字节，支持相对 CSS 与逻辑源位置，详见 `docs/ui-content.md`。引擎不实现 HTTP/HTTPS 或网络请求栈。

`xl_document` 归属 Runtime UI 线程，C++/JS 共用同一 Lexbor DOM。节点 ID 跨文档唯一，导航建立新文档、重置 JS 并使旧 ID 失效；脱离树的节点在当前文档内保留，避免 JS 引用或 C++ ID 指向释放内存。View 借用文档，附着期间销毁文档返回 busy；Runtime 有文档时不能销毁。

HTML 从内存加载，解析时不执行 script。宿主通过 `xl_document_eval` 显式执行一次，再单独读取结果/错误；获取结果缓冲大小不会重复运行脚本。脚本运行时惰性建立，限制内存、栈与每次运行预算，并执行当前微任务。当前桥接包含 document/body、getElementById、querySelector、createElement、属性/textContent、appendChild/remove、style.cssText/setProperty、事件监听/冒泡、focus/click 和声明查询。完整浏览器事件、CSSOM、模块、计时器及前端框架兼容仍需补齐。

`xl_view_set_document` 接入现有独立/借用/离屏视图，文档与底层矩形入口互斥。首次渲染或文档修订时生成快照；当前先全量重算样式/块布局，再对新旧绘制项计算局部失效。没有修订时复用快照与像素，不重复布局或绘制。样式、布局、绘制、合成和 AX 的更细粒度失效继续推进。

Lexbor 当前处理内联 style 的变更；class、祖先/兄弟及结构选择器失效由框架负责重新匹配，不能假定上游已完整提供浏览器样式系统。已接入块级宽高、margin/padding 简写和单边声明、box-sizing、像素/百分比及部分相对单位、background-color 和颜色继承。连续文字和嵌套普通行内元素共用带样式片段的文字上下文，支持跨元素空白处理、字号/颜色/字体族/粗斜体、行内背景、br、块级后代拆分，以及段落方向和 start/end/left/right/center 对齐。同样式文字在整形前合并，避免没有水平装饰的元素边界破坏连字或组合字符。普通行内盒的水平边距、padding 和边框参与换行；原子行内盒和Flex/Grid已接入下述能力。unicode-bidi 嵌入/隔离、margin 折叠、表格、定位、变换、overflow、伪元素、媒体查询、变量与完整 CSS 计算值仍待实现。unsupported_layout_nodes 是已识别缺口的诊断计数，不是完整标准覆盖率。

行内盒使用显式装饰边缘，占据水平宽度，并允许负边距；仅有垂直 padding/边框时保持连续整形。去除装饰占位后的逻辑文字用于 Unicode 换行分析，元素边缘不会凭空增加单词内部换行机会。折叠行尾空格可以跨越关闭装饰，保留原文到段落的 UTF-16 边界；装饰占位不进入 DOM 原文或 AX 名称，文字范围跳过它们。空装饰盒保留字体/行高 strut，无可见文字时也按需准备文字服务。末尾 br 的关闭装饰归最后一行，不额外生成一行。

每行生成背景和边框切片，先画背景/边框再画该行文字；默认 slice 的左右装饰只出现在元素首尾。行内垂直 padding/边框超出字体内容框，但不增加普通行内元素的行高；元素几何和 AX 合并装饰片段，DOM 文字范围仍是字形几何。装饰和文字墨迹共同进入局部失效，包含清除旧装饰的范围。solid、dotted、dashed、double、groove、ridge、inset、outset、none/hidden 已接入；虚线分布和三维明暗使用本引擎的确定性绘制，尚未声明与各浏览器逐像素一致。border-radius、border-image、box-decoration-break:clone、复杂 bidi 隔离与完整行内格式化仍待完成。

行内 vertical-align 已接通 baseline、长度/百分比偏移及 inherit/initial/unset。正值向上，百分比相对于该元素自身行高；显式继承的百分比按子元素自身行高重新计算。子孙基线相对祖先累积，祖先自身行高仍参与行盒，即使只有移位后代。字形、装饰、DOM/AX范围、命中、墨迹损伤与静止复用使用同一位移；普通行内垂直padding/border仍不撑高行盒。已接通 middle、text-top、text-bottom、sub、super，前几者基于父/自身字体度量，sub/super使用父字号的确定性偏移且不隐式缩小字号。inherit保留关键字并对当前父字体重新计算。

top/bottom在每条实际行换行后求解：嵌套组先计算内部高度，再对齐到最近top/bottom祖先或整行，保留普通祖先的基线偏移。最大对齐高度相同时按DOM顺序择定普通文字基线，匹配当前Chromium对照；不宣称所有浏览器的歧义场景均一致。空元素仍有strut，连续br或保留换行没有字形时也保留所属字体/行高。节点包围框在有非空片段时忽略空片段，全空时保留首个位置；绘制和DOM/AX共用这些片段。新增18项几何对照覆盖嵌套反向对齐、祖先偏移、等高顺序、空盒、装饰溢出、变高换行和空行，Windows/musl定向测试同时验证命中、选区和局部像素一致。表格对齐、复杂双向隔离及完整行内格式化继续待实现。

display:inline-block及inline flow-root建立独立子布局。显式/百分比宽度、自动shrink-to-fit、padding/border/box-sizing和四边margin进入外层占位尺寸；auto边距按零处理。自动宽度通过实际文字与子盒的min/max-content测量得到，长单词可以撑出容器；内部white-space只控制子内容，盒外换行由父格式化上下文控制。子内容最后in-flow文字行的基线用于外层，空盒用底部margin边；长度/百分比及已有关键字对齐共同支持。普通块、普通行内与原子盒可以嵌套，外层只保留一个不进入DOM/AX文本的占位。

原子子树保存为只读PaintScene，文字适配层使用SkParagraph提供的真实blob/簇clip，将原子内容按逻辑顺序插入前后背景/字形之间，包含负边距重叠。绘制场景和RasterView只保留绘制字段，原文与UTF16索引留在DocumentFrame；子文本的最终几何平移合并供AX/范围/光标/命中使用，DOM文字顺序独立于段落存储及绘制顺序。子场景墨迹参与旧新损伤，静止页面继续复用。原子尺寸已纳入下述统一约束，当前overflow保持可见；裁剪/滚动、复杂内在尺寸和完整布局计算值仍需补齐，不能据此声明全部inline-block/CSS行为完成。新增32项浏览器几何对照最大差值0.026px，Windows/musl定向回归同时覆盖嵌套选区、命中、前后遮挡和局部/完整像素一致。

Flex使用框架自有求解器和现有子布局，支持display:flex/inline-flex以及block/inline flex、row/column及reverse、RTL、wrap/wrap-reverse。每行按假定外部尺寸收集item，按flex-grow/shrink与基准尺寸分配空间，min/max违规项冻结后再分配；因子总和小于1时保留相应剩余空间。已接通flex-basis的长度/百分比/auto/content、自动最小内容尺寸、item min/max-width/height、padding/border/box-sizing及自动边距。伸缩后的主轴尺寸交给真实内容排版，计算交叉尺寸/首行基线，再按stretch交叉尺寸更新子内容。

主轴justify-content、交叉轴align-items/self、align-content支持对应的start/end、flex-start/end、center、space-between/around/evenly、stretch和baseline；不是每个值都适用于每个属性。gap/row-gap/column-gap支持非负长度和百分比，百分比按对应定尺寸内容轴解析，内在尺寸阶段与不定高度的百分比行gap按零处理。flex/flex-flow/gap在层叠前展开，保留原位置/important；普通无效负值不会遮蔽先前有效声明。Lexbor缺少的对齐值由适配层补成其原生属性节点，仍走相同选择器层叠；space-evenly使用内部值，并让公开声明查询/C++/JS返回正确字符串。公开SDK没有新增三方类型或布局ABI。

Flex直接子元素blockify，连续直接文字组成匿名item，纯可折叠空白不成item。order稳定排序改变视觉和绘制顺序，DOM文字顺序保持原样。普通块/行内、Flex和原子盒可以嵌套，最终子快照的文字/盒/绘制索引平移合并；AX、范围、命中和选区读取相同坐标。静止复用、JS改变order后的局部/完整像素一致、跨内外选区原文均有定向回归。新增61项浏览器几何对照最大差值0.501px，Windows/musl分别验证Flex/atomic/selection三项。

当前Flex内在尺寸采用内容贡献测量，完整flex fraction内在尺寸公式、aspect-ratio、纵向writing-mode/高级baseline、display:contents/绝对定位item、safe/unsafe对齐及overflow滚动/裁剪仍需补齐；这些边界保留在PLAN，不把核心求解器和已过案例当作完整Flexbox或现代CSS验收。容器尺寸已纳入统一约束，Grid剩余边界及输入编辑继续推进。

Grid支持display:grid/inline-grid及block/inline grid。轨道可使用非负长度、百分比、fr、auto、min-content、max-content、minmax和fit-content；支持数字repeat、固定尺寸模式的auto-fill/auto-fit，auto-fit折叠没有item占用的重复轨道。显式轨道、循环使用的grid-auto-rows/columns、命名线、正负行号、span和矩形命名区域进入同一放置求解，grid-auto-flow支持row/column与dense/sparse；order改变item视觉/绘制顺序，DOM顺序保持不变。每轴256条轨道是当前实现上限，模板展开、自动重复及隐式放置共用该边界，运行期超限返回XL_UNSUPPORTED。

列轨道先按最小/最大内容贡献求解并分配fr和auto剩余空间，再用实际列区域宽度排子内容，得到行高和首行基线；行轨道求解后按最终区域处理justify-items/self、align-items/self、自动边距及stretch。百分比item尺寸与padding/margin按其区域宽度或高度重新计算；不定行高先求自然高度，再回解百分比行或gap，保留真实溢出。普通文本、匿名item、嵌套Grid/Flex/原子盒共用现有文字/子布局能力；最终坐标、绘制索引、DOM选区和AX快照统一合并，没有另外绘制一套占位内容。

grid、grid-template、grid-row/column/area和place-items/self/content在样式层叠前展开，保留原顺序、选择器特异性和important；无效普通值移除，原style属性不重写。命名区域产生对应的-start/-end线，区域inherit保留二维定义并对当前轨道生成命名线。新增65项Grid浏览器几何对照最大差值0.501px；Windows/musl各有Grid/Flex/atomic/selection四项定向通过证据，包含跨组件原文选择、AX/命中、重叠order绘制、JS改轨道后局部/完整像素一致及静止复用。

Grid核心仍不是完整标准实现：复杂跨轨道增长分配、内在/循环百分比尺寸公式、subgrid、部分合法简写中的复杂命名线、纵向书写/高级baseline和绝对定位item继续待补。统一min/max容器约束已接通，overflow裁剪/滚动及完整遮挡命中继续推进；当前等距离重叠文字选择后绘制片段的行为不代表这些系统已经完成。

尺寸适配层保存width/height/min/max的计算长度、百分比与内在关键词，避免过早把百分比固定为旧父级数值。显式inherit复制计算值：长度已转成像素，百分比则在新包含块解析。普通块、inline-block及Flex/Grid容器共用min/max和box-sizing转换；最小值优先，边框/padding给出尺寸下限，自动边距按最终宽度分配。width/min/max-width支持min-content/max-content/fit-content，先测真实内容，再按最终宽度重排；普通inline仍忽略这些盒尺寸属性。

百分比高度使用包含块的确定内容高度，根格式化上下文以视图高度为基准；自动高度不会借用视口，也不会仅因min-height约束而成为百分比基准。明确高度被min/max夹取后，后代使用夹取后的内容高度。Flex/Grid额外的分配/拉伸确定性独立记录；列向Flex在不定主轴中使用内容而非百分比basis，未解析的百分比交叉尺寸也不会被误当成auto而获得stretch资格。相关子百分比在布局分配后再解析。

每块的内容在独立布局快照中生成；自动Flex/Grid容器被高度约束改变时重新分配内容，再合并绘制索引、DOM文字/盒几何及基线。源文本原文通过移动合并，RasterView仍只保存绘制字段。新增50项尺寸浏览器几何对照最大差值0.012px，Windows/musl各有sizing/Grid/Flex/atomic/selection五项定向通过；范围、命中、AX、保留选区、局部/完整像素一致和静止复用同时回归。高级循环尺寸、所有内在高度语义、纵向尺寸属性及overflow仍不视作完整验收。

边框适配层在层叠前将 border、四边简写和 width/style/color 的一至四值语法展开为规范长属性，补齐 Lexbor 没有的 width/style 属性，保留原位置、特异性和 important。省略成员复位；inherit 使用父级计算值，initial/unset 恢复初始值，非法普通声明移除，不覆盖先前有效声明。原 style 属性不改写，分离节点重挂继续使用相同生命周期规则。框架块级盒的内容宽高、padding、border-box 与背景绘制也纳入边框。var/calc、revert/revert-layer 和未完成的边框能力仍诊断为缺口。

outline简写/长属性、正负offset、多行片段合并及默认focus-visible轮廓已接入实际保留绘制，不增加布局、AX或滚动范围。原子场景包含轮廓墨迹与相等判断，清除/移动按旧新损伤重绘；详细样式和边界见outlines.md。

字体适配层在样式层叠前展开普通 font 简写，保留原声明位置、选择器优先级、内联优先级和 important；省略成员写入初始值，reset-only 字体属性也保持重置声明。支持字号/行高、粗斜体、CSS3 宽度关键字、字体名列表及 inherit/initial/unset。字号支持绝对关键字、larger/smaller、百分比和已实现的长度单位，initial 使用 16px；rem 使用根元素计算字号，根元素自身的 font-size rem 以初始 16px 为基准。

展开使用原属性/样式表的源偏移和 CSS tokenizer，避免未知属性序列化后丢失标识符转义。多词非通用字体名规范化为带引号的名称，font-family 长属性的同类 Lexbor 解析缺口也由适配层补齐。原 style 属性保持原文。重建前先清除连接与分离树上的层叠引用，再修改声明并重新匹配，防止其他元素仍引用已释放的共享声明。

font-stretch 的九个标准宽度类别进入 Skia 字体匹配；任意可变宽度轴、任意 oblique 角度、small-caps/字体特性渲染和完整字体合成仍待补齐。系统字体简写 caption/menu 等、revert/revert-layer、var/calc 保留为未支持输入，不能当成已经实现的普通简写；已识别的使用进入诊断。xl_document_get_style 与当前 JS getComputedStyle 包装仍返回层叠声明的序列化值，不是完整计算样式或 CSSOM；展开后的 font 应通过各长属性查询，font 本身可能返回 XL_NOT_FOUND。

空白处理支持 normal、nowrap、pre、pre-wrap、pre-line、break-spaces，pre元素具有预格式化默认样式；继承、initial与unset进入同一计算链。保留模式的空格、tab和换行保持在AX片段及DOM偏移映射中；CRLF折为一次换行，原文两单元仍映射到同一位置。首次只有保留空白或换行也会触发文字数据懒加载；普通被折叠的空白不触发。末尾换行终止当前行，不额外增加一条空行。

CSS换行由框架文字适配层负责：先利用锁定Skia的整形簇测量逻辑序列，再按Unicode换行机会、各片段wrap策略和有效宽度分行，最后对实际行整形与绘制。普通长单词溢出，不自动强制拆成字母；nowrap/pre片段不在内部软换行。pre-wrap尾部空白可悬挂，break-spaces则逐空格/制表符参与换行。tab-size支持空格宽度倍数及长度，并按每条实际行的当前位置计算下一个stop；tab用不绘制的占位符进入字形布局，保留一个UTF-16单元，范围和命中使用其实际宽度。这一适配层只读Skia私有ParagraphImpl/Cluster的测量数据，依赖锁与两配置编译门禁保护其版本边界，不把私有类型暴露给SDK。

line-height支持normal、无单位倍数、百分比和长度，区分倍数继承与计算长度继承；父级strut参与各行的基线和最小度量合并。零行高和含装饰边缘的行按CSS半行距合并，避免平台占位符向上取整额外增加行高；字形墨迹仍可溢出并参与脏区。混合字号与平台字体度量可能存在亚像素至1px差异；开发浏览器对照使用显式24px基础行高，并记录实际差值和来源哈希。完整语言相关segment-break转换、软连字符/断词控制、word-break/overflow-wrap、复杂双向段落跨行隔离与完整行内格式化仍需继续验证和补齐。

文字使用 SkParagraph/HarfBuzz/ICU，首次可见文字需求才请求内存 ICU，未准备完成时 render 返回 busy，宿主稍后重试。文字对象持有 ICU 会话，先结束 paragraph 再释放会话。段落缓存保存弱引用，限制128条和1MiB键数据；图形保留当前快照，文字局部失效采用实际字形墨迹边界。字体可由宿主通过 `xl_document_add_font` 传入内存并复制为私有数据，Windows 还可使用系统 DirectWrite 回退；Linux 当前需要显式字体，系统字体发现/回退和内置字体资源后续补齐。测试字体仅来自锁定 Skia 的测试资源，不随 SDK 独立分发。

文字上下文保存每个DOM文字节点的原文UTF-16边界到规范化UTF-16边界的映射；各实际行再记录自己的上下文范围及位置。被折叠的空白对应重复边界，补充平面字符保留两个UTF-16单元。规范化只改变布局输入，document_get_text仍返回原文。AX文字名来自同一规范化片段，可见的保留空白也具有文字节点；元素几何合并行内片段，范围矩形保留逐行结果。

xl_view_text_rects 保留规范化后节点贡献内的 UTF-16 范围；在跨节点上下文中，折叠空白归首次产生该空格的节点。新增 xl_view_dom_text_rects 接受 DOM 原文 UTF-16 范围，空范围或完全被折叠的范围返回零个矩形；它还不负责绘制光标。xl_view_hit_test_text 从 drawable 像素选择最近的实际源文字片段及对应段落的整形光标位置，避免包含原子盒的外层段落遮蔽子文本命中；返回原文节点、UTF-16 偏移及上下游 affinity，不改变焦点或选区。空白边界依 affinity 选择相应原文端点，跨节点连接处据同一规则定位。输入必须为有限坐标，失败不改写位置输出；小缓冲不部分写入矩形。两项接口与 AX 查询一样在快照过期时返回 busy。

C++ View 提供 text_rects（默认原文偏移，可显式选择规范化偏移）与 hit_text 包装，另有视图级光标/选区接口。JS DOM Range/Selection、编辑、IME、逐字/逐词导航和完整双向文字隔离仍需继续实现。

私有 Skia 增加带来源标记的 ICU 迭代器缓存清理钩子。最后一个会话先确认 paragraph/Unicode 使用者退出，再清理 Skia 缓存、执行 u_cleanup、释放内存数据；不能留下静态断词迭代器引用已解压数据。字体与段落仍按需建立，12 次完整 Runtime/文档/字体/JS/文字生命周期回归用于检查重新初始化。

动态加载回归单独使用 LoadLibrary/FreeLibrary 或 dlopen/dlclose，执行字体/文字/DOM/JS及晚于Runtime释放的资源租约，确保关闭库前释放所有句柄。Windows检查模块已卸载；Linux同时记录dlclose后映射是否仍驻留，加载器保留映射时不能将关闭成功描述成代码已经解除映射。

AX 从同一 DOM 和布局快照产生角色、名称、父子关系、状态、几何及焦点/激活动作。语义快照与文档版本或视图尺寸不一致时返回 busy，等待成功渲染；框架 AX 查询接口不代表已完成 UIA、AT-SPI、NSAccessibility 或 Web 语义 DOM 桥接。完整名称计算、列表/表格/表单关系、动态事件、文本编辑和真实读屏验收继续推进。

本轮选型是解析器/语言运行时接入的实施决定：Lexbor 和 QuickJS-NG 已有锁、许可、两平台源码编译与真实调用基础，能保留框架对布局、绘制和 AX 的控制。它们不会自动补齐现代排版与浏览器 Web API；Yoga/Taffy 等布局候选仍按缺口和跨平台成本继续评估，不能把当前块布局当作最终标准范围。

视图级光标/选区、原文读取和可选独立窗口鼠标选区已实现，契约及剩余编辑/IME边界见 docs/text-selection.md。
