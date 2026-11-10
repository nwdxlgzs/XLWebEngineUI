# details / summary

本阶段在Document内提供原生披露控件。首个直接summary是标题；其余summary是普通内容。关闭时所有非标题内容（包括裸文本和嵌套控件）不参与布局、文本命中、焦点和AX，作者的display声明不能强制显示关闭内容。打开后标题先于正文布局。DOM查询和textContent仍可访问原文。

summary及其非交互后代的click在未取消时切换父details的open。内嵌按钮、链接、label等保留自己的激活行为。原生标题进入Tab顺序，Enter按下激活，Space释放激活，keydown取消同样阻止默认行为。后来添加/移除summary会立即改变标题身份。ARIA角色本身不增加HTML默认操作。

open是布尔存在属性，JS `.open`反射，`open="false"`仍为打开。非空name按同一DOM树和大小写精确分组：显式打开关闭同组其他成员；改name或插入时已有打开成员优先。解析多个open保留首个。独立树、Document、Runtime不互相影响；innerHTML、clone和fragment插入复用同一文档逻辑。

open存在状态变化排入Document任务，不在setter中同步回调。每个details合并连续切换，保留第一次oldState和最后一次newState；即使最终回到原状态也发一条事件。toggle使用ToggleEvent，非冒泡、不可取消，原生事件可信。任务内产生的新任务留到下一轮；导航释放旧队列。沿用单轮64任务/100ms预算和Promise检查点，next_wakeup_ms在有待处理toggle时返回0。监听器异常按现有脚本错误契约返回。

默认标题前绘制随open和方向改变的三角标记，来自原生Skia形状，不依赖字体中的箭头字形，不加入DOM文字或选区。支持summary的默认list-item、`list-style:none` / `list-style-type:none`隐藏标记；显式其他display使用作者布局。无作者summary时绘制默认英文“Details”，使用details句柄代理默认标题的键盘和AX动作；其AX几何只覆盖标题，作者DOM不注入summary。普通JS details.click()不代替这个原生标题。

AX标题为button，公开EXPANDED/COLLAPSED及FOCUS/ACTIVATE/EXPAND/COLLAPSE动作；EXPAND/COLLAPSE幂等，不合成click，直接修改真实open并排toggle。Windows UIA提供IExpandCollapseProvider、状态属性和变化通知；COM线程仍只读不可变树，动作通过有界队列由UI线程执行，沿用超时取消和导航身份检查。其他平台目前仅引擎AX快照，不代表已完成其系统桥。

`xl_view_pointer` / C++ View.pointer接收主指针按下、释放、移动和取消，坐标为drawable像素，要求当前布局；按下/焦点回调使布局过期时宿主先render再继续。几何命中不依赖aria-hidden，普通内容点击不受ARIA隐藏控制。按下聚焦最近可聚焦祖先；释放向按下/释放目标的共同祖先派可信click，距离超过4像素的拖动不激活。取消、原生失焦、导航、重新绑定文档清除按下状态。原生窗口同时开启keyboard_input和selection_input走同一入口，WebTest已经开启；嵌入/离屏宿主自行转交。select_at仍只操作选区。

这是一条主指针默认行为桥，不是完整DOM MouseEvent/PointerEvent：坐标事件、pointer capture、多指针、触摸/笔、原生拖放仍待。命中沿当前布局几何和DOM顺序，完整重叠堆叠上下文/变换命中待实现。嵌套禁用按钮后代的原生指针焦点/激活已加祖先guard，fieldset首legend保持例外。作者before/after和用户页summary::after装饰已进入后续生成内容实现，见pseudo-elements.md；默认标题本地化、完整marker/list-style级联、动画、details匿名正文容器与全部CSS组合仍待，不声称整页视觉一致。

规范依据：WHATWG HTML interactive-elements（details / summary、name排他组、toggle任务）；Microsoft UI Automation ExpandCollapse Control Pattern。
