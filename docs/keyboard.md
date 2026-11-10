# 键盘、焦点和DOM事件

文本控件的文字输入/组合事件、编辑默认动作、历史与候选定位入口已新增，具体范围见docs/editing.md。原生输入测试现单列.native，输入桌面不可用日常明确skip，正式full/release要求实际执行。

键盘通过View传入，共用Document的DOM焦点和事件。C ABI提供`xl_view_key`和`xl_view_set_keyboard_input`，C++包装为`view.key(event)`和`view.keyboard_input(true)`；不向宿主暴露SDL类型。

```cpp
xl_key_event tab{sizeof(xl_key_event), XL_KEY_DOWN, 0, 0, 0, 0, "Tab", "Tab"};
bool handled = view.key(tab);
view.render();
```

公开输入归Runtime UI线程，key/code为调用期间借用的NUL结尾UTF-8字符串，各最多128字节；key不能为空、code可以为空，拒绝非法UTF-8/控制字符、未知modifier位、无效location/repeat/reserved。handled为0/1，错误时不改写；不保留宿主字符串。文档已有即可派发，不要求先完成第一帧。事件立即进入脚本与默认动作，显示和AX在render后同步；脚本异常返回XL_SCRIPT_ERROR，超预算返回XL_TIMEOUT，已派发事件不重放。

key采用DOM命名，例如Tab、Enter、ArrowDown、a、中文字符或空格，code表示物理位置，如KeyA/Space。location为0普通、1左、2右、3数字键盘。提供Shift/Control/Alt/Meta/CapsLock/NumLock，repeat独立记录。宿主发送keydown/keyup，文字插入和IME组合不混用键盘逻辑键。

Tab按正tabindex升序、同值DOM顺序，然后零/默认可聚焦元素；Shift反向。负tabindex可程序聚焦，不进入顺序；从其位置继续时按DOM邻接方向处理。当前单文档在首尾循环，不把焦点移到宿主其他控件；嵌入宿主可取消Tab或自行接管。hidden、display:none祖先、inert、禁用表单控件和disabled fieldset后代跳过，首个legend内控件按例外处理。ARIA角色本身不赋予HTML聚焦/键盘默认行为，自定义控件应写tabindex和键盘处理；aria-disabled仍可聚焦，激活受禁用语义限制。

JS focus/blur、Tab和AX动作使用同一聚焦逻辑；无效目标不改变当前焦点。导航清空焦点，失去可聚焦资格的节点在下一次输入/查询/布局时清理。activeElement无控件焦点时回到body。focus/blur不冒泡，focusin/focusout冒泡，relatedTarget记录另一端；事件内重新聚焦使用代次检查，后发聚焦优先，避免旧请求覆盖新状态。焦点属于共享Document，不因多个View而复制DOM activeElement。

焦点快照在各View绘制可见蓝色轮廓，与选区叠加、裁剪和旧新损伤共用保留绘制；清除选区保留焦点轮廓。新聚焦目标在render时逐级滚入祖先及视口，支持hidden的程序滚动；之后主动滚走不会反复拉回。JS/AX/Tab聚焦一致，同文档各View仍各自保存滚动偏移。当前轮廓是固定样式，CSS outline、:focus/:focus-visible/:focus-within和输入方式对应的样式失效仍待接入。

默认动作在未preventDefault时执行：按钮Enter于keydown激活，Space于keyup激活；重复Space不多次激活，焦点改变/失焦/关闭输入取消待激活状态。复选框Space、JS click和AX激活共用checked状态；click处理器可读取预切换值，取消时恢复，成功后发送input/change。checked属性状态与HTML checked内容属性分别保存。链接Enter派发click；内部页面导航和表单提交的完整默认行为仍待实现。

非编辑目标的箭头按40 drawable像素、PageUp/Down按滚动区高度的7/8、Home/End按边界滚动；Space/Shift+Space翻页。优先目标祖先滚动区，无法消费时向外查找；hidden不接受键盘用户滚动。文本控件/contenteditable不被当成页面滚动目标，后续编辑层接其光标与输入默认动作。

DOM事件分派支持捕获/目标/冒泡、eventPhase、固定传播路径、preventDefault、stopPropagation/stopImmediatePropagation、currentTarget/relatedTarget、键盘字段和getModifierState。监听支持capture、once、passive、函数及handleEvent对象；传播期间移除的监听跳过，passive不能取消默认动作，on处理器返回false取消。分派后currentTarget为空、eventPhase为0；宿主输入isTrusted为true，JS click为false。外部事件有100ms执行预算和微任务检查点；无脚本监听时不会为键盘或焦点创建JS VM。回调异常按SDK错误返回并停止当前事件，不等同浏览器全套reportError策略。

独立窗口可选有界128项键盘队列；溢出/失焦加入重置以清掉Space待激活，默认关闭。SDL物理扫描码与当前布局逻辑键分别映射code/key，保留左右键、数字键盘、重复与修饰状态。宿主继续pump/render，needs_frame包含待输入；借用窗口和离屏视图通过xl_view_key转发，WebTest独立窗口默认启用。Windows定向验证使用实际前台测试窗口与SendInput，结束恢复原前台窗口，隐藏窗口PostMessage不冒充系统raw-keyboard验证。

当前不是完整编辑/输入系统：radio组/完整表单、keypress与文本插入、编辑撤销、系统快捷键/剪贴板、IME/dead-key/AltGraph、窗口EventTarget、事件构造器/dispatchEvent、Shadow DOM传播、完整on处理器注册时序和平台AX事件/读屏仍待实现。Linux离屏行为通过不能替代Linux桌面真实键盘或目标旧系统验收。
