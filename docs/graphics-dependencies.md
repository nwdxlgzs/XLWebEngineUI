# 图形依赖及分发边界

本项目保留闭源商用目标；源码依赖的运行闭包选择宽松许可，构建工具和目标平台运行库另外记录。表中“接入”以对应阶段行为报告为准，锁定或编译成功不代表全部平台可用。

| 运行组件 | 选择的许可 | 来源与交付方式 |
|---|---|---|
| Skia、ANGLE、Chromium zlib utils | BSD 系许可；zlib utils 保留双来源声明 | 由锁定源码构建并私有静态链接，原始许可随 SDK |
| SwiftShader、marl、SPIRV-Tools、SPIRV-Headers、ASTC | Apache-2.0 / MIT，按实际文件声明 | 选择 Subzero Reactor，关闭未选 LLVM 与测试；原始许可随 SDK |
| Subzero 与其 LLVM 支持 | NCSA / MIT | 私有符号闭包，原始许可随 SDK |
| Linux GL/GLX 头文件 | MIT | 锁定 libglvnd 官方提交，仅提取 GL/KHR 声明；原文件许可随 SDK，不引入 GLVND 驱动实现 |
| DXC、其 LLVM/Clang 运行闭包 | NCSA / MIT，完整声明见上游 LICENSE.TXT | 真实静态实现，排除 TableGen 等主机构建工具；miniz.c 的 MIT 声明单独保留 |

锁为 `third_party/graphics-sources.lock.json`；Skia 传递库按其 DEPS 锁在普通依赖记录中。SDK 不附带独立 libEGL、libGLESv2、SwiftShader 或 DXC DLL。系统 GPU 驱动和系统图形 API 是平台边界，不能将驱动实现的许可写成引擎源码许可。

Windows 目标由 Clang/LLD 编译，但使用 Windows SDK、MSVC C/C++ 运行库及 DXC 上游 Windows 路径所需的 ATL 支持。它们由 Microsoft 条款管理，不属于 DXC 的 NCSA 许可。静态链接不消除对应分发条件，也不能把工具链文件或 ATL 源码按 MIT 再许可。正式商业 SDK 使用合适授权的构建工具链和 Release 运行库，交付前按该版本条款核对可分发代码。Debug 预览包用于开发验证，不作为生产商业发布验收；包内 `production_ready=false` 保持明确。

`d3dcompiler_47.dll` 是可删除的可选 FXC 组件，SDK 不从开发机复制它。需要随客户产品附带时，必须采用允许再分发的 Microsoft 来源与原有条款；不能从系统目录抽取后视为宽松许可依赖。支持 SM6 的 D3D12 设备优先使用本库静态 DXC，旧 D3D11/SM5 路径仍可能需要 FXC；缺失时继续图形候选降级。

2026-11-04 核对的官方条款入口：

- Visual C++ 分发说明：https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170
- Visual Studio 2022 可分发代码清单：https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
- ATL 分发说明：https://learn.microsoft.com/en-us/cpp/windows/redistributing-an-atl-application?view=msvc-170

这些入口用于核对工具链边界；项目没有替客户取得软件授权，也不将开发机安装状态当成商业发布许可证明。正式发布门禁继续审查实际配置、运行闭包和随包声明。
