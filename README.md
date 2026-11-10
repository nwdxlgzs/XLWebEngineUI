# XLWebEngineUI

> 这个项目既失败又成功，失败在实在是卡，优化一坨，成功在确认了多后端渲染可行和Web渲染可行性。就这样吧，屎山代码，愿意折腾就玩吧，简单GUI已经没问题了，但是剩下的一坨。

跨平台 Web UI 框架，当前正在将实际依赖逐层接入运行时和静态／动态 SDK。

原始目标和约束见 `RAW-PLAN.MD`，实施计划见 `PLAN.MD`，实际验证与任务见 `TODO.MD`。早期网页管线 SDK 已通过双平台交付验证；当前推进跨节点行内排版、DOM 原文 UTF-16 范围和文字命中，延续 Lexbor、QuickJS-NG、SkParagraph 与 AX 语义的同一数据链。完整现代网页、平台 AX、GPU、输入/编辑与媒体继续推进，阶段 SDK 不代表完整引擎。

发布包自带 `bin/WebTest`（Windows 为 WebTest.exe），在网页目录运行即可预览当前目录的 `index.html`，本地依赖按需读取，无需编译或服务器。选项与能力边界见 `docs/webtest.md`，包内也有可直接运行的示例。

资源接口、线程与所有权约束见 `docs/public-resources.md`，视图契约见 `docs/views.md`，网页管线及当前能力边界见 `docs/web-core.md`，内部内容协议见 `docs/ui-content.md`。普通构建会使用锁定 Clang/LLVM 准备私有依赖及资源，所有可重建输出进入 build。引擎不实现 HTTP/HTTPS 或网络请求栈。

## 本地入口

```text
python tools/ci.py doctor
python tools/ci.py quick
python tools/ci.py full
python tools/ci.py release
python tools/ci.py gc --dry-run
```

命令实现与验证状态以 TODO 中的结果为准。构建、依赖缓存、报告及包统一位于 build。正常开发复用 Debug 增量目录，用 quick --tests/--build-targets 做必要局部验证；完成记录及缓存按类别/目标保留最近 3 份，Windows 与 WSL 及时清理。
