# Windows UI Automation 桥

原生summary及缺省标题已提供IExpandCollapseProvider：幂等Expand/Collapse共用真实open，状态及变化通知来自发布的AX快照。动作仍排队到UI线程，不在COM线程访问DOM；细节与后续模式见 details.md。

`xl_view_set_native_accessibility(view, 1)` / C++ `View::native_accessibility(true)` 显式启用，SDK 默认关闭。Windows 自有 HWND 安装 WM_GETOBJECT 的 UIA root provider。WebTest 的 Windows 图形窗口自动启用，离屏 --check 不启用。其他平台和离屏开启返回 XL_UNSUPPORTED；关闭总可调用。

桥从系统目录按需加载 UIAutomationCore.dll，运行时解析基础导出，单独探测 Win8+ 的 UiaDisconnectProvider 和较新系统的 UiaRaiseNotificationEvent。它们不是 SDK 的硬导入；缺少时走旧接口，不新增随 SDK 分发的依赖。Win7 实机、旧系统 ABI 与读屏行为仍需验证。

实现 IRawElementProviderSimple / Fragment / FragmentRoot 树：DOM 顺序导航、稳定运行标识、屏幕矩形、命中、焦点、名称/角色/可用/密码/离屏/必填/有效状态，以及 Invoke、Toggle、Value 模式。几何来自最后成功 render 与 Win32 客户区到物理屏幕的换算；完整变换/遮挡、混合 DPI、多屏实测仍待补。

UIA 工作线程只读不可变语义树，不访问 DOM/JS/布局。native 更新订阅不消费宿主显式开启的 AX 批次，成功 render 才发布。动作队列每 View 最多64项/4MiB，进入 needs_frame，由宿主 render 所属 UI 线程执行；不创建 DOM 线程或隐式消息循环。等待最多2秒，尚未开始的超时动作取消，不会在恢复渲染后补执行；已经开始的动作可能已产生事件。导航使旧文档排队动作失效。UI 线程直接调用 provider 动作返回 RPC_E_CALL_REJECTED；宿主 UI 线程使用原有文档/视图接口。

Invoke/Toggle 复用 AX 激活和 DOM click 默认动作。Value 设置聚焦文本控件，以 insertReplacementText 进入 beforeinput/input、maxlength、撤销和重入守卫；readonly/disabled/失效节点拒绝修改。密码的 provider getter 返回 E_ACCESSDENIED，ValueValue 属性不提供内容；系统 UIA 客户端可能把未提供值转换为空字符串。内部 AX/live 仍先屏蔽密码。

结构变化映射为子树失效，其他批次映射属性/原生焦点/live 事件。显式 aria-live 通过 AX 的 POLITE_REGION/ASSERTIVE_REGION 状态映射 UIA LiveSetting，aria-live=off 不被角色默认值覆盖。新系统使用 Notification，缺少时退回 LiveRegionChanged；Win7 live 播报及读屏排队/打断策略未实测。TextPattern/文本范围/系统选区、完整富文本、列表表格模式与 MSAA/IA2 继续后续。

新 Notification 的客户端验证采用 CUIAutomation8；原 CUIAutomation 在当前系统上订阅返回成功但未收到该事件，不能把这个返回值当成通知验收。新系统中的旧客户端通知兼容策略继续跟踪。窗口焦点状态要求真实前台窗口和线程键盘焦点一致，SetFocus 会请求窗口与 DOM 焦点交接；系统不允许前台转移或窗口不可见时拒绝成功声明。实际前台操作的回归单列 windows_focus，在非交互输入桌面 daily 明确 skip、正式门禁要求通过。

借用 HWND 不安装窗口过程。`xl_view_get_native_ax_provider` 返回 kind=XL_NATIVE_AX_UIA 和带一个 COM 引用的 IRawElementProviderSimple*，宿主在自己的 WM_GETOBJECT 中使用 UiaReturnRawElementProvider 转交。任意原生 AX 子树自动嫁接尚未实现。宿主应先停止转交，再关闭桥，并自行清理其 UiaReturnRawElementProvider 映射；先关闭/销毁 View，再销毁借用 HWND，SDK 不接管窗口所有权。

关闭撤销自有 subclass、停止排队、唤醒等待者、释放树并逐个断连已创建的 provider；已保留接口在关闭/节点删除后返回 UIA_E_ELEMENTNOTAVAILABLE。Win8+ 使用 UiaDisconnectProvider，旧系统回退 CoDisconnectObject 和自有 HWND 的 UiaReturnRawElementProvider 清理。同步 SendMessage 回调中不能断连 COM，此时关闭/销毁返回 XL_BUSY，应回到普通 UI 循环再调用。

COM 接口由调用者 Release。`xl_native_ax_get_object_count` 任意线程查询此 SDK 的原生 provider 对象数；宿主关闭全部桥、释放/断连客户端接口，确认计数归零且所有 SDK/COM 调用均已结束，再卸载 SDK。其他 SDK 句柄也必须释放。保留 provider 可晚于 View/Document/Runtime，但不能晚于 SDK。测试包含独立 LoadLibrary→真实 UIA 客户端→关闭→保留失效 provider→释放至零→FreeLibrary。

依据 Microsoft 原始文档：

- https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-howto-expose-serverside-uiautomation-provider
- https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nn-uiautomationcore-irawelementproviderfragment
- https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiadisconnectprovider
- https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading
