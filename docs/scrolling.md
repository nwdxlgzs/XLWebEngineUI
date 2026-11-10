# 裁剪与滚动

滚动位置属于 View。同一 Document 可以显示在多个窗口或离屏视图中，各自保持滚动位置和选区。滚动不修改 DOM、文档 revision 或焦点；修改文档尺寸后按新范围夹取，导航或切换文档时清空。

```cpp
auto panel = document.query("#panel");
auto info = view.scroll(panel);
view.scroll_to(panel, info.x, 120);
view.render();
// 0 表示视口；正位移向右、向下。
view.scroll_to(0, 0, 200);
// 宿主的滚轮/触控板桥，参数均为 drawable 像素。
view.scroll_at(pointer_x, pointer_y, delta_x, delta_y);
```

公开 C 接口为 `xl_view_get_scroll/set_scroll/scroll_at/set_scroll_input`，归 Runtime UI 线程。get_scroll 需要当前成功布局；输出包含位置、client/scroll 尺寸、最小/最大偏移和两轴的程序/用户滚动标志。set_scroll 接受有限浮点数，在最近有效布局的范围内夹取；允许在下一帧前多次更新位置。滚动待同步时几何、文字、选区和 AX 查询返回 XL_BUSY，render 后重新一致。无滚动容器返回 XL_NOT_FOUND，错误不写输出。

普通块、inline-block、Flex/Grid支持overflow-x/y的visible、hidden、auto、scroll、clip及普通overflow一至二值简写、inherit/initial/unset。与可滚动轴组合时，visible计算为auto，clip计算为hidden；单独clip不创建滚动容器。hidden允许程序滚动，不接受用户滚动；auto/scroll接受二者。裁剪取padding边，边框和自身背景保持原位；普通inline不建立滚动容器。RTL及Flex反向溢出使用负偏移范围。Flex/Grid可滚动item的自动最小宽/主轴尺寸不再强制最小内容尺寸。

根元素或按规则传播的body overflow用于视口，公开节点0访问；普通body独立约束不冒充视口。滚动范围由子布局的可见溢出和自然高度计算，嵌套裁剪不把内部隐藏内容泄漏到祖先范围。scroll_at先定位指针处的滚动子树，未消耗的两轴位移逐级传给祖先；普通视觉遮挡和定位/变换系统尚未完成，不宣称完整浏览器命中行为。

背景/边框、文字、原子子场景均保存同坐标系的两轴裁剪，祖先平移和裁剪依次组合。文字范围、高亮及光标返回裁剪后的可见几何，完全隐藏的范围没有矩形；命中不会选择裁剪外或全隐藏的文字。node_bounds仍返回滚动后的完整布局盒，便于宿主定位；AX几何取可见交集，全隐藏节点带XL_AX_OFFSCREEN，语义节点和完整名称保留。selected_text仍按DOM端点读取原文，隐藏部分不会因裁剪而丢失。

RasterView比较旧新位置、裁剪和内容，仅重画可见损伤；原子场景按同样的clip计算墨迹和内容相等性。静止帧继续复用。同revision/尺寸的纯滚动现在复用布局和段落，仅投影几何、裁剪及AX可见边界，layout_frames不增加；原子inline场景和多View保持独立。机制、像素对照和性能边界见docs/scroll-projection.md；独立滚动合成与更细失效继续推进。

独立窗口通过`view.scroll_input(true)`启用有界滚轮队列，默认关闭。SDL浮点滚轮单位按48逻辑像素转换为drawable像素，按指针位置合并相邻事件；宿主继续pump/render，needs_frame包含待处理输入。借用窗口和离屏视图由宿主调用scroll_at，WebTest为自己的窗口默认启用。

键盘滚动、焦点与编辑光标自动显现已接入，见docs/keyboard.md及docs/editing.md。当前仍没有滚动条控件、预留gutter、拖动thumb、惯性/平滑/吸附、CSS overflow-clip-margin、完整margin/定位溢出传播或JS scrollTop/scrollLeft/window.scrollTo。浏览器几何对照在双方统一使用scrollbar-width:none，避免平台传统滚动条的占位差异干扰核心尺寸验证。平台AX滚动动作/完整事件仍待接入；本阶段不等于完整Overflow/CSS验收。
