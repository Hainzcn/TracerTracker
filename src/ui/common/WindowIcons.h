#pragma once
#include <QIcon>

// ============================================================
// WindowIcons — 窗口控制按钮图标（无锯齿、像素对齐）
//
// 提供与原生 Windows 标题栏视觉一致的最小化/最大化/还原/关闭
// 图标。所有图标均按 1px 描边、10px 主体边长在 16×16 画布上居中
// 绘制，使用统一的灰色调（#cccccc / #787878）。
//
// 既被 FramelessWindow 顶栏复用，也被 SidePanel 等内嵌面板的关闭
// 按钮复用，保证整套 UI 的"叉号"风格统一。
// ============================================================

namespace WindowIcons {

QIcon makeMinimizeIcon();
QIcon makeMaximizeIcon();
QIcon makeRestoreIcon();
QIcon makeCloseIcon();

} // namespace WindowIcons
