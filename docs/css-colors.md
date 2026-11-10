# CSS 颜色

`src/style/color.cpp` 负责框架自己的颜色表达式、颜色空间转换与 SDR 输出。表达式不可变，
不依赖平台颜色解析器；第三方锁源没有为此修改。颜色先保留空间、双精度分量、alpha 和
`none` 标记，继承、`currentColor` 与混色期间不提前转成 8 位 RGB。

本阶段支持：

- 命名颜色、hex、RGB/RGBA、HSL/HSLA、HWB、Lab/LCH、Oklab/OkLCH；现代空格和旧逗号语法
  分开校验，转义和注释交给 CSS tokenizer，能够区分颜色参数中的真实 token 边界。
- `color()` 的 sRGB、线性 sRGB、Display P3、线性 Display P3、A98 RGB、ProPhoto RGB、
  Rec.2020、XYZ D50/D65；转换包含传递函数、白点适配与极坐标转换。
- 两颜色的 `color-mix()`，支持显式插值空间、默认 Oklab、四种色相路径、百分比省略及
  归一化、总和不足 100% 时的 alpha 衰减。非色相分量按 alpha 预乘；`none` 在对应空间
  借用另一颜色的分量，转换时处理 analogous/remaining 分量和转换产生的无效色相。
  同空间直接指定的灰色色相保留，不把它误当作转换产生的缺失色相。
- `light-dark()` 在元素实际使用的 `color-scheme` 下求值，可以嵌套混色或引用
  `currentColor`。`color-scheme` 的继承、`normal`、`only` 和 light/dark 支持列表参与
  求值；宿主媒体偏好在元素允许的方案中选择，不把该函数直接等同系统明暗偏好。
- 颜色通道内有界的 `calc()`、`min()`、`max()` 和 `clamp()` 数值计算，校验 number、
  percentage、angle 的类型及除零。它不是项目其他长度属性的通用 CSS 数学实现。

`VariableStyles` 与原有重要性、层、specificity、声明顺序及变量回退共用同一候选选择。
Lexbor 不支持的颜色形式保存在框架私有候选中，不注入 DOM 属性，也不改作者的 HTML、
style 文本或挂载 CSS。边框与 outline 简写展开保留完整颜色函数；变量计算无效使用
unset，语法无效的常量声明丢弃，二者不会错误地采用相同的回退方式。

前景、背景、四边边框和轮廓保留独立的高精度计算值，绘制时才产生 ARGB8。
根背景是否透明依据计算后的 alpha 判断，不能因为 RGB 分量非零而阻止 body 背景
向画布传播，也不能把微小的非零 alpha 量化成零后误判为 CSS transparent。
块、inline、生成内容、文字、裁剪和保留式失效继续使用同一套绘制值。

SDR 超色域映射采用固定 Oklch 明度/色相、减少 chroma 的 binary search + local MINDE，
Oklab JND 为 0.02，停止容差 0.0001。它与浏览器某些 Canvas2D 路径的直接裁剪并不相同；
本机 Rec.2020 样例观察到最大 3/255 通道差异，未把该样例写为像素完全一致。
原生广色域 surface、HDR、ICC 配置和其他 gamut mapping 策略仍待后续。

表达式缓存归 Document/编译样式表所有，并沿用样式来源和原声明的隔离。它与已有解析
缓存合计按 2048 项或 2 MiB 键文本阈值，在下一代清空 cascade 后回收；超额结果只活在
当前代。该阈值不是整个 AST 的总内存上限。没有新增工作线程、资源文件或 ICU 预热；
纯颜色页面保持零解压。单颜色输入不超过 64 KiB、8192 tokens、64 层括号、32 层表达式
递归和 1024 个颜色节点；标量输入绝对值上限为 1e9。

`xl_document_get_style` / 现有 JS `getPropertyValue()` 继续返回选择后的声明序列化，
不是完整浏览器的 resolved/computed color API。已有 Lexbor 支持的拼写维持原查询契约，
新颜色形式保留替换变量后的 token 文本。公共 SDK 没有新增第三方类型或导出。

相对颜色、`contrast-color()`、`device-cmyk()`、完整系统色/forced-color 调色板、注册颜色
配置、多于两颜色的 mix、完整 CSS 数学/颜色动画、meta color-scheme 与页面视觉全覆盖
继续推进。渐变与多层背景已通过私有背景适配接入，具体形式、插值与验证边界见
docs/backgrounds.md；URL图片等仍待。未支持节点计数只覆盖已埋点的机制，零计数不代表完整兼容。

测试包括颜色数值和转换往返、alpha/缺失分量/色相、非法语法、变量/层回退、宿主偏好和
作者 `:has(:checked)` 主题切换、边框/轮廓/伪元素像素、缓存回收、静止帧及真实 SwiftShader。
用户网页原文只读消费。Windows/musl 的实际结果及阶段包以 TODO 的最终记录为准；
旧系统、macOS/Web 和完整引擎验收不由这些局部测试替代。

语义与公式依据：W3C CSS Color 4 的 Color Syntax、Color Interpolation、Gamut Mapping
及 Sample Code，以及 CSS Color 5 的 color-mix/light-dark 与 CSS Color Adjustment 的
color-scheme。来源为 `https://www.w3.org/TR/css-color-4/`、
`https://www.w3.org/TR/css-color-5/`、`https://www.w3.org/TR/css-color-adjust-1/`。
