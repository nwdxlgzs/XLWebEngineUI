# 动态焦点样式

`:focus`、`:focus-within`、`:focus-visible` 从 Document 的共享焦点状态匹配，C/C++ query、JS querySelector/querySelectorAll、样式层叠与绘制共用结果。HTML 的 focus 属性不决定焦点；适配器不添加、移除或伪造 DOM 属性。导航清空焦点和输入方式状态，DOM 移动、脱离、disabled/inert 变化参与当前匹配与后续焦点有效性检查。

目前采用清晰可重复的 focus-visible 策略：初始/键盘方式聚焦显示提示，文字控件始终显示提示；真实 View 指针选择切换为指针方式，普通按钮不显示提示；接下来的非修饰键 keydown 恢复键盘方式。显式脚本/AX 聚焦按当前输入方式与是否文字控件决定。完整平台偏好、触屏和仅快捷键的细化启发式仍待补。默认蓝色焦点轮廓遵循此状态，并已改用真实outline样式/片段绘制，作者可覆盖样式、颜色、宽度和offset；支持范围及保留绘制说明见outlines.md。

`style/focus_selectors.*` 将解析后的动态伪类临时替换为等价结构路径，并保留原始 selector list 的 specificity。适配覆盖组合选择器、:is/:where/:not/:has 与 nth-child/nth-last-child 的 of 列表；字符串、转义类名、同名属性不会被误替换。RAII 在匹配结束或异常时还原原 AST，每个临时解析内存独立释放；层叠顺序与 !important 继续交给原有样式引擎。公共 SDK 不暴露 Lexbor 类型。

锁定版本的 Lexbor 解析器会拒绝 focus-within/visible，且未计入普通伪类权重。third_party/patches/lexbor-focus-parser.json 定向放行这两种语法并补上伪类权重，CMake 验证每个替换只匹配一次，仅在 build 内生成有修改标记的单个 C 源文件，原始依赖源码不变。原语法、转义与声明源偏移继续由原解析器处理；匹配语义由上述引擎适配负责。配方目录内容进入现有精确依赖键，仅需重编译 Lexbor 并更新 Debug SDK 输入，其他依赖复用，无全套依赖行为重验。quick复用目录时同时刷新当前输入的GC引用，保护实际仍用的依赖，并允许退役旧版本。

路径以 :root 和元素 nth-child 序列构成，注释/文字兄弟不计入序号，不依赖 id 唯一。动态焦点祖先链限制 256 层，单份临时路径表达式限制 1MiB；超限明确返回 UNSUPPORTED。当前仍按既有 revision 重建样式与布局，再通过保留绘制损伤更新像素；静止不重新绘制。该实现尚未完成增量样式依赖索引、Shadow DOM、完整动态伪类、复杂轮廓、CSS嵌套规则或完整选择器规范覆盖。

定向测试覆盖焦点/祖先/复合/嵌套函数/相邻兄弟、选择器权重、共享 View、真实指针入口切换方式、控件可见提示、事件重入、DOM 移动/脱离/disabled/inert/隐藏、导航、重复匹配与局部/完整像素一致和静止复用。测试证明当前适配范围，不替代真实系统输入法或平台读屏验收。
