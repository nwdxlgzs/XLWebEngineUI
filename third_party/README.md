# 第三方依赖

SVG/图像使用同一锁定Skia的SVG、skresources、Expat XML与PNG/JPEG/WebP/GIF/BMP/ICO解码路径。Wuffs e3f919ccfe3ef542cfc983a82146070258fb57f8按Skia DEPS提交从Google官方GitHub镜像锁SHA256与Apache-2.0原LICENSE，进入skia-sources.lock.json；Expat原MIT锁继续。配方显式构建svg/skresources等真实目标并合并闭包，不手改build源。网页入口与未完成能力见docs/svg-and-images.md；动画栅格、AVIF/JXL及Dawn/WebGPU继续实施。

`data/icu-quotes-source.json`锁定同一ICU源码内的原始dat及commit，`data/icu-quotes.inc`为tools/generate_quotes.py生成的CLDR引号小表（907locale/123父语言差异，Unicode-3.0）。它不修改上游源码，运行期不需要解压完整ICU数据；生成方法和边界见docs/generated-content.md，原ICU LICENSE继续随SDK保留。

源码锁定信息位于 `dependencies.lock.json`，工具链位于 `toolchains.lock.json`。两者记录具体版本或提交、内容 SHA256、来源与许可；所选源码已实际拉取。展开源码与编译结果分别位于根目录 `build/deps/src`、`build/deps/work` 和 `build/deps/install`，不把唯一来源或补丁保存在 build。

ICU数据先按Chromium桌面common profile裁剪，再压缩内置。原始规则、33份编码映射、提交/哈希/许可在`icu-chromium/`，ICU 79.1官方数据源码归档单独锁定；构建工具配方在`recipes/icu-data-tools/`。当前版本适配、构建边界与验证说明见`docs/icu-data.md`，不直接复用其他ICU版本的预编译数据。

WebTest 另带未经修改的 Roboto 字体，来源为 google/fonts 提交 `1c627bfa375fc51cf86fabeca4f6e08a95f0aa5c` 的 `ofl/roboto/Roboto[wdth,wght].ttf`。原文件、OFL-1.1 原始许可和 SHA256 锁在 `assets/webtest`，压缩后仅嵌入 WebTest，字体按其原许可随程序分发；不改变引擎或宿主源码许可。此字体与 Skia 开发测试字体分开管理。

| 组件 | 锁定版本/提交 | 主要职责 | 许可选择 |
|---|---|---|---|
| SDL3 | 3.4.18 | 窗口、输入、音频与宿主接入 | Zlib |
| Skia | c40d8c9ce629 | 绘制、合成和绘图缓存 | BSD-3-Clause |
| ICU | 79.1 | Unicode、国际化与文字边界 | Unicode-3.0 |
| HarfBuzz | 15.0.0 | 文字整形 | MIT |
| FreeType | 2.15.0 | 字体栅格化 | FTL |
| zlib / libpng | 1.3.2 / 1.6.59 | PNG 等图片 | Zlib / Libpng |
| libjpeg-turbo | 3.2.1 | JPEG | BSD-3-Clause、IJG、Zlib |
| libwebp | 1.6.0 | WebP | BSD-3-Clause |
| Brotli | 1.2.1 | 压缩字体等数据 | MIT |
| Zstandard | 1.5.7 | 压缩内置资源 | BSD-3-Clause 分支 |
| libwebm | 1.0.0.32 | WebM 容器 | BSD-3-Clause |
| libvpx | 1.17.0 | VP8 / VP9 | BSD-3-Clause |
| Opus | 1.6.1 | 音频 | BSD-3-Clause |
| libogg / Vorbis | 1.3.6 / 1.3.7 | Vorbis 及其构建依赖 | BSD-3-Clause |
| Lexbor | 3.0.1 | HTML / DOM / CSS 解析及选择器 | Apache-2.0 |
| QuickJS-NG | 0.18.0 | 嵌入式 JS 与共享 DOM 桥接 | MIT |

版本核对日期为 2026-11-02。有些上游没有 GitHub Release，或最新标签高于其 Release 页面，锁定时已同时核对标签。正式源包有公开 SHA256 时与上游比对；仓库快照固定完整提交并记录官方来源字节的 SHA256。正常构建只使用锁，不随上游分支漂移。

`python -B tools/deps.py fetch` 下载、解包、校验所声明的许可文件；`status` 查询当前机器的源码和独立编译状态。`build --target windows-x64 --jobs 8` 或对应 Linux 目标按配方与依赖顺序实际编译，CMake 配方在 `recipes/cmake.json`，Skia 最后通过 GN 构建；`verify` 用同一真实程序调用全部已编译依赖。可以显式给出组件名称缩小编译范围。已完成其他组件继续保留；源码状态、独立编译、最终 SDK 接入、行为测试及旧系统验证分别报告。

GN 固定为锁定 Skia 自身 `bin/fetch-gn` 使用的提交，由官方 CIPD 包准备；私有依赖接入与 Win7 的 DirectWrite/WIC 适配记录在 `patches/`，构建使用每目标的源码副本，原始校验源码不改写。当前 Skia 编译了 Raster、Ganesh/OpenGL、skshaper、skparagraph；SVG、PDF 等辅助 Skia 模块暂未启用，不表示整个引擎的对应能力已完成或放弃。VP8/VP9 当前通过上游脚本生成可移植 C 解码器，支持多线程及 VP9 高位深；手写 SIMD 汇编路径仍待补齐。其 Windows 生成步骤需要 Git for Windows 的 Bash/Perl 和 GNU make，目标编译与链接仍使用 Clang/LLVM。

2026-11-03 本机证据：18 项源码与许可文件核对通过；Windows/musl Release 全部依赖编译及组合调用通过，并已验证 Win32/X11 真实像素和原生子窗口所有权。组合测试包含 Skia、图片、Unicode/字体、HTML DOM、JS、压缩、音频编解码和 VP8/VP9 初始化。真实网页排版、AX、完整 WebM 播放和最终 SDK 合并仍在实施。

LLVM 目标 C++ 运行库由锁定的 23.1.4 源码实际编译，Linux 主机编译器为官方 focal 仓库固定的 23.1.5 构建。当前 musl ABI 验证仍使用本机 1.1.24 开发运行库边界；另已拉取并锁定 musl 1.2.6 源码，后续独立构建、加载器与旧用户态验证尚未完成。静态 SDK 不合并 libc。

当前 ICU 配方使用无文件数据加载开关和空数据占位，实际数据由压缩内置懒加载服务在首次请求后注册。Windows/WSL 已测试 UDATA_NO_FILES、内存注册、Emoji、土耳其语大小写、NFC 和字素分段；生产 Runtime 的多实例协调及 SDK 接入仍待完成。ICU C 符号和 C++ 命名空间增加 `_xl` 后缀，宿主冲突仍需独立验收。

Zstandard 通用配方服务于依赖验证和构建期编码器；`recipes/zstd-runtime` 单独仅编译解压部分，资源测试和符号审查已通过。引擎 SDK 后续接入同一解压配方，编码器留在构建侧。FreeType 明确选择 FTL，需要随交付保留其声明和文档要求；其他组件及传递依赖也按实际启用部分收集完整声明。

资源和软件视图 SDK 的私有依赖合并、C/C++ 符号隔离及移动消费已通过双平台两配置验证。当前继续将 Lexbor/QuickJS 文档及文字管线合入同一 SDK，完整网页、输入、AX 与媒体仍待完成。Win7/UOS 真机验证继续保持未验证。

Skia 的 `patches/skia-icu-lifetime.json` 给私有 SkUnicode 增加迭代器缓存清理入口；最后一个 ICU 会话先关闭 paragraph/Unicode 使用者，再清理缓存、执行 u_cleanup 和释放内存数据。补丁内容进入构建键，不修改上游校验源码。测试字体仅取自同一锁定 Skia 源码测试资源，不作为独立字体随 SDK 分发；应用字体的许可由其实际来源单独记录。

Lexbor 的 `patches/lexbor-focus-parser.json` 让锁定解析器接受 `:focus-within`、`:focus-visible`，并补齐普通伪类权重；由引擎动态选择器适配提供匹配语义。该补丁同时修正嵌套选择器函数的分项权重累加；新增 `patches/lexbor-selector-retry.json` 修复函数或复合选择器失败后的关系候选重试。专用CMake配方只在build从原文重新生成带修改标记的C文件并替换对应编译输入，原始源码不变。补丁和配方均进入精确依赖键；Apache-2.0来源许可继续随SDK保存，边界见docs/focus-styles.md。`lexbor-empty-inline-style.json`允许在框架私有颜色计算使Lexbor级联为空时，仍正常替换或移除inline style；原始DOM属性文本和列表所有权不变。

Linux 原生 X11 基础栈保存在 `platform-sources.lock.json`、`recipes/x11.json`，libXtst 扩展保存在 `platform-extensions.lock.json`、`recipes/x11-extensions.json`，统一通过 `python -B tools/platform_deps.py x11` 使用 Clang/musl 编译。SDL musl 构建要求当前私有栈成功，`deps verify --native` 验证真实像素与原生子窗口所有权，详细边界见 `docs/native-display.md`。

依赖验证要求当前源码、配方、编译器和传递依赖的精确构建键。`tools/validate_dependencies.py` 自动恢复依赖、构建编码器、生成压缩资源并验证真实显示与 ICU，已接入两平台完整 CI。配方变化后旧的成功归档仍属于历史证据。
