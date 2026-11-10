# AX 动态更新

原生 Windows 桥通过独立订阅消费同源快照，契约见 windows-accessibility.md。区域节点的 XL_AX_LIVE_POLITE_REGION / XL_AX_LIVE_ASSERTIVE_REGION 状态保留显式或角色默认的 live 设置；off 清除它们，平台桥无需从角色猜测显式覆盖。

`xl_view_set_ax_updates(view, 1)` 显式启用，默认为关闭。宿主继续驱动 render；只有最终渲染成功才发布更新，布局中间态、资源尚未就绪或呈现失败不会发布。启用静止 View 会请求一次帧，积压的通知不会持续请求绘制。纯语义属性变化可以产生批次而不增加光栅帧。多个 View 分别消费自己的几何与选区。

UI 线程通过 `xl_view_take_ax_update` 取得拥有所有权的 `xl_ax_update`；没有更新返回 `XL_NOT_FOUND`，输出保持不变。可以在当前 DOM 尚未重新渲染时读取已发布批次。C++ 使用 `View::ax_updates(true)`、`View::take_ax_update(AXUpdate&)`；无更新保留原对象。

批次包含完整当前 AX 树（DOM 前序）、文档标识、DOM revision、单 View 单调递增 sequence，以及事件。旧 role/state 数值不变。启用、重新启用、导航和 detach 产生 RESET，宿主用完整树替换旧状态；detach 的文档标识为零且树为空。禁用清除待取队列，不影响已取对象。sequence 不因禁用清零。相同语义的 DOM 变化不强制发送空批次，因此 revision 可以跳号；滚动和 View 选区变化无需改变 DOM revision。

事件顺序：移除节点（子先于父）、增加/属性变化（当前 DOM 顺序）、子节点列表变化、焦点、选区、live region。NODE_CHANGED 的 properties 标识 role/name/value/states/actions/bounds/parent。FOCUS_CHANGED 的 node 为当前焦点、related 为旧焦点，零表示无焦点。SELECTION_CHANGED 携带 DOM/控件 UTF-16 anchor/focus；node 为 focus 端点，清除时 node 为零且 selection 全零。聚焦文本控件的编辑选区优先于 View 的普通阅读选区。事件 flags 目前仅 LIVE_REGION 使用，值为 POLITE 或 ASSERTIVE；related 为合并播报的 atomic 节点或文本来源节点。RESET 的 node 是新文档标识。新旧文档节点不能混用。

handle 的 read/clone/release 可在任意线程使用。每个 handle 不能与自身 release 并发；多个 clone 共享不可变批次，可以分别在线程中读取和释放。批次持有复制后的语义数据，不借用 View、Document、Runtime、ICU 或宿主内存，因此可以晚于这些对象销毁。全部 handle 必须在 SDK 卸载前释放；不启动线程、不回调宿主。

待取队列最多 8 批，保守估算的保留载荷最多 64 MiB：计入 vector capacity、元素、string capacity 和结束字节（短字符串也保守计入），不含分配器元数据。当前 DocumentFrame、差分基线、live 比较状态、临时差分索引和宿主已取批次不属于队列预算。超限时丢弃所有待取批次，发送完整 RESET|OVERFLOW 及当前焦点/选区；瞬时 live 播报不会在积压后重新播放。宿主应及时取走并释放批次。单个批次超限返回 `XL_UNSUPPORTED`，分配失败返回 `XL_OUT_OF_MEMORY`；它们只终止通知流当前积压，不让成功的视觉渲染失败。下一次有效变化用 RESET 恢复。

语义补充：HTML for/wrapping label、ARIA labelledby/label、checked/mixed、selected、expanded/collapsed、required、invalid、multiline、readonly、busy 与继承的 aria-disabled。原生 disabled 优先，aria-disabled=false 可覆盖语义继承；aria-disabled 不替代 HTML 输入行为。密码值先屏蔽后进入快照和播报。隐藏、inert 和 aria-hidden 子树排除，裁剪到视口外的节点保留 OFFSCREEN 状态。完整 Accessible Name 算法、关系和完整表单角色仍待完善。

live region 当前支持显式 polite/assertive/off；status/log 默认 polite，alert 默认 assertive，timer/marquee 默认 off。status/alert 默认 atomic，可被显式 false 解除。普通区域首次出现建立静默基线，初始 alert 播报非空内容；文本替换、增加和可选 removals 按 aria-relevant 过滤，atomic 合并为当前组内文字。嵌套 live 区域独立，off 子树静默，命名元素只用替代名称参与播报，避免重复后代文本；完整区域移除不再播报。移除项依据其最后观察到的 relevant/atomic 上下文。busy 保存最后非 busy 基线，解除后汇总变化；目前任意 busy 后代会保守推迟整个所属 live 区域。平台桥仍需实现 polite 排队、assertive 中断和读屏程序的实际投递。

语义依据 WAI-ARIA 1.2 的 aria-live/atomic/relevant/busy，以及 Core AAM 的事件分类；这是可供平台桥消费的内部语义更新契约，不声称完整 ARIA/AAM 一致性或已接入系统读屏：

- https://www.w3.org/TR/wai-aria-1.2/
- https://www.w3.org/TR/core-aam-1.2/

Windows UIA（含 Win7 的回退边界）、macOS NSAccessibility、Linux AT-SPI2、Web 语义 DOM、宿主原生子树及真实读屏验收继续按 PLAN 推进。
