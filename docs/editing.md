# 文本控件与组合输入

文本控件用Document私有Editor保存实时value、UTF-16选择、组合预编辑和有界历史。`input`的text/search/tel/password/url/email及未识别文本类型、`textarea`接入这条路径；number/date等特殊表单控件与富文本contenteditable还需各自实现。DOM的value内容属性、textarea默认子文本和运行时实时value分别保存，JS、C/C++和View用户输入访问同一状态。

```cpp
auto field = document.query("#editor");
document.value(field, "初始文字");
document.edit_selection(field, 0, 2);
// 先由程序、Tab或AX将field聚焦；然后传入用户实际提交的文字。
view.text_input("替换");
view.render();
auto state = document.edit_state(field);
```

C接口为document_get/set_value、document_get_edit_state/set_edit_selection、view_text_input/set_composition和view_ax_value。归Runtime UI线程，不持锁调用宿主或脚本；输入文本按UTF-8复制，上限4MiB，拒绝非法UTF-8和NUL，选择/组合范围按UTF-16计数并拒绝代理对中间边界。程序选择夹取至值长度；小输出缓冲不写部分文本，错误不改handled。布局尚未准备好的字素/词键盘操作可返回BUSY，宿主等待懒加载文字服务后重试，不用按键字符代替文本输入。

JS暴露value、defaultValue、selectionStart/End/Direction、setSelectionRange、select和readOnly；checked仍属于前一阶段的独立状态。程序赋值不发input/change，设置dirty value并清历史及组合状态；未dirty时内容属性或textarea默认子文本继续更新实时值。input移除CR/LF，textarea把CRLF/CR规范为LF，url/email额外修剪ASCII外缘空白。完整各类型HTML清洗、表单复位/提交/验证和完整Selection API继续待补。

焦点文本控件支持Backspace/Delete按ICU字素边界删除，Control/Meta加词边界；左右键及Shift扩选、Control/Meta+A、Home/End和上下行导航、Control/Meta+Home/End、textarea Enter换行。竖向使用真实段落caret/hit几何及保持的目标横坐标；复杂双向视觉光标、Page导航和平台快捷键约定仍待补。Control/Meta+Z、Shift+Z、Y访问独立控件历史，最多64项且总快照预算4MiB（单项最大值另有4MiB上限）；一次组合提交只占一个undo项。readonly允许聚焦/选择，阻止用户修改；maxlength按UTF-16截断且不切代理对。

用户编辑先派发可取消beforeinput，允许时提交并派发input；离开焦点时对真实用户改值发送change。data/inputType/isComposing共享现有事件链和100ms预算。回调内改变value、选择、焦点、只读状态或连接关系后，通过版本/焦点代次检查阻止旧操作覆盖新状态。beforeinput取消保留原值和选择；程序赋值不伪造用户事件。当前输入类型包含insertText/insertLineBreak、字素/词删除、historyUndo/Redo、insertCompositionText/insertFromComposition；尚不是完整Input Events或浏览器异常报告实现。

组合接口UPDATE以当前选择建立原始快照，发送compositionstart/update，并用不可取消的insertCompositionText更新可见预编辑；UTF-16相对选择随预编辑更新。COMMIT用提交文字替换原始选择，发送可取消的insertFromComposition和compositionend，形成一个历史项；未处于组合时按普通插入处理。CANCEL、Escape或失焦恢复原快照并结束组合；显式程序改选择会保留当前预编辑、结束组合并记一个历史项，程序整体赋值替换并清除组合。组合事件与input回调的状态修改同样受版本检查保护。具体平台IME的事件先后约定还需要实机对照，不宣称完整UI Events组合协议一致性。

value通过现有TextParagraph/Skia实际绘制。默认控件尺寸由20个0字宽、textarea两行及size/cols/rows决定，可由CSS尺寸覆盖；文本类型默认单行pre、textarea默认pre-wrap，padding-box内裁剪/滚动。空值、placeholder及尾部换行保留可用光标。控件元素ID同时作为值文本的范围/光标/命中键，区别于普通DOM文本节点；原子/Flex/Grid父级继续合并同一套几何和绘制场景。密码用圆点绘制，保留原值UTF-16到显示偏移映射，不向AX输出明文。

编辑选择属于共享控件状态，区别于原有View的文档阅读选区；通过edit_state查询。显示编辑高亮、光标、组合下划线及焦点轮廓，叠加现有裁剪和旧新损伤。编辑或选择变动时把caret逐级滚入控件和祖先，静止不重新布局/栅格化/提交。value、文字范围及AX在render后对应同一快照；view_node_bounds保留完整控件盒，view_ax_value读取快照值，readonly/protected状态同时提供。密码显式document_get_value仍返回完整值，AX值只返回圆点。

原生窗口在可写文本控件聚焦时显式开启SDL文本服务，退出时停止；将最终drawable caret换算成逻辑坐标设置文本输入区域，相同区域静止复用。按键、文本提交、预编辑及启用键盘时的文字选择指针在同一有界128项/4MiB队列按序消费，移动事件可合并；失焦/溢出清理待激活/组合状态。SDL编辑事件的字符计数转换为UTF-16，空预编辑与紧随其后的text提交共同处理。宿主仍负责pump/render，借用/离屏通过公开接口转发，不引入后台事件循环。

Windows原生测试独立为document.keyboard.native和document.editing.native；交互输入桌面不可用时日常quick明确skip，不能算通过。正式full/release设置XL_REQUIRE_NATIVE_INPUT=1，将该条件转为失败，不能以跳过项宣称完整验收。当前Windows处于Screen-saver输入桌面，实际系统IME/候选窗口和新原生文字链尚未验收；公共组合接口的离屏测试不能替代系统IME。Linux/macOS/Web的输入法和候选定位也需要各平台证据。

纯文本剪贴板与命令历史已接入同一编辑状态，契约见 clipboard.md。后续仍需富文本contenteditable、光标选择与文字插入的完整DOM变更模型、JS Range/Selection、富文本剪贴板/撤销分组、完整特殊表单类型、平台IME实测、自动填充、触屏输入、平台AX文本事件/读屏及CSS动态focus/outline。当前文字控件编辑子阶段不等于完整输入或网页兼容验收。
