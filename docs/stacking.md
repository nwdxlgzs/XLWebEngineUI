# 层叠绘制、命中与阅读顺序

position、z-index 与 isolation 进入同一私有层叠计算。支持 auto、有符号整数、CSS变量、继承、重要性、层与 revert/revert-layer；整数范围至少覆盖32位，超限钳制。普通静态盒的 z-index 不生效，定位盒及直接 Flex/Grid 项目的整数值建立层叠上下文，isolation:isolate 也建立独立上下文。默认 auto 与显式0有不同的后代归属：auto定位组和原子inline的定位后代仍可参与最近真实祖先上下文，显式0则把后代留在自身上下文。

私有 PaintCommand 引用实际的背景/边框、段落分片和轮廓。真实上下文按自身背景、负层级上下文、正常流block背景、inline与原子内容、auto/0定位组、正层级上下文、轮廓的顺序绘制；同层按树序，Flex/Grid使用order修改后的顺序。匿名文字项也参与对应排序。普通inline按行输出装饰与glyph部分，跨DOM整形、换行及UTF-16映射延用现有段落；原子inline内部几何与绘制内容展开到共同帧，段落的占位仍负责正常流尺寸与基线，避免重复绘制并允许后代层级参与正确的祖先上下文。

AX、键盘和文字复制保留DOM阅读顺序，不按绘制层级重排。指针按命令逆序命中，仍使用真实圆角、裁剪及文字Source片段。SkTextBlob的保守墨迹范围只用于损伤，不能当文字命中范围。文字重叠及遮挡使用同一视觉层次；滚轮从可见目标的最近可滚祖先开始，前方兄弟盒不会把滚轮送进被遮住的滚动口。布局帧保留命令，纯滚动只投影几何；排序变化按不同区间的旧新范围补损伤，静止画面继续复用。

公开C ABI保持111个导出，所有新类型均为私有实现；新增层叠数据归Runtime UI线程，不启动资源线程、不增加内置数据或运行时文件。低层矩形/段落绘制与原生标记场景仍可独立使用，文档场景采用命令顺序。

样式刷新中的候选向量按已知规模预留；回退表仅在实际revert-layer时分配，临时键引用同次计算内稳定的声明和候选字符串。Scope查找只构建一次键。它们减少新增层叠声明带来的临时分配压力，不扩大脚本预算；原页插入元素后比较两次计算颜色的100ms脚本检查保持原限制。完整细粒度样式失效及更低帧时延继续后续优化。

候选属性名引用不可变声明/AST或同代驻留的展开名称，按元素不再复制相同长属性字符串。保存的元素计算结果仍拥有自己的键，不把临时string_view暴露到代次之外。限时测试受机器负载影响，阶段证据报告实际运行与失败记录，不把一次通过当作所有环境的严格耗时保证。

本阶段同时修正受控 Lexbor selector-retry 补丁：回查失败复合选择器时，forward沿prev、backward沿next寻找同一复合的关系入口，不能跨过失败的右侧subject继续匹配祖先。修前新旧DLL均将 details:nth-of-type(2) p 错选为SUMMARY；现在该查询、否定、:has和原details/嵌套测试共同验证。变更只在 third_party/patches/lexbor-selector-retry.json，配方从锁定原文重放，原始三方源码只读。

验证使用公开动态/静态SDK及真实内置SwiftShader，覆盖正负/相等层级、auto/0、上下文隔离、普通及原子inline、逐行与匿名项、Flex/Grid、伪元素、点击/文字/滚轮、AX阅读序、旧新局部损伤、纯滚动与完整重排、新View画面对照及导航复用。原用户HTML保持原文，WebTest检查只是现有支持范围的首帧/脚本证据；不能据此宣称完整网页兼容。

后续仍需实现 absolute/fixed 脱流与包含块、sticky滚动约束、完整opacity/transform/filter/混合、浮动布局及其绘制阶段、垂直书写/逻辑轴、pointer-events/visibility等相邻机制。它们的上下文触发与合成行为继续纳入完整目标，本阶段仅对已实现的布局与样式机制给出证据。Win7、旧Linux、macOS/Web的实机与完整平台验收继续单列。GPU圆角覆盖率整数平移的差异边界见relative-positioning.md，同一位置的保留/完整绘制仍要求一致。

规范依据：CSS 2.2 Appendix E（https://www.w3.org/TR/CSS22/zindex.html），CSS Flexible Box Layout §4.3（https://www.w3.org/TR/css-flexbox-1/#painting），CSS Grid Layout §6.5（https://www.w3.org/TR/css-grid-1/#z-order）。实现范围和阶段证据以TODO及本阶段report.json为准。
