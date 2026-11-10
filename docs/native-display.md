# 原生显示依赖与验证边界

Windows 使用锁定的 Clang/LLD 与 SDL Windows 后端。Linux 正式目标使用 Clang/LLD、musl 以及单独编译的私有 X11 客户端栈；WSLg 的 X server 通过 X11 协议提供显示，不向引擎进程加载 glibc 版 X11 库。

## 构建与实际测试

```powershell
python -B tools/platform_deps.py x11 --configuration Release --jobs 8
python -B tools/deps.py build --target linux-x64-musl --configuration Release --jobs 8
python -B tools/deps.py verify --target linux-x64-musl --configuration Release --native
python -B tools/deps.py verify --target windows-x64 --configuration Release --native
```

第一条命令在 Windows 上转发至 WSL Ubuntu。Linux 原生测试要求可连接的 X11 显示服务器；缺少显示环境时测试失败，不会改用 dummy。不带 `--native` 的组合调用测试另行保留，使用 dummy 后端，只证明依赖函数可调用。

X11 来源位于 `third_party/platform-sources.lock.json`，配方位于 `third_party/recipes/x11.json`。协议头、xtrans、Xau、Xdmcp、XCB、X11 与基础扩展库顺序编译到同一私有前缀。XTEST 所需 libXtst 另存 `platform-extensions.lock.json`、`recipes/x11-extensions.json`，在独立前缀中编译并引用基础栈，新增扩展不要求重编整个基础栈。pkg-config 仅查看这两个前缀；SDL 的静态符号探测补齐 XCB、Xau、Xdmcp 等传递库。SDL 查找库、头与包时仅使用私有前缀和 musl sysroot，宿主程序工具仍使用主机环境。所有源码、生成文件、日志、静态库和安装树均位于根 `build/`。

`deps verify` 按源码哈希、配方内容、兼容补丁、编译器、配置和传递依赖的键选择归档。不会按时间挑选“曾成功”的旧归档。测试目录也按输入键登记，使用期间有租约，并保留对依赖安装树的引用，接受统一保留和清理规则。最新报告仍位于 `build/deps/integration/<target>/<configuration>/report.json`，其中 `work` 指向该次实际 CTest 目录。

## 测试证明的行为

- Skia Raster 绘制到内存，将像素转换到 SDL 原生窗口 surface 并提交；通过 Win32 `GetPixel` 或 X11 `XGetImage` 从平台读取红色像素。
- 创建原生子窗口，以外部窗口属性接入 SDL；测试绘制和销毁包装后的窗口存活、父子关系。Windows 额外检查原始窗口过程恢复。
- 销毁子窗口后继续向宿主绘制，检查宿主仍然可用。
- musl 组合程序及原生测试程序使用 `readelf` 审查加载器、直接动态依赖和 GNU 符号版本。报告随实际执行结果保存。

这是依赖层的行为测试。尚未证明引擎公开视图接口、任意宿主消息循环、GPU 合成、原生控件与网页交错遮挡、无障碍子树或旧系统运行。

2026-11-03：Windows 和 WSLg 的真实像素及子窗口所有权测试均通过。WSLg 程序以 musl 链接私有 X11 客户端库，ELF 直接动态依赖只包含 `libc.so`，加载器为 musl，未出现 glibc/GNU C++ 符号版本。软件显示结果不能替代 GPU 路径验证。

## 后续必须完成的接入

X11 客户端栈只解决当前 musl 显示准备。Wayland、GPU 驱动边界、实际音频后端、IBus/Fcitx、原生 AX 桥接仍需各自锁定来源、许可、运行库和行为证据。编译时未找到某个后端不表示对应产品能力从计划中删除。

X11 locale/XIM/Compose 数据、光标主题及字体数据需要逐项确定运行时读取方式与系统边界。凡需要随 SDK 分发的数据须纳入资源清单、许可和压缩内置懒加载策略；不能把开发机安装树作为隐含运行前提。当前像素测试不构成国际化输入完成证据。

各源码归档的声明完整保存至私有安装树 `share/licenses/<component>`。`LicenseRef-XOrg-*` 记录组件含多个历史声明；发布前还须完成实际启用文件的声明映射、归属和 SDK 汇总，不能把“未发现 GPL 关键字”当成许可审查通过。

WSL/WSLg 结果仅记录为开发环境实测。Win7 SP1、UOS/Linux 4.x 的原生验证与性能验收继续单独记录。
