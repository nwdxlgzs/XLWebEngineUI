# 计数器与引号

counter-reset、counter-increment、counter-set与content中的counter()/counters()按作者树及before/after顺序计算，共用生成文字、绘制和AX。Flex/Grid的order不改变编号顺序；aria-hidden不会改变视觉计数。display:none、hidden、closed details隐藏分支和不生成的伪元素不改变状态。display:contents节点不执行自身counter操作，子项继续处理；完整contents布局仍是后续工作。

同名counter有独立创建身份与作用域，重设可自嵌套，set修改当前实例；父计数器的后代更新会传播到后续兄弟。counter()使用最内层值，counters()由外到内连接。每次样式失效重建当前文档的状态；detached根各自隔离，导航和多个Runtime之间不共享可变状态。属性语法、CSS变量计算无效、important、specificity和声明顺序由同一样式入口处理，非法普通声明不会覆盖早先合法声明，计算无效的var声明使用unset。

counter-reset支持reversed(name)，省略初值时先扫描实际作用域的增减和首次set，再执行内容计算。整数范围为有符号32位，溢出钳制。重复reset/set以最后项为准，重复increment相加。属性的整数目前为整数token；calc()与完整数学表达式另行实施。

ol/ul缺少作者counter-reset时建立list-item实例，li或显式display:list-item默认增减，ol的start/reversed和li的value属性参与计算；CSS显式覆盖沿用层叠结果。嵌套列表、反向列表、动态属性变化使用相同机制。此阶段提供编号语义与生成内容，完整默认列表marker、list-style布局、::marker、HTML专用IDL反射和自定义@counter-style仍待完成，不能把counter文本等同于完整列表排版。

当前格式包括decimal、decimal-leading-zero、lower/upper-alpha和latin别名、lower-greek、lower/upper-roman、arabic-indic、persian、devanagari、bengali、thai、cjk-decimal，以及disc/circle/square/disclosure符号与none。字母非正值、罗马数字范围外和未定义style名回退decimal。后续继续其他内置格式、symbols()及自定义counter-style。

quotes支持auto、none、字符串对和CSS全局值，按来源元素继承。open-quote/close-quote输出并修改文档嵌套深度，no-open-quote/no-close-quote只修改深度；深度不小于0，超过所列层次重复最后一对。quotes:none仍修改深度。q默认生成before/after引号，作者content可覆盖；作者树、textContent、选择器、clone和选区不混入这些生成节点。

自动引号来自锁定ICU79.1中的CLDR数据：907个已解析locale合并为123条父语言差异，源码表为third_party/data/icu-quotes.inc（约9.6KB），Unicode-3.0许可及原ICU LICENSE随SDK保留。依据lang逐级回退，未知语言回到CLDR root；zh-TW/HK/MO别名选择Hant。表在编译时进入库，查询样式不会加载ICU或读取文件。不同语言在共同引号深度上选择各自的字符，动态lang重新计算。

表可重建：显式编译xl_quote_data目标后，用tools/generate_quotes.py的--program和--data指向该工具及锁源source/data/in/icudt79l.dat。工具只读完整锁数据并抽取定界符，不修改上游源码。生成文件记录上游commit、输入dat SHA256、locale数量与原许可；删除build不影响使用已受控的表，按锁可重新生成。运行SDK仍使用原来的Chromium裁剪压缩ICU资源，不将完整dat或提取工具带入发布包。

content的slash替代文字也可使用counter()/counters()。AX名称、可见生成文字和旧不可变AX更新继续使用既有伪元素契约；生成文字不冒充作者UTF16偏移。getComputedStyle(...,pseudo).content仍返回本引擎解析后的字符串，完整浏览器computed CSSOM尚未实现。

每个content限制64KiB/8192token，文档每代生成文字及替代文字总4MiB；每个根最多65536个计数器实例，单作用域最多256实例，遍历深度256，引号深度65536。超过资源界限返回XL_UNSUPPORTED。重算先释放所有旧生成文字缓冲（包括inactive），保留节点/AX身份，arena可复用空闲块；这不代表整个Document的所有内存均受4MiB约束。创建生成节点时的分配失败会撤销来源/ID映射再销毁节点，重试可恢复。Document在获取原始DOM前完成PseudoElements的C++分配，避免构造异常遗留DOM。

参考W3C CSS Lists and Counters Level3、CSS Generated Content Level3、CSS Counter Styles Level3与HTML列表呈现规则。本阶段仅做直接相关Debug行为和SDK验证；Win7、旧Linux、macOS/Web实机、完整无障碍与完整现代CSS仍属后续总目标。
