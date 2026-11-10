# Dawn / WebGPU 与 Skia Graphite

本阶段将锁定 Skia DEPS 对应的 Dawn/Tint 接入私有静态依赖闭包。
原生 WGSL 探针在 Windows 已验证内置 SwiftShader、D3D11 的可选 FXC、D3D12 的静态 DXC、静态 ANGLE 的 GLES/OpenGL 和真实 NVIDIA Vulkan。
Windows 的 Graphite View 动态/静态、硬件恢复、八轮真实 DLL 卸载及阶段 SDK 已通过检查。
包内 WebTest 的 Dawn 和删除可选 FXC 后的原生 OpenGL 均实际绘制 Canvas/SVG；111 个导出与独立符号包已核验。
musl 原生 WGSL、线程析构和像素探针已通过，Skia/View 与阶段 SDK 仍在验证中。以对应阶段报告和包内 BUILD-INFO 为准。

## 接口与范围

公共后端枚举追加 `XL_GRAPHICS_DAWN`（8）、`XL_GRAPHICS_DAWN_SWIFTSHADER`（9），原值和结构布局保持。
`WebTest --graphics dawn`、`--graphics dawn-swiftshader` 用于显式选择。
硬件候选和内置 CPU Vulkan 设备分别报告，不能用 CPU 回退成功声称硬件后端可用。

本阶段的原生渲染接口不等于网页 `navigator.gpu` 或 `canvas.getContext('webgpu')` 已实现。
网页 API 的异步请求、对象生命周期、取消、缓冲区映射和 Canvas 呈现仍是后续完整目标。
引擎没有加入 HTTP/HTTPS 请求栈。

## 渲染与生命周期

Graphite 使用自己的 Recorder、Recording、插入、提交和读回流程，与现有 Ganesh 共用 GpuDevice 适配接口。
绘制保留 Skia surface；静止帧复用现有结果，滚动沿用已实现的保留布局投影。
提交绘制时不主动等待全部 GPU 工作；宿主实际请求 CPU 像素时提交读回并等待。
异步回调持有独立状态，设备销毁后不引用 View 或 GpuDevice 的裸指针。
设备或着色器失败进入现有候选降级流程，再绘制保留内容。

每个 Recorder 有独立 ImageProvider。SVG/Canvas/解码位图首次实际绘制时上传，后续按 Skia 的图像内容、子集键和 mipmap 要求复用纹理。
LRU 最多保留 64 项、64 MiB 的载荷估算；这不代表整个 GPU 或进程的内存上限。淘汰只移除缓存引用，录制和活帧继续持有自己的引用。
上传失败标记设备失效，让整帧走既有候选恢复，避免只跳过缺失图像。Recorder 不在多个线程或 View 之间共享。

Windows 硬件丢失对照实测仅红蓝渐变的 400 个通道存在最大 1/255 的跨设备量化差；Canvas/SVG 和 alpha 继续精确比较，恢复结果还与同后端全新 View 精确对照。
这项限度只适用于该测试图案，不能推断所有页面像素差异都在这个范围。

这条路径不包含 GPU 直接呈现到宿主交换链，也不表示已经消除原网页首帧布局或定时器造成的重排。
性能结论必须绑定具体页面、库指纹、设备和调用范围。

## 依赖与可重建性

- `third_party/dawn-sources.lock.json` 固定源码、来源、内容 SHA256、许可证和生成器版本。
- `tools/dawn_deps.py` 使用私有配方构建单份 Dawn/Tint 静态归档，复用锁定的 SPIRV/Vulkan 源码，以及已有 ANGLE、SwiftShader、DXC 实现。
- 关闭的是上游测试 corpus、示例、CLI 和 C++ module 接口的独立构建；原生 D3D11/12、Vulkan、OpenGL/GLES 和 WGSL 运行路径保留。
- Skia 的调试 SPIR-V 验证复用 Dawn 中的同版实现。安装仅收集当前 Ninja 目标所需归档，避免增量目录中的旧库被带入。
- 新 Ninja 用 `inputs`，旧 Ninja 1.10 用同一目标的 `graph` 闭包收集归档，保留带空格路径并排除外部 Dawn 与不可达旧库。未知目标等实际错误继续失败；Windows 真实 12 个归档在两种收集方式下保持一致。
- 三方适配只通过 `third_party/patches` 从原始锁源生成独立 overlay。删除 overlay 后补丁可以重放，删除 build 后由锁、补丁、配方重新取得所有输入。
- SDK 保留各依赖实际许可证。OpenGL Registry 的当前 `gl.xml` 标注 Apache-2.0；不能按历史 MIT 描述归类。外置 shader 测试 corpus 和 CTS 不参与运行时构建或 SDK 分发。
- Windows 的 `d3dcompiler_47.dll` 继续作为包内可删除的独立选项；DXC、SwiftShader、EGL/GLES 的实现沿用私有静态闭包。
- Dawn 的 SharedFence 比较辅助函数按需探测 `CompareObjectHandles`；缺失时继续实际打开句柄，仅放弃包装器复用优化，不对 Win10 接口产生硬导入。
- 本机旧 musl 不提供 `__cxa_thread_atexit_impl`，现有运行库的静态编译探测曾错误记录为存在。Dawn 闭包编译同一锁定 LLVM 源码的原始 pthread TLS 析构后备实现，阻止链接器提取错误的强引用对象；来源、许可和构建输入均进入闭包，未手改工具链或三方源码。工作线程退出实际核对两个非平凡 TLS 析构。上游后备实现不延迟 DSO 卸载、主线程析构发生于静态退出，不能推广为任意 libc/宿主卸载保证；本机 musl 的 dlclose 保留代码映射，仍单列报告。

系统 Vulkan loader 在开发机枚举 ICD 时，系统安装的 D3D 映射驱动自行加载其 DXC/DXIL。调用栈已定位到 `vulkan_dzn`，不是 SDK 静态入口的 DLL 依赖。其他探针路径继续禁止独立 DXC/DXIL、SwiftShader、EGL/GLES DLL。

旧 Windows/Linux、macOS、Web 与所有硬件驱动的支持必须分别取得平台证据；当前开发机通过不替代这些验收。
