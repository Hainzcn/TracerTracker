# 协议配置面板关闭后右侧 ~40 px 像素残留

> 涉及文件：[`src/ui/MainWindow.cpp`](../../src/ui/MainWindow.cpp) · [`src/ui/ProtocolConfigPanel.cpp`](../../src/ui/ProtocolConfigPanel.cpp) · [`src/ui/opengl/Viewer3D.cpp`](../../src/ui/opengl/Viewer3D.cpp)
>
> 关键 commit 修改集中在 `MainWindow::toggleConfigPanel` 与 `m_configPanelAnim` 的 `valueChanged` / `finished` 处理。

---

## 概述

**现象**：唤出协议配置面板后，

- 点面板右上角的"叉号"关闭 → 面板干净退出，无任何残留；
- 点击侧栏的"配置"按钮再次关闭 → 面板回收部分，**侧栏右侧会留下约 40 px 宽的面板残留**；该残留在点击任意处后消失。

**原因**：协议配置面板是 3D 视口（一个 `QOpenGLWidget`）里的子控件，Qt6 在这种"OpenGL 合成"模式下有一个长期存在bug——**子控件移出可视区或被隐藏时，父级 OpenGL 后台缓冲并不会自动把子控件曾经覆盖的像素擦除**。点叉号关闭时，鼠标在面板内部的进出/失焦事件链恰好顺带触发了一次视口重绘，遮盖残留；点侧栏按钮关闭时，鼠标全程在视口之外，这次"侥幸的副作用刷新"不存在，残留裸露。任何后续的鼠标点击都会触发一次新的重绘使得残留消失。

**解决办法**：让面板动画的每一帧都主动驱动视口重绘 (`m_viewer->update()`)，并在动画结束时用 `setVisible(false)` 先隐藏、再 `move()` 到视口外、再用同步的 `repaint()` 强制立刻走一次 `paintGL` 把整张 OpenGL 后台缓冲清掉，最后兜底再请求一次顶层窗口刷新。这套组合彻底切断了"子控件已经走了，父级却还以为它在原地"的不一致窗口。

---

## 复现路径

1. 启动程序，等待 3D 视口初始化完成；
2. 点击侧栏 ⚙ "配置"按钮 —— 协议配置面板从左侧滑入；
3. **再次**点击同一个 ⚙ 按钮 —— 面板向左滑出；
4. 动画结束的瞬间，**侧栏右侧紧贴着会残留约 40 px 宽的面板碎片**（看起来像是面板的最右侧那一条还卡在视口左边缘没收回去）；
5. 在窗口任意非面板位置点一下 → 残留立刻消失。

如果第 3 步用面板内的"叉号"关闭，则不会有残留。这是该 bug 最迷惑人的地方，让人以为是关闭逻辑本身有差异。

## 控件层级与渲染上下文

```
MainWindow (FramelessWindow)
├── ToolBar        （顶栏）
├── HBox
│   ├── SideBar    （宽 48 px，3D 视口左侧）
│   └── VBox
│       ├── Viewer3D   ←  QOpenGLWidget，整个窗口因此切换到 OpenGL 合成
│       │     ├── AttitudeWidget        （隐藏位置 -width+50，永远部分可见）
│       │     ├── SensorChartPanel      （同上）
│       │     ├── ProtocolConfigPanel   （隐藏位置 -360，完全移出视口）★
│       │     ├── SensorInfoOverlay
│       │     └── ViewOrientationGizmo
│       └── DebugConsole
└── StatusBar
```

★ `ProtocolConfigPanel` 是唯一一个"完全滑出视口"的浮动子控件——其他几个隐藏位置都设计成"留 50 px 在视口里露出做 hover 触发热区"，因此**只有协议配置面板会触发这个 bug**。

## 根因详解

### Qt6 OpenGL 合成模式的已知行为

引用自 Qt 论坛 [#152366](https://forum.qt.io/topic/152366/) 与 [`QOpenGLWidget` 文档](https://doc.qt.io/qt-6/qopenglwidget.html)：

> Once a `QOpenGLWidget` is added to a hierarchy, the entire top-level window switches to OpenGL-based compositing. The window's backing store is **not always correctly invalidated** by the underlying platform when the child widget moves/hides.

也就是说，只要窗口里有一个 `QOpenGLWidget`，整个顶层窗口的合成就走 OpenGL 路径：

1. 每一帧先调用 `paintGL()` 渲染 OpenGL 内容到 FBO；
2. 然后 Qt 合成器把所有非 GL 的子控件按当前 `pos`/`size`/`visible` 状态贴到 FBO 上；
3. FBO 提交给系统窗口后端。

理论上子控件 `move()` 或 `hide()` 时 Qt 应该自动把 FBO 里被让出的区域标脏。**但在 Qt6 的 Windows 平台合成路径下，这个"标脏"在动画末尾会丢**——尤其是当 `move()` 与下一帧 `paintGL()` 的时序不凑巧时。

### 为什么是 ~40 px

- `Viewer3D` 是 `QOpenGLWidget`，`Viewer3D::paintGL()` 内部 `glClear()` 会把整个 FBO 擦干净，之后才合成子控件。**只要 `paintGL()` 真的被触发**，残留必然会被清掉。
- 但 `update()` 是异步的：它只是 schedule 一次 `UpdateRequest` 事件。Qt 的事件循环会把短时间内的多次 `update()` **合并成一次** `paintGL()`。
- 关闭动画用的是 `QEasingCurve::InCubic`，前慢后快——最后几帧 `pos` 大幅跳变。事件循环极容易把"动画末尾几帧的 update + finished 后的 update"合并成一次 paint。
- 这次合并后的 paint **恰好落在 `m_configPanel->setVisible(false)` 之前**，于是 paintGL 仍然把面板按"快收完但还没收完"（约 `pos = (-320, 0)`）的位置合成上去，最后一帧 `pos = (-360, 0)` 就再也没有机会被渲染。
- 面板宽 360，最后停留位置约 -320，`-320 + 360 = 40` —— 这就是那 ~40 px 残留的来源。

### 为什么"叉号关闭"恰好不会出问题

`closeBtn` 在面板内部。点击它会产生一连串事件：

1. mouseRelease 在 `closeBtn` 上 → `closeBtn` 自身重绘"释放态"；
2. closeRequested 信号 → `toggleConfigPanel` → 启动关闭动画；
3. 动画进行中面板向左滑动，鼠标位置（不动）相对面板的关系 hover-leave；
4. 面板隐藏后，焦点从面板内的子控件 (`closeBtn`) 上失去，触发 focus-out；

第 3、4 步都会**意外地**触发一次 viewer 重绘——不是设计行为，纯属副作用。这次顺手的重绘正好把残留擦掉了，让人误以为这条路径"本来就没毛病"。

而点侧栏 ⚙ 按钮关闭时，鼠标全程在 `SideBar` 上（视口的兄弟控件，不是子控件），不会产生任何"波及到 viewer 的鼠标/焦点事件"，副作用刷新链断了，残留就裸露出来。

### 为什么 `m_viewer->update()` 一次还不够

这是排查时走的弯路。第一版补丁就是在 `valueChanged` 与 `finished` 里都加了 `m_viewer->update()`，逻辑上看起来已经覆盖了所有动画帧，但实测**仍有残留**。原因正如前文所述——`update()` 异步可合并，事件循环把它们打包成一次 paint，落地时机与 `setVisible(false)` 的先后顺序不保证。

## 解决方案

修改集中在 `MainWindow::toggleConfigPanel` 与 `m_configPanelAnim` 的两个槽：

### 1. 每一帧 `valueChanged` 主动 `update()`

```cpp
connect(m_configPanelAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
    if (m_viewer) m_viewer->update();
});
```

保证动画过程中 OpenGL 视口持续重绘，避免因数据流为空而完全没有 paint 的情况。

### 2. `finished` 收尾分为三步走

```cpp
connect(m_configPanelAnim, &QPropertyAnimation::finished, this, [this]() {
    if (m_configPanelExpanded) {
        m_configPanel->move(0, 0);
        if (m_viewer) m_viewer->update();
    } else {
        // ① 先 hide：让 Qt 把子控件从合成层摘除并把它让出的区域标脏
        m_configPanel->setVisible(false);
        // ② 再 move：把面板状态归位到 (-PANEL_WIDTH, 0)
        m_configPanel->move(-ProtocolConfigPanel::PANEL_WIDTH, 0);
        if (m_viewer) {
            // ③ 同步 repaint：强制立刻走一次 paintGL，glClear 整个 FBO，
            //    合成器用最新（已 hide）的子控件状态重新合成
            m_viewer->repaint();
            // ④ 顶层窗口的 backing store 兜底刷新
            if (auto* top = m_viewer->window()) top->update();
        }
    }
});
```

三个关键点缺一不可：

| 关键点 | 解决的问题 |
| --- | --- |
| **`setVisible(false)` 先于 `move()`** | 直接告诉 Qt 子控件不再参与合成，由它来标记被让出区域为 dirty；如果反过来先 move、再 hide，合成器有可能把"上一帧位置"上的像素继续留在 FBO 里 |
| **`repaint()` 替换最后一次 `update()`** | `update()` 异步可合并，会被事件循环合并到 `setVisible(false)` 之前；`repaint()` 同步、立刻走一次 `paintGL()`，时序保证 FBO 落在已 hide 之后 |
| **追加 `window()->update()`** | 兜底刷新顶层窗口的 backing store——按 Qt6 报告，OpenGL 合成路径下 top-level backing store 也可能持有旧像素 |

### 3. 同时修正 `toggleConfigPanel` 启动一帧

```cpp
m_configPanelAnim->start();
if (m_viewer) m_viewer->update();
```

避免动画第一帧之前 OpenGL 视口里仍是上一次关闭后的旧画面。

## 排查过程小结

| 轮次 | 假设 | 结果 |
| --- | --- | --- |
| 1 | 单纯加 `valueChanged → m_viewer->update()` 与 `finished → m_viewer->update()` 即可 | **失败** —— `update()` 异步可合并，paint 时序仍然落在 hide 之前 |
| 2 | 调换顺序（先 hide 再 move）+ 用同步 `repaint()` + 顶层 `window()->update()` 兜底 | **成功** |

走过的弯路：

- 一开始误判这是协议配置面板自身的"动画位置算错"或"按钮 checked 状态不同步"——加了一堆状态对齐代码，并无实质帮助。
- 从"叉号关闭正常 / 侧栏关闭异常"的差异里**误判为路径有差异**，去研究 `closeRequested` 信号链；实际上两条路径调用的都是同一个 `toggleConfigPanel`，差异完全在于鼠标事件的副作用刷新。
- 没有意识到 `update()` 与 `repaint()` 在 Qt6 OpenGL 合成模式下的时序差异是关键——`update()` 在普通 widget 树上几乎总是够用，但在 OpenGL 合成路径上未必。

## 相关代码位置

- 动画构造与槽连接：`MainWindow::MainWindow()` 内 `m_configPanelAnim` 一段
- 切换入口：[`MainWindow::toggleConfigPanel()`](../../src/ui/MainWindow.cpp)
- 子控件层级：[`MainWindow.h`](../../src/ui/MainWindow.h) 头部注释 + 构造函数
- 协议面板本体：[`ProtocolConfigPanel`](../../src/ui/ProtocolConfigPanel.cpp)（与本 bug 无直接关系，仅作为受影响子控件存在）

## 触类旁通

同样的修复也顺带应用到了 `m_attitudePanelAnim` 与 `m_sensorChartPanelAnim`：它们因为隐藏位置只到 `-width+50`（永远部分可见），所以**碰巧**没有暴露过这个 bug，但当时机不凑巧（数据流停止 + 用户快速切换）时同样可能出现轻微闪烁。统一加上 `valueChanged → m_viewer->update()` 后顺势封堵。

未来如果再有"贴在 `Viewer3D` 上的浮动子控件需要完全滑出可视区"的需求，请直接复用本文件描述的三步收尾模式（hide → move → 同步 repaint → top-level update），或考虑把这种全退场的浮动控件 reparent 到 `MainWindow` 而非 `Viewer3D`，从根上规避 OpenGL 合成路径。

## 参考资料

- Qt Forum: [Is it expected I have to update the parent widget myself with using QOpenGLWidget?](https://forum.qt.io/topic/152366/)
- Qt 文档：[QOpenGLWidget Class · Painting Techniques](https://doc.qt.io/qt-6/qopenglwidget.html#painting-techniques)
- Stack Overflow: [QWidgets Leaving Artifacts Of Previous Paint](https://stackoverflow.com/questions/27058165/)
