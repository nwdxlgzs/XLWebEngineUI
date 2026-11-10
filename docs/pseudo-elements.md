# 作者生成内容

本阶段支持作者 `::before` / `::after`（含单冒号旧写法）。选择器匹配来源元素，保留伪元素的类型权重、完整作者顺序及important；普通元素声明不会误用于伪元素。生成项使用独立私有渲染节点，不加入作者DOM，不影响children、querySelector、:first-child/:nth-child、cloneNode、innerHTML或textContent。

生成项沿用现有每元素级联/计算样式代次。自定义属性从来源元素的已计算scope继承，可在伪元素规则中覆盖，循环/失效/嵌套fallback与普通元素共用；原始作者AST不按元素改写。font/border/outline/Flex/Grid等已支持的属性、简写和变量进入相同布局绘制路径。普通流、inline、inline-block、块、Flex/Grid子项、intrinsic计算、overflow裁剪/滚动、墨迹损伤和静止帧复用均复用现有链路；高级CSS本身未支持的属性不会因用于伪元素而自动支持。

content支持相邻CSS字符串、转义（含Unicode）、空串、原始字符串attr(name)及字符串fallback、`/`后的替代无障碍文本，并已扩展counter()/counters()及quote关键字。attr取来源元素的当前属性，作为文字使用，不重新解释为CSS或HTML。none/normal不生成盒；空串仍可生成带尺寸/背景/边框的盒。不支持的图片/url/leader等形式计入unsupported_layout_nodes，不伪造绘制或发网络请求。typed/namespaced attr与完整图片替换继续后续。

来源元素的属性/class/open/checked/focus与inline样式变更、样式资源替换/卸载都会重算生成项。节点私有身份在同一Document内稳定，导航销毁代次和生成树；重复布局不会每帧新增生成节点。原始第三方源码不修改；Lexbor语法放行和权重/旧别名适配全部由third_party/patches/lexbor-focus-parser.json从锁定源重建。

查询可使用 `getComputedStyle(element,'::before').getPropertyValue(name)`、C `xl_document_get_pseudo_style` 和C++ `Document::pseudo_style`。支持before/after，名称ASCII大小写不敏感；content返回已解析文字的CSS字符串（可含替代文字），无生成值返回none。其他属性沿用当前样式查询的覆盖范围和XL_NOT_FOUND契约；还不是完整浏览器的computed CSSOM。普通getComputedStyle不混入生成项样式。

AX将可见生成内容作为来源元素下的文字项，控件基于内容的名称也包含生成内容；替代文字优先，显式空替代文字可隐藏装饰的语义。AX身份/几何和动态名称更新来自同一布局代次，旧不可变更新对象保持原值。完整AccName仍有既存边界，不能以此宣称读屏全验收。

生成文字没有作者DOM的UTF16偏移：公共文字选区和复制只使用真实作者文字，避免将装饰伪装成可编辑节点；AX仍可读取生成文字。指针几何命中生成盒或其文字后回到来源元素，再走原生焦点/可取消click/summary默认行为；作者监听器得到来源目标。完整DOM PointerEvent/堆叠/变换命中仍待。input/textarea/img/video等替换元素不在本阶段生成before/after盒。

生成content单值最多64KiB/8192个token、attr fallback深度32，Document每代生成文字/替代文字合计4MiB；计算样式仍沿用变量代次预算。私有生成节点最多32768个盒（另有各自文字节点），防止无限新身份积压；导航释放。仅空串块装饰不会主动准备ICU，实际文字/段落或原子排版首次需要时才走既有懒加载。

阶段SDK使用流式level6 ZIP/TGZ，保留安装输入清单、最终解包逐文件哈希、解包WebTest实跑、独立符号和暂存清理。正式full的CPack流程保持独立；level调整不替代发布核验。

后续已接入counter()/counters()、quotes及q默认引号，详见generated-content.md；inactive旧文字会释放缓冲后保留节点身份。规范依据：W3C CSS Pseudo-Elements Level4的before/after及继承规则，CSS Generated Content Level3的content与替代文本，CSS Values的attr字符串。完整::marker/::first-letter/::first-line/::selection、图片content、嵌套/条件规则和其他现代CSS继续完整PLAN。
