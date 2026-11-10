# 任务与资源基础服务

## 有界执行器

`src/base/executor.h` 为内部接口。构造、关闭和销毁属于创建线程；提交与统计可并发调用。构造时不启动工作线程，首次有效任务才启动固定数量的线程。

队列具有固定上限；提交返回 accepted/full/closed/invalid，不在 UI 或呈现线程等待队列腾出。任务体在锁外运行，异常计入统计而不会终止工作线程。每项任务及其捕获对象在锁外销毁。

关闭支持 drain（排空已接受任务）和 cancel_pending（销毁等待任务、等待正在运行的任务结束）。关闭不抢占执行中的代码；任务必须能在可控时间内完成，不得同步等待同一执行器中的其他任务。宿主对象保持有效，直到任务结束和关闭返回。

## 对齐内存池

`src/base/memory_pool.h` 提供内部线程安全分配、缓存整理与统计。小块按 256 字节至 1 MiB 的二次幂容量分组，并按对齐要求隔离；大块独立分配，不加入热缓存。缓存预算限制已释放但保留的内存，活跃引用所占容量单独统计，不声称它是整个进程的内存硬上限。

最后一份共享引用释放后，内存才可回收。引用可跨线程，也可晚于池的外层对象销毁；实际分配器状态由引用共同持有。宿主分配/释放函数必须成对提供，不得抛异常，用户上下文必须活到最后一份引用释放。回调在池锁外运行。

宿主返回空指针报告内存不足；错误对齐会释放该缓冲并拒绝使用。trim 只回收空闲内存，不影响正在使用的数据。资源层使用只读租约控制向使用者暴露的内容。

## 内存资源服务

`src/resources/service.h` 目前是内部 C++ 接口。构造只建立不可变压缩目录，不解压、不启动线程。首次 `request(id)` 才提交有界执行器；`prewarm(id)` 为宿主明确选择的同一路径。调用立即返回独立请求，完成状态可轮询；`get()` 是显式阻塞等待，不能放进 UI 的帧循环。

同一资源在加载期间合并请求；完成后共享不可变 `ResourceLease`。取消只完成该请求的取消结果，不取消其他使用者的解压。队列已满返回 busy，资源损坏、内存不足、关闭和未知 ID 有独立状态。Zstd 帧声明大小必须与内置目录一致，生成器启用帧校验和；任何运行时解压数据均只写进对齐内存块。

资源预算分为已解压目录缓存（3/4）和池中的空闲块（1/4），两者总保留量受配置约束。活跃租约单独持有内存；大于 1 MiB 的块释放后直接归还分配器。trim 撤销目录缓存及空闲池，保留活跃租约，并可继续复用其共享内存。当前目录缓存满时不缓存新增项；后续按实际负载补充淘汰策略。

关闭先禁止请求，再在锁外完成等待者的 closed 结果，取消排队任务并等待正在执行的解压退出，最后整理缓存。完成通知使用 future，工作线程不调用 UI 或宿主完成回调。分配器回调在目录锁和池锁外执行。服务必须在创建线程关闭/销毁；只读租约允许晚于服务外壳销毁，分配器上下文仍需活到最后租约释放。

## 资源生成与 ICU 实测

`assets/resources.json` 保存资源 ID、来源、路径和对齐约束。`tools/resource_pack.cpp` 使用锁定的 Zstd 压缩器生成含校验和、最大 8 MiB 窗口的帧；`tools/embed_resources.py` 生成静态字节数组、轻量目录和带输入/输出哈希的清单。所有生成文件、压缩中间件和工具都在 build 中。内容相同的生成结果可复用；源码、锁和生成工具留在 Git，清空 build 不丢唯一输入。

依赖测试构建中的 `xl_resource_pack` 用于构建期压缩，运行时 `xl_zstd_decoder` 仅编译 common/decompress 源码。验证脚本检查该归档不存在压缩器和字典训练导出。编码器不会作为运行库功能分发。

示例（先完成对应目标的依赖构建与组合验证，encoder 路径由报告中的 work 获取）：

```powershell
python -B tools/embed_resources.py --encoder <build中的xl_resource_pack路径> --output build/generated/resources/windows-x64
python -B tools/deps.py verify --target windows-x64 --native --embedded-catalog build/generated/resources/windows-x64
python -B tools/deps.py verify --target linux-x64-musl --native --embedded-catalog build/generated/resources/windows-x64
```

旧阶段直接压缩完整 ICU 数据：原始33,558,160字节、压缩10,504,647字节。现按用户要求改为Chromium common profile先裁剪再压缩；来源、兼容层、生成和检查范围见docs/icu-data.md，实际大小以当前RESOURCE-BUILD-INFO.json为准。生成目录本身不触发解压。测试在首次请求后设置 `UDATA_NO_FILES`，以数据租约调用 `udata_setCommonData`，验证实际多语言数据消费者；持有租约直到所有 ICU 使用者结束并完成 `u_cleanup`。运行时不生成 `.dat`。

## 尚待接入

资源服务已接入公开 C ABI 和 Runtime，ICU 通过 SDK 内会话令牌协调首次注册和最后清理；静态／动态 SDK 正在验证真实私有库合并与移动消费。具体接口、所有权及阶段边界见 `docs/public-resources.md`。视图、文字排版、AX 与媒体仍需持有统一 Runtime 和 ICU 会话，完整引擎尚未完成。
