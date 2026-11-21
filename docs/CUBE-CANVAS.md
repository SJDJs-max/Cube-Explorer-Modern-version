# 魔方视图控件（0.3.1）

`src/app/cube_canvas.hpp/.cpp` 是纯显示/指针事件组件，不依赖求解器，也不修改输入状态。

## 参考与几何

- `Classic` 使用原 `FaceCube.pas:DrawCube` 的六面拓扑和贴纸坐标：L/F/D/B 为方形、U/R 接在 F 上，所有坐标只乘一个统一缩放值。紧凑结果缩略图默认使用该模式。
- `Net2D` 参考指定的 `D:/dsh_workspace/cube/web/index.html` 网格：U / L-F-R-B / D，贴纸保持正方形，不随容器宽高单独拉伸。
- `Cube3D` 参考该项目实际使用的 TwistyPlayer Cube3D。默认相机经度 30°、纬度 35°、视场角 20°，没有滚转；相机距单位宽魔方为 6，控件采用三单位宽魔方，因此内部距离为 18。XYZ 轴同尺度，所有面共用一个透视投影，不单独拉伸各面。
- 3D 使用 26 个独立等尺寸块体，每块包含平面、倒角面和角部三角面，按法向量剔除背面并按深度绘制。贴纸宽度为单位块体的 0.85，具有轻微圆角、柔和明暗和底部软阴影。贴纸纹理与中心方向箭头使用同一面的透视变换。绘制和点击由相同的物理贴纸投影计算，拖动相机不重排贴纸。
- 指定项目仅供只读参考，未复制其搜索算法或结果整理逻辑。无新增运行库。

## 集成 API

```cpp
#include "cube_canvas.hpp"
auto* canvas = new CubeCanvas(false, parent);
canvas->setPattern(pattern);
canvas->setView(CubeCanvas::View::Classic); // Net2D / Cube3D
canvas->setDarkMode(dark);
canvas->setColors(colors); // array<QColor,6> 或 7；第7项是未知色
canvas->onSticker = [](int physicalFacelet, Qt::MouseButton button,
                       Qt::KeyboardModifiers modifiers) { /* 原版编辑逻辑 */ };
canvas->onBackground = [](Qt::MouseButton button,
                          Qt::KeyboardModifiers modifiers) { /* 原版背景点击逻辑 */ };
```

`setFacelets(string54, logicalCenterOrientations)` 适用于正在染色、尚不满足合法角棱的编辑状态。中心方向单独按逻辑 URFDLB 传入；渲染根据六个中心颜色推导握持参考系，再转换箭头方向。所有回调的 index 仍是物理 URFDLB 54 面顺序，不因相机变化而重新编号。

`CubeCanvas(true)` 创建紧凑视图且默认禁用编辑。如结果缩略图需要点击行为，显式 `setEditable(true)` 并连接回调。`setShowFaceLabels(true)` 可显示未指定中心方向的面名称，默认不额外加字。`resetCamera()` 恢复默认等比例观察角度。

默认姿态为白顶、绿前、红右。URFDLB 对应白、红、绿、黄、橙、蓝，精确 RGB 依次为 `(255,255,255)`、`(238,0,0)`、`(0,216,0)`、`(254,254,0)`、`(255,161,0)`、`(0,0,242)`；3D 的明暗仅作用于绘制，不改写这些颜色值。特殊标记按 `FaceCube.pas:372-373` 绘制：Shift 身份标记（小写字母）是黑底加对应目标色的交叉斜线；Ctrl 朝向标记 `@` 是黑底加 U 色单向斜线。`@` 不携带当前画笔颜色，不因改变显示而更改标记语义。

2D/原版视图在 mousePress 回调，保持原版操作时机。3D 短点击在 release 回调；左键无修饰拖动只旋转相机，不发送编辑事件。Ctrl/Shift/Alt 与鼠标键原样传递；3D 滚轮只缩放视图。

## 原版染色区域容器

`src/app/original_editor_view.hpp/.cpp` 把经典魔方、当前画笔方块和自动修正组合为原版 FacePaint 区域。默认以 332×250 为基准统一缩放；魔方占 330×240，画笔方块位于 (228,150)–(293,215)。原 DFM 的 FacePaint.Top=12、ColorCheck.Top=231，因此自动修正在本容器内位于 y=219。画笔只显示颜色，说明放在 tooltip 中。

容器 `heightForWidth(width)` 返回 `ceil(width*250/332)`，高度跟随宽度。`setView(0/1/2/3)` 分别选择经典展开、2D 展开、3D、并排 2D+3D。四种视图共用经典画笔和自动修正的位置与尺寸。2D 利用展开图右下方的空白，3D 在画笔左侧显示，并排时立体图位于画笔上方。两个画布同步贴纸、朝向、配色与主题，点击贴纸会传递原鼠标键与修饰键。

`CubeCanvas::setDrawingMargin(0)` 供该容器对齐原始坐标。其他调用者的默认边距不变；负值恢复默认。

显示缩放以 Qt 逻辑像素为单位：`applyAppTheme(app,dark,scale)` 缩放全局字体和控件样式，`ResultRow::setUiScale(scale)` 缩放结果行坐标、字体、图标和高度，范围 0.75–2.0。染色区域则从实际宽度相对于 500 的比例统一缩放。不要另外乘显示器 DPR，Qt 会处理显示器缩放。字体优先 Calibri，中文回退 Microsoft YaHei；名称标签右对齐紧贴输入框。结果只显示原式 `(nf)` / `(nf*)`，不展示旋转次数，但保留并校验算法内部旋转计数。

主窗口收起 / 展开的默认尺寸分别为 510×730 和 1120×730，首次展开时居中，编辑器底部没有多余伸展占位。结果行基准高度为 140，经典缩略图为 168×122；求解 / 生成标签与名称标签对齐，长公式可增加行高。公式窗口独立按 610×740 参考尺寸缩放，默认大小为 550×670，具有最小化 / 最大化按钮且不设置始终置顶。以上尺寸均为逻辑像素。

## 验证

`tests/cube_canvas_tests.cpp` 检查 54 个贴纸在多种长宽比下的完整命中映射与边界、2D 正方形、原版展开拓扑，以及 3D 的透视几何。测试将每个面的投影反变换为 3×3 网格，确认每块都是相同的真实正方形；检查三面公共顶点、无滚转相机和缩放一致性，不错误要求远近贴纸具有相同屏幕尺寸。

本轮遍历全部 24 种逻辑握持参考系及 5 个观察方向，覆盖全部 54 个物理贴纸，检查实际渲染颜色、命中、Ctrl+Shift+右键传递、相机拖动和缩放不发送编辑，以及相机重置；同时覆盖全部 54 个外层 / 宽转 / 中层 / 整体旋转动作。375×293、500×390、1000×780、930×280、250×650 等尺寸下仍按同一比例绘制。精确 RGB 与特殊标记的目标色、黑底和纹理区别也有验证。

0.3.1 专项结果：offscreen 共 24,610 项通过（含四张截图保存检查），Windows 原生共 24,606 项通过。`tests/cube-canvas-v031-3d-day.png`、`tests/cube-canvas-v031-3d-night.png` 展示默认白顶绿前、真实宽转后状态及拖动视角；`tests/cube-canvas-v2-day.png`、`tests/cube-canvas-v2-night.png` 在本轮重新生成，用于经典 / 2D / 3D 和标记对照。四图已实际查看。

`tests/original_editor_view_tests.cpp` 有 500 项检查，覆盖四种尺寸下的原版坐标、字体比例、画笔正方形、控件不覆盖贴纸、54 面映射、右键 Ctrl+Shift、自动修正回调和四种视图。截图 `tests/original-editor-{0,1,2,3}-{day,night}.png` 已生成，经典日夜和并排日间已经实际查看；最新日志为 `logs/original_editor_view_tests-visual-v4.txt`。

结果行有 91 项验证（含截图保存），包括四档缩放、名称间距、字体比例及旋转计数仅隐藏显示的检查。日夜缩放样例见 `tests/editor-markers-scale-{75,100,150,200}-{day,night}.png` 与 `tests/result-row-scale-{75,100,150,200}-{day,night}.png`。最终整体验收、DPI 验证及便携 EXE 记录见 `CORE-VALIDATION.md`。
