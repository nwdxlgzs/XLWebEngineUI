# 本地开发与验证

## 统一入口

Windows 使用 `python -B tools/ci.py`，Linux 使用 `python3 -B tools/ci.py`。PowerShell/Shell 包装只转交参数。无需预先打开 VS 开发者终端。

构建使用 `third_party/toolchains.lock.json` 固定的 Clang/LLVM。Windows 使用 clang-cl/LLD 与已有 Windows SDK、MSVC 运行库头文件；Linux 使用 Clang/LLD，musl 目标单独构建 compiler-rt、libc++、libc++abi、libunwind，不能混入 glibc 的 C++ 运行库。可用 `python -B tools/bootstrap.py llvm` 准备当前主机工具，`python -B tools/bootstrap.py musl` 准备 Linux 目标（Windows 自动转交 WSL）。首次下载核对大小和官方 SHA256，大归档采用有界并发分段续传；所有源码、工具、运行库和暂存均在 build。Windows LLVM 大窗口 Zstd 归档使用 Git for Windows 自带的 libzstd.dll 流式解码，脚本仅依赖 Python 标准库；需要含该 DLL 的 Git for Windows。

Linux 的 LLVM/musl 包装脚本在内容相同时直接复用，变化时先写完整可执行临时文件再原子替换。并行准备其他配置时，不得原地截断正在执行的包装脚本；否则 WSL 文件系统可能返回 `Text file busy`。

运行时依赖源码由 `third_party/dependencies.lock.json` 固定，用 `python -B tools/deps.py fetch` 下载、解包并核对许可文件，`status` 查看实际源码准备状态。源码位于 `build/deps/src/<name>/<content-key>`；版本库保存锁、配方、补丁和许可。源码准备通过不等于编译、SDK 接入或旧系统兼容通过。

`python -B tools/deps.py build --target windows-x64 --configuration Release --jobs 8` 按依赖顺序构建所选整套组件，Skia 使用 GN。`verify` 将已编译库放进同一程序并执行真实调用；详细说明见 `third_party/README.md`。Linux 目标从 Windows 自动转交 WSL。资源阶段主库已接入压缩 ICU 与解压器；其他组件仍按网页、显示与媒体模块继续合入。

```text
python -B tools/ci.py doctor
python -B tools/ci.py quick
python -B tools/ci.py quick --targets windows-x64 --tests document.uri --build-targets xl_uri_tests
python -B tools/ci.py quick --targets windows-x64 linux-x64-musl --tests "^(document.focus_style|webtest.local)$" --build-targets xl_focus_style_tests WebTest --package
python -B tools/ci.py quick --plan
python -B tools/ci.py full --targets windows-x64 linux-x64-glibc-dev
python -B tools/ci.py full
python -B tools/ci.py gc --dry-run
```

- full 默认要求 Windows 与 musl，musl 未准备时明确失败。显式选择 glibc 开发目标仅验证开发矩阵，不改变正式交付目标。
- release 额外要求干净且已提交的源码、正式完整矩阵及功能验收门禁。当前阶段不能正式发布；full 产生标记为 web-core 的早期文档管线候选包。

每完成一个已验证功能阶段，用 `quick --package` 衔接阶段打包。`--tests` 和 `--build-targets` 选择本次直接受影响检查；命令额外增量构建 WebTest 和静态库，安装 Runtime/Symbols 并由 CPack 分别归档，不运行完整矩阵或 SDK 消费工程。Windows 调用会把 musl 部分转交 WSL；`linux-x64-glibc-dev` 不生成正式目标的阶段包。

归档在 `build/packages/<target>/<run-id>/`，Windows 为 ZIP，musl 为 TAR.GZ，含完整静态/动态 SDK 和可直接运行的 WebTest；符号为独立的 `-symbols` 包，均附 SHA256。`STAGE-BUILD-INFO.json` 记录 Git 提交、工作区修改、验证源码指纹、检查范围与逐文件哈希；包名明确标注 Debug-preview、production_ready=false。打包前后核对冻结源码，解压实际 SDK、核对文件哈希并运行其中 WebTest --check；报告不把此冒烟检查当作旧系统、平台IME/AX或完整SDK验收。安装、CPack 和解压副本在收尾立即删除，每目标阶段记录保留最近3份，跨设备以该目录 report.json 和校验文件核对交付物。
- quick 复用最近使用的本机 Debug 增量目录，`--tests` 指定直接涉及的 CTest 名称表达式，`--build-targets` 指定构建目标；未指定时构建现有默认目标并运行根测试。没有准备好的目录时仅准备 Debug 所需输入并配置，不进行完整验证和打包。公开头文件、CMake 修改不自动扩大到完整矩阵。
- 日常 quick 不做 SDK 安装/移动消费、整套依赖行为重验、工具全套测试或包审查，也不更新正式 full 基线。`quick --plan` 保留源码影响分析供判断范围；完整矩阵由 full/release 显式触发。工具变更单独运行直接涉及的 Python 测试。
- TODO/WORKMEM 不参与代码指纹，但每次都检查管理文档。修改期间正在运行的 CI 不会建立成功基线。

## SDK 消费

```cmake
find_package(XLWebEngineUI CONFIG REQUIRED)
target_link_libraries(app PRIVATE XLWebEngineUI::Shared)
# 或 XLWebEngineUI::Static；同一最终程序不应同时链接两者。
```

静态和动态库的 C++ 包装要求 C++20；C ABI 可由 C 工程调用。Windows 静态 SDK 使用 MSVC /MT 或 Debug /MTd，静态消费者需选择对应运行库；动态库通过 C ABI 保留分配/释放边界。Linux 包显式标明 libc ABI。

运行时在创建线程绑定 UI 所有权，操作和销毁必须在该线程。C++ Runtime 包装为 RAII；跨线程销毁属于契约错误，会终止程序，不隐式泄漏对象。新增业务能力前仍需进一步审查公开接口。

安装验证实际移动 SDK 目录，复制独立消费源码，分别构建 C/C++ 静态与动态消费者。测试字体从精确锁定的Skia输入复制到消费者目录，再以宿主内存传给SDK；包本身不引用测试字体路径。四种消费实际创建离屏视图、HTML文档、执行JS修改样式、绘制和读回像素，检查行内文字/原文范围/文字命中、增量与完整画面一致及静止复用，并与宿主同名 Skia/SDL/ICU/Zstd/QuickJS/Lexbor 定义共同链接。构建报告记录源码指纹、工具链、配置与包校验和。

## build 与清理

- `build/work/<target>/<toolchain-key>/<configuration>` 用于增量构建。日常 CMake 修改在已有 Debug 目录自动重配置；正式 full 仍按工具链和精确输入生成键。工具链或依赖锁有变化时需显式准备匹配输入，不能继续使用不兼容的旧目录。
- `build/runs/<mode--targets>/<run-id>` 保存该次日志、安装树、独立消费者、报告与候选包。
- `build/state` 保存可重建的基线、清理时间和进程锁；`build/reports` 保存环境探测。
- 完成记录按任务和目标保留 3 份；归档随所属记录一起清理。登记的旧增量目录按目标/配置保留 3 份。
- 下载归档、下载残片、工具链和按日轮换的编译缓存进入 `build/state/artifacts` 登记。每类保留最近 3 份；当前锁定哈希、保留构建引用和活动租约额外保护，依赖引用可递归追踪。未登记的任意目录不自动认领或删除。
- 每次 CI 和各目标任务收尾立即清理完成记录、旧增量目录、报告及缓存，各类/目标保留最近 3 份。Windows 的显式 gc 同时调用 WSL Ubuntu，让两边分别核对本系统进程与失效租约；dry-run 不删除产物。
- CPack 完成后的 `_CPack_Packages` 展开副本是暂存，任务终态即删除。full/release 的独立审查通过后，consumer-build/source、sdk-relocated/install 和展开 symbol-files 也删除，先保存消费测试日志及资源构建信息到 evidence。最终 SDK/符号归档、报告和日志按最近 3 份保留。根 reports 的旧临时日志、快照和截图按类型/目标登记，原生对照报告连同独立浏览器 profile 整体回收。
- 浏览器对照在创建目录时原子登记并持有租约，也登记实际浏览器 PID。失败、取消、超时均释放进程/租约并执行清理；租约记录进程创建时间，防止 Windows/WSL 的 PID 重用使过期目录长期被误判为活动。
- 清理拒绝根目录、越界路径、符号链接和 Windows junction，保护运行中的记录与实际持有的工作目录锁；不自动删除未登记目录。
- 锁使用操作系统文件锁，文件是否存在不代表任务仍运行；不通过删除锁文件抢占持有者。

删除 build 会删除缓存与报告，但源码、锁定信息、补丁和配置仍在仓库中；依赖恢复需要可访问的来源或另行保存的原始下载包。

`python -B tools/rebuild_check.py --target windows-x64` 从当前提交建立隔离副本，验证构建成功、删除该副本整个 build、再次构建成功，检查源码干净及 RAW 哈希不变。它只验证指定目标和提交，不等于其他平台或依赖链已覆盖。

musl 可使用 `--target linux-x64-musl --seed-downloads build/downloads`，在每次清空之后从已保存的原始归档重新恢复下载源；只接受与副本锁定信息的大小和 SHA256 相符的归档。不会复用编译好的工具链、库或缓存，报告明确标注本地归档来源。

## 调试符号

完整 CI 分别生成 SDK 与 `-symbols` 归档。Windows 的动态库 PDB 单独打包；静态库使用 /Z7 将信息保留在对象文件，避免要求开发机的编译器 PDB。Linux 的共享库保留 `.gnu_debuglink`，调试信息移到符号包 `.debug` 文件；静态归档保留对象调试信息供最终应用链接。开发构建树不被裁剪。

## 实际依赖与资源验证

`python -B tools/validate_dependencies.py --target windows-x64` 或 `--target linux-x64-musl` 会恢复当前锁定依赖、运行真实函数调用，生成压缩 ICU 静态目录，并执行原生窗口/嵌入及内存资源验证。Linux 原生测试需要可用 X11 显示服务器；测试不会改用 dummy 来替代该项证据。

`ci full` 默认对两正式目标执行这项 Release 依赖行为验证，SDK 覆盖 Debug/Release、公开接口、原生视图和移动消费，并检查完整私有 C/C++ 符号与精确的公开导出。quick 仅做所选局部验证；报告明确范围，阶段包不能宣称完整引擎。

官方双平台 full 在建立基线前运行 `tools/audit_sdk.py`，独立检查移动安装树、四个 SDK/符号包、启用依赖的原始许可证、私有图形实现、内嵌 ICU、公开 ABI 与运行库边界，结果写入该次记录的 `sdk-audit.json`。验收清理展开副本后，以已保存的审查报告、日志和归档哈希作为证据，原审查命令不再能直接重跑；正式重新验收须显式准备验证输入。源码变化后不能把历史包当作当前源码证明。

可选的Windows文字标准对照：`python -B tools/compare_text_reference.py --browser "C:/Program Files/Google/Chrome/Application/chrome.exe" --library build/work/<target>/<key>/Debug/XLWebEngineUI.dll`。使用显式选定的本机Chromium、锁定Skia字体和tests/document/text-reference.json中的案例，比较文字行高及tab位置；1px容差如实记录，不代表完整浏览器一致性。生成的页面、独立浏览器profile、日志与JSON均在build/reports/text-reference，包含源码/字体/浏览器/库指纹。此开发工具不作为SDK分发依赖；当前仅Windows DLL路径已验证。

第三方源码、原生显示与资源服务细节分别见 `third_party/README.md`、`docs/native-display.md`、`docs/runtime-services.md`。
