# CSS 轮廓

支持outline简写及outline-width、outline-style、outline-color、outline-offset。简写重置前三项，保留独立offset；width支持非负长度及thin/medium/thick，offset支持正负长度，color沿现有颜色解析/currentColor，支持inherit/initial/unset。按原声明位置、选择器权重和!important层叠；无效的hidden样式、负宽度、百分比offset和无效颜色不会覆盖前面有效声明。作者的style属性原文不改写，C/C++和JS仍共用样式链。

实线、点线、虚线、双线、groove/ridge/inset/outset、none和auto均进入实际绘制。auto采用可重复的2px轮廓；focus-visible默认蓝色auto样式，作者outline:none可覆盖，文字控件和输入方式策略见focus-styles.md。none时宽度计算为0；inherit复制父计算宽度/颜色，currentColor在当前元素解析。现有getComputedStyle序列化仍有项目已记录的不完整计算值边界，不宣称完整浏览器CSSOM。

outline是独立绘制项，不改变行高、盒尺寸、文本/命中/AX边界或scroll overflow。块、Flex/Grid使用真实border box；普通行内使用真实逐行片段，并包含在行内拆分处的块盒。PathOps合并片段轮廓，保留短行外的空白区域，重叠部分只应用一次透明度。每元素最多4096个片段，超过返回UNSUPPORTED。负offset向内收缩，极端负值按各轴夹取到至少保留2倍轮廓宽度的外部尺寸，不因缩过中心而消失。

轮廓在当前保留场景内容之后、阅读/编辑选区之前绘制；原子行内的轮廓留在它自己的不可变场景内，外层不重复画。元素自身overflow不剪掉自身轮廓，祖先与视口裁剪照常应用；滚动平移继承同一片段与clip。outline不参与滚动范围计算，不能用轮廓墨迹制造额外可滚区域。

OutlineShape在轮廓/片段/clip改变时创建，缓存合并几何、双线、阴影路径与虚线效果。Raster比较保留项，旧新裁后墨迹进入带抗锯齿扩展的损伤；原子PaintScene的墨迹和内容相等判断也包含轮廓。局部重绘不用重新求PathOps，静止视图不重复布局/绘制/提交。轮廓位于布局范围之外的像素也需在移动、清除、样式变化时清掉。

锁定Skia的PathOps是独立source_set，现有主Skia归档未包含它；third_party/recipes/skia-pathops从同一源gn/pathops.gni提取精确cpp补编这一个模块。静态SDK合并并隔离符号，动态SDK私有链接，不暴露Skia类型，不触发完整Skia重编。包内PATHOPS-BUILD-INFO.json记录源码和上游源清单哈希，原Skia许可继续随包保留。

当前边界：圆角轮廓、完整层叠上下文/定位变换、系统高对比与平台auto主题、CSS变量/calc及revert/revert-layer仍待实现。保持未完成声明的unsupported诊断，不能把本阶段视作完整CSS、系统可见焦点偏好或平台读屏验收。测试覆盖真实像素、四边立体明暗、透明多行/原子场景、旧新损伤、祖先裁剪/滚动、几何/AX不变、增量/完整一致与静止复用。
