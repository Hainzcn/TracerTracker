#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>

class QFrame;
class QScrollArea;
class QLabel;

// ============================================================
// SidePanel — 侧滑面板基类
//
// 提供与 ProtocolConfigPanel 相同的视觉骨架，便于其他面板复用，
// 保持整套 UI（标题、关闭叉号、分隔线、滚动区、动作栏、缩进梯度
// 与 QSS）的风格统一。
//
// 布局自顶向下：
//   ┌──────────────────────────────────┐
//   │ titleLabel              [×]      │   ← 标题栏（14/10/8/10）
//   │──────────────────────────────────│   ← configSep
//   │ scrollArea (configScrollContent) │
//   │   contentLayout()  ←  子类填充    │
//   │──────────────────────────────────│   ← configSep
//   │ actionLayout() (按需出现)         │   ← 14/10/14/10
//   └──────────────────────────────────┘
//
// 子类协议：
//   1. 在自己的构造函数中调用 SidePanel(title, width, parent)，
//      然后向 contentLayout() 添加业务控件、向 actionLayout()
//      添加按钮（若需要）。
//   2. 关闭按钮触发 closeRequested() 信号，由调用方驱动滑出动画。
//   3. 静态助手 wrapIndent / addSeparator 与缩进常量 INDENT_L2/L3
//      可直接复用，确保不同面板内的缩进梯度一致。
//
// objectName 固定为 "sidePanel"，与 Styles::SIDE_PANEL_STYLE() 锁死。
// ============================================================

class SidePanel : public QWidget {
    Q_OBJECT
public:
    explicit SidePanel(const QString& title,
                       int width = DEFAULT_WIDTH,
                       QWidget* parent = nullptr);

    // 通用建议宽度，子类可在构造时覆盖
    static constexpr int DEFAULT_WIDTH = 360;

    // 一致的缩进梯度（L2 子标签、L3 内容）
    static constexpr int INDENT_L2 = 10;
    static constexpr int INDENT_L3 = 22;

    // 把任意 widget 包进带左缩进的 HBox（沿用 ProtocolConfigPanel 习惯）
    static QHBoxLayout* wrapIndent(QWidget* w, int leftIndent);

    // 创建一条 1px configSep 分隔线（QSS 已定义 #configSep 样式）
    static QFrame* makeSeparator(QWidget* parent = nullptr);

signals:
    // 关闭按钮被点击；由外部决定隐藏/滑出动画
    void closeRequested();

protected:
    // 子类向这里追加业务控件；外边距 14/14/14/14、间距 14
    QVBoxLayout* contentLayout() const { return m_contentLayout; }

    // 底部动作栏；首次访问时自动插入分隔线 + 行容器
    // 边距 14/10/14/10、间距 8（与原 ProtocolConfigPanel 一致）
    QHBoxLayout* actionLayout();

    // 屏蔽鼠标 / 滚轮事件向父 QOpenGLWidget 透传
    void mousePressEvent(QMouseEvent* ev)       override { ev->accept(); }
    void mouseReleaseEvent(QMouseEvent* ev)     override { ev->accept(); }
    void mouseMoveEvent(QMouseEvent* ev)        override { ev->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* ev) override { ev->accept(); }
    void wheelEvent(QWheelEvent* ev)            override { ev->accept(); }

private:
    void buildSkeleton(const QString& title);

    QVBoxLayout* m_outerLayout   = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QHBoxLayout* m_actionLayout  = nullptr;
};
