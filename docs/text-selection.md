# 文本光标与选区

新增input/textarea编辑选择保存在Document控件状态，通过document_get_edit_state/set_edit_selection访问；原有View选择仍用于文档阅读。控件元素ID可查询值文本的range/caret/hit几何，选中原始值与密码圆点映射独立于DOM子文本，具体见docs/editing.md。

选区属于 View。多个视图可以显示同一个 Document，并保持各自的选区；设置选区不会改变 DOM、焦点或文档布局修订。宿主通过 C ABI 或 C++ 包装操作，无需依赖 Skia/SDL 类型。

```cpp
auto begin = view.hit_text(x1, y1);
auto end = view.hit_text(x2, y2);
view.selection(begin, end);
auto original = view.selected_text();
auto highlight = view.selection_rects();
auto caret = view.caret(end);
view.clear_selection();
// 或在鼠标按下/拖动时分别调用：
view.select_at(x1, y1);
view.select_at(x2, y2, true);
```

`xl_text_selection` 保存 anchor/focus，端点沿用 `xl_text_position` 的节点 ID、DOM UTF-16 偏移与 affinity。保留选择方向，内部按文档顺序计算范围。端点必须是当前布局中的文字节点，偏移不能越界或切开代理对；组合字符内部偏移的光标贴到相应整形簇边界。折叠空白与 CRLF 使用已有 DOM 到段落映射，装饰占位不会进入文本。upstream/downstream 用于软换行和双向文字边缘；复杂 bidi、精确连字内部插入点仍需后续完善。

选择可以跨越普通文本、inline-block内部及嵌套原子盒。文档快照单独保存DOM文字顺序，原子内容在独立段落中的存储或绘制位置不会改变selected_text顺序；占位不会成为选中文本。坐标命中使用各源节点的实际文字片段，范围/光标/AX取子布局平移后的相同几何。片段在布局时一次准备，拖动不重复生成全部文字范围。

Flex的order、反向和RTL布局以及Grid放置/区域/order可以改变视觉位置，选区原文仍按DOM顺序读取。匿名文字item、嵌套Flex/Grid和原子盒的最终片段共同参与命中、范围与AX，视觉重排不会把组件内容在复制文本中重复或乱序。等距离重叠片段选择后绘制者；overflow裁剪与滚动已进入相同几何链，完整遮挡命中仍待后续完成。

C API 为 `xl_view_set_selection/get_selection`、`xl_view_selection_rects/selected_text/caret_rect`、`xl_view_select_at` 和 `xl_view_set_selection_input`。传 NULL 给 set_selection 清除；相同端点绘制一像素光标，非空范围绘制半透明高亮。selected_text 返回原始 DOM UTF-8，保留原空白，不插入块间或 br 分隔符；它不是浏览器 Selection.toString 的完整复刻。缓冲查询不执行脚本、不修改选择，小缓冲不部分写入。

公开操作归 Runtime UI 线程。需要已有的成功文档布局；DOM/尺寸修订后返回 XL_BUSY，render 同步后继续。仅选区待绘制时仍可连续更新选区和查询几何，拖动不需要先提交上一帧；其他视觉快照查询保持原有待渲染规则。样式或重排后保留合法 DOM 端点并重算几何；节点被替换、脱离可布局树、偏移失效、导航或换 Document 后清除。当前不会像浏览器 Range 一样自动跟随 DOM 文字插入调整偏移。

RasterView 保留高亮/光标矩形，只将旧新覆盖范围加入损伤；静止页不重复布局、栅格化或提交。重叠的相邻文字片段合并，避免同一字形被半透明高亮重复着色。光标不自行闪烁，不启动定时器、工作线程或事件循环；闪烁/输入法/编辑命令属于后续输入层。

独立窗口可通过 `view.selection_input(true)` 显式启用左键点击/拖动和 Shift 点击。事件由宿主 `xl_runtime_pump_events(..., XL_PUMP_NATIVE_EVENTS)` 驱动，render 消费每窗口有界事件队列；鼠标坐标按窗口逻辑尺寸换算到 drawable 像素，移动事件合并，失焦停止拖动。默认关闭；借用窗口和离屏视图由宿主转发坐标到 select_at，不接管宿主窗口输入。WebTest 已为自己的窗口启用。键盘扩展选择、系统剪贴板、编辑/IME、自动滚动和 JS Selection/Range 尚未接入。

overflow裁剪与滚动会同步范围/高亮/光标和命中，完全隐藏的范围不返回矩形，完全隐藏的光标返回零面积；原始选区端点和selected_text仍保留。node_bounds返回完整布局盒，AX几何取可见交集并标记全隐藏节点OFFSCREEN。具体滚动契约见docs/scrolling.md。

平台 AX 快照与文字范围仍取相同布局；选区 API 可供后续 UIA/AT-SPI 文本接口使用，但目前并不代表平台读屏选区事件或完整 AX 文本桥已完成。
