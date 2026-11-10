# Label、radio 与动态 checked

`label[for]` 和包裹控件的 label 使用同一个可取消 click 激活路径；label 内普通文字/元素可触发代理，内部链接和其他交互控件保留各自行为。允许代理到 display:none 的 checkbox/radio，符合用户示例的纯 CSS 交互需求。隐藏控件不因此获得键盘焦点。disabled/inert、click 取消、同节点同步递归和焦点事件重入均受统一入口管理。

radio 按树、非空 name 和 form owner 互斥，支持 form 属性关联；设置 checked 属性值与 JavaScript checked 实时状态分开。已选 radio 再次激活不会重复 input/change，取消激活恢复先前同组状态。动态 :checked 用独立、可还原的选择器 AST 适配进入查询和样式计算，组合选择器、嵌套 :is/:not/:has/of 及作者 specificity 保留，避免把运行时状态写成 HTML checked 假属性。option 的 selected 属性也进入 :checked 匹配；完整 select 选择模型仍待实现。

当前路径表达式上限 1MiB、树深度 256。完整 radio 默认初始化/动态 name、type、form、checked attribute 的重分组、表单 reset、组内方向键和单个 Tab 停靠、select/options、pointer 完整事件序列、details/summary、伪元素和 CSS 变量均继续跟踪，不能用标签页交互通过宣称示例已达到浏览器视觉一致。

`document.checkable` 直接读取用户原始示例检查标签页切换和主题 checked 状态，不修改文件。`webtest.user_pages` 在每次选中运行时发现 tests/user-dev-tests 的 HTML/HTM 并执行 WebTest --check；它验证加载和绘制成功，不证明所有 CSS 功能已实现。新增网页可直接纳入此 smoke；针对新机制继续补行为和几何断言。
