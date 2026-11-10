# UI 内部资源协议

引擎只负责 UI，不内建 HTTP/HTTPS 请求、文件读取或通用网络浏览器。宿主取得内容后，以不可变内存对象挂载到 `xl-webengine-ui://<authority>/<path>`。内置压缩 ICU 等资源继续使用原有懒加载服务，首次使用才在内存解压；此处的 UI 内容对象是宿主已提供的字节，不会触发启动解压或落盘。

每个 Runtime 有独立映射表，authority 是该表内的命名空间，不是网络主机，也不作为跨 Runtime 权限。两 Runtime 可以把同一 URI 映射到不同内容；显式共享同一个 Content 时共享只读存储，路径、替换和卸载互不影响。一个 Runtime 内的文档可读取该 Runtime 挂载的全部内容；不同信任域应使用不同 Runtime。没有全局文件系统或网络后备解析。

```cpp
xl::Runtime runtime;
xl::Content page("<link rel=stylesheet href=theme.css><p>Hello</p>", "text/html");
xl::Content dark("p{color:white;background-color:black}", "text/css");
xl::Content light("p{color:black;background-color:white}", "text/css");
runtime.mount("xl-webengine-ui://app/dark/page.html", page);
runtime.mount("xl-webengine-ui://app/light/page.html", page);
runtime.mount("xl-webengine-ui://app/dark/theme.css", dark);
runtime.mount("xl-webengine-ui://app/light/theme.css", light);
xl::Document first(runtime), second(runtime);
first.load("xl-webengine-ui://app/dark/page.html");
second.load("xl-webengine-ui://app/light/page.html");
// 两页使用同一份 HTML 存储，相对 CSS 指向各自的目录。
```

C API 提供 `xl_content_create/clone/data/release`、`xl_runtime_mount_content/unmount_content/open_content`、`xl_resolve_ui_uri`、`xl_document_load_uri/load_html_at/get_uri`。创建复制宿主字节一次；clone、挂载和打开不复制内容字节。仅设置源位置可用 `document.html(html, uri)`，不必先挂载 HTML。无显式位置的内存 HTML 使用 `xl-webengine-ui://app/`。

宿主可选注册 `xl_runtime_set_content_loader`（C++ `Runtime::content_loader`）。仅未命中的 URI 在 UI 线程同步回调宿主，传入规范化且无 query/fragment 的地址；成功交付一个拥有的 Content 句柄，由 SDK 接管并按普通挂载缓存。清除/替换 loader 不清已有挂载；要重读可先 unmount。回调上下文由宿主保证存活，不得抛出异常，回调内只创建/复制内容，Runtime API 重入返回 XL_BUSY。引擎不持资源互斥锁调用宿主；慢速IO需由宿主预先异步准备后mount，避免阻塞UI。WebTest用此接口按需读取本地文件，引擎本身仍无文件/网络栈。

地址支持相对路径、根路径、相对 authority、`.`/`..`、query 和 fragment。scheme 和 authority 规范为小写；path 区分大小写。查询和片段保留在文档地址里，但不区分挂载内容。非 ASCII UTF-8 字节需百分号编码；未保留字符的转义规范化，编码后的点参与路径归一。路径不接受编码斜杠、反斜杠、控制字符和无效转义；向上越界停在 authority 根目录。URI 最长 16 KiB，authority 最长 128 字符；不支持端口、用户信息或系统文件路径。

挂载表的变更、打开和文档操作归 Runtime UI 线程。Content 可跨线程读取、复制或显式跨 Runtime 挂载，存储可晚于 Runtime 存活；同一句柄释放不能与使用并发，所有句柄必须在 SDK 卸载前释放。替换/卸载不改变已打开内容。内容上限 256 MiB，HTML、CSS 和脚本文本读取各限制 16 MiB；MIME 为至多 256 字符的可打印 ASCII，类型匹配不区分大小写。

当前已接通：HTML 导航、文档源位置、首个 `<base href>`、相对 `<link rel=stylesheet>`、`document.URL/documentURI`、`Node.baseURI`、href/src 地址解析，以及同步只读 `xl.resolveURI()` / `xl.readText()`。脚本仍显式执行，解析 HTML 不执行 script。加载失败保留旧文档；重挂载 CSS 会在下次样式/视图刷新时失效，HTML 是解析后的快照，需显式重新加载。

样式表按 DOM 顺序与 style 元素交错层叠，disabled 和 alternate 表跳过。media 暂支持空值、all、screen，其余跳过；完整媒体查询仍待实现。HTML 需要 text/html，外部表需要 text/css。尚未接通 CSS @import、图片/字体的 URL 解码、脚本模块解析或 WebM 资源消费者；这些后续共用此协议，不引入网络栈。href/src 当前只提供地址解析，不代表对应元素已实现资源加载和交互导航。
