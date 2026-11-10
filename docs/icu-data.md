# ICU 数据裁剪

使用 `third_party/icu-chromium/common.json` 的 Chromium 桌面 common profile。原始文件保留注释，Git blob、SHA256、Chromium 提交和来源记录在同目录 source.json；32份 HTML 编码映射及 windows-936 数据源也按该提交校验。CHROMIUM-LICENSE、ICU-LICENSE 和映射文件自身声明保留，许可证随SDK分发。更新规则时必须同时更新锁和实际验证证据。

数据由项目锁定的 ICU 79.1 源码及官方 icu4c-79.1-data.zip 生成，数据源码归档的官方发布 SHA256、大小和来源固定在 source.json。不会载入与库版本不匹配的 Chromium 预编译 dat。规则裁剪语言/资源子表、编码映射、排序和断行等数据；Unicode 核心属性、规范化和双向文字的编译内置部分仍由锁定 ICU 提供。

2026-11-03生成结果：33,558,160字节原始数据裁剪为10,470,976字节（2,343项），Zstd内置为4,362,007字节；较旧完整压缩数据10,504,647字节减少约58.5%。裁剪数据SHA256为5d16b1182fafdc1e8ee5318c9acc37929ea12a7680df858471bf9d394618410e，具体构建证据由TODO及阶段包记录，不把数据生成成功等同运行时全部验收。

`tools/icu_data.py` 把数据源码合并到 build 内的临时副本，使用原始 common filter 和 Chromium 编码映射。对 ICU 79 的 brkitr 源进行受校验的兼容处理：CJK line_cj 引用改向 Chromium 使用的 line 表，root 默认换行使用 line_normal，去掉 Chromium 源码同样没有的可选 LSTM 引用。原始依赖源码不改写。伪语言按 Chromium make_data_all.sh 移除；打包依照 copy_data.sh 的 --ignore-deps 行为，不能把过滤后残留的可选别名引用当作完整 ICU 服务承诺。缺失可选 LSTM 目标的上游生成器警告会保留在日志。

构建期只编译需要的数据工具，使用专用 main 适配在内存注册原始数据，并固定 UTF-8 输入编码；SDK继续采用禁文件IO的原始 ICU 库配方，不引入构建工具或编码器。工具、输入、规则、生成器共同进入内容键。生成结果在 build/generated/icu-data/<key>，manifest.json 记录原始/裁剪大小、SHA256、文件清单及GC依赖引用。Windows与musl可复用同一份小端数据。

资源目录明确声明 chromium-common transform，未提供预处理结果即失败，不会静默退回完整数据。裁剪后仍经Zstd压缩静态内置，首次实际使用才解压到内存；显式预热、合并加载、租约、池预算和线程规则不变。应用运行不写dat文件。

日常只运行相关验证：resources.icu_data实际注册裁剪数据，检查多语言字符/词/行/句边界、泰语字典分词、CJK严格/宽松换行、双向文字、Emoji、NFC、土耳其语大小写、法语排序和HTML编码转换；runtime.resources验证懒加载/多实例生命周期，document.editing和WebTest验证实际文字消费者。旧完整数据固定字节数不再作为API契约，测试限制在裁剪预算内。每个已验证功能阶段继续生成SDK和独立符号包，完整CI仍留正式阶段。
