# 纯文本剪贴板

`xl_view_clipboard` / C++ `View::clipboard` 将宿主菜单命令和键盘快捷键接入同一条 copy/cut/paste 事件与默认动作链。命令归 Runtime UI 线程；无需先绘制即可编辑已聚焦控件，文档阅读选区的复制需要当前有效布局。Ctrl/Meta+C/X/V、Ctrl+Insert、Shift+Insert/Delete 共用该入口，先经过可取消的 keydown；WebTest 已开启的原生键盘入口自动使用这条链路。

自有窗口默认通过私有 SDL 适配访问系统剪贴板；借用窗口与离屏 View 由宿主提供 `xl_clipboard_handler`。handler 为每 View 保存，不影响其他 View 或 Runtime；同一 Document 的编辑值和选择继续共享。显式 handler 覆盖整个原生适配，缺少的 read/write 方向返回 UNSUPPORTED，不隐式回退；传 NULL 恢复默认适配。

```cpp
xl_clipboard_handler handler{sizeof(handler), 0, context, read_text, write_text};
view.clipboard_handler(&handler);
view.clipboard(XL_CLIPBOARD_COPY);
```

read 回调在 XL_OK 时交付一个由 `xl_content_create/clone` 得到的独占句柄，SDK 接管并释放；其他状态应交付 NULL。内容 MIME 为 text/plain（允许参数与大小写），编码 UTF-8；write 借用字节仅在调用期间有效。文本最多 4MiB，拒绝非法 UTF-8、NUL 和过大内容。宿主应对读取大小提前限流；原生读取在 SDL 已取回数据后检查上限。context 必须存活到 handler 替换、清除或 View 销毁，回调不得跨 C ABI 抛异常。回调同步运行且锁外调用；同 Runtime 的 View/Document/挂载变更重入返回 BUSY，内容句柄的创建/克隆/释放允许使用。宿主回调耗时由宿主管理，不计入 JS 中断预算。

copy/cut 的 clipboardData 初始为空，只提供 text/plain 与 text 别名；setData/clearData/types/getData 在同步事件期间有效。脚本必须 preventDefault 才用自定义文本替代默认复制；取消且无数据时不更改剪贴板。paste 的 clipboardData 只读，setData/clearData 不更改插入内容。事件结束时对象立即失效，保留对象和微任务只能读到空文本/空类型，不能延迟读写系统剪贴板。事件支持现有捕获、冒泡、once、passive、阻止传播与 100ms 脚本预算；处理异常/超时不会触发后续默认剪贴板写入或删除。实现参考 W3C Clipboard API and events 的同步事件语义，尚不包含完整 DataTransfer/ClipboardEvent 构造器或 navigator.clipboard。

默认复制优先使用焦点控件的 UTF-16 编辑选区，否则使用该 View 的阅读选区。普通阅读选区只复制原有 DOM 原文，不合成块分隔符，跳过控件值（包含失焦密码），不能用 cut 删除阅读 DOM。readonly 可复制，但默认剪切/粘贴不能改值；password 的默认 copy/cut 被消费且不写出明文，显式程序 value 访问仍遵循现有接口。密码粘贴允许。活动组合输入期间直接命令不执行剪贴板操作，键盘由既有 IME 路径消费，宿主先完成组合再发命令。

剪切先成功写出选中文字，再以 deleteByCut 执行可取消 beforeinput 和真实删除；失败时保留原值/选择/历史。beforeinput 取消删除时剪贴板仍保存已复制文字。粘贴以 insertFromPaste 进入原有替换、清洗、maxlength、撤销历史和 input/change 链，每个命令是一个历史项。事件处理修改值、选择、焦点代次或连接关系时旧操作停止；beforeinput 同样检查当前版本和只读状态。取消事件中的脚本主动 DOM 更改不由 SDK 回滚。错误不修改 handled，回调错误原样返回。

定向测试以宿主内存剪贴板验证 Unicode、事件生命周期、取消、失败、线程、回调重入、密码/readonly/IME、共享 Document 与 View 隔离和静止绘制。SDL 原生适配的编译不等于真实 Windows/Linux 系统剪贴板互操作验收；当前非交互 Windows 桌面不做系统剪贴板写入。富文本/HTML/图片/文件、contenteditable、完整浏览器 Selection 与跨进程系统交互继续保留任务。
