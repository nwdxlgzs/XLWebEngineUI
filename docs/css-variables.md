# CSS 自定义属性和变量

样式层支持大小写敏感的 `--name`、继承、级联特异性、作者顺序和 `!important`。`var(--name, fallback)` 接受嵌套 fallback、空 fallback、转义名称和 token 内的字符串/注释；替换保留 token 边界，不能把 `var(--number)px` 拼成原先不存在的尺寸 token。自定义属性的 `initial` 为保证无效值，`inherit`/`unset` 从父元素继承。父属性先完成变量解析再继承，子元素覆盖同名依赖不会改变已继承的计算值。

每元素依赖图包含 fallback 中的引用；循环内的变量失效，循环外的调用者仍可选择 fallback。存在但为空的变量不会选择 fallback。语法正确但缺变量或替换后不满足目标属性语法的声明在计算阶段成为 `unset`，保留它的层叠位置，不重新选择更早声明。语法本身无效的 `var()` 则在层叠前丢弃。

解析通过 Lexbor 的 CSS tokenizer，框架保存每元素的计算代次和独立 arena。共享作者声明不按元素重写；生成 longhand 保留原声明的来源位置、特异性和 important。更新代次前先清除全部元素的层叠引用，再释放旧代次，连同脱离文档的子树处理。`getComputedStyle(element).getPropertyValue('--name')` 和现有 C 样式查询共用计算值；行内 style 属性及 CSSOM 作者值保持原文。

当前可用于已经实现的颜色、尺寸、间距、字体、边框、outline、Flex/Grid 等属性及其已支持的 shorthand。补齐 `background` 的单一颜色路径；背景图、多层背景、渐变、动画变量、`@property` 注册、嵌套规则和完整 `@layer`/条件规则仍待实现。变量可以保存其他 token，但这不会自动增加对应属性或 `calc()` 的布局能力，也不等于完整现代 CSS 支持。

资源限制：单次展开至多 8192 token、64 KiB 文本；解析深度 128，代次计算变量的 token 存储和生成值共用 8 MiB 预算。超限的变量成为无效值，可由调用者选择 fallback；不无限展开成倍引用。预算之外仍有作者 AST、匹配引用和继承 scope 元信息，不能把此预算当整个 Document 的内存上限。

`document.variables` 和 `document.variables_static` 直接核对实际颜色像素、尺寸、两级继承、大小写、important、循环、空值/无效值、转义、token 边界、shorthand 顺序、脱离/重挂、动态修改及静止帧复用。链接样式表重挂载/卸载与 Runtime 映射隔离共用同一计算入口。动态测试还只读加载用户原始示例，检查日夜变量和后代继承，不改写用户 HTML；网页 smoke 继续单列，不能代替完整视觉兼容性。随包 WebTest 示例使用同一变量路径。

语义基准：W3C CSS Custom Properties for Cascading Variables Level 1；实现上限和未支持范围以上述实际行为与阶段报告为准。
