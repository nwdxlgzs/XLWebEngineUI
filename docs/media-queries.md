# 条件样式、级联层与媒体查询

@media、style/link的media属性、C/C++查询与JS matchMedia使用同一媒体求值器。条件分组递归收集活动样式，再进入现有变量、选择器、伪元素、简写、布局和AX链；不改写作者DOM，也不将未匹配规则无条件套用到页面。未知媒体特性/值为unknown，not/and/or采用三值逻辑，只有true匹配；未知媒体类型为false。列表逐项恢复，非法项成为not all，其他有效项继续匹配。

支持screen/all/print判断（当前View是screen）、only/not、and/or、嵌套括号、布尔特性、冒号、min/max前缀、单边/双边范围和比例。查询最多64KiB/8192token/64层/256列表项。公开查询超过64KiB返回XL_INVALID_ARGUMENT，CSS/JS编译超限为不匹配；损坏内存或分配失败沿用现有错误协议。全空媒体列表匹配，逗号产生的空项为非法项。序列化规范化已支持的语法；完整浏览器CSSOM序列化仍待。

已实现的环境特性：width/height、device-width/device-height、aspect-ratio/device-aspect-ratio、orientation、resolution、color/color-index/monochrome/grid、scan/update、overflow-block/inline、color-gamut、dynamic-range、pointer/hover/any-pointer/any-hover，以及prefers-color-scheme、prefers-reduced-motion、prefers-contrast和forced-colors。未知的特性和值不伪造支持。

长度支持px、零、em/rem（媒体查询使用初始16px字体）、in/cm/mm/q/pt/pc及vw/vh/vmin/vmax和对应宽高的sv/lv/dv单位；当前没有动态浏览器工具栏，这些宽高等价于同一View视口。resolution支持dppx/x/dpi/dpcm，ratio支持a/b。calc()、ex/ch等其他字体度量单位和完整数学表达式继续后续；不把它们当成px。颜色位深是平台位深的RGB分量估算；当前绘制环境报告sRGB/standard、progressive/fast、连续滚动，未宣称HDR或广色域输出。独立显示设备信息未知时，device尺寸回退到View尺寸。

原生环境由平台适配层提供：SDL主题、鼠标/触摸信息、显示尺寸和位深；Windows另查询系统动画和高对比设置。旧Windows不新增现代主题API硬导入。无法获得的偏好按浅色、无减少动画、无对比偏好和无强制颜色处理；Linux的动画/高对比设置目前仍用该默认值，宿主可以覆盖。forced-colors媒体条件可由页面使用，尚未实现浏览器完整的强制调色板重映射，不能以媒体匹配宣称该绘制功能完成。

宿主通过xl_document_set_media_preferences覆盖颜色/动画/对比/强制颜色偏好，结构体各字段0表示沿用View原生/默认，传NULL恢复全部跟随；参数校验遵循struct_size/size及保留字段规则。C的xl_document_matches_media和C++Document::matches_media只求值，不初始化字体/ICU、图形或工作线程。偏好跨同一Document导航保留；不同Runtime/Document状态独立。Document::media_preferences提供对应C++包装。

View在原生尺寸更新后同步环境，再运行脚本任务与布局。离屏resize会立即更新已绑定Document环境；样式查询和matchMedia.matches可读到新尺寸，变化事件在后续render/任务推进时派发。条件结果未变且没有其他样式变更时复用计算声明代次，布局仍使用新尺寸；无失效的静止视图继续跳过绘制。多个View绑定一个Document时，每次render使用调用View的环境与自己的帧，查询反映最近一次绑定/resize/render同步的环境，不提供同时存在的多个JS窗口对象。

matchMedia返回MediaQueryList：media/matches只读，支持change监听、onchange、旧addListener/removeListener、once和对象handleEvent；MediaQueryListEvent提供不可变matches/media快照。只在有change监听时保留原生订阅，移除最后监听释放；导航关闭QuickJS前先释放订阅回调。变化异步合并，最终值回到旧值不派发；同一任务期间读matches总是当前值。最多1024活动订阅，与计时器/details共用每轮64任务、100ms预算和Promise检查点；剩余任务通过既有wakeup接口继续。窗口resize事件同样合并。完整DOM事件选项（例如AbortSignal）仍遵循当前事件系统的未完成边界。

resize只在通过窗口监听接口或onresize存在观察者时排入任务，移除最后观察者取消待派发事件；只有绑定/尺寸变化且无人观察时，不额外唤醒脚本。生成内容的缺省counter/quotes解析走无分配快路，层排序共用不可变无层标识，相同声明替换/四边展开有计算代次缓存；这些减少查询样式的重复工作，JS仍按原有100ms默认预算和显式调用预算处理，不延长预算掩盖耗时。

@layer支持顺序声明、命名/匿名块、点分名称、嵌套层及跨style/link同名合并；名称大小写敏感，条件未匹配的层不参与建立顺序。普通声明中后层优先，根的无层声明优先于命名层；important反向排序，元素inline对同重要级别的样式表优先。先比较层/inline，再比较原specificity和声明顺序，变量与普通属性共用。每代层树和状态属于单一Document，单父节点最多4096子层，递归64层、规则最多65536。

revert-layer按支持的长属性与已支持简写展开后回退，变量、自定义属性和伪元素也参与；important回退同时排除当前层normal/important区间，inline回退保留样式表important。revert回退作者来源。margin/padding显式展开四边，避免一个边的回退错误地消除其他边；相同替换值/展开声明在计算代次内复用。all、逻辑属性成组回退、@import layer、嵌套选择器、CSSOM分组编辑、完整字体/动画/注册属性的层优先级仍待实现；其他尚未支持的CSS不会因处于layer内自动成为已实现。

原始用户页面保留原文，测试覆盖其@layer内的响应式侧栏和明暗变量；其中.sidebar-foot的窄屏display:none位于layout层，后面的components层display:flex优先，因此真实层叠结果是flex。该结果不被测试改成作者可能期望的隐藏，也不修改用户HTML。页面整体视觉仍受既有SVG、定位、渐变、字体与其他现代CSS边界影响，机制通过不等于完整视觉对照通过。

参考CSS Media Queries Level4/5、CSS Cascading and Inheritance Level5/6及CSSOM View媒体变化规则。本阶段仅直接Debug/阶段SDK验证，完整发布矩阵和旧系统实机另记。
