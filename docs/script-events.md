# 事件、任务与宿主调度

Event、CustomEvent、EventTarget 与 DOM 节点共用监听器和传播实现。支持 capture/bubble、once/passive、按 callback/capture 去重、removeEventListener、handleEvent、preventDefault、传播中止、事件阶段与 composedPath。原生键盘/编辑/剪贴板事件走同一个派发器，CustomEvent 保留 detail，脚本合成事件没有原生默认动作。事件对象的派发中重入被拒绝，结束后可再次派发，isTrusted 随脚本重派发变为 false。当前监听器异常仍向调用者传播，完整 Window 错误事件、AbortSignal、Shadow DOM retarget 和全部专用 Event 子类继续实现。

setTimeout/setInterval 接收函数与额外参数；clearTimeout/clearInterval 取消共享任务 ID。requestAnimationFrame/cancelAnimationFrame 在宿主驱动的渲染轮次执行，当前最早约16ms后可运行，不承诺显示器 vsync；回调收到文档的单调毫秒时间。performance.now 也使用单调时钟；queueMicrotask 进入 QuickJS Promise job 队列，执行完当前脚本或任务后清空。字符串形式定时器尚不支持。

任务只在所属 UI 线程由 xl_view_render 执行，不创建隐式事件循环或线程。一个 Document 的多 View 共用任务；每轮取已到期的稳定快照并按截止时间、ID排序，最多64项，回调内新加任务留下一轮。每文档最多1024个定时器；interval最小1ms，超长延时限制到2147483647ms，不补发错过的多次 interval。单轮脚本预算100ms，超限 XL_TIMEOUT；异常 XL_SCRIPT_ERROR，结果文本可从 Document 读取。任务未修改 DOM 时，不因此强制栅格化/提交像素。导航/销毁释放所有回调、闭包与未执行任务。

xl_view_next_wakeup_ms / View::next_wakeup_ms 返回下一脚本任务剩余毫秒数，-1表示没有任务，0表示已到期。宿主将它与平台事件、资源完成等等待源组合，到期后调用 render；只有到期任务才使 needs_frame 为真，不要求静态页面忙轮询。宿主仍负责调度，不能只看当前 needs_frame 后永久睡眠而忽略未来截止时间。

DOM 增补 children/childNodes、first/last child、parentElement、contains/closest/matches、DocumentFragment、cloneNode、innerHTML写入、classList/dataset、getElementsByName。移除/替换的旧节点保存到文档结束，已有 JS/C++ 句柄不悬空；fragment追加会迁移其子节点。当前返回的集合是调用时快照；模板content、完整 Node/Element/HTML控件接口、选项独立的选中态/范围等仍待。style对象保持同一节点身份，支持get/set/removeProperty、priority与普通camelCase赋值，真实写入样式属性。

window.innerWidth/innerHeight来自当前View drawable尺寸。现阶段一CSS坐标对应一个drawable像素，devicePixelRatio为1；完整DPI/缩放转换尚待。原生screen读实际显示模式，离屏没有关联显示器时screen尺寸/色深为0，不能冒充设备实测。WebTest先关联View再执行页面脚本，提供正确初始视口。localStorage、Canvas、Intl等未实现项目不伪装成支持，探测页面加载通过不能代表全部现代API或CSS视觉一致。
